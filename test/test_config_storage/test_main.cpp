#include "config_storage.h"
#include "pairing_reset.h"
#include <limits>
#include <unity.h>
#include <vector>
using namespace ridesync;
struct Memory : ConfigStore {
  ConfigRecord slots[2];
  bool gate = true;
  StoreStatus read_status = StoreStatus::Ok;
  StoreResult fault;
  bool apply_failure = false, fail_readback = false, close_after_write = false;
  unsigned writes = 0, target = 9;
  bool allowed() const override { return gate; }
  StoreResult read(unsigned s, ConfigRecord &r) override {
    if (!gate)
      return {StoreStatus::Refused};
    if (read_status != StoreStatus::Ok)
      return {read_status, 42};
    r = slots[s];
    return {r.size ? StoreStatus::Ok : StoreStatus::Missing};
  }
  StoreResult write(unsigned s, const ConfigRecord &r) override {
    ++writes;
    target = s;
    if (fault.status == StoreStatus::Ok || apply_failure)
      slots[s] = r;
    if (close_after_write)
      gate = false;
    if (fail_readback)
      read_status = StoreStatus::Error;
    return fault;
  }
};
SourceConfig camera(const char *name = "Front") {
  SourceConfig c;
  c.count = 1;
  auto &p = c.cameras[0];
  p.name = name;
  p.model = CameraModel::X5;
  p.family = CameraFamily::Insta360;
  p.identifier = "01:23:45:67:89:AB";
  p.address_type = AddressType::Public;
  return c;
}
void defaults_are_read_only() {
  Memory m;
  ConfigPersistence p(m);
  SourceConfig c = camera();
  TEST_ASSERT_EQUAL((int)PersistStatus::Defaults, (int)p.load(c).status);
  TEST_ASSERT_EQUAL(0, c.count);
  TEST_ASSERT_EQUAL(0, m.writes);
  p.service(90000);
  TEST_ASSERT_EQUAL(0, m.writes);
}
void wake_identifiers_roundtrip_without_schema_change() {
  for (const char *value : {"ABC123", "01:23:45:67:89:ab"}) {
    auto c = camera();
    c.cameras[0].wake_identifier = value;
    ConfigRecord record;
    TEST_ASSERT_EQUAL((int)PersistStatus::Encoded, (int)encodeConfig(c, 7, record).status);
    SourceConfig out;
    uint64_t generation = 0;
    TEST_ASSERT_EQUAL((int)PersistStatus::Loaded,
                      (int)decodeConfig(record, out, generation).status);
    TEST_ASSERT_EQUAL(7, generation);
    TEST_ASSERT_EQUAL_STRING(value, out.cameras[0].wake_identifier.c_str());
  }
}
void codec_bounds_and_roundtrip() {
  auto c = camera();
  c.cameras[7].name = std::string(100000, 'x');
  c.button.double_enabled = true;
  c.button_gpio.pin = 18;
  ConfigRecord r;
  TEST_ASSERT_EQUAL((int)PersistStatus::Encoded, (int)encodeConfig(c, 7, r).status);
  SourceConfig out;
  uint64_t gen = 0;
  TEST_ASSERT_EQUAL((int)PersistStatus::Loaded, (int)decodeConfig(r, out, gen).status);
  TEST_ASSERT_EQUAL(7, gen);
  TEST_ASSERT_EQUAL_STRING("Front", out.cameras[0].name.c_str());
  TEST_ASSERT_TRUE(out.cameras[7].name.empty());
  TEST_ASSERT_TRUE(out.button.double_enabled);
  r.bytes[r.size++] = 0;
  TEST_ASSERT_EQUAL((int)PersistStatus::Corrupt, (int)decodeConfig(r, out, gen).status);
  c.cameras[0].name = std::string(65, 'x');
  TEST_ASSERT_EQUAL((int)PersistStatus::Invalid, (int)encodeConfig(c, 8, r).status);
}
void throttled_coalescing_and_reset() {
  Memory m;
  ConfigPersistence p(m);
  SourceConfig out;
  p.load(out);
  TEST_ASSERT_EQUAL((int)PersistStatus::Pending, (int)p.request(camera(), 0).status);
  p.service(999);
  TEST_ASSERT_EQUAL(0, m.writes);
  p.request(camera("Back"), 999);
  p.service(1999);
  TEST_ASSERT_EQUAL(1, m.writes);
  p.request(camera("Back"), 2000);
  p.service(7000);
  TEST_ASSERT_EQUAL(1, m.writes);
  p.reset(7001);
  p.service(8001);
  TEST_ASSERT_EQUAL(2, m.writes);
  ConfigPersistence reboot(m);
  reboot.load(out);
  TEST_ASSERT_EQUAL(0, out.count);
}
void indeterminate_write_rescans_before_retry() {
  Memory m;
  encodeConfig(camera("Old"), 1, m.slots[0]);
  ConfigPersistence p(m);
  SourceConfig out;
  p.load(out);
  m.fault = {StoreStatus::Error, 55};
  m.apply_failure = true;
  p.request(camera("New"), 0);
  TEST_ASSERT_EQUAL((int)PersistStatus::Indeterminate, (int)p.service(1000).status);
  TEST_ASSERT_EQUAL(1, m.target);
  // Failed commit actually produced a valid slot; preserve it on next change.
  m.fault = {};
  p.request(camera("Latest"), 1001);
  p.service(6000);
  TEST_ASSERT_EQUAL(0, m.target);
  ConfigPersistence reboot(m);
  reboot.load(out);
  TEST_ASSERT_EQUAL_STRING("Latest", out.cameras[0].name.c_str());
}
void refusal_errors_retry_cap_and_generation() {
  Memory m;
  ConfigPersistence p(m);
  SourceConfig out;
  m.gate = false;
  TEST_ASSERT_EQUAL((int)PersistStatus::Refused, (int)p.load(out).status);
  TEST_ASSERT_EQUAL((int)PersistStatus::Refused, (int)p.request(camera(), 0).status);
  m.gate = true;
  m.read_status = StoreStatus::Error;
  TEST_ASSERT_EQUAL((int)PersistStatus::ReadError, (int)p.load(out).status);
  m.read_status = StoreStatus::Ok;
  p.request(camera(), 0);
  m.fault = {StoreStatus::Error, 1};
  p.service(1000);
  p.service(6000);
  p.service(16000);
  p.service(40000);
  TEST_ASSERT_EQUAL(3, m.writes);
  TEST_ASSERT_EQUAL((int)PersistStatus::Latched, (int)p.result().status);
  Memory n;
  encodeConfig(camera(), std::numeric_limits<uint64_t>::max(), n.slots[0]);
  ConfigPersistence q(n);
  q.load(out);
  q.request(camera("changed"), 0);
  TEST_ASSERT_EQUAL((int)PersistStatus::GenerationLimit, (int)q.service(1000).status);
  TEST_ASSERT_EQUAL(0, n.writes);
}

struct Bonds : BondResetPort {
  bool gate = true, quiet = true;
  unsigned removals = 0, checks = 0, quiesces = 0;
  BondOutcome deletion = BondOutcome::Removed, verification = BondOutcome::Absent;
  BondIdentity seen;
  bool admitted() const override { return gate; }
  BondOutcome remove(const BondIdentity &i) override {
    seen = i;
    ++removals;
    return deletion;
  }
  bool quiesce() override {
    ++quiesces;
    return quiet;
  }
  BondOutcome verify(const BondIdentity &) override {
    ++checks;
    return verification;
  }
};
void pairing_reset_is_scoped_bounded_and_verified() {
  Bonds b;
  BondIdentity i;
  i.address = {{1, 2, 3, 4, 5, 0xc0}};
  i.verified = true;
  TEST_ASSERT_EQUAL((int)BondOutcome::Refused, (int)resetPairing(b, i));
  TEST_ASSERT_EQUAL(0, b.removals);
  i.type = IdentityType::RandomStatic;
  TEST_ASSERT_EQUAL((int)BondOutcome::Removed, (int)resetPairing(b, i));
  TEST_ASSERT_EQUAL((int)IdentityType::RandomStatic, (int)b.seen.type);
  TEST_ASSERT_EQUAL_MEMORY(i.address.data(), b.seen.address.data(), 6);
  b.deletion = BondOutcome::Busy;
  b.removals = 0;
  TEST_ASSERT_EQUAL((int)BondOutcome::Busy, (int)resetPairing(b, i));
  TEST_ASSERT_EQUAL(2, b.removals);
  TEST_ASSERT_EQUAL(1, b.quiesces);
  b.deletion = BondOutcome::Removed;
  b.verification = BondOutcome::Present;
  TEST_ASSERT_EQUAL((int)BondOutcome::Indeterminate, (int)resetPairing(b, i));
  b.gate = false;
  b.removals = 0;
  TEST_ASSERT_EQUAL((int)BondOutcome::Refused, (int)resetPairing(b, i));
  TEST_ASSERT_EQUAL(0, b.removals);
}
void bond_capacity_requires_no_eviction_and_restore_evidence() {
  TEST_ASSERT_FALSE(admitBond(true, true, false, 0, 4, false));
  TEST_ASSERT_FALSE(admitBond(true, false, true, 0, 4, false));
  TEST_ASSERT_FALSE(admitBond(true, true, true, 3, 3, false));
  TEST_ASSERT_TRUE(admitBond(true, true, true, 3, 4, false));
  TEST_ASSERT_TRUE(admitBond(true, true, true, 3, 3, true));
}

// Independent, intentionally designed synthetic schema1 fixture; never deployed.
ConfigRecord golden1() {
  const uint8_t bytes[] = {0x52, 0x53, 0x43, 0x46, 0x01, 0x00, 0x01, 0x00, 0x07, 0x00, 0x00, 0x00,
                           0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x37, 0xad, 0xa4, 0x71,
                           0x01, 0x08, 0x05, 0x46, 0x72, 0x6f, 0x6e, 0x74, 0x11, 0x30, 0x31, 0x3a,
                           0x32, 0x33, 0x3a, 0x34, 0x35, 0x3a, 0x36, 0x37, 0x3a, 0x38, 0x39, 0x3a,
                           0x41, 0x42, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00};
  ConfigRecord r;
  r.size = sizeof(bytes);
  std::copy(bytes, bytes + sizeof(bytes), r.bytes.begin());
  return r;
}
void fixIntegrity(ConfigRecord &r) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < r.size; ++i) {
    if (i >= 20 && i < 24)
      continue;
    crc ^= r.bytes[i];
    for (unsigned b = 0; b < 8; ++b)
      crc = crc & 1 ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
  }
  crc = ~crc;
  for (unsigned i = 0; i < 4; ++i)
    r.bytes[20 + i] = uint8_t(crc >> (8 * i));
}
void golden_migration_requires_explicit_save() {
  Memory m;
  m.slots[0] = golden1();
  ConfigPersistence p(m);
  SourceConfig out;
  TEST_ASSERT_EQUAL((int)PersistStatus::Migrated, (int)p.load(out).status);
  TEST_ASSERT_EQUAL_STRING("Front", out.cameras[0].name.c_str());
  TEST_ASSERT_EQUAL(20, out.button.debounce_ms);
  TEST_ASSERT_EQUAL(800, out.button.long_ms);
  TEST_ASSERT_EQUAL(300, out.button.double_ms);
  TEST_ASSERT_FALSE(out.button.double_enabled);
  TEST_ASSERT_EQUAL((int)ButtonAction::RecordingIntent, (int)out.button.short_action);
  TEST_ASSERT_EQUAL((int)ButtonAction::WakeReconnect, (int)out.button.long_action);
  TEST_ASSERT_EQUAL((int)ButtonAction::Resync, (int)out.button.double_action);
  TEST_ASSERT_FALSE(out.button_gpio.enabled);
  TEST_ASSERT_FALSE(out.button_gpio.board_qualified);
  TEST_ASSERT_EQUAL(-1, out.button_gpio.pin);
  TEST_ASSERT_TRUE(out.button_gpio.active_low);
  p.service(50000);
  TEST_ASSERT_EQUAL(0, m.writes);
  p.request(out, 50000);
  p.service(51000);
  TEST_ASSERT_EQUAL(1, m.writes);
  TEST_ASSERT_EQUAL(2, m.slots[1].bytes[6]);
  TEST_ASSERT_EQUAL(8, m.slots[1].bytes[8]);
  TEST_ASSERT_EQUAL_MEMORY(golden1().bytes.data(), m.slots[0].bytes.data(), m.slots[0].size);
  // Independent golden schema2 defaults, fixed little endian and CRC.
  const uint8_t expected[] = {0x52, 0x53, 0x43, 0x46, 0x01, 0x00, 0x02, 0x00, 0x07, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00,
                              0xa7, 0xa5, 0xb0, 0xac, 0x00, 0x08, 0x14, 0x00, 0x00, 0x00,
                              0x20, 0x03, 0x00, 0x00, 0x2c, 0x01, 0x00, 0x00, 0x00, 0x00,
                              0x01, 0x02, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01};
  ConfigRecord r;
  encodeConfig(SourceConfig{}, 7, r);
  TEST_ASSERT_EQUAL(sizeof(expected), r.size);
  TEST_ASSERT_EQUAL_MEMORY(expected, r.bytes.data(), r.size);
}
void corrupt_future_ambiguity_preserve_bytes() {
  Memory m;
  encodeConfig(camera("old"), 1, m.slots[0]);
  encodeConfig(camera("new"), 2, m.slots[1]);
  m.slots[1].bytes[25] ^= 1;
  ConfigPersistence p(m);
  SourceConfig out;
  TEST_ASSERT_EQUAL((int)PersistStatus::Recovered, (int)p.load(out).status);
  TEST_ASSERT_EQUAL_STRING("old", out.cameras[0].name.c_str());
  auto original = m.slots[0];
  m.slots[1] = original;
  m.slots[1].bytes[6] = 3;
  fixIntegrity(m.slots[1]);
  TEST_ASSERT_EQUAL((int)PersistStatus::Future, (int)p.load(out).status);
  TEST_ASSERT_EQUAL(0, out.count);
  p.reset(0);
  p.service(1000);
  TEST_ASSERT_EQUAL(0, m.writes);
  m.slots[1] = original;
  m.slots[1].bytes[4] = 9;
  TEST_ASSERT_EQUAL((int)PersistStatus::Future, (int)p.load(out).status);
  encodeConfig(camera("conflict"), 1, m.slots[1]);
  TEST_ASSERT_EQUAL((int)PersistStatus::Ambiguous, (int)p.load(out).status);
  p.reset(2000);
  TEST_ASSERT_EQUAL((int)PersistStatus::Ambiguous, (int)p.service(3000).status);
  TEST_ASSERT_EQUAL(0, m.writes);
  m.slots[0].bytes[0] = 0;
  m.slots[1].bytes[0] = 0;
  TEST_ASSERT_EQUAL((int)PersistStatus::Corrupt, (int)p.load(out).status);
  p.reset(4000);
  TEST_ASSERT_EQUAL((int)PersistStatus::Corrupt, (int)p.service(5000).status);
  TEST_ASSERT_EQUAL(0, m.writes);
}
void malformed_corpus_never_partially_publishes() {
  auto r = golden1();
  SourceConfig out = camera("sentinel");
  uint64_t generation = 99;
  for (size_t n = 0; n < r.size; ++n) {
    auto short_record = r;
    short_record.size = n;
    TEST_ASSERT_EQUAL((int)PersistStatus::Corrupt,
                      (int)decodeConfig(short_record, out, generation).status);
    TEST_ASSERT_EQUAL_STRING("sentinel", out.cameras[0].name.c_str());
    TEST_ASSERT_EQUAL(99, generation);
  }
  const size_t positions[] = {5, 18, 19, 24, 25, 26, 32, 50, 51, 52, 53, 54, 55};
  for (auto position : positions) {
    auto malformed = r;
    malformed.bytes[position] = 255;
    fixIntegrity(malformed);
    TEST_ASSERT_EQUAL((int)PersistStatus::Corrupt,
                      (int)decodeConfig(malformed, out, generation).status);
  }
  r.size = kConfigRecordMax + 1;
  TEST_ASSERT_EQUAL((int)PersistStatus::Corrupt, (int)decodeConfig(r, out, generation).status);
}
void verification_failure_and_retry_preserve_last_good() {
  Memory m;
  encodeConfig(camera("saved"), 1, m.slots[0]);
  auto saved = m.slots[0];
  ConfigPersistence p(m);
  SourceConfig out;
  p.load(out);
  p.request(camera("new"), 0);
  m.fail_readback = true;
  auto failed = p.service(1000);
  TEST_ASSERT_EQUAL((int)PersistStatus::VerificationError, (int)failed.status);
  TEST_ASSERT_TRUE(failed.indeterminate);
  TEST_ASSERT_TRUE(p.pending());
  TEST_ASSERT_EQUAL_MEMORY(saved.bytes.data(), m.slots[0].bytes.data(), saved.size);
  m.fail_readback = false;
  m.read_status = StoreStatus::Ok;
  TEST_ASSERT_EQUAL((int)PersistStatus::Durable, (int)p.service(6000).status);
  TEST_ASSERT_EQUAL(0, m.target);
  TEST_ASSERT_EQUAL(2, m.writes);
}
void rollover_and_gate_closure_discard_pending() {
  Memory m;
  ConfigPersistence p(m);
  SourceConfig out;
  p.load(out);
  p.request(camera(), 0xfffffe0bu);
  p.service(498);
  TEST_ASSERT_EQUAL(0, m.writes);
  p.service(499);
  TEST_ASSERT_EQUAL(1, m.writes);
  p.request(camera("pending"), 500);
  m.gate = false;
  p.service(2000);
  m.gate = true;
  p.service(10000);
  TEST_ASSERT_EQUAL(1, m.writes);
  TEST_ASSERT_FALSE(p.pending());
}

void refusal_during_request_must_discard_earlier_mailbox() {
  Memory m;
  ConfigPersistence p(m);
  SourceConfig out;
  p.load(out);
  p.request(camera("old pending"), 0);
  m.gate = false;
  TEST_ASSERT_EQUAL((int)PersistStatus::Refused, (int)p.request(camera("refused"), 100).status);
  m.gate = true;
  p.service(5000);
  TEST_ASSERT_EQUAL(0, m.writes);
  TEST_ASSERT_FALSE(p.pending());
}

void refusal_during_write_must_discard_pending() {
  Memory m;
  ConfigPersistence p(m);
  SourceConfig out;
  p.load(out);
  p.request(camera(), 0);
  m.close_after_write = true;
  TEST_ASSERT_EQUAL((int)PersistStatus::Refused, (int)p.service(1000).status);
  TEST_ASSERT_FALSE(p.pending());
  m.close_after_write = false;
  m.gate = true;
  p.service(6000);
  TEST_ASSERT_EQUAL(1, m.writes);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(wake_identifiers_roundtrip_without_schema_change);
  RUN_TEST(refusal_during_write_must_discard_pending);
  RUN_TEST(refusal_during_request_must_discard_earlier_mailbox);
  RUN_TEST(golden_migration_requires_explicit_save);
  RUN_TEST(corrupt_future_ambiguity_preserve_bytes);
  RUN_TEST(malformed_corpus_never_partially_publishes);
  RUN_TEST(verification_failure_and_retry_preserve_last_good);
  RUN_TEST(rollover_and_gate_closure_discard_pending);
  RUN_TEST(pairing_reset_is_scoped_bounded_and_verified);
  RUN_TEST(bond_capacity_requires_no_eviction_and_restore_evidence);
  RUN_TEST(defaults_are_read_only);
  RUN_TEST(codec_bounds_and_roundtrip);
  RUN_TEST(throttled_coalescing_and_reset);
  RUN_TEST(indeterminate_write_rescans_before_retry);
  RUN_TEST(refusal_errors_retry_cap_and_generation);
  return UNITY_END();
}
void setUp() {}
void tearDown() {}

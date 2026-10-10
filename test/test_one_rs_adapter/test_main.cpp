#include "profiles/insta360_one_rs.h"
#include "recording_manager.h"
#include <algorithm>
#include <cstring>
#include <unity.h>
#include <vector>
using namespace ridesync;
struct Time : Clock {
  uint32_t value = 0;
  uint32_t now() const override { return value; }
};
struct Host : BleHost {
  std::vector<BleCommand> commands;
  std::vector<BleContext *> contexts, quiet;
  BleContext *links[4] = {};
  bool reject = false, drop = false, bad_crc = false, missing = false, cccd = true, property = true;
  bool security = true, refuse_write = false, hold_barrier = false, verify_cccd = true;
  int att_status = 0;
  bool reserve_submit = false, reserve_after_first_mtu = false;
  mutable unsigned mtu_reads = 0;
  unsigned reserved_submissions = 0;
  uint16_t negotiated = 67;
  unsigned starts = 0;
  BleHostState start(bool, bool) override {
    ++starts;
    return BleHostState::Ready;
  }
  BleHostState state() const override { return BleHostState::Ready; }
  BleFault fault() const override { return BleFault::None; }
  void sealStartup() override {}
  BleBondAdmission bondAdmission(const BondIdentity &) override {
    BleBondAdmission b;
    b.stack_ready = b.restore_verified = b.refusal_installed = true;
    b.existing_verified_identity = b.identity_matches = b.persistence_allowed = true;
    b.used = b.capacity = 4;
    return b;
  }
  int submit(const BleCommand &c, BleContext &x) override {
    if (c.phase == BlePhase::Write && reserve_submit) {
      ++reserved_submissions;
      return kBleHostReserved; // No SDK ownership or submission.
    }
    commands.push_back(c);
    contexts.push_back(&x);
    quiet.erase(std::remove(quiet.begin(), quiet.end(), &x), quiet.end());
    return c.phase == BlePhase::Write && refuse_write ? 99 : 0;
  }
  void deliver(BleContext &x, BleEvent e, bool terminal = false) {
    if (terminal) {
      x.terminal.store(true);
      quiet.push_back(&x);
    }
    x.receiver->copied(x, e);
  }
  int retire(BleContext &x) override {
    BleEvent e;
    e.kind = BleEventKind::Disconnected;
    e.connection = x.connection.load();
    deliver(x, e, true);
    return 0;
  }
  int cancelScan(BleContext &) override { return 0; }
  bool quiescent(const BleContext &x) const override {
    return !hold_barrier && x.terminal.load() &&
           std::find(quiet.begin(), quiet.end(), &x) != quiet.end();
  }
  bool releaseContext(BleContext &) override { return true; }
  uint16_t mtu(uint16_t) const override {
    if (reserve_after_first_mtu && ++mtu_reads > 1)
      return kBleMtuReserved;
    return negotiated;
  }
  void complete(size_t n) {
    BleEvent e;
    e.status = commands[n].phase == BlePhase::Write ? att_status : 0;
    e.connection = commands[n].connection;
    e.handle = commands[n].handle;
    if (commands[n].phase == BlePhase::VerifySubscription) {
      e.size = 2;
      e.bytes[0] = verify_cccd ? 1 : 0;
    }
    deliver(*contexts[n], e, true);
  }
  void notify(uint8_t peer, const std::vector<uint8_t> &p, size_t from = 0, size_t size = 0) {
    BleEvent e;
    e.kind = BleEventKind::Notification;
    e.connection = 10 + peer;
    e.handle = 6;
    if (!size)
      size = p.size() - from;
    e.size = size;
    std::copy(p.begin() + from, p.begin() + from + size, e.bytes.begin());
    deliver(*links[peer], e);
  }
};
struct Rig {
  Host host;
  Time clock;
  OneRsAdapter adapter;
  CameraManager manager;
  size_t handled = 0;
  unsigned count;
  Rig(unsigned peers = 1)
      : adapter(host, clock), manager(clock, adapter, OneRsAdapter::managerPolicy()), count(peers) {
    adapter.attach(manager);
    SourceConfig c;
    c.count = peers;
    OneRsQualification q;
    q.source_qualified = true;
    q.identity.verified = true;
    q.identity.type = IdentityType::Public;
    const char fw[] = "declared-version";
    std::memcpy(q.firmware.data(), fw, sizeof(fw) - 1);
    q.firmware_size = sizeof(fw) - 1;
    q.core_one_rs = q.ordinary_360_lens = q.video_mode_declared = true;
    for (unsigned i = 0; i < peers; ++i) {
      c.cameras[i].family = CameraFamily::Insta360;
      c.cameras[i].model = CameraModel::ONE_RS;
      c.cameras[i].name = "go";
      c.cameras[i].identifier = "00:00:00:00:00:0" + std::to_string(i + 1);
      c.cameras[i].address_type = AddressType::Public;
      q.identity.address[0] = i + 1;
      TEST_ASSERT_TRUE(adapter.configurePeer(i, q));
    }
    TEST_ASSERT_TRUE(manager.configure(c).ok());
    TEST_ASSERT_TRUE(adapter.start(true, true));
  }
  ~Rig() {
    adapter.stop();
    for (unsigned n = 0; n < 20 && !adapter.canDestroy(); ++n)
      adapter.service();
    TEST_ASSERT_TRUE(adapter.canDestroy());
  }
  void handle(size_t n) {
    const auto c = host.commands[n];
    auto &x = *host.contexts[n];
    BleEvent e;
    e.connection = 10 + x.peer;
    if (c.phase == BlePhase::Connect) {
      host.links[x.peer] = &x;
      e.kind = BleEventKind::Connected;
      host.deliver(x, e);
    } else if (c.phase == BlePhase::Security) {
      e.kind = BleEventKind::Security;
      e.encrypted = e.bonded = host.security;
      e.identity.verified = true;
      e.identity.type = IdentityType::Public;
      e.identity.address[0] = x.peer + 1;
      host.deliver(x, e);
    } else if (c.phase == BlePhase::Services) {
      if (!host.missing) {
        e.kind = BleEventKind::Service;
        e.uuid = c.uuid;
        e.start = 1;
        e.end = 12;
        host.deliver(x, e);
      }
      host.complete(n);
    } else if (c.phase == BlePhase::Characteristics) {
      for (unsigned j = 0; j < 2; ++j) {
        e.kind = BleEventKind::Characteristic;
        e.uuid = BleUuid::shortUuid(j ? 0xbe82 : 0xbe81);
        e.start = 2 + j * 3;
        e.handle = e.start + 1;
        e.properties = j ? 0x10 : (host.property ? 8 : 4);
        host.deliver(x, e);
      }
      host.complete(n);
    } else if (c.phase == BlePhase::Descriptors) {
      if (host.cccd) {
        e.kind = BleEventKind::Descriptor;
        e.uuid = BleUuid::shortUuid(0x2902);
        e.handle = c.start + 1;
        host.deliver(x, e);
      }
      host.complete(n);
    } else if (c.phase == BlePhase::Subscribe || c.phase == BlePhase::VerifySubscription) {
      host.complete(n);
    } else if (c.phase == BlePhase::Write) {
      if (!host.drop)
        host.complete(n);
    }
  }
  void pump() {
    for (unsigned n = 0; n < 100; ++n) {
      adapter.service();
      adapter.service();
      if (handled < host.commands.size())
        handle(handled++);
      else
        break;
    }
    adapter.service();
  }
  void connect() {
    for (unsigned i = 0; i < count; ++i)
      TEST_ASSERT_EQUAL(static_cast<int>(CameraError::None),
                        static_cast<int>(manager.request(i, Operation::Connect)));
    pump();
  }
  unsigned sent(unsigned code, uint8_t peer = 0) {
    unsigned n = 0;
    for (size_t i = 0; i < host.commands.size(); ++i) {
      const auto &c = host.commands[i];
      if (host.contexts[i]->peer == peer && c.phase == BlePhase::Write && c.size == 18 &&
          c.bytes[7] == code)
        ++n;
    }
    return n;
  }
};

void explicit_literal_shutter_att_only_unknown() {
  Rig r;
  r.connect();
  TEST_ASSERT_TRUE(r.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(0, r.sent(7));
  TEST_ASSERT_EQUAL(static_cast<int>(CameraError::Unsupported),
                    static_cast<int>(r.manager.request(0, Operation::Query)));
  TEST_ASSERT_EQUAL(static_cast<int>(CameraError::Unsupported),
                    static_cast<int>(r.manager.request(0, Operation::Wake)));
  r.manager.request(0, Operation::Start);
  r.pump();
  const uint8_t start[] = {0x12, 0, 0, 0, 4, 0, 0, 4, 0, 2, 1, 0, 0, 0x80, 0, 0, 8, 1};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(start, r.host.commands.back().bytes.data(), 18);
  TEST_ASSERT_EQUAL(static_cast<int>(Lifecycle::Ready),
                    static_cast<int>(r.manager.state(0)->lifecycle));
  TEST_ASSERT_FALSE(r.manager.state(0)->has_observation);
  TEST_ASSERT_EQUAL(static_cast<int>(RecordingState::Unknown),
                    static_cast<int>(r.manager.state(0)->observed));
  r.manager.request(0, Operation::Stop);
  r.pump();
  const uint8_t stop[] = {0x12, 0, 0, 0, 4, 0, 0, 5, 0, 2, 2, 0, 0, 0x80, 0, 0, 0x10, 1};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(stop, r.host.commands.back().bytes.data(), 18);
  r.host.notify(0, {0xff, 0x06, 0x41, 0x01, 0, 0xab, 0x98, 0x57});
  r.pump();
  TEST_ASSERT_FALSE(r.manager.state(0)->has_observation);
}
void missing_security_gatt_cccd_mtu_has_no_shutter() {
  for (unsigned mode = 0; mode < 6; ++mode) {
    Rig r;
    if (mode == 0)
      r.host.missing = true;
    if (mode == 1)
      r.host.cccd = false;
    if (mode == 2)
      r.host.property = false;
    if (mode == 3)
      r.host.negotiated = 20;
    if (mode == 4)
      r.host.security = false;
    if (mode == 5)
      r.host.verify_cccd = false;
    r.connect();
    TEST_ASSERT_FALSE(r.adapter.commandReady(0));
    TEST_ASSERT_EQUAL_UINT(0, r.sent(4));
    TEST_ASSERT_EQUAL_UINT(0, r.sent(5));
  }
  Rig minimum;
  minimum.host.negotiated = 21;
  minimum.connect();
  TEST_ASSERT_TRUE(minimum.adapter.commandReady(0));
}
void late_att_timeout_rollover_peer_isolation_no_replay() {
  Rig r(2);
  r.clock.value = 0xfffffff0;
  r.connect();
  r.host.drop = true;
  r.manager.request(0, Operation::Start);
  r.pump();
  size_t write = r.host.commands.size() - 1;
  r.host.notify(0, {18, 0, 0, 0, 4, 0, 0, 4, 0, 2, 1, 0, 0, 0x80, 0, 0, 8, 1});
  r.pump();
  TEST_ASSERT_EQUAL(static_cast<int>(Lifecycle::Operating),
                    static_cast<int>(r.manager.state(0)->lifecycle));
  r.host.drop = false;
  r.manager.request(1, Operation::Stop);
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.sent(5, 1));
  r.clock.value += OneRsAdapter::kCommandMs;
  r.host.complete(write);
  r.pump();
  TEST_ASSERT_FALSE(r.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(1, r.sent(4));
  TEST_ASSERT_FALSE(r.manager.state(0)->has_observation);
  r.manager.reset();
  r.pump();
  r.connect();
  TEST_ASSERT_TRUE(r.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(1, r.sent(4));
  r.manager.request(0, Operation::Stop);
  r.pump();
  TEST_ASSERT_EQUAL_UINT8(1, r.host.commands.back().bytes[10]);
}
void uncertain_submit_cancel_and_sequence_exhaustion_seal() {
  Rig r;
  r.connect();
  r.host.refuse_write = true;
  r.manager.request(0, Operation::Start);
  r.pump();
  TEST_ASSERT_FALSE(r.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(1, r.sent(4));
  Rig reset;
  reset.connect();
  reset.host.drop = true;
  reset.manager.request(0, Operation::Start);
  reset.pump();
  const auto write = reset.host.commands.size() - 1;
  reset.manager.reset();
  reset.pump();
  reset.host.complete(write);
  reset.pump();
  TEST_ASSERT_FALSE(reset.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(1, reset.sent(4));
  Rig exhausted;
  exhausted.connect();
  for (unsigned n = 0; n < 254; ++n) {
    exhausted.manager.request(0, Operation::Stop);
    exhausted.pump();
  }
  TEST_ASSERT_EQUAL_UINT(254, exhausted.sent(5));
  exhausted.manager.request(0, Operation::Stop);
  exhausted.pump();
  TEST_ASSERT_FALSE(exhausted.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(254, exhausted.sent(5));
}
void declaration_identity_binding_and_disabled_start() {
  Host h;
  Time c;
  OneRsAdapter a(h, c);
  CameraManager m(c, a, OneRsAdapter::managerPolicy());
  a.attach(m);
  OneRsQualification q;
  TEST_ASSERT_FALSE(a.configurePeer(0, q));
  TEST_ASSERT_FALSE(a.start(false, true));
  a.service();
  TEST_ASSERT_EQUAL_UINT(0, h.starts);
  TEST_ASSERT_TRUE(h.commands.empty());
  a.stop();
  a.service();
  Rig mismatch;
  mismatch.manager.reset();
  mismatch.pump();
  SourceConfig config;
  config.count = 1;
  config.cameras[0].family = CameraFamily::Insta360;
  config.cameras[0].model = CameraModel::ONE_RS;
  config.cameras[0].name = "same";
  config.cameras[0].identifier = "00:00:00:00:00:02";
  config.cameras[0].address_type = AddressType::Public;
  TEST_ASSERT_TRUE(mismatch.manager.configure(config).ok());
  mismatch.connect();
  TEST_ASSERT_FALSE(mismatch.adapter.commandReady(0));
  TEST_ASSERT_TRUE(mismatch.host.commands.empty());
}

void qualification_each_guard_and_pending_cleanup_are_enforced() {
  Host h;
  Time c;
  OneRsAdapter a(h, c);
  CameraManager m(c, a, OneRsAdapter::managerPolicy());
  a.attach(m);
  OneRsQualification q;
  q.source_qualified = q.core_one_rs = q.ordinary_360_lens = q.video_mode_declared = true;
  q.identity.verified = true;
  q.identity.type = IdentityType::Public;
  q.identity.address[0] = 1;
  q.firmware_size = 3;
  std::memcpy(q.firmware.data(), "1.2", 3);
  TEST_ASSERT_TRUE(a.configurePeer(0, q));
  for (unsigned mode = 0; mode < 10; ++mode) {
    auto bad = q;
    if (mode == 0)
      bad.source_qualified = false;
    if (mode == 1)
      bad.core_one_rs = false;
    if (mode == 2)
      bad.ordinary_360_lens = false;
    if (mode == 3)
      bad.video_mode_declared = false;
    if (mode == 4)
      bad.identity.verified = false;
    if (mode == 5)
      bad.identity.type = IdentityType::UnresolvedPrivate;
    if (mode == 6)
      bad.firmware_size = 0;
    if (mode == 7)
      bad.firmware_size = 33;
    if (mode == 8)
      bad.firmware[0] = '\n';
    if (mode == 9)
      bad.identity.type = IdentityType::RandomStatic;
    TEST_ASSERT_FALSE(a.configurePeer(0, bad));
  }
  a.stop();
  a.service();
  Rig r;
  r.connect();
  TEST_ASSERT_FALSE(r.adapter.configurePeer(0, q));
  r.host.hold_barrier = true;
  r.manager.reset();
  r.pump();
  TEST_ASSERT_FALSE(r.adapter.canDestroy());
  TEST_ASSERT_FALSE(r.adapter.configurePeer(0, q));
  r.host.hold_barrier = false;
  r.pump();
  TEST_ASSERT_TRUE(r.adapter.canDestroy());
  TEST_ASSERT_TRUE(r.adapter.configurePeer(0, q));
}
void overflow_att_error_and_old_generation_never_observe_or_replay() {
  Rig error;
  error.connect();
  error.host.att_status = 7;
  error.manager.request(0, Operation::Start);
  error.pump();
  TEST_ASSERT_FALSE(error.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(1, error.sent(4));
  Rig overflow;
  overflow.connect();
  overflow.manager.request(0, Operation::Stop);
  overflow.host.drop = true;
  overflow.pump();
  for (unsigned n = 0; n < 40; ++n)
    overflow.host.notify(0, {0});
  overflow.pump();
  TEST_ASSERT_FALSE(overflow.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(1, overflow.sent(5));
  overflow.host.complete(overflow.host.commands.size() - 1);
  overflow.pump();
  Rig old;
  old.connect();
  const auto generation = old.host.links[0]->generation;
  old.manager.reset();
  old.pump();
  old.connect();
  BleResult stale;
  stale.kind = BleResultKind::LinkClosed;
  stale.event.generation = generation;
  old.adapter.result(stale);
  old.pump();
  TEST_ASSERT_TRUE(old.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(0, old.sent(4));
  TEST_ASSERT_FALSE(old.manager.state(0)->has_observation);
}
struct Audit : CameraAudit {
  unsigned acks = 0, observations = 0;
  void request(size_t, Operation, CameraError, bool, uint32_t) override {}
  void attempt(size_t, Operation, Token, uint32_t, bool) override {}
  void accepted(const Event &e, Operation, uint32_t) override {
    if (e.kind == EventKind::RecordingObserved || e.kind == EventKind::CommandRecordingObserved)
      ++observations;
  }
  void cancelled(size_t, Operation, Token, uint32_t) override {}
  void failure(size_t, Operation, Token, uint32_t, CameraError) override {}
  void wireAck(size_t, Operation, Token, uint32_t, CameraAckDomain, CameraAckAction) override {
    ++acks;
  }
};
void group_att_completion_cannot_confirm_recording() {
  Rig r;
  Audit audit;
  TEST_ASSERT_TRUE(r.manager.attachAudit(audit));
  RecordingManager group(r.manager, r.clock);
  r.adapter.attachGroup(group);
  TEST_ASSERT_EQUAL(static_cast<int>(GroupError::None),
                    static_cast<int>(group.request(RecordingState::Recording)));
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.sent(4));
  TEST_ASSERT_EQUAL_UINT(0, group.status().recording);
  TEST_ASSERT_EQUAL_UINT(1, group.status().unknown);
  TEST_ASSERT_EQUAL_UINT(1, group.status().pending);
  TEST_ASSERT_EQUAL_UINT(0, audit.acks);
  TEST_ASSERT_EQUAL_UINT(0, audit.observations);
  r.adapter.stop();
  r.pump();
  r.manager.detachAudit(&audit);
}
// Reservation before copied admission refuses without replay; reservation after
// admission may defer only unsubmitted work, and cancellation retires that intent.
void reserved_mtu_and_launch_never_submit_or_replay_cancelled_intent() {
  for (unsigned mode = 0; mode < 3; ++mode) {
    Rig r;
    r.connect();
    if (mode == 0)
      r.host.negotiated = kBleMtuReserved;
    if (mode == 1) {
      r.host.reserve_after_first_mtu = true;
      r.host.mtu_reads = 0;
    }
    if (mode == 2)
      r.host.reserve_submit = true;
    const auto error = r.manager.request(0, Operation::Start);
    TEST_ASSERT_EQUAL(static_cast<int>(CameraError::None), static_cast<int>(error));
    r.pump();
    TEST_ASSERT_EQUAL_UINT(0, r.sent(4));
    TEST_ASSERT_EQUAL_UINT(0, r.sent(5));
    TEST_ASSERT_FALSE(r.manager.state(0)->has_observation);
    if (mode == 2)
      TEST_ASSERT_GREATER_THAN_UINT(0, r.host.reserved_submissions);
    r.manager.reset();
    r.pump();
    r.host.negotiated = 67;
    r.host.reserve_after_first_mtu = r.host.reserve_submit = false;
    r.pump();
    r.connect();
    TEST_ASSERT_TRUE(r.adapter.commandReady(0));
    TEST_ASSERT_EQUAL_UINT(0, r.sent(4));
    TEST_ASSERT_EQUAL_UINT(0, r.sent(5));
    r.manager.request(0, Operation::Stop);
    r.pump();
    TEST_ASSERT_EQUAL_UINT(1, r.sent(5));
    TEST_ASSERT_EQUAL_UINT8(1, r.host.commands.back().bytes[10]);
  }
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(reserved_mtu_and_launch_never_submit_or_replay_cancelled_intent);
  RUN_TEST(qualification_each_guard_and_pending_cleanup_are_enforced);
  RUN_TEST(overflow_att_error_and_old_generation_never_observe_or_replay);
  RUN_TEST(group_att_completion_cannot_confirm_recording);
  RUN_TEST(explicit_literal_shutter_att_only_unknown);
  RUN_TEST(missing_security_gatt_cccd_mtu_has_no_shutter);
  RUN_TEST(late_att_timeout_rollover_peer_isolation_no_replay);
  RUN_TEST(uncertain_submit_cancel_and_sequence_exhaustion_seal);
  RUN_TEST(declaration_identity_binding_and_disabled_start);
  return UNITY_END();
}

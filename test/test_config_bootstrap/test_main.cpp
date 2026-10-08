#include "config_bootstrap.h"
#include <limits>
#include <thread>
#include <unity.h>
using namespace ridesync;
struct Memory : ConfigStore {
  ConfigRecord slots[2];
  bool gate = true;
  bool fail = false, read_error = false;
  unsigned reads = 0, writes = 0;
  bool allowed() const override { return gate; }
  StoreResult read(unsigned slot, ConfigRecord &out) override {
    ++reads;
    if (!gate)
      return {StoreStatus::Refused};
    if (read_error)
      return {StoreStatus::Error, 33};
    out = slots[slot];
    return {out.size ? StoreStatus::Ok : StoreStatus::Missing};
  }
  StoreResult write(unsigned slot, const ConfigRecord &in) override {
    ++writes;
    if (fail)
      return {StoreStatus::CommitError, 77};
    slots[slot] = in;
    return {};
  }
};
SourceConfig camera(const char *name = "Front") {
  SourceConfig c;
  c.count = 1;
  c.cameras[0].name = name;
  c.cameras[0].family = CameraFamily::GoPro;
  c.cameras[0].model = CameraModel::HERO12_BLACK;
  c.cameras[0].identifier = "01:23:45:67:89:AB";
  c.cameras[0].address_type = AddressType::Public;
  c.button.double_enabled = true;
  return c;
}
CameraPeers peers() {
  CameraPeers p;
  p.count = 1;
  p.entries[0].id = 42;
  p.entries[0].model = CameraModel::HERO12_BLACK;
  return p;
}
struct Rig {
  Memory store;
  ConfigPersistence persistence{store};
  SettingsPublication publication;
  SettingsRequests requests;
  ConfigBootstrap owner;
  SettingsSnapshot snapshot;
  explicit Rig(const CameraPeers &p = {}) : owner(persistence, publication, requests, 7, p) {}
  void start() {
    owner.start(false, true);
    TEST_ASSERT_TRUE(publication.take(snapshot));
  }
};
void startup_is_barrier_and_defaults_inactive() {
  Rig r;
  TEST_ASSERT_FALSE(r.publication.take(r.snapshot));
  r.start();
  TEST_ASSERT_TRUE(r.snapshot.completed);
  TEST_ASSERT_TRUE(r.snapshot.effective);
  TEST_ASSERT_EQUAL(7, r.snapshot.epoch);
  TEST_ASSERT_EQUAL(1, r.snapshot.generation);
  TEST_ASSERT_EQUAL((int)PersistStatus::Defaults, (int)r.snapshot.load.status);
  TEST_ASSERT_EQUAL(0, r.snapshot.settings.count);
  TEST_ASSERT_FALSE(r.snapshot.settings.button_gpio.enabled);
  SettingsQualification q;
  q.cameras = q.button = q.nvs_allowed = true;
  auto a = admitSettings(r.snapshot, q);
  TEST_ASSERT_FALSE(a.cameras);
  TEST_ASSERT_FALSE(a.button);
  r.owner.service(90000, false, true);
  TEST_ASSERT_EQUAL(0, r.store.writes);
  TEST_ASSERT_FALSE(r.publication.take(r.snapshot));
}
void bounded_copy_ignores_unused_strings_and_rejects_partial_changes() {
  auto c = camera();
  c.cameras[7].name.assign(100000, 'x');
  Settings s;
  TEST_ASSERT_TRUE(copySettings(c, s));
  TEST_ASSERT_EQUAL_STRING("Front", s.cameras[0].name);
  TEST_ASSERT_TRUE(s.button.double_enabled);
  TEST_ASSERT_EQUAL_STRING("", s.cameras[7].name);
  c.cameras[0].name.assign(65, 'x');
  TEST_ASSERT_FALSE(copySettings(c, s));
  TEST_ASSERT_EQUAL_STRING("Front", s.cameras[0].name);
  c.cameras[0].name = std::string("Front\0Rear", 10);
  TEST_ASSERT_FALSE(copySettings(c, s));
  TEST_ASSERT_EQUAL_STRING("Front", s.cameras[0].name);
  s.cameras[0].name[64] = 'x';
  for (auto &v : s.cameras[0].name)
    v = 'x';
  SourceConfig out = camera("sentinel");
  TEST_ASSERT_FALSE(expandSettings(s, out));
  TEST_ASSERT_EQUAL_STRING("sentinel", out.cameras[0].name.c_str());
}
void insta360_wake_identifiers_survive_fixed_settings_handoff() {
  auto c = camera();
  c.cameras[0].family = CameraFamily::Insta360;
  c.cameras[0].model = CameraModel::X5;
  for (const char *value : {"ABC123", "01:23:45:67:89:ab"}) {
    c.cameras[0].wake_identifier = value;
    Settings settings;
    TEST_ASSERT_TRUE(copySettings(c, settings));
    TEST_ASSERT_EQUAL_STRING(value, settings.cameras[0].wake_identifier);
    SourceConfig expanded;
    TEST_ASSERT_TRUE(expandSettings(settings, expanded));
    TEST_ASSERT_EQUAL_STRING(value, expanded.cameras[0].wake_identifier.c_str());
  }
}
void mailbox_preserves_owned_copy_and_rejects_pressure() {
  SettingsRequests m;
  SettingsSave in, out;
  in.epoch = 7;
  in.generation = 11;
  in.settings.button.long_ms = 1234;
  TEST_ASSERT_TRUE(m.put(in));
  in.generation = 12;
  TEST_ASSERT_FALSE(m.put(in));
  TEST_ASSERT_TRUE(m.take(out));
  TEST_ASSERT_EQUAL(11, out.generation);
  TEST_ASSERT_EQUAL(1234, out.settings.button.long_ms);
  TEST_ASSERT_FALSE(m.take(out));
  TEST_ASSERT_TRUE(m.put(in));
  TEST_ASSERT_EQUAL(11, out.generation);
}
void application_rejects_stale_wrong_epoch_and_incomplete() {
  ApplicationSettings app(7);
  SettingsSnapshot s;
  s.epoch = 7;
  s.generation = 2;
  s.completed = true;
  s.settings.button.long_ms = 1234;
  TEST_ASSERT_TRUE(app.accept(s));
  s.generation = 1;
  s.settings.button.long_ms = 999;
  TEST_ASSERT_FALSE(app.accept(s));
  s.generation = 3;
  s.epoch = 8;
  TEST_ASSERT_FALSE(app.accept(s));
  s.epoch = 7;
  s.completed = false;
  TEST_ASSERT_FALSE(app.accept(s));
  TEST_ASSERT_EQUAL(1234, app.snapshot().settings.button.long_ms);
  s.completed = true;
  TEST_ASSERT_TRUE(app.accept(s));
}
void refusal_is_explicit_read_only_and_local_eligibility_independent() {
  Rig r;
  r.owner.start(true, true);
  TEST_ASSERT_TRUE(r.publication.take(r.snapshot));
  TEST_ASSERT_TRUE(r.snapshot.completed);
  TEST_ASSERT_FALSE(r.snapshot.effective);
  TEST_ASSERT_EQUAL((int)PersistStatus::Refused, (int)r.snapshot.load.status);
  TEST_ASSERT_EQUAL(0, r.store.reads);
  SettingsQualification q;
  q.cameras = q.button = q.local_telemetry = q.nvs_allowed = true;
  q.safe_mode = true;
  auto a = admitSettings(r.snapshot, q);
  TEST_ASSERT_FALSE(a.cameras);
  TEST_ASSERT_FALSE(a.button);
  TEST_ASSERT_TRUE(a.local_telemetry);
  r.owner.service(1000, false, true);
  TEST_ASSERT_EQUAL(0, r.store.writes);
  TEST_ASSERT_FALSE(r.publication.take(r.snapshot));
  Rig failed;
  failed.owner.unavailable({PersistStatus::ReadError, -1});
  TEST_ASSERT_TRUE(failed.publication.take(failed.snapshot));
  TEST_ASSERT_EQUAL(-1, failed.snapshot.outcome.code);
}
void corrupt_and_future_do_not_publish_camera_settings() {
  for (unsigned future = 0; future != 2; ++future) {
    Rig r;
    encodeConfig(camera(), 1, r.store.slots[0]);
    r.store.slots[0].bytes[future ? 6 : 0] = future ? 99 : 0;
    if (future) {
      auto &record = r.store.slots[0];
      uint32_t crc = 0xffffffffu;
      for (size_t i = 0; i < record.size; ++i) {
        if (i >= 20 && i < 24)
          continue;
        crc ^= record.bytes[i];
        for (unsigned bit = 0; bit != 8; ++bit)
          crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
      }
      crc = ~crc;
      for (unsigned i = 0; i != 4; ++i)
        record.bytes[20 + i] = uint8_t(crc >> (8 * i));
    }
    r.start();
    TEST_ASSERT_FALSE(r.snapshot.effective);
    TEST_ASSERT_EQUAL(0, r.snapshot.settings.count);
    TEST_ASSERT_EQUAL((int)(future ? PersistStatus::Future : PersistStatus::Corrupt),
                      (int)r.snapshot.load.status);
    TEST_ASSERT_EQUAL(0, r.store.writes);
  }
}
void peers_require_separate_stable_unique_model_slot_provisioning() {
  auto provision = peers();
  Rig r(provision);
  provision.entries[0].id = 999;
  encodeConfig(camera(), 1, r.store.slots[0]);
  r.start();
  TEST_ASSERT_TRUE(r.snapshot.peers_valid);
  TEST_ASSERT_EQUAL(42, r.snapshot.peers.entries[0].id);
  SettingsQualification q;
  q.nvs_allowed = true;
  TEST_ASSERT_FALSE(admitSettings(r.snapshot, q).cameras);
  q.cameras = true;
  TEST_ASSERT_TRUE(admitSettings(r.snapshot, q).cameras);
  q.safe_mode = true;
  TEST_ASSERT_FALSE(admitSettings(r.snapshot, q).cameras);
  for (unsigned bad = 0; bad != 5; ++bad) {
    auto p = peers();
    if (bad == 0)
      p.count = 0;
    if (bad == 1)
      p.entries[0].id = 0;
    if (bad == 2)
      p.entries[0].slot = 1;
    if (bad == 3)
      p.entries[0].model = CameraModel::X5;
    if (bad == 4) {
      p.count = 2;
      p.entries[1] = p.entries[0];
    }
    Rig missing(p);
    encodeConfig(camera(), 1, missing.store.slots[0]);
    missing.start();
    TEST_ASSERT_FALSE(missing.snapshot.peers_valid);
    q.safe_mode = false;
    q.local_telemetry = true;
    auto a = admitSettings(missing.snapshot, q);
    TEST_ASSERT_FALSE(a.cameras);
    TEST_ASSERT_TRUE(a.local_telemetry);
  }
}
void authorized_save_becomes_effective_only_after_verified_persistence() {
  Rig r(peers());
  encodeConfig(camera("Old"), 1, r.store.slots[0]);
  r.start();
  SettingsSave save;
  save.epoch = 7;
  save.generation = 1;
  TEST_ASSERT_TRUE(copySettings(camera("New"), save.settings));
  TEST_ASSERT_TRUE(r.requests.put(save));
  r.owner.service(0, false, true);
  TEST_ASSERT_EQUAL((int)SaveOutcome::Accepted, (int)r.owner.saveOutcome());
  TEST_ASSERT_FALSE(r.publication.take(r.snapshot));
  r.store.fail = true;
  r.owner.service(1000, false, true);
  TEST_ASSERT_FALSE(r.publication.take(r.snapshot));
  TEST_ASSERT_EQUAL_STRING("Old", r.snapshot.settings.cameras[0].name);
  TEST_ASSERT_EQUAL((int)SaveOutcome::PersistenceError, (int)r.owner.saveOutcome());
  TEST_ASSERT_EQUAL((int)PersistStatus::CommitError, (int)r.owner.saveResult().status);
  r.store.fail = false;
  r.owner.service(6000, false, true);
  TEST_ASSERT_TRUE(r.publication.take(r.snapshot));
  TEST_ASSERT_EQUAL(2, r.snapshot.generation);
  TEST_ASSERT_EQUAL_STRING("New", r.snapshot.settings.cameras[0].name);
  TEST_ASSERT_EQUAL((int)PersistStatus::Loaded, (int)r.snapshot.load.status);
  TEST_ASSERT_EQUAL((int)PersistStatus::Durable, (int)r.snapshot.outcome.status);
  TEST_ASSERT_EQUAL((int)SaveOutcome::Durable, (int)r.owner.saveOutcome());
  save.generation = 1;
  TEST_ASSERT_TRUE(r.requests.put(save));
  r.owner.service(7000, false, true);
  TEST_ASSERT_EQUAL((int)SaveOutcome::Stale, (int)r.owner.saveOutcome());
  TEST_ASSERT_EQUAL(2, r.store.writes);
}
void publication_pressure_retains_latest_and_refusal_drops_save() {
  Rig r;
  r.owner.start(false, true); // leave first publication full
  SettingsSave s;
  s.epoch = 7;
  s.generation = 1;
  TEST_ASSERT_TRUE(copySettings(camera(), s.settings));
  TEST_ASSERT_TRUE(r.requests.put(s));
  r.owner.service(0, false, true);
  r.store.gate = false;
  r.owner.service(1000, false, false);
  TEST_ASSERT_TRUE(r.publication.take(r.snapshot));
  TEST_ASSERT_TRUE(r.snapshot.effective);
  r.store.gate = true;
  r.owner.service(6000, false, true);
  TEST_ASSERT_TRUE(r.publication.take(r.snapshot));
  TEST_ASSERT_FALSE(r.snapshot.effective);
  TEST_ASSERT_EQUAL(2, r.snapshot.generation);
  TEST_ASSERT_EQUAL(0, r.store.writes);
  TEST_ASSERT_TRUE(r.requests.put(s));
  r.owner.service(7000, false, true);
  TEST_ASSERT_EQUAL((int)SaveOutcome::Refused, (int)r.owner.saveOutcome());
  TEST_ASSERT_EQUAL(0, r.store.writes);
}
void mailbox_concurrent_handoff_never_tears_or_reuses_consumer_data() {
  SettingsRequests m;
  std::atomic<bool> correct{true};
  std::thread producer([&]() {
    SettingsSave value;
    value.epoch = 7;
    for (uint32_t i = 1; i <= 10000; ++i) {
      value.generation = i;
      value.settings.button.long_ms = i + 800;
      while (!m.put(value))
        std::this_thread::yield();
    }
  });
  SettingsSave value;
  for (uint32_t i = 1; i <= 10000; ++i) {
    while (!m.take(value))
      std::this_thread::yield();
    if (value.epoch != 7 || value.generation != i || value.settings.button.long_ms != i + 800)
      correct.store(false);
    std::this_thread::yield(); // producer can reuse slot while our copy remains live
    if (value.generation != i || value.settings.button.long_ms != i + 800)
      correct.store(false);
  }
  producer.join();
  TEST_ASSERT_TRUE(correct.load());
}
void owner_rejects_wrong_epoch_invalid_and_busy_without_replacing_candidate() {
  Rig r;
  r.start();
  SettingsSave save;
  save.epoch = 8;
  save.generation = 1;
  TEST_ASSERT_TRUE(copySettings(camera(), save.settings));
  TEST_ASSERT_TRUE(r.requests.put(save));
  r.owner.service(0, false, true);
  TEST_ASSERT_EQUAL((int)SaveOutcome::Stale, (int)r.owner.saveOutcome());
  save.epoch = 7;
  save.settings.count = 9;
  TEST_ASSERT_TRUE(r.requests.put(save));
  r.owner.service(0, false, true);
  TEST_ASSERT_EQUAL((int)SaveOutcome::Invalid, (int)r.owner.saveOutcome());
  TEST_ASSERT_TRUE(copySettings(camera("First"), save.settings));
  TEST_ASSERT_TRUE(r.requests.put(save));
  r.owner.service(0, false, true);
  TEST_ASSERT_TRUE(copySettings(camera("Second"), save.settings));
  TEST_ASSERT_TRUE(r.requests.put(save));
  r.owner.service(0, false, true);
  TEST_ASSERT_EQUAL((int)SaveOutcome::Busy, (int)r.owner.saveOutcome());
  r.owner.service(1000, false, true);
  TEST_ASSERT_TRUE(r.publication.take(r.snapshot));
  TEST_ASSERT_EQUAL_STRING("First", r.snapshot.settings.cameras[0].name);
  TEST_ASSERT_EQUAL(1, r.store.writes);
}
void safe_mode_cancels_pending_even_with_open_nvs_and_cannot_replay() {
  Rig r;
  r.start();
  SettingsSave save;
  save.epoch = 7;
  save.generation = 1;
  TEST_ASSERT_TRUE(copySettings(camera(), save.settings));
  TEST_ASSERT_TRUE(r.requests.put(save));
  r.owner.service(0, false, true);
  TEST_ASSERT_TRUE(r.persistence.pending());
  r.owner.service(1000, true, true);
  TEST_ASSERT_FALSE(r.persistence.pending());
  TEST_ASSERT_TRUE(r.publication.take(r.snapshot));
  TEST_ASSERT_FALSE(r.snapshot.effective);
  r.owner.service(6000, false, true);
  TEST_ASSERT_EQUAL(0, r.store.writes);
}
void duplicate_peer_ids_and_session_model_changes_refuse_camera_admission() {
  auto c = camera();
  c.count = 2;
  c.cameras[1] = c.cameras[0];
  c.cameras[1].name = "Back";
  c.cameras[1].identifier = "01:23:45:67:89:AC";
  auto p = peers();
  p.count = 2;
  p.entries[1] = p.entries[0];
  p.entries[1].slot = 1;
  Rig duplicate(p);
  encodeConfig(c, 1, duplicate.store.slots[0]);
  duplicate.start();
  TEST_ASSERT_FALSE(duplicate.snapshot.peers_valid);
  Rig r(peers());
  encodeConfig(camera(), 1, r.store.slots[0]);
  r.start();
  TEST_ASSERT_TRUE(r.snapshot.peers_valid);
  SettingsSave save;
  save.epoch = 7;
  save.generation = 1;
  c = camera();
  c.cameras[0].family = CameraFamily::Insta360;
  c.cameras[0].model = CameraModel::X5;
  TEST_ASSERT_TRUE(copySettings(c, save.settings));
  TEST_ASSERT_TRUE(r.requests.put(save));
  r.owner.service(0, false, true);
  r.owner.service(1000, false, true);
  TEST_ASSERT_TRUE(r.publication.take(r.snapshot));
  TEST_ASSERT_FALSE(r.snapshot.peers_valid);
  TEST_ASSERT_EQUAL(42, r.snapshot.peers.entries[0].id);
  TEST_ASSERT_EQUAL((int)CameraModel::HERO12_BLACK, (int)r.snapshot.peers.entries[0].model);
}
void unavailable_read_during_save_revokes_settings_without_rewrite() {
  Rig r(peers());
  encodeConfig(camera("Old"), 1, r.store.slots[0]);
  r.start();
  SettingsSave save;
  save.epoch = 7;
  save.generation = 1;
  TEST_ASSERT_TRUE(copySettings(camera("New"), save.settings));
  TEST_ASSERT_TRUE(r.requests.put(save));
  r.owner.service(0, false, true);
  r.store.read_error = true;
  r.owner.service(1000, false, true);
  TEST_ASSERT_TRUE(r.publication.take(r.snapshot));
  TEST_ASSERT_FALSE(r.snapshot.effective);
  TEST_ASSERT_EQUAL((int)PersistStatus::Loaded, (int)r.snapshot.load.status);
  TEST_ASSERT_EQUAL((int)PersistStatus::ReadError, (int)r.snapshot.outcome.status);
  TEST_ASSERT_EQUAL(33, r.snapshot.outcome.code);
  TEST_ASSERT_EQUAL(0, r.snapshot.settings.count);
  r.store.read_error = false;
  r.owner.service(6000, false, true);
  TEST_ASSERT_EQUAL(0, r.store.writes);
}
void peer_mapping_cannot_rebind_to_changed_address_or_be_restored_in_session() {
  Rig r(peers());
  encodeConfig(camera(), 1, r.store.slots[0]);
  r.start();
  TEST_ASSERT_TRUE(r.snapshot.peers_valid);
  SettingsSave save;
  save.epoch = 7;
  save.generation = 1;
  auto c = camera();
  c.cameras[0].identifier = "01:23:45:67:89:AC";
  TEST_ASSERT_TRUE(copySettings(c, save.settings));
  TEST_ASSERT_TRUE(r.requests.put(save));
  r.owner.service(0, false, true);
  r.owner.service(1000, false, true);
  TEST_ASSERT_TRUE(r.publication.take(r.snapshot));
  TEST_ASSERT_FALSE(r.snapshot.peers_valid);
  TEST_ASSERT_EQUAL(42, r.snapshot.peers.entries[0].id);
  save.generation = 2;
  TEST_ASSERT_TRUE(copySettings(camera(), save.settings));
  TEST_ASSERT_TRUE(r.requests.put(save));
  r.owner.service(2000, false, true);
  r.owner.service(6000, false, true);
  TEST_ASSERT_TRUE(r.publication.take(r.snapshot));
  TEST_ASSERT_FALSE(r.snapshot.peers_valid);
  TEST_ASSERT_EQUAL(42, r.snapshot.peers.entries[0].id);
}
void consumer_generation_limit_cannot_wrap_or_accept_zero_epoch() {
  ApplicationSettings app(7);
  SettingsSnapshot s;
  s.epoch = 7;
  s.completed = true;
  s.generation = std::numeric_limits<uint64_t>::max();
  TEST_ASSERT_TRUE(app.accept(s));
  s.generation = 0;
  TEST_ASSERT_FALSE(app.accept(s));
  s.generation = std::numeric_limits<uint64_t>::max();
  TEST_ASSERT_FALSE(app.accept(s));
  ApplicationSettings invalid(0);
  s.epoch = 0;
  TEST_ASSERT_FALSE(invalid.accept(s));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(insta360_wake_identifiers_survive_fixed_settings_handoff);
  RUN_TEST(peer_mapping_cannot_rebind_to_changed_address_or_be_restored_in_session);
  RUN_TEST(consumer_generation_limit_cannot_wrap_or_accept_zero_epoch);
  RUN_TEST(unavailable_read_during_save_revokes_settings_without_rewrite);
  RUN_TEST(mailbox_concurrent_handoff_never_tears_or_reuses_consumer_data);
  RUN_TEST(owner_rejects_wrong_epoch_invalid_and_busy_without_replacing_candidate);
  RUN_TEST(safe_mode_cancels_pending_even_with_open_nvs_and_cannot_replay);
  RUN_TEST(duplicate_peer_ids_and_session_model_changes_refuse_camera_admission);
  RUN_TEST(startup_is_barrier_and_defaults_inactive);
  RUN_TEST(bounded_copy_ignores_unused_strings_and_rejects_partial_changes);
  RUN_TEST(mailbox_preserves_owned_copy_and_rejects_pressure);
  RUN_TEST(application_rejects_stale_wrong_epoch_and_incomplete);
  RUN_TEST(refusal_is_explicit_read_only_and_local_eligibility_independent);
  RUN_TEST(corrupt_and_future_do_not_publish_camera_settings);
  RUN_TEST(peers_require_separate_stable_unique_model_slot_provisioning);
  RUN_TEST(authorized_save_becomes_effective_only_after_verified_persistence);
  RUN_TEST(publication_pressure_retains_latest_and_refusal_drops_save);
  return UNITY_END();
}
void setUp() {}
void tearDown() {}

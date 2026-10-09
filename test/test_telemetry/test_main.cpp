#include "../fixtures/motion/evidence.h"
#include "telemetry_admission.h"
#include <condition_variable>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <unity.h>
using namespace ridesync;
struct Sink : StorageSink {
  std::string bytes;
  bool flush_ok = true;
  size_t limit = 9999;
  bool mount() override { return true; }
  bool openExclusive(const char *p) override {
    return std::string(p) == "/telemetry-000000000000002a.csv";
  }
  size_t write(const char *p, size_t n) override {
    size_t k = n < limit ? n : limit;
    bytes.append(p, k);
    return k;
  }
  bool flush() override { return flush_ok; }
  void close() override {}
};
struct TestClock : Clock {
  uint32_t time = 0;
  uint32_t now() const override { return time; }
};
ImuEvidence evidence(RecordKind kind = RecordKind::ImuSample) {
  ImuEvidence e;
  e.session_id = 42;
  e.kind = kind;
  e.config.generation = 7;
  e.config.sensor_id = 10;
  e.config.accel_range_mg = 16000;
  e.config.gyro_range_mdps = 2000000;
  e.config.accel_scale_numerator = 1;
  e.config.accel_scale_denominator = 2048;
  e.config.gyro_scale_numerator = 125;
  e.config.gyro_scale_denominator = 2048;
  e.accel[0] = -32768;
  e.gyro[2] = 32767;
  return e;
}
void drain(Storage &s) {
  for (unsigned i = 0; i < 500; ++i)
    s.workerStep();
}
void copied_config_old_anchor_and_kind_health() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::MixedV2});
  TestClock raw;
  SessionClock clock(raw, 42, 1000);
  ImuInbox inbox;
  TelemetryAdmission a(clock, s, inbox);
  TEST_ASSERT_TRUE(clock.anchor({2026, 1, 1, 0, 0, 0, 0}));
  ImuBatch b;
  b.count = 2;
  b.records[0] = evidence(RecordKind::ImuConfig);
  b.records[1] = evidence();
  TEST_ASSERT_TRUE(inbox.publish(b));
  b.records[1].config.generation = 999;
  TEST_ASSERT_EQUAL_UINT32(2, a.tick());
  TEST_ASSERT_TRUE(clock.anchor({2026, 1, 2, 0, 0, 0, 0}));
  drain(s);
  TEST_ASSERT_EQUAL_UINT32(1, s.kindHealth(RecordKind::ImuSample).flushed);
  TEST_ASSERT_EQUAL_UINT32(1, s.kindHealth(RecordKind::ImuConfig).flushed);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, sink.bytes.find("#ridesync_telemetry,2"));
  TEST_ASSERT_EQUAL(std::string::npos, sink.bytes.find("999"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, sink.bytes.find("-32768"));
}
void inbox_overflow_and_gps_reservation() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::MixedV2});
  TestClock raw;
  SessionClock clock(raw, 42, 1000);
  ImuInbox inbox;
  TelemetryAdmission a(clock, s, inbox);
  ImuBatch b;
  b.count = 4;
  for (auto &e : b.records)
    e = evidence();
  for (unsigned i = 0; i < ImuInbox::kCapacity; ++i)
    TEST_ASSERT_TRUE(inbox.publish(b));
  TEST_ASSERT_FALSE(inbox.publish(b));
  TEST_ASSERT_EQUAL_UINT32(4, inbox.dropped(RecordKind::ImuSample));
  for (unsigned i = 0; i < 4; ++i)
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(2, a.tick());
  TEST_ASSERT_EQUAL_UINT32(6, s.kindHealth(RecordKind::ImuSample).accepted);
  TEST_ASSERT_EQUAL_UINT32(2, s.kindHealth(RecordKind::ImuSample).dropped);
  ModemSnapshot gps;
  gps.session_id = 42;
  TEST_ASSERT_TRUE(a.gps(gps));
  TEST_ASSERT_TRUE(a.event(evidence(RecordKind::ImuHealth)));
  TEST_ASSERT_FALSE(a.gps(gps));
  drain(s);
  TEST_ASSERT_EQUAL_UINT32(1, s.kindHealth(RecordKind::Gps).flushed);
}
void cross_session_and_stop_final_publication() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::MixedV2});
  TestClock raw;
  SessionClock clock(raw, 42, 1000);
  ImuInbox inbox;
  TelemetryAdmission a(clock, s, inbox);
  auto e = evidence();
  e.session_id = 43;
  TEST_ASSERT_FALSE(a.event(e));
  TEST_ASSERT_EQUAL_UINT32(1, s.kindHealth(RecordKind::ImuSample).rejected);
  a.requestStop();
  TEST_ASSERT_TRUE(inbox.stopRequested());
  ModemSnapshot gps;
  gps.session_id = 42;
  TEST_ASSERT_FALSE(a.gps(gps));
  TEST_ASSERT_EQUAL_UINT32(1, s.kindHealth(RecordKind::Gps).dropped);
  TEST_ASSERT_FALSE(a.stopped());
  ImuBatch b;
  b.count = 1;
  b.records[0] = evidence();
  TEST_ASSERT_TRUE(inbox.publish(b));
  inbox.finish();
  TEST_ASSERT_FALSE(inbox.publish(b));
  TEST_ASSERT_EQUAL_UINT32(1, a.tick());
  TEST_ASSERT_TRUE(a.stopped());
  drain(s);
  TEST_ASSERT_EQUAL_UINT32(1, s.kindHealth(RecordKind::ImuSample).flushed);
  TEST_ASSERT_TRUE(s.health().stopped);
}
void mixed_flush_and_partial_loss() {
  for (bool short_write : {false, true}) {
    for (auto format : {StorageFormat::MixedV2, StorageFormat::MotionV4}) {
      Sink sink;
      Storage s(sink, {42, "fw", "synthetic", 2, 4, format});
      TestClock raw;
      SessionClock clock(raw, 42, 1000);
      ImuInbox inbox;
      TelemetryAdmission a(clock, s, inbox);
      TEST_ASSERT_TRUE(a.event(evidence()));
      TEST_ASSERT_TRUE(a.event(evidence(RecordKind::ImuHealth)));
      if (short_write)
        sink.limit = 3;
      else
        sink.flush_ok = false;
      drain(s);
      TEST_ASSERT_TRUE(s.health().terminal);
      TEST_ASSERT_TRUE(s.health().stopped);
      TEST_ASSERT_EQUAL_UINT32(1, s.kindHealth(RecordKind::ImuSample).lost);
      TEST_ASSERT_EQUAL_UINT32(1, s.kindHealth(RecordKind::ImuHealth).lost);
      TEST_ASSERT_EQUAL_UINT32(0, s.kindHealth(RecordKind::ImuSample).flushed);
    }
  }
}

struct CloseSink : Sink {
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false, release = false;
  void close() override {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    cv.notify_one();
    cv.wait(lock, [&] { return release; });
  }
};
void terminal_close_remains_supervised() {
  CloseSink sink;
  sink.flush_ok = false;
  Storage s(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::MixedV2});
  TestClock raw;
  SessionClock clock(raw, 42, 1000);
  ImuInbox inbox;
  TelemetryAdmission a(clock, s, inbox);
  TEST_ASSERT_TRUE(a.event(evidence()));
  std::thread worker([&] { drain(s); });
  {
    std::unique_lock<std::mutex> lock(sink.mutex);
    sink.cv.wait(lock, [&] { return sink.entered; });
  }
  const auto h = s.health();
  TEST_ASSERT_TRUE(h.terminal);
  TEST_ASSERT_FALSE(h.stopped);
  TEST_ASSERT_EQUAL_UINT32(1, s.kindHealth(RecordKind::ImuSample).lost);
  TEST_ASSERT_FALSE(a.event(evidence()));
  {
    std::lock_guard<std::mutex> lock(sink.mutex);
    sink.release = true;
  }
  sink.cv.notify_one();
  worker.join();
  TEST_ASSERT_TRUE(s.health().stopped);
}
void metadata_survives_dropped_config_event_and_invalid_evidence() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::MixedV2});
  TestClock raw;
  SessionClock clock(raw, 42, 1000);
  ImuInbox inbox;
  TelemetryAdmission a(clock, s, inbox);
  for (unsigned i = 0; i < 8; ++i)
    TEST_ASSERT_TRUE(a.event(evidence(RecordKind::ImuHealth)));
  TEST_ASSERT_FALSE(a.event(evidence(RecordKind::ImuConfig)));
  drain(s);
  TEST_ASSERT_TRUE(a.event(evidence()));
  drain(s);
  TEST_ASSERT_EQUAL_UINT32(1, s.kindHealth(RecordKind::ImuConfig).dropped);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, sink.bytes.find(",7,10,0,0,"));
  auto e = evidence();
  e.config.accel_scale_denominator = 0;
  TEST_ASSERT_FALSE(a.event(e));
  e = evidence();
  e.config.mount_state = Qualification::Qualified;
  TEST_ASSERT_FALSE(a.event(e));
  e = evidence();
  e.kind = RecordKind::ImuControl;
  e.sensor_time_present = true;
  e.event_code = 5;
  e.event_length = 3;
  e.sensor_time_ticks24 = 1;
  TEST_ASSERT_FALSE(a.event(e));
  TEST_ASSERT_EQUAL_UINT32(2, s.kindHealth(RecordKind::ImuSample).rejected);
  TEST_ASSERT_EQUAL_UINT32(1, s.kindHealth(RecordKind::ImuControl).rejected);
  Storage legacy(sink, {42, "fw", "synthetic"});
  TEST_ASSERT_FALSE(legacy.enqueueImu(clock.snapshot(), evidence()));
}
void concurrent_copied_inbox_storage_and_final_finish() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::MixedV2});
  TestClock raw;
  SessionClock clock(raw, 42, 1000);
  ImuInbox inbox;
  TelemetryAdmission a(clock, s, inbox);
  std::thread transport([&] {
    ImuBatch b;
    b.count = 4;
    for (unsigned sequence = 0; sequence < 1000; ++sequence) {
      for (auto &e : b.records) {
        e = evidence();
        e.batch_sequence = sequence;
      }
      while (!inbox.publish(b))
        std::this_thread::yield();
    }
    inbox.finish();
  });
  std::thread worker([&] {
    while (!s.health().stopped) {
      s.workerStep();
      std::this_thread::yield();
    }
  });
  a.requestStop();
  while (!a.stopped()) {
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(2, a.tick());
    std::this_thread::yield();
  }
  transport.join();
  worker.join();
  auto h = s.kindHealth(RecordKind::ImuSample);
  TEST_ASSERT_EQUAL_UINT32(4000, h.accepted + h.dropped);
  TEST_ASSERT_EQUAL_UINT32(h.accepted, h.flushed);
  TEST_ASSERT_EQUAL_UINT32(0, h.lost);
}

void gps_progress_under_sustained_backlog_and_clock_reset_rejection() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::MixedV2});
  TestClock raw;
  SessionClock clock(raw, 42, 1000);
  ImuInbox inbox;
  TelemetryAdmission a(clock, s, inbox);
  ImuBatch batch;
  batch.count = 4;
  for (auto &e : batch.records)
    e = evidence();
  ModemSnapshot gps;
  gps.session_id = 42;
  for (unsigned round = 0; round < 100; ++round) {
    for (unsigned i = 0; i < 4; ++i)
      inbox.publish(batch);
    for (unsigned i = 0; i < 3; ++i)
      TEST_ASSERT_EQUAL_UINT32(2, a.tick());
    TEST_ASSERT_TRUE(a.gps(clock.snapshot(), gps));
    TEST_ASSERT_TRUE(a.event(evidence(RecordKind::ImuHealth)));
    drain(s);
  }
  TEST_ASSERT_EQUAL_UINT32(100, s.kindHealth(RecordKind::Gps).flushed);
  TEST_ASSERT_TRUE(clock.reset(43));
  const auto before = s.kindHealth(RecordKind::ImuSample).rejected;
  TEST_ASSERT_EQUAL_UINT32(2, a.tick());
  TEST_ASSERT_EQUAL_UINT32(before + 2, s.kindHealth(RecordKind::ImuSample).rejected);
}
void v4_copies_paired_evidence_and_preserves_raw_rows() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::MotionV4});
  TestClock raw;
  SessionClock clock(raw, 42, 1000);
  auto e = motion_fixture::sample(42, 0);
  MotionEvidence m;
  m.state = MotionAdmission::Enabled;
  m.config = motion_fixture::config();
  m.snapshot_max_age_ms = 100;
  m.estimate.measurements_valid = true;
  m.estimate.specific_force_mps2.z = 9.80665f;
  TEST_ASSERT_TRUE(s.enqueueImu(clock.snapshot(), e, false, m));
  e.accel[2] = -1;
  m.estimate.specific_force_mps2.z = -99;
  drain(s);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, sink.bytes.find("#ridesync_telemetry,4\n"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos,
                        sink.bytes.find("#imu_layout,4,see_docs/motion_logging.md\n"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos,
                        sink.bytes.find("#camera_layout,3,see_docs/log_format.md\n"));
  TEST_ASSERT_EQUAL(std::string::npos, sink.bytes.find("-99"));
  TEST_ASSERT_EQUAL_UINT32(1, s.kindHealth(RecordKind::ImuSample).accepted);
}
void v4_refuses_nonfinite_dynamic_and_inconsistent_presence() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::MotionV4});
  TestClock raw;
  SessionClock clock(raw, 42, 1000);
  auto e = motion_fixture::sample(42, 0);
  MotionEvidence m;
  m.state = MotionAdmission::Enabled;
  m.config = motion_fixture::config();
  m.snapshot_max_age_ms = 100;
  m.estimate.measurements_valid = true;
  m.estimate.specific_force_mps2.z = std::numeric_limits<float>::infinity();
  TEST_ASSERT_FALSE(s.enqueueImu(clock.snapshot(), e, false, m));
  m.estimate.specific_force_mps2.z = 9.80665f;
  m.estimate.dynamic_lean_valid = true;
  TEST_ASSERT_FALSE(s.enqueueImu(clock.snapshot(), e, false, m));
  m.estimate.dynamic_lean_valid = false;
  m.estimate.static_tilt_valid = true;
  TEST_ASSERT_FALSE(s.enqueueImu(clock.snapshot(), e, false, m));
  m.estimate.static_tilt_valid = false;
  e.config.mount_id = 123;
  TEST_ASSERT_FALSE(s.enqueueImu(clock.snapshot(), e, false, m));
  e.config.mount_id = 7;
  m = {};
  m.state = MotionAdmission::Refused;
  TEST_ASSERT_TRUE(s.enqueueImu(clock.snapshot(), e, false, m));
  drain(s);
  TEST_ASSERT_EQUAL_UINT32(4, s.kindHealth(RecordKind::ImuSample).rejected);
  TEST_ASSERT_EQUAL_UINT32(1, s.kindHealth(RecordKind::ImuSample).written);
}
struct ReferenceSource : StaticMotionReferenceSource {
  unsigned calls = 0;
  uint32_t declaration = 0;
  bool stationary = true, reuse = false;
  StaticMotionReference referenceFor(const ImuEvidence &e) override {
    ++calls;
    StaticMotionReference r;
    r.externally_stationary = stationary;
    r.session_id = e.session_id;
    r.config_generation = e.config.generation;
    r.batch_sequence = e.batch_sequence;
    r.declaration = reuse ? declaration : ++declaration;
    return r;
  }
};
MotionAdmissionConfig motionOptions(MotionInputRoute route = MotionInputRoute::Inbox) {
  MotionAdmissionConfig m;
  m.requested = m.imu_qualified = true;
  m.estimator = motion_fixture::config();
  m.snapshot_max_age_ms = 100;
  m.route = route;
  return m;
}
void motion_route_current_and_declaration_lifetime() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::MotionV4});
  TestClock raw;
  SessionClock clock(raw, 42, 1000);
  ImuInbox inbox;
  ReferenceSource source;
  TelemetryAdmission a(clock, s, inbox, nullptr, motionOptions(), &source);
  ImuBatch b;
  b.count = 1;
  b.records[0] = motion_fixture::sample(42, 0);
  TEST_ASSERT_TRUE(inbox.publish(b));
  a.beginMotionPass();
  TEST_ASSERT_EQUAL_UINT8(1, a.tick());
  auto current = a.motionSnapshot(0);
  TEST_ASSERT_TRUE(current.current);
  TEST_ASSERT_TRUE(current.estimate.static_tilt_valid);
  TEST_ASSERT_FLOAT_WITHIN(.0001, 9.80665, current.estimate.specific_force_mps2.z);
  TEST_ASSERT_FALSE(a.motionSnapshot(101).current);
  a.withdrawMotionReference();
  source.reuse = true;
  TEST_ASSERT_TRUE(inbox.publish(b));
  a.tick();
  TEST_ASSERT_TRUE(a.motionSnapshot(0).current);
  TEST_ASSERT_FALSE(a.motionSnapshot(0).estimate.static_tilt_valid);
  TEST_ASSERT_TRUE(inbox.publish(b));
  TEST_ASSERT_TRUE(a.event(b.records[0]));
  TEST_ASSERT_EQUAL_INT(MotionAdmission::Revoked, a.motionAdmission());
  a.tick();
  TEST_ASSERT_EQUAL_UINT32(2, source.calls);
  TEST_ASSERT_FALSE(a.motionSnapshot(0).current);
  drain(s);
  TEST_ASSERT_EQUAL_UINT32(4, s.kindHealth(RecordKind::ImuSample).accepted);
}
void motion_direct_timing_and_stop() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::MotionV4});
  TestClock raw;
  SessionClock clock(raw, 42, 1000);
  ImuInbox inbox;
  TelemetryAdmission a(clock, s, inbox, nullptr, motionOptions(MotionInputRoute::Direct));
  auto e = motion_fixture::sample(42, 0);
  TEST_ASSERT_TRUE(a.event(e));
  TEST_ASSERT_TRUE(a.motionSnapshot(0).current);
  TEST_ASSERT_FALSE(a.motionSnapshot(0).estimate.static_tilt_valid);
  a.beginMotionPass();
  TEST_ASSERT_FALSE(a.motionSnapshot(0).current);
  e.receipt_known = false;
  TEST_ASSERT_TRUE(a.event(e));
  TEST_ASSERT_FALSE(a.motionSnapshot(0).current);
  e.receipt_known = true;
  e.timing_flags = 1;
  TEST_ASSERT_TRUE(a.event(e));
  TEST_ASSERT_FALSE(a.motionSnapshot(0).current);
  a.requestStop();
  TEST_ASSERT_EQUAL_INT(MotionAdmission::Revoked, a.motionAdmission());
  drain(s);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(motion_route_current_and_declaration_lifetime);
  RUN_TEST(motion_direct_timing_and_stop);
  RUN_TEST(v4_copies_paired_evidence_and_preserves_raw_rows);
  RUN_TEST(v4_refuses_nonfinite_dynamic_and_inconsistent_presence);
  RUN_TEST(copied_config_old_anchor_and_kind_health);
  RUN_TEST(inbox_overflow_and_gps_reservation);
  RUN_TEST(cross_session_and_stop_final_publication);
  RUN_TEST(mixed_flush_and_partial_loss);
  RUN_TEST(terminal_close_remains_supervised);
  RUN_TEST(metadata_survives_dropped_config_event_and_invalid_evidence);
  RUN_TEST(concurrent_copied_inbox_storage_and_final_finish);
  RUN_TEST(gps_progress_under_sustained_backlog_and_clock_reset_rejection);
  return UNITY_END();
}

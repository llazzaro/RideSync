#include "camera_event_session.h"
#include "local_telemetry_runtime.h"
#include <string>
#include <unity.h>
using namespace ridesync;
struct RawClock : Clock {
  uint32_t value = 0;
  uint32_t now() const override { return value; }
};
struct Disk : StorageSink {
  std::string bytes;
  bool mount() override { return true; }
  bool openExclusive(const char *) override { return true; }
  size_t write(const char *s, size_t n) override {
    bytes.append(s, n);
    return n;
  }
  bool flush() override { return true; }
  void close() override {}
};
struct Adapter : CameraTransport, CameraRuntimePort {
  bool begin(size_t, const CameraConfig &, Operation, Token) override { return true; }
  void cancel(size_t, Token) override {}
  void close(size_t, Token) override {}
  void attachGroup(RecordingManager &) override {}
  void detachGroup(const RecordingManager *) override {}
  void service(CameraServiceAction *action = nullptr) override {
    if (action)
      action->beforeAdvance();
  }
  void stop() override {}
  bool canDestroy() const override { return true; }
  Hero12Fault fault(uint8_t) const override { return Hero12Fault::None; }
  CameraError requestRecovery(uint8_t, bool, Hero12PowerCondition) override {
    return CameraError::Unsupported;
  }
  CameraError cancelRecovery(uint8_t) override { return CameraError::None; }
  Hero12RecoveryState recoveryState(uint8_t) const override { return {}; }
  bool recoveryReady(uint8_t) const override { return false; }
  bool commandReady(uint8_t) const override { return false; }
  bool linkRetiring(uint8_t) const override { return false; }
  bool linkReleased(uint8_t) const override { return true; }
};
struct Uart : ModemUart {
  size_t available() override { return 0; }
  int read() override { return -1; }
  size_t writable() override { return 256; }
  size_t write(const char *, size_t n) override { return n; }
};
struct StorageWorker : TelemetryStorageWorker {
  Disk disk;
  Storage *bound = nullptr;
  bool finished = false;
  bool start() override { return true; }
  IdentityAllocation allocation() const override {
    IdentityAllocation result;
    result.status = IdentityStatus::Committed;
    result.id = 45;
    return result;
  }
  StorageSink &sink() override { return disk; }
  bool bind(Storage &s) override {
    bound = &s;
    return true;
  }
  void cancel() override {}
  bool workerFinished() const override { return finished; }
  int ioError() const override { return 0; }
  void step() {
    if (bound && !finished) {
      bound->workerStep();
      finished = bound->health().stopped;
    }
  }
};
struct ImuWorker : TelemetryImuWorker {
  bool stop_requested = false;
  bool finished = false;
  bool start(ImuInbox &, uint64_t, bool) override { return true; }
  void requestStop() override { stop_requested = true; }
  ImuWorkerObservation observation() const override {
    ImuWorkerObservation result;
    result.outcome = DeviceHealth::Ok;
    result.finished = finished;
    return result;
  }
};
void composed_v7_keeps_explicit_local_bookmark_admission() {
  RawClock raw;
  Disk disk;
  Adapter adapter;
  CameraManager manager(raw, adapter);
  RecordingManager group(manager, raw);
  CameraEventSession session(raw, disk, adapter, manager, group, 42, "fw", "synthetic",
                             MotionAdmissionConfig{}, nullptr, true, false, true);
  TEST_ASSERT_TRUE(session.storage().configValid());
  TEST_ASSERT_TRUE(session.activateLocal());
  const auto receipt = session.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
  TEST_ASSERT_EQUAL_INT(BookmarkOutcome::Admitted, receipt.outcome);
  TEST_ASSERT_EQUAL_UINT32(1, receipt.bookmark_id);
  session.finishImu();
  session.requestStop();
  for (unsigned i = 0; i < 500 && !session.stopped(); ++i) {
    session.service();
    session.storage().workerStep();
  }
  TEST_ASSERT_TRUE(session.stopped());
  TEST_ASSERT_NOT_EQUAL(std::string::npos, disk.bytes.find("#ridesync_telemetry,7\n"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, disk.bytes.find("\nbookmark,"));
}
void health_v7_without_bookmark_opt_in_refuses_without_id() {
  RawClock raw;
  Disk disk;
  Adapter adapter;
  CameraManager manager(raw, adapter);
  RecordingManager group(manager, raw);
  CameraEventSession session(raw, disk, adapter, manager, group, 43, "fw", "synthetic",
                             MotionAdmissionConfig{}, nullptr, false, false, true);
  TEST_ASSERT_TRUE(session.activateLocal());
  const auto receipt = session.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
  TEST_ASSERT_EQUAL_INT(BookmarkOutcome::DisabledFormat, receipt.outcome);
  TEST_ASSERT_EQUAL_UINT32(0, receipt.bookmark_id);
  TEST_ASSERT_EQUAL_UINT32(1, session.storage().kindHealth(RecordKind::Bookmark).rejected);
}
void native_without_local_bookmark_format_refuses_activation() {
  RawClock raw;
  Disk disk;
  Adapter adapter;
  CameraManager manager(raw, adapter);
  RecordingManager group(manager, raw);
  CameraEventSession session(raw, disk, adapter, manager, group, 44, "fw", "synthetic",
                             MotionAdmissionConfig{}, nullptr, false, true, false);
  TEST_ASSERT_FALSE(session.storage().configValid());
  TEST_ASSERT_FALSE(session.activateLocal());
}
void runtime_stop_refuses_bookmark_while_imu_drains() {
  RawClock raw;
  Uart uart;
  StorageWorker storage;
  ImuWorker imu;
  Adapter adapter;
  CameraManager manager(raw, adapter);
  RecordingManager group(manager, raw);
  LocalTelemetryConfig config;
  config.opt_in = config.gps_qualified = config.imu_enabled = config.imu_qualified = true;
  config.bookmark_enabled = true;
  config.power_timing.qualified = true;
  config.modem.documentary_profile_opt_in = true;
  config.modem.terminal_retires_transaction = true;
  config.firmware = "fw";
  config.provenance = "synthetic";
  LocalTelemetryRuntime runtime(raw, uart, storage, imu, adapter, manager, group, config);
  TEST_ASSERT_TRUE(runtime.start());
  runtime.service();
  TEST_ASSERT_EQUAL_INT(TelemetryPhase::Running, runtime.status().phase);
  const auto admitted = runtime.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
  TEST_ASSERT_EQUAL_INT(BookmarkOutcome::Admitted, admitted.outcome);
  TEST_ASSERT_EQUAL_UINT32(1,
                           runtime.session()->storage().kindHealth(RecordKind::Bookmark).accepted);
  runtime.requestStop();
  TEST_ASSERT_TRUE(imu.stop_requested);
  TEST_ASSERT_EQUAL_INT(TelemetryPhase::Stopping, runtime.status().phase);
  const auto refused = runtime.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
  TEST_ASSERT_EQUAL_INT(BookmarkOutcome::Inactive, refused.outcome);
  TEST_ASSERT_EQUAL_UINT32(0, refused.bookmark_id);
  TEST_ASSERT_EQUAL_UINT32(1,
                           runtime.session()->storage().kindHealth(RecordKind::Bookmark).accepted);
  imu.finished = true;
  for (unsigned i = 0; i < 500 && !runtime.canRelease(); ++i) {
    runtime.service();
    storage.step();
  }
  TEST_ASSERT_TRUE(runtime.canRelease());
  TEST_ASSERT_NOT_EQUAL(std::string::npos, storage.disk.bytes.find("\nbookmark,"));
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(composed_v7_keeps_explicit_local_bookmark_admission);
  RUN_TEST(health_v7_without_bookmark_opt_in_refuses_without_id);
  RUN_TEST(native_without_local_bookmark_format_refuses_activation);
  RUN_TEST(runtime_stop_refuses_bookmark_while_imu_drains);
  return UNITY_END();
}

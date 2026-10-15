#include "camera_event_session.h"
#include "handlebar_control.h"
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
  unsigned commands = 0;
  bool begin(size_t, const CameraConfig &, Operation, Token) override {
    ++commands;
    return true;
  }
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

struct Button : ButtonInput {
  bool down = false;
  bool pressed() override { return down; }
};
struct Led : LedSink {
  int write(LedFrame) override { return 0; }
};
struct MarkHarness {
  RawClock raw;
  Uart uart;
  StorageWorker storage;
  ImuWorker imu;
  Adapter adapter;
  CameraManager manager{raw, adapter};
  RecordingManager group{manager, raw};
  Button record, mark;
  Led led;
  HandlebarControl control{raw, adapter, manager, group, record, led};
  LocalTelemetryConfig config;
  LocalTelemetryRuntime *runtime = nullptr;
  void start(bool enabled = true) {
    config.opt_in = config.gps_qualified = config.power_timing.qualified = true;
    config.modem.documentary_profile_opt_in = config.modem.terminal_retires_transaction = true;
    config.bookmark_enabled = enabled;
    config.firmware = "fw";
    config.provenance = "synthetic";
    runtime = new LocalTelemetryRuntime(raw, uart, storage, imu, adapter, manager, group, config);
    TEST_ASSERT_TRUE(runtime->attachControl(control));
    TEST_ASSERT_TRUE(control.begin(ButtonConfig{}, &mark, 20));
    TEST_ASSERT_TRUE(runtime->start());
    tick(0, false);
    TEST_ASSERT_EQUAL_INT(TelemetryPhase::Running, runtime->status().phase);
    tick(20, false);
  }
  void tick(uint32_t now, bool pressed) {
    raw.value = now;
    mark.down = pressed;
    runtime->service();
  }
  void press(uint32_t now) {
    tick(now, true);
    tick(now + 20, true);
  }
  void release(uint32_t now) {
    tick(now, false);
    tick(now + 20, false);
  }
  ~MarkHarness() {
    if (!runtime)
      return;
    runtime->requestStop();
    for (unsigned i = 0; i < 1000 && !runtime->canRelease(); ++i) {
      runtime->service();
      storage.step();
    }
    runtime->detachControl(control);
    delete runtime;
  }
};
void dedicated_mark_logs_short_press_without_cameras_or_fix() {
  MarkHarness h;
  h.start();
  h.press(30);
  auto receipt = h.control.status().bookmark;
  TEST_ASSERT_EQUAL_INT(BookmarkOutcome::Admitted, receipt.outcome);
  TEST_ASSERT_EQUAL_UINT32(1, receipt.bookmark_id);
  TEST_ASSERT_EQUAL_UINT64(50, receipt.timestamp.monotonic_ms);
  TEST_ASSERT_FALSE(receipt.timestamp.has_utc_estimate);
  h.tick(2000, true);
  TEST_ASSERT_EQUAL_UINT32(1, h.control.status().accepted);
  h.release(2010);
  h.press(2040);
  TEST_ASSERT_EQUAL_UINT32(2, h.control.status().bookmark.bookmark_id);
  TEST_ASSERT_EQUAL_UINT(0, h.adapter.commands);
  for (unsigned i = 0; i < 500; ++i)
    h.storage.step();
  TEST_ASSERT_NOT_EQUAL(std::string::npos, h.storage.disk.bytes.find(",1,2,1\n"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, h.storage.disk.bytes.find(",2,2,1\n"));
}
void dedicated_mark_refusal_does_not_retry_a_held_button() {
  MarkHarness h;
  h.start();
  BookmarkReceipt last;
  for (unsigned i = 0; i < 10; ++i) {
    last = h.runtime->bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
    if (last.outcome == BookmarkOutcome::Overflow)
      break;
  }
  TEST_ASSERT_EQUAL_INT(BookmarkOutcome::Overflow, last.outcome);
  h.press(30);
  TEST_ASSERT_EQUAL_INT(BookmarkOutcome::Overflow, h.control.status().bookmark.outcome);
  const auto refused_id = h.control.status().bookmark.bookmark_id;
  TEST_ASSERT_EQUAL_UINT32(1, h.control.status().overflow);
  for (unsigned i = 0; i < 500; ++i)
    h.storage.step();
  h.tick(2000, true);
  TEST_ASSERT_EQUAL_UINT32(refused_id, h.control.status().bookmark.bookmark_id);
  TEST_ASSERT_EQUAL_UINT32(0, h.control.status().accepted);
  h.release(2010);
  h.press(2040);
  TEST_ASSERT_EQUAL_INT(BookmarkOutcome::Admitted, h.control.status().bookmark.outcome);
  TEST_ASSERT_EQUAL_UINT32(refused_id + 1, h.control.status().bookmark.bookmark_id);
}
void dedicated_mark_disabled_reset_and_shutdown_retire_pending_presses() {
  {
    MarkHarness h;
    h.start(false);
    h.press(30);
    TEST_ASSERT_EQUAL_INT(BookmarkOutcome::DisabledFormat, h.control.status().bookmark.outcome);
    TEST_ASSERT_EQUAL_UINT32(0, h.control.status().accepted);
    TEST_ASSERT_EQUAL_UINT32(0, h.control.status().bookmark.bookmark_id);
  }
  MarkHarness h;
  h.start();
  h.tick(30, true);
  h.control.reset();
  h.tick(50, true);
  h.tick(1000, true);
  TEST_ASSERT_EQUAL_UINT32(0, h.control.status().accepted);
  h.release(1010);
  h.press(1040);
  TEST_ASSERT_EQUAL_UINT32(1, h.control.status().accepted);
  h.release(1070);
  h.tick(1100, true);
  h.runtime->requestStop();
  h.tick(1120, true);
  TEST_ASSERT_EQUAL_UINT32(1, h.control.status().accepted);
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(composed_v7_keeps_explicit_local_bookmark_admission);
  RUN_TEST(health_v7_without_bookmark_opt_in_refuses_without_id);
  RUN_TEST(native_without_local_bookmark_format_refuses_activation);
  RUN_TEST(runtime_stop_refuses_bookmark_while_imu_drains);
  RUN_TEST(dedicated_mark_logs_short_press_without_cameras_or_fix);
  RUN_TEST(dedicated_mark_refusal_does_not_retry_a_held_button);
  RUN_TEST(dedicated_mark_disabled_reset_and_shutdown_retire_pending_presses);
  return UNITY_END();
}

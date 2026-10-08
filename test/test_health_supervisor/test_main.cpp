#include "health_supervisor.h"
#include "recording_manager.h"
#include "storage.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unity.h>
#include <vector>
using namespace ridesync;
std::array<WorkerPolicy, 4> required() {
  std::array<WorkerPolicy, 4> p{};
  p[0].enabled = p[0].required = p[0].qualified = true;
  p[0].deadline_ms = 500;
  p[0].startup_grace_ms = 300;
  return p;
}
void stalled_required_worker_withholds_feed_even_while_control_runs() {
  HealthSupervisor h;
  TEST_ASSERT_TRUE(h.begin(required(), 0));
  h.progress(Worker::At).completed(DeviceHealth::NoFix);
  TEST_ASSERT_TRUE(h.evaluate(100).feed);
  for (uint32_t now = 200; now <= 500; now += 100) {
    auto d = h.evaluate(now);
    TEST_ASSERT_TRUE(d.execution_healthy);
    TEST_ASSERT_FALSE(d.feed); // Loop execution alone cannot renew subscription.
  }
  auto d = h.evaluate(600);
  TEST_ASSERT_FALSE(d.execution_healthy);
  TEST_ASSERT_EQUAL(1, d.stalled);
}

void absent_devices_storms_and_exhaustion_keep_execution_live() {
  HealthSupervisor h;
  TEST_ASSERT_TRUE(h.begin(required(), 0));
  for (auto outcome : {DeviceHealth::Missing, DeviceHealth::NoFix, DeviceHealth::Desynchronized,
                       DeviceHealth::RetryExhausted, DeviceHealth::DisconnectStorm}) {
    h.progress(Worker::At).completed(outcome);
    TEST_ASSERT_TRUE(h.evaluate(100).feed);
    TEST_ASSERT_EQUAL((int)outcome, (int)h.progress(Worker::At).outcome());
  }
}
void grace_deadline_and_generation_rollover_are_bounded() {
  HealthSupervisor h;
  TEST_ASSERT_TRUE(h.begin(required(), UINT32_MAX - 200));
  TEST_ASSERT_TRUE(h.evaluate(UINT32_MAX - 1).feed);
  TEST_ASSERT_FALSE(h.evaluate(99).feed); // exactly 300 ms grace across wrap
  h.progress(Worker::At).observe(UINT32_MAX, DeviceHealth::Ok);
  TEST_ASSERT_TRUE(h.evaluate(100).feed);
  h.progress(Worker::At).observe(0, DeviceHealth::Ok);
  TEST_ASSERT_TRUE(h.evaluate(200).feed);
  h.progress(Worker::At).observe(0, DeviceHealth::Missing);
  TEST_ASSERT_FALSE(h.evaluate(300).feed);
  TEST_ASSERT_FALSE(h.evaluate(700).execution_healthy);
}
void saturated_progress_and_optional_stall_cannot_fake_required_liveness() {
  HealthSupervisor h;
  auto p = required();
  p[1] = p[0];
  p[1].required = false;
  TEST_ASSERT_TRUE(h.begin(p, 0));
  h.progress(Worker::At).observe(UINT32_MAX, DeviceHealth::Ok);
  TEST_ASSERT_TRUE(h.evaluate(100).feed);
  h.progress(Worker::At).completed(); // saturation remains same value
  TEST_ASSERT_FALSE(h.evaluate(200).feed);
  TEST_ASSERT_FALSE(h.evaluate(600).feed);
  h.progress(Worker::At).observe(0, DeviceHealth::RetryExhausted);
  auto d = h.evaluate(700);
  TEST_ASSERT_TRUE(d.feed);
  TEST_ASSERT_EQUAL(2, d.stalled); // optional BLE stall is surfaced, not escalated
}
void invalid_configuration_cannot_start_or_feed() {
  for (int kind = 0; kind < 6; ++kind) {
    HealthSupervisor h;
    auto p = required();
    if (kind == 0)
      p[0].enabled = false;
    if (kind == 1)
      p[0].qualified = false;
    if (kind == 2)
      p[0].deadline_ms = 0;
    if (kind == 3)
      p[0].deadline_ms = 5000;
    if (kind == 4)
      p[0].startup_grace_ms = 2000;
    if (kind == 5)
      p[0].startup_grace_ms = 0;
    TEST_ASSERT_FALSE(h.begin(p, 0));
    TEST_ASSERT_FALSE(h.evaluate(100).feed);
  }
  HealthSupervisor h;
  TEST_ASSERT_TRUE(h.begin({}, 0));
  TEST_ASSERT_TRUE(h.evaluate(100).feed); // protects own schedulability, no fake worker
  TEST_ASSERT_FALSE(h.begin(required(), 200));
}
void boot_failures_latch_until_explicit_clear_and_cold_record_is_untrusted() {
  BootRecovery b;
  auto s = b.begin({}, ResetClass::Cold, 0);
  TEST_ASSERT_FALSE(s.retention_valid);
  TEST_ASSERT_TRUE(BootRecovery::valid(b.record()));
  for (int i = 0; i < 3; ++i) {
    s = b.begin(b.record(), ResetClass::Watchdog, 0);
    TEST_ASSERT_TRUE(s.retention_valid);
  }
  TEST_ASSERT_TRUE(s.safe_mode);
  TEST_ASSERT_EQUAL(3, b.record().failed_boots);
  b.execution(1, true);
  b.execution(60001, true);
  TEST_ASSERT_TRUE(b.record().safe_mode);
  s = b.begin(b.record(), ResetClass::Operator, 0);
  TEST_ASSERT_TRUE(s.safe_mode);
  b.operatorClear(0);
  TEST_ASSERT_FALSE(b.record().safe_mode);
  auto r = b.record();
  r.failed_boots ^= 1;
  TEST_ASSERT_FALSE(b.begin(r, ResetClass::Panic, 0).retention_valid);
  TEST_ASSERT_EQUAL(0, b.record().failed_boots);
  TEST_ASSERT_FALSE(b.begin(b.record(), ResetClass::Brownout, 0).retention_valid);
  TEST_ASSERT_FALSE(b.begin(b.record(), ResetClass::Unknown, 0).retention_valid);
}
void stable_window_needs_worker_execution_handles_wrap_and_counters_saturate() {
  BootRecovery b;
  b.begin({}, ResetClass::Cold, 0);
  b.begin(b.record(), ResetClass::Panic, 0);
  TEST_ASSERT_FALSE(b.execution(UINT32_MAX - 100, true));
  TEST_ASSERT_FALSE(b.execution(59898, true));
  TEST_ASSERT_TRUE(b.execution(59899, true));
  TEST_ASSERT_EQUAL(0, b.record().failed_boots);
  TEST_ASSERT_FALSE(b.record().armed);
  b.begin(b.record(), ResetClass::Panic, 0);
  TEST_ASSERT_EQUAL(0, b.record().failed_boots); // previous stable boot not failed
  b.execution(0, true);
  b.execution(30000, false);
  TEST_ASSERT_FALSE(b.execution(60000, true));
  TEST_ASSERT_FALSE(b.execution(119999, true));
  TEST_ASSERT_TRUE(b.execution(120000, true));
  b.operatorClear(0);
  for (int i = 0; i < 300; ++i)
    b.begin(b.record(), ResetClass::Watchdog, 0);
  TEST_ASSERT_EQUAL(255, b.record().failed_boots);
  TEST_ASSERT_EQUAL(255, b.record().watchdog_resets);
}
void application_restart_cause_is_consumed_separately_from_sdk_class() {
  BootRecovery b;
  b.begin({}, ResetClass::Cold, 0);
  b.annotateHealthRestart();
  auto s = b.begin(b.record(), ResetClass::Software, 0);
  TEST_ASSERT_EQUAL((int)AppResetCause::RequiredWorkerStall, (int)s.previous_app_cause);
  TEST_ASSERT_EQUAL(1, b.record().health_restarts);
  TEST_ASSERT_EQUAL(0, b.record().watchdog_resets);
  TEST_ASSERT_EQUAL(1, b.record().failed_boots);
  b.begin(b.record(), ResetClass::Operator, 0);
  TEST_ASSERT_EQUAL(1, b.record().failed_boots);
  TEST_ASSERT_EQUAL(1, b.record().health_restarts);
  b.annotateHealthRestart();
  b.begin(b.record(), ResetClass::Operator, 0);
  TEST_ASSERT_EQUAL(1,
                    b.record().health_restarts); // external operator reset cannot prove SW restart
}
void annotation_after_stable_boot_rearms_failure_accounting() {
  BootRecovery b;
  b.begin({}, ResetClass::Cold, 0);
  b.execution(0, true);
  TEST_ASSERT_TRUE(b.execution(60000, true));
  b.annotateHealthRestart();
  TEST_ASSERT_FALSE(b.execution(60000, true));
  b.begin(b.record(), ResetClass::Software, 0);
  TEST_ASSERT_EQUAL(1, b.record().failed_boots);
}
struct Watchdog : WatchdogPort {
  Subscription subscribed = Subscription::Missing;
  int adds = 0, feeds = 0, removes = 0, add_error = 0, feed_error = 0, remove_error = 0;
  Subscription status() override { return subscribed; }
  int addCurrent() override {
    ++adds;
    return add_error;
  }
  int feedCurrent() override {
    ++feeds;
    return feed_error;
  }
  int removeCurrent() override {
    ++removes;
    return remove_error;
  }
};
void watchdog_owns_only_added_subscription_and_errors_are_visible() {
  Watchdog port;
  HealthWatchdog w(port);
  TEST_ASSERT_TRUE(w.begin());
  TEST_ASSERT_FALSE(w.begin());
  HealthDecision d;
  d.valid = d.feed = d.execution_healthy = true;
  TEST_ASSERT_TRUE(w.service(d));
  d.feed = false;
  TEST_ASSERT_FALSE(w.service(d));
  TEST_ASSERT_EQUAL(1, port.feeds);
  TEST_ASSERT_TRUE(w.stop());
  TEST_ASSERT_TRUE(w.stop());
  TEST_ASSERT_EQUAL(1, port.adds);
  TEST_ASSERT_EQUAL(1, port.removes);
  for (auto status :
       {WatchdogPort::Subscription::Present, WatchdogPort::Subscription::Uninitialized,
        WatchdogPort::Subscription::Error}) {
    Watchdog p;
    p.subscribed = status;
    HealthWatchdog failed(p);
    TEST_ASSERT_FALSE(failed.begin());
    TEST_ASSERT_NOT_EQUAL((int)WatchdogState::NotStarted, (int)failed.state());
    failed.stop();
    TEST_ASSERT_EQUAL(0, p.adds);
    TEST_ASSERT_EQUAL(0, p.removes);
  }
  Watchdog p;
  p.add_error = 42;
  HealthWatchdog fail(p);
  TEST_ASSERT_FALSE(fail.begin());
  TEST_ASSERT_EQUAL(42, fail.error());
  fail.stop();
  TEST_ASSERT_EQUAL(0, p.removes);
  Watchdog f;
  HealthWatchdog feed(f);
  TEST_ASSERT_TRUE(feed.begin());
  f.feed_error = 43;
  d.feed = true;
  TEST_ASSERT_FALSE(feed.service(d));
  TEST_ASSERT_EQUAL(43, feed.error());
  f.remove_error = 44;
  TEST_ASSERT_FALSE(feed.stop());
  TEST_ASSERT_EQUAL((int)WatchdogState::RemoveFailed, (int)feed.state());
}
struct BlockedSink : StorageSink {
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false, release = false;
  bool mount() override {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    cv.notify_all();
    cv.wait(lock, [this] { return release; });
    return false;
  }
  bool openExclusive(const char *) override { return false; }
  size_t write(const char *, size_t) override { return 0; }
  bool flush() override { return false; }
  void close() override {}
};
void blocked_storage_snapshot_and_control_do_not_wait_for_worker() {
  BlockedSink sink;
  Storage storage(sink, StorageConfig(1, "test", "host", 1));
  HealthSupervisor h;
  auto p = required();
  p[0] = {};
  p[2] = required()[0];
  TEST_ASSERT_TRUE(h.begin(p, 0));
  std::thread worker([&] { storage.workerStep(); });
  {
    std::unique_lock<std::mutex> lock(sink.mutex);
    sink.cv.wait(lock, [&] { return sink.entered; });
  }
  bool no_feed = true;
  int controls = 0;
  for (uint32_t now = 300; now <= 1000; now += 100) {
    observeStorageHealth(storage, h.progress(Worker::Sd));
    no_feed &= !h.evaluate(now).feed;
    ++controls;
  }
  {
    std::lock_guard<std::mutex> lock(sink.mutex);
    sink.release = true;
    sink.cv.notify_all();
  }
  worker.join();
  TEST_ASSERT_TRUE(no_feed);
  TEST_ASSERT_EQUAL(8, controls);
  observeStorageHealth(storage, h.progress(Worker::Sd));
  TEST_ASSERT_TRUE(h.evaluate(1100).feed);
  TEST_ASSERT_EQUAL((int)DeviceHealth::IoError, (int)h.progress(Worker::Sd).outcome());
}

struct CloseBlockedSink : BlockedSink {
  bool mount() override { return false; }
  void close() override {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    cv.notify_all();
    cv.wait(lock, [this] { return release; });
  }
};
void terminal_storage_must_complete_close_before_execution_can_quiesce() {
  CloseBlockedSink sink;
  Storage storage(sink, StorageConfig(1, "test", "host", 1));
  HealthSupervisor h;
  auto p = required();
  p[0] = {};
  p[2] = required()[0];
  TEST_ASSERT_TRUE(h.begin(p, 0));
  storage.workerStep(); // terminal mount failure, cleanup still outstanding
  std::thread worker([&] { storage.workerStep(); });
  {
    std::unique_lock<std::mutex> lock(sink.mutex);
    sink.cv.wait(lock, [&] { return sink.entered; });
  }
  observeStorageHealth(storage, h.progress(Worker::Sd));
  const bool initial = h.evaluate(100).feed;
  observeStorageHealth(storage, h.progress(Worker::Sd));
  const bool blocked = h.evaluate(600).feed;
  {
    std::lock_guard<std::mutex> lock(sink.mutex);
    sink.release = true;
    sink.cv.notify_all();
  }
  worker.join();
  TEST_ASSERT_TRUE(initial);
  TEST_ASSERT_FALSE(blocked);
  observeStorageHealth(storage, h.progress(Worker::Sd));
  TEST_ASSERT_TRUE(h.evaluate(700).feed);
  TEST_ASSERT_TRUE(h.evaluate(2000).feed); // finished worker has no remaining execution obligation
}
struct ManagerClock : Clock {
  uint32_t time = 0;
  uint32_t now() const override { return time; }
};
struct ManagerTransport : CameraTransport {
  std::vector<Operation> calls;
  Token token;
  int closed = 0;
  bool begin(size_t, const CameraConfig &, Operation op, Token t) override {
    calls.push_back(op);
    token = t;
    return true;
  }
  void cancel(size_t, Token) override {}
  void close(size_t, Token) override { ++closed; }
};
void actual_manager_reset_discards_inflight_and_queued_commands_without_replay() {
  ManagerClock clock;
  ManagerTransport transport;
  CameraManager cameras(clock, transport);
  RecordingManager recording(cameras, clock);
  SourceConfig c;
  c.count = 1;
  auto &peer = c.cameras[0];
  peer.name = "host";
  peer.family = CameraFamily::Insta360;
  peer.model = CameraModel::X5;
  peer.identifier = "01:23:45:67:89:A0";
  peer.address_type = AddressType::Random;
  TEST_ASSERT_TRUE(cameras.configure(c).ok());
  cameras.request(0, Operation::Connect);
  Event ready{0, transport.token, EventKind::Completed};
  ready.capabilities.start = ready.capabilities.stop = CapabilityState::Supported;
  TEST_ASSERT_TRUE(recording.event(ready));
  recording.request(RecordingState::Recording); // actual Start is in-flight
  TEST_ASSERT_EQUAL((int)Operation::Start, (int)transport.calls.back());
  auto old = transport.token;
  cameras.request(0, Operation::Stop); // queued physical intent
  // Owner-context reset sequence; supervisor never invokes this concurrently.
  recording.cancel();
  cameras.reset();
  const auto calls = transport.calls.size();
  Event stale{0, old, EventKind::Completed};
  stale.capabilities.start = stale.capabilities.stop = CapabilityState::Supported;
  TEST_ASSERT_FALSE(recording.event(stale));
  clock.time = 10000;
  recording.tick();
  cameras.tick();
  TEST_ASSERT_EQUAL(calls, transport.calls.size());
  TEST_ASSERT_EQUAL((int)RecordingState::Unknown, (int)recording.status().intent);
  TEST_ASSERT_EQUAL(0, recording.status().pending);
  TEST_ASSERT_EQUAL((int)RecordingState::Unknown, (int)cameras.state(0)->observed);
  TEST_ASSERT_EQUAL((int)RecordingState::Unknown, (int)cameras.state(0)->desired);
  TEST_ASSERT_GREATER_THAN(0, transport.closed);
}

void actual_camera_retry_exhaustion_is_terminal_device_state_not_execution_stall() {
  ManagerClock clock;
  ManagerTransport transport;
  RetryPolicy retry;
  retry.timeout_ms = 10;
  retry.backoff_ms = 5;
  retry.max_attempts = 2;
  CameraManager cameras(clock, transport, retry);
  SourceConfig c;
  c.count = 1;
  auto &peer = c.cameras[0];
  peer.name = "host";
  peer.family = CameraFamily::Insta360;
  peer.model = CameraModel::X5;
  peer.identifier = "01:23:45:67:89:A0";
  peer.address_type = AddressType::Random;
  TEST_ASSERT_TRUE(cameras.configure(c).ok());
  HealthSupervisor h;
  auto p = required();
  p[1] = p[0];
  p[0] = {};
  TEST_ASSERT_TRUE(h.begin(p, 0));
  cameras.request(0, Operation::Connect);
  for (uint32_t time : {10U, 15U, 25U, 1000U, 2000U}) {
    clock.time = time;
    cameras.tick();
    const bool failed = cameras.state(0)->lifecycle == Lifecycle::Failed;
    h.progress(Worker::Ble)
        .completed(failed ? DeviceHealth::RetryExhausted : DeviceHealth::Missing);
    TEST_ASSERT_TRUE(h.evaluate(time).feed);
  }
  TEST_ASSERT_EQUAL(2, transport.calls.size());
  TEST_ASSERT_EQUAL((int)Lifecycle::Failed, (int)cameras.state(0)->lifecycle);
}
void refused_admission_is_not_completed_work_or_a_stall() {
  HealthSupervisor h;
  TEST_ASSERT_TRUE(h.begin(required(), 0));
  h.progress(Worker::At).refused();
  auto d = h.evaluate(100);
  TEST_ASSERT_EQUAL_UINT8(1, d.refused);
  TEST_ASSERT_EQUAL_UINT8(0, d.stalled);
  TEST_ASSERT_EQUAL_UINT32(0, h.progress(Worker::At).generation());
  TEST_ASSERT_FALSE(d.feed);
  TEST_ASSERT_FALSE(d.execution_healthy);
  TEST_ASSERT_FALSE(d.stable_candidate);
  TEST_ASSERT_FALSE(h.begin({}, 200));
}
void early_cancelled_required_lifetime_cannot_prove_stable_boot() {
  HealthSupervisor h;
  TEST_ASSERT_TRUE(h.begin(required(), 0));
  h.progress(Worker::At).finished();
  const auto d = h.evaluate(100000);
  TEST_ASSERT_EQUAL_UINT8(0, d.stalled);
  TEST_ASSERT_FALSE(d.stable_candidate);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(refused_admission_is_not_completed_work_or_a_stall);
  RUN_TEST(early_cancelled_required_lifetime_cannot_prove_stable_boot);
  RUN_TEST(stalled_required_worker_withholds_feed_even_while_control_runs);
  RUN_TEST(absent_devices_storms_and_exhaustion_keep_execution_live);
  RUN_TEST(grace_deadline_and_generation_rollover_are_bounded);
  RUN_TEST(saturated_progress_and_optional_stall_cannot_fake_required_liveness);
  RUN_TEST(invalid_configuration_cannot_start_or_feed);
  RUN_TEST(boot_failures_latch_until_explicit_clear_and_cold_record_is_untrusted);
  RUN_TEST(stable_window_needs_worker_execution_handles_wrap_and_counters_saturate);
  RUN_TEST(application_restart_cause_is_consumed_separately_from_sdk_class);
  RUN_TEST(annotation_after_stable_boot_rearms_failure_accounting);
  RUN_TEST(watchdog_owns_only_added_subscription_and_errors_are_visible);
  RUN_TEST(blocked_storage_snapshot_and_control_do_not_wait_for_worker);
  RUN_TEST(actual_manager_reset_discards_inflight_and_queued_commands_without_replay);
  RUN_TEST(terminal_storage_must_complete_close_before_execution_can_quiesce);
  RUN_TEST(actual_camera_retry_exhaustion_is_terminal_device_state_not_execution_stall);
  return UNITY_END();
}
void setUp() {}
void tearDown() {}

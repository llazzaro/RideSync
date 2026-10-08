#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace ridesync {
enum class Worker : uint8_t { At, Ble, Sd, Imu, Count };
enum class DeviceHealth : uint8_t {
  Ok,
  Missing,
  NoFix,
  Desynchronized,
  RetryExhausted,
  DisconnectStorm,
  IoError
};
struct WorkerPolicy {
  bool enabled = false, required = false, qualified = false;
  uint32_t deadline_ms = 1000, startup_grace_ms = 1000;
};
// One publisher per slot; outcome is independently observational, not transactional.
// Only call after a completed service pass. Disabled workers publish nothing.
class HealthProgress {
public:
  void completed(DeviceHealth outcome = DeviceHealth::Ok);
  // Admission failed: no worker and no completed work was fabricated.
  void refused() { refused_.store(true, std::memory_order_release); }
  bool isRefused() const { return refused_.load(std::memory_order_acquire); }
  void observe(uint32_t completed_generation, DeviceHealth outcome);
  // Irreversible worker lifetime completion, only AFTER all cleanup returns.
  // Never use a device terminal/error flag alone; no further work may be admitted.
  void finished() { finished_.store(true, std::memory_order_release); }
  bool isFinished() const { return finished_.load(std::memory_order_acquire); }
  uint32_t generation() const { return generation_.load(std::memory_order_acquire); }
  DeviceHealth outcome() const { return outcome_.load(std::memory_order_relaxed); }

private:
  std::atomic<uint32_t> generation_{0};
  std::atomic<bool> finished_{false}, refused_{false};
  std::atomic<DeviceHealth> outcome_{DeviceHealth::Ok};
};
struct HealthDecision {
  bool valid = false, feed = false, execution_healthy = false, stable_candidate = false;
  uint8_t stalled = 0, refused = 0;
};
class HealthSupervisor {
public:
  static constexpr uint32_t kCadenceMs = 100, kMaxDeadlineMs = 2000, kMaxGraceMs = 1000;
  bool begin(const std::array<WorkerPolicy, 4> &policy, uint32_t now);
  HealthProgress &progress(Worker worker) { return progress_[static_cast<uint8_t>(worker)]; }
  HealthDecision evaluate(uint32_t now);

private:
  std::array<WorkerPolicy, 4> policy_{};
  std::array<HealthProgress, 4> progress_;
  std::array<uint32_t, 4> seen_{}, fed_{}, last_{};
  std::array<bool, 4> started_{};
  uint32_t boot_ = 0;
  bool valid_ = false;
};
enum class ResetClass : uint8_t {
  Cold,
  Brownout,
  Unknown,
  Operator,
  Software,
  DeepSleep,
  Watchdog,
  Panic,
  HealthRestart
};
enum class AppResetCause : uint8_t { None, RequiredWorkerStall };
struct BootRecord {
  uint32_t magic = 0, version = 0, checksum = 0;
  uint8_t failed_boots = 0, watchdog_resets = 0, health_restarts = 0;
  uint8_t armed = 0, safe_mode = 0;
  AppResetCause pending = AppResetCause::None;
};
struct BootStatus {
  bool retention_valid = false, safe_mode = false;
  AppResetCause previous_app_cause = AppResetCause::None;
};
class BootRecovery {
public:
  static constexpr uint32_t kStableMs = 60000;
  BootStatus begin(const BootRecord &retained, ResetClass reason, uint32_t now);
  // Call each supervisory pass. True means completed required workers remain live,
  // not startup grace alone. A detected stall restarts the stable window.
  bool execution(uint32_t now, bool completed_workers_healthy);
  void annotateHealthRestart();
  void operatorClear(uint32_t now);
  const BootRecord &record() const { return record_; }
  static bool valid(const BootRecord &record);

private:
  BootRecord record_;
  uint32_t stable_since_ = 0;
  bool tracking_ = false;
  void seal();
};
// All methods invoked only by the dedicated supervisor task, never by workers.
class WatchdogPort {
public:
  enum class Subscription { Missing, Present, Uninitialized, Error };
  virtual ~WatchdogPort() = default;
  virtual Subscription status() = 0;
  virtual int addCurrent() = 0;
  virtual int feedCurrent() = 0;
  virtual int removeCurrent() = 0;
};
enum class WatchdogState : uint8_t {
  NotStarted,
  Running,
  ExistingSubscription,
  Uninitialized,
  StatusFailed,
  AddFailed,
  FeedFailed,
  RemoveFailed,
  Stopped
};
class HealthWatchdog {
public:
  explicit HealthWatchdog(WatchdogPort &port) : port_(port) {}
  bool begin();
  bool service(const HealthDecision &decision);
  bool stop();
  WatchdogState state() const { return state_; }
  int error() const { return error_; }

private:
  WatchdogPort &port_;
  bool owned_ = false;
  WatchdogState state_ = WatchdogState::NotStarted;
  int error_ = 0;
};
class Storage;
// Read only Storage's atomics, never FS locks or producer enqueue activity.
void observeStorageHealth(const Storage &storage, HealthProgress &progress);
} // namespace ridesync

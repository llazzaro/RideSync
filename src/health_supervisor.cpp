#include "health_supervisor.h"
#include "storage.h"
static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_CHAR_LOCK_FREE == 2,
              "Health publication requires lock-free word/byte atomics");
namespace ridesync {
void HealthProgress::completed(DeviceHealth outcome) {
  const uint32_t old = generation_.load(std::memory_order_relaxed);
  outcome_.store(outcome, std::memory_order_relaxed);
  // Saturation fails closed rather than permitting a repeated value to feed.
  if (old != UINT32_MAX)
    generation_.store(old + 1, std::memory_order_release);
}
void HealthProgress::observe(uint32_t generation, DeviceHealth outcome) {
  outcome_.store(outcome, std::memory_order_relaxed);
  generation_.store(generation, std::memory_order_release);
}
bool HealthSupervisor::begin(const std::array<WorkerPolicy, 4> &policy, uint32_t now) {
  if (valid_)
    return false; // No in-place rebasing of liveness while subscribed.
  for (const auto &p : policy) {
    if (p.required && !p.enabled)
      return false;
    if (p.enabled &&
        (!p.qualified || p.deadline_ms < 2 * kCadenceMs || p.deadline_ms > kMaxDeadlineMs ||
         p.startup_grace_ms < kCadenceMs || p.startup_grace_ms > kMaxGraceMs))
      return false;
  }
  policy_ = policy;
  boot_ = now;
  valid_ = true;
  return true;
}
HealthDecision HealthSupervisor::evaluate(uint32_t now) {
  HealthDecision d;
  d.valid = valid_;
  if (!valid_)
    return d;
  bool fresh = true, all_started = true;
  for (size_t i = 0; i < policy_.size(); ++i) {
    const auto &p = policy_[i];
    if (!p.enabled)
      continue;
    if (progress_[i].isRefused()) {
      d.refused |= static_cast<uint8_t>(1U << i);
      if (p.required) {
        all_started = false;
        fresh = false;
      }
      continue;
    }
    if (progress_[i].isFinished()) {
      if (p.required && !started_[i] && !progress_[i].generation())
        all_started = false;
      continue;
    }
    const uint32_t generation = progress_[i].generation();
    // Accept forward movement across wrap, reject repeats and backward generations.
    const uint32_t delta = generation - seen_[i];
    if ((!started_[i] && generation != 0) || (delta && delta < 0x80000000U)) {
      seen_[i] = generation;
      last_[i] = now;
      started_[i] = true;
    }
    const bool live =
        started_[i] ? now - last_[i] < p.deadline_ms : now - boot_ < p.startup_grace_ms;
    if (!live)
      d.stalled |= static_cast<uint8_t>(1U << i);
    if (p.required && !started_[i])
      all_started = false;
    if (p.required && (!live || (started_[i] && seen_[i] == fed_[i])))
      fresh = false;
  }
  uint8_t required_mask = 0;
  for (size_t i = 0; i < policy_.size(); ++i)
    if (policy_[i].required)
      required_mask |= static_cast<uint8_t>(1U << i);
  d.execution_healthy = ((d.stalled | d.refused) & required_mask) == 0;
  d.feed = d.execution_healthy && fresh;
  d.stable_candidate = d.execution_healthy && all_started;
  if (d.feed)
    fed_ = seen_;
  return d;
}
void observeStorageHealth(const Storage &storage, HealthProgress &progress) {
  const auto h = storage.health();
  progress.observe(h.progress, h.terminal ? DeviceHealth::IoError : DeviceHealth::Ok);
  if (h.stopped)
    progress.finished(); // Close returned; terminal before close may still block.
}

namespace {
constexpr uint32_t kMagic = 0x52534853, kVersion = 1;
uint32_t checksum(const BootRecord &r) {
  uint32_t hash = 2166136261U;
  const uint32_t values[] = {
      r.magic,           r.version, r.failed_boots, r.watchdog_resets,
      r.health_restarts, r.armed,   r.safe_mode,    static_cast<uint8_t>(r.pending)};
  for (uint32_t value : values)
    for (uint8_t byte = 0; byte < 4; ++byte) {
      hash = (hash ^ (value & 255)) * 16777619U;
      value >>= 8;
    }
  return hash;
}
void saturate(uint8_t &value) {
  if (value != UINT8_MAX)
    ++value;
}
} // namespace
bool BootRecovery::valid(const BootRecord &r) {
  return r.magic == kMagic && r.version == kVersion && r.armed <= 1 && r.safe_mode <= 1 &&
         static_cast<uint8_t>(r.pending) <=
             static_cast<uint8_t>(AppResetCause::RequiredWorkerStall) &&
         r.checksum == checksum(r);
}
void BootRecovery::seal() {
  record_.magic = kMagic;
  record_.version = kVersion;
  record_.checksum = checksum(record_);
}
BootStatus BootRecovery::begin(const BootRecord &retained, ResetClass reason, uint32_t now) {
  BootStatus status;
  status.retention_valid = reason != ResetClass::Cold && reason != ResetClass::Brownout &&
                           reason != ResetClass::Unknown && valid(retained);
  record_ = status.retention_valid ? retained : BootRecord{};
  status.previous_app_cause = record_.pending;
  const bool watchdog = reason == ResetClass::Watchdog || reason == ResetClass::Panic;
  const bool health =
      reason == ResetClass::HealthRestart ||
      (reason == ResetClass::Software && record_.pending == AppResetCause::RequiredWorkerStall);
  if (status.retention_valid) {
    if (watchdog)
      saturate(record_.watchdog_resets);
    if (health)
      saturate(record_.health_restarts);
    if (record_.armed && (watchdog || health))
      saturate(record_.failed_boots);
    if (record_.failed_boots >= 3)
      record_.safe_mode = 1;
  }
  record_.armed = 1;
  record_.pending = AppResetCause::None;
  stable_since_ = now;
  tracking_ = false;
  seal();
  status.safe_mode = record_.safe_mode != 0;
  return status;
}
bool BootRecovery::execution(uint32_t now, bool healthy) {
  if (!healthy) {
    tracking_ = false;
    return false;
  }
  if (!record_.armed)
    return false;
  if (!tracking_) {
    stable_since_ = now;
    tracking_ = true;
  }
  if (now - stable_since_ < kStableMs)
    return false;
  record_.armed = 0;
  if (!record_.safe_mode)
    record_.failed_boots = 0;
  seal();
  return true;
}
void BootRecovery::annotateHealthRestart() {
  tracking_ = false; // A previous stable window cannot disarm this restart annotation.
  record_.pending = AppResetCause::RequiredWorkerStall;
  record_.armed = 1;
  seal();
}
void BootRecovery::operatorClear(uint32_t now) {
  record_.safe_mode = 0;
  record_.failed_boots = 0;
  record_.pending = AppResetCause::None;
  record_.armed = 1;
  tracking_ = false;
  stable_since_ = now;
  seal();
}
bool HealthWatchdog::begin() {
  if (state_ != WatchdogState::NotStarted)
    return false;
  switch (port_.status()) {
  case WatchdogPort::Subscription::Present:
    state_ = WatchdogState::ExistingSubscription;
    return false;
  case WatchdogPort::Subscription::Uninitialized:
    state_ = WatchdogState::Uninitialized;
    return false;
  case WatchdogPort::Subscription::Error:
    state_ = WatchdogState::StatusFailed;
    return false;
  case WatchdogPort::Subscription::Missing:
    break;
  }
  error_ = port_.addCurrent();
  if (error_) {
    state_ = WatchdogState::AddFailed;
    return false;
  }
  owned_ = true;
  state_ = WatchdogState::Running;
  return true;
}
bool HealthWatchdog::service(const HealthDecision &d) {
  if (state_ != WatchdogState::Running || !d.valid || !d.feed || !d.execution_healthy)
    return false;
  error_ = port_.feedCurrent();
  if (error_) {
    state_ = WatchdogState::FeedFailed;
    return false;
  }
  return true;
}
bool HealthWatchdog::stop() {
  if (!owned_)
    return true;
  error_ = port_.removeCurrent();
  if (error_) {
    state_ = WatchdogState::RemoveFailed;
    return false;
  }
  owned_ = false;
  state_ = WatchdogState::Stopped;
  return true;
}
} // namespace ridesync

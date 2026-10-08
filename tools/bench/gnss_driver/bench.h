#pragma once
#include "camera_manager.h"
#include "gps_manager.h"
#include <algorithm>
#include <cstdio>

namespace gnss_bench {
using namespace ridesync;
constexpr uint32_t kObservationMs = 120000;
constexpr uint64_t kControlGapTargetUs = 20000;
struct MicroClock {
  virtual ~MicroClock() = default;
  virtual uint64_t microsNow() const = 0;
};
struct BenchPort : ModemUart {
  virtual bool begin() = 0;
};
enum class StopReason { None, Cancelled, Cap, OpenFailed };
// Gate changes transport delivery only; production classes own all AT parsing
// and exchange state. No resume or recovery operation exists on this bench.
class Gate : public ModemUart {
public:
  explicit Gate(BenchPort &port) : port_(port) {}
  bool open() {
    active_ = port_.begin();
    return active_;
  }
  void close() { active_ = false; }
  void suppress() { suppressed_ = true; }
  size_t available() override { return active_ && !suppressed_ ? port_.available() : 0; }
  int read() override {
    if (!active_ || suppressed_)
      return -1;
    const int value = port_.read();
    if (value >= 0)
      ++received;
    return value;
  }
  size_t writable() override { return active_ ? port_.writable() : 0; }
  size_t write(const char *data, size_t length) override {
    if (!active_)
      return 0;
    const size_t result = port_.write(data, length);
    transmitted += result;
    return result;
  }
  void discard() {
    if (!active_ || !suppressed_)
      return;
    for (unsigned n = 0; n < 64 && port_.available(); ++n) {
      if (port_.read() < 0)
        break;
      ++discarded;
    }
  }
  uint64_t received = 0, transmitted = 0, discarded = 0;

private:
  BenchPort &port_;
  bool active_ = false, suppressed_ = false;
};
class Bench {
public:
  Bench(Clock &raw, MicroClock &micro, BenchPort &port)
      : raw_(raw), micro_(micro), gate_(port), clock_(raw, 1, 3000),
        modem_(gate_, experimentalConfig()), gps_(clock_, modem_, nullptr, noPowerAdmission()) {}
  bool start() {
    if (attempted_ || reason_ != StopReason::None) {
      ++start_rejected_;
      return false;
    }
    attempted_ = true;
    started_ms_ = raw_.now();
    if (!gate_.open()) {
      stop(StopReason::OpenFailed);
      return false;
    }
    active_ = true;
    return true;
  }
  void cancel() { stop(StopReason::Cancelled); }
  bool suppress() {
    if (!active_ || suppressed_ || !baseline_ || expired()) {
      ++suppress_rejected_;
      return false;
    }
    suppressed_ = true;
    suppression_ms_ = elapsed();
    gate_.suppress();
    return true;
  }
  void loop() {
    const uint64_t entry = micro_.microsNow();
    if (seen_loop_) {
      max_gap_us_ = std::max(max_gap_us_, entry - last_entry_us_);
      max_idle_us_ = std::max(max_idle_us_, entry - last_exit_us_);
    }
    seen_loop_ = true;
    last_entry_us_ = entry;
    ++heartbeat_; // Independent control work, also in idle/failed/stopped states.
    if (active_ && expired())
      stop(StopReason::Cap);
    if (active_) {
      gate_.discard();
      const uint64_t before = micro_.microsNow();
      gps_.tick();
      max_tick_us_ = std::max(max_tick_us_, micro_.microsNow() - before);
      observe();
    }
    last_exit_us_ = micro_.microsNow();
    max_work_us_ = std::max(max_work_us_, last_exit_us_ - entry);
  }
  ModemSnapshot snapshot() { return gps_.snapshot(); }
  StopReason stopReason() const { return reason_; }
  bool baseline() const { return baseline_; }
  bool suppressed() const { return suppressed_; }
  uint64_t discarded() const { return gate_.discarded; }
  uint64_t heartbeats() const { return heartbeat_; }
  uint64_t maxLoopGapUs() const { return max_gap_us_; }
  uint32_t summaryDrops() const { return drops_; }
  void reportDropped() {
    if (drops_ != UINT32_MAX)
      ++drops_;
  }
  size_t report(char *out, size_t capacity) {
    const auto s = snapshot();
    const int size = std::snprintf(
        out, capacity,
        "state=%u health=%u validity=%u age_avail=%u age_ms=%llu power_stage=%u "
        "power=%u ready=%u active=%u stop=%u elapsed_ms=%u services=%u heartbeats=%llu "
        "rx=%llu tx=%llu discard=%llu baseline=%u exchanges=%u suppressed=%u "
        "power_at_ms=%lld ready_at_ms=%lld baseline_at_ms=%lld suppression_at_ms=%lld "
        "tick_max_us=%llu gap_max_us=%llu idle_max_us=%llu work_max_us=%llu "
        "gap_target_us=20000 gap_exceeded=%u drops=%u A_rejected=%u R_rejected=%u "
        "recovery_tested=0\n",
        unsigned(s.state), unsigned(s.health), unsigned(s.validity), unsigned(s.age_available),
        (unsigned long long)s.age_ms, unsigned(gps_.powerStage()), unsigned(s.gnss_power_enabled),
        unsigned(s.receiver_ready), unsigned(active_), unsigned(reason_),
        attempted_ ? elapsed() : 0, gps_.completed(), (unsigned long long)heartbeat_,
        (unsigned long long)gate_.received, (unsigned long long)gate_.transmitted,
        (unsigned long long)gate_.discarded, unsigned(baseline_), exchanges_, unsigned(suppressed_),
        (long long)power_ms_, (long long)ready_ms_, (long long)baseline_ms_,
        (long long)suppression_ms_, (unsigned long long)max_tick_us_,
        (unsigned long long)max_gap_us_, (unsigned long long)max_idle_us_,
        (unsigned long long)max_work_us_, unsigned(max_gap_us_ > kControlGapTargetUs), drops_,
        start_rejected_, suppress_rejected_);
    if (size < 0 || size_t(size) >= capacity) {
      reportDropped();
      return 0;
    }
    return size_t(size);
  }

private:
  static ModemConfig experimentalConfig() {
    ModemConfig config;
    // Experimental documentary premise, NOT physical qualification evidence.
    config.documentary_profile_opt_in = true;
    config.terminal_retires_transaction = true;
    return config;
  }
  static QualifiedPowerTiming noPowerAdmission() {
    QualifiedPowerTiming timing;
    // nullptr power: route admission only. No pulse, settle or ready claim.
    timing.qualified = true;
    return timing;
  }
  uint32_t elapsed() const { return uint32_t(raw_.now() - started_ms_); }
  bool expired() const { return elapsed() >= kObservationMs; }
  void stop(StopReason reason) {
    if (reason_ == StopReason::None)
      reason_ = reason;
    active_ = false;
    gps_.cancel();
    gate_.close();
  }
  void observe() {
    const auto s = snapshot();
    const auto now = elapsed();
    if (s.gnss_power_enabled && power_ms_ < 0)
      power_ms_ = now;
    if (s.receiver_ready && ready_ms_ < 0)
      ready_ms_ = now;
    if (s.health == UartHealth::Healthy &&
        (s.validity == FixValidity::NoFix || s.validity == FixValidity::Valid) &&
        (!baseline_ || s.fix.receipt_monotonic_ms != last_receipt_ms_)) {
      if (!baseline_)
        baseline_ms_ = now;
      baseline_ = true;
      last_receipt_ms_ = s.fix.receipt_monotonic_ms;
      if (exchanges_ != UINT32_MAX)
        ++exchanges_;
    }
  }
  Clock &raw_;
  MicroClock &micro_;
  Gate gate_;
  SessionClock clock_;
  ModemGnss modem_;
  GpsManager gps_;
  bool attempted_ = false, active_ = false, baseline_ = false, suppressed_ = false;
  bool seen_loop_ = false;
  StopReason reason_ = StopReason::None;
  uint32_t started_ms_ = 0, exchanges_ = 0, drops_ = 0;
  uint32_t start_rejected_ = 0, suppress_rejected_ = 0;
  int64_t power_ms_ = -1, ready_ms_ = -1, baseline_ms_ = -1, suppression_ms_ = -1;
  uint64_t last_receipt_ms_ = 0, heartbeat_ = 0, last_entry_us_ = 0, last_exit_us_ = 0;
  uint64_t max_gap_us_ = 0, max_idle_us_ = 0, max_tick_us_ = 0, max_work_us_ = 0;
};
} // namespace gnss_bench

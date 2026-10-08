#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace x5_probe {
enum class Kind : uint8_t {
  Sync,
  Connect,
  Disconnect,
  Mtu,
  Subscribe,
  Read,
  Write,
  Security,
  Passkey,
  RepeatPairing,
  AdvertisingEnd,
  Error,
  Stop,
  Attribute,
  ShutterRequest,
  ShutterResult,
  WakeOption,
  Count
};
enum class StopReason : uint8_t {
  None,
  Requested,
  StartupTimeout,
  Deadline,
  Disconnected,
  Error,
  Security
};
struct Event {
  uint64_t time_ms = 0;
  uint32_t sequence = 0;
  uint16_t connection = 0xffff, attribute = 0, length = 0, copied = 0;
  int32_t value = 0;
  Kind kind = Kind::Error;
  uint8_t bytes[256]{};
};
struct Stats {
  uint32_t seen = 0, dropped = 0, truncated = 0, reported = 0;
  uint32_t by_kind[unsigned(Kind::Count)]{};
};
// Single serialized context; SDK callbacks and loop supply an external lock.
// No SDK handles/pointers or counterpart state is fabricated by this policy.
class Capture {
public:
  static constexpr uint32_t kStartupMs = 15000, kWindowMs = 120000;
  bool begin(uint32_t now) {
    if (used_)
      return false;
    used_ = active_ = true;
    began_ = now;
    return true;
  }
  void ready() { ready_ = true; }
  void tick(uint32_t now) {
    if (!active_)
      return;
    if (uint32_t(now - began_) >= kWindowMs)
      stop(StopReason::Deadline);
    else if (!ready_ && uint32_t(now - began_) >= kStartupMs)
      stop(StopReason::StartupTimeout);
  }
  void stop(StopReason reason) {
    if (active_) {
      active_ = false;
      reason_ = reason;
    }
  }
  bool push(Kind kind, uint64_t time, uint16_t connection, uint16_t attribute, int32_t value,
            const uint8_t *bytes = nullptr, uint16_t length = 0) {
    ++stats_.seen;
    ++stats_.by_kind[unsigned(kind)];
    if (length > sizeof(Event::bytes))
      ++stats_.truncated;
    if (count_ == kSlots) {
      ++stats_.dropped;
      return false;
    }
    Event &event = queue_[(head_ + count_) % kSlots];
    event.time_ms = time;
    event.sequence = stats_.seen;
    event.kind = kind;
    event.connection = connection;
    event.attribute = attribute;
    event.value = value;
    event.length = length;
    event.copied = bytes ? (length < sizeof(event.bytes) ? length : sizeof(event.bytes)) : 0;
    if (event.copied)
      std::memcpy(event.bytes, bytes, event.copied);
    ++count_;
    return true;
  }
  const Event *front() const { return count_ ? &queue_[head_] : nullptr; }
  void pop() {
    if (!count_)
      return;
    // Clear sensitive bytes once their report is admitted; storage stays alive.
    queue_[head_] = Event{};
    head_ = (head_ + 1) % kSlots;
    --count_;
    ++stats_.reported;
  }
  bool used() const { return used_; }
  bool active() const { return active_; }
  uint32_t remaining(uint32_t now) const {
    const uint32_t elapsed = now - began_;
    return active_ && elapsed < kWindowMs ? kWindowMs - elapsed : 0;
  }
  StopReason reason() const { return reason_; }
  const Stats &stats() const { return stats_; }
  unsigned queued() const { return count_; }

private:
  static constexpr unsigned kSlots = 32;
  Event queue_[kSlots];
  Stats stats_;
  unsigned head_ = 0, count_ = 0;
  uint32_t began_ = 0;
  bool used_ = false, active_ = false, ready_ = false;
  StopReason reason_ = StopReason::None;
};
enum class SdkAction : uint8_t {
  None,
  Startup,
  Prepare,
  Advertise,
  StopAdvertising,
  Terminate,
  Shutter
};
struct SdkPublication {
  SdkAction action = SdkAction::None, last_returned_action = SdkAction::None;
  bool in_flight = false, stop_requested = false;
  uint32_t accepted = 0, returned = 0;
  int last_status = 0;
};
// Fixed one-owner request/publication boundary; callers serialize with the
// capture lock, then release it BEFORE any SDK call. A stop is never consumed
// or cleared by startup/preparation/result publication. This is admission and
// return evidence only, never RF state, counterpart success or quiescence.
class SdkControl {
public:
  void requestStop() { state_.stop_requested = true; }
  bool begin(SdkAction action) {
    if (state_.in_flight)
      return false;
    state_.action = action;
    state_.in_flight = true;
    ++state_.accepted;
    return true;
  }
  bool admitPreparation(Capture &capture, uint32_t now) {
    return allowed(capture, now) && begin(SdkAction::Prepare);
  }
  uint32_t admitAdvertising(Capture &capture, uint32_t now) {
    if (!allowed(capture, now))
      return 0;
    const uint32_t remaining = capture.remaining(now);
    return remaining && begin(SdkAction::Advertise) ? remaining : 0;
  }
  void complete(int status) {
    if (!state_.in_flight)
      return;
    state_.last_status = status;
    state_.last_returned_action = state_.action;
    state_.in_flight = false;
    ++state_.returned;
  }
  SdkPublication snapshot() const { return state_; }

private:
  bool allowed(Capture &capture, uint32_t now) {
    capture.tick(now);
    if (!capture.active())
      requestStop();
    return capture.active() && !state_.stop_requested && !state_.in_flight;
  }
  SdkPublication state_;
};
// One boot-lifetime peer and one pending operator request. No command queue,
// retry, recording state, retained payload or disconnect replay.
class ShutterControl {
public:
  void connected(uint16_t connection) {
    disconnected();
    connection_ = connection;
  }
  void disconnected() {
    cancel();
    connection_ = 0xffff;
    attribute_ = 0;
  }
  void subscription(uint16_t connection, uint16_t attribute, bool notify) {
    if (connection != connection_)
      return;
    if (!notify || attribute != attribute_)
      cancel();
    attribute_ = notify ? attribute : 0;
  }
  bool request(Capture &capture, const SdkControl &sdk, uint32_t now) {
    if (pending_ || !allowed(capture, sdk, now))
      return false;
    pending_ = true;
    return true;
  }
  bool admit(Capture &capture, SdkControl &sdk, uint32_t now, uint16_t &connection,
             uint16_t &attribute) {
    const bool submit = pending_ && allowed(capture, sdk, now);
    cancel(); // Consumed/refused exactly once, irrespective of SDK return.
    if (!submit || !sdk.begin(SdkAction::Shutter))
      return false;
    connection = connection_;
    attribute = attribute_;
    return true;
  }
  void cancel() { pending_ = false; }
  bool pending() const { return pending_; }

private:
  bool allowed(Capture &capture, const SdkControl &sdk, uint32_t now) {
    capture.tick(now);
    const auto state = sdk.snapshot();
    return capture.active() && !state.stop_requested && !state.in_flight && connection_ != 0xffff &&
           attribute_ != 0;
  }
  uint16_t connection_ = 0xffff, attribute_ = 0;
  bool pending_ = false;
};
inline bool canReport(size_t length, size_t available) { return length && length <= available; }
inline size_t formatEvent(const Event &event, bool private_hex, char *line, size_t capacity) {
  const int n = std::snprintf(line, capacity,
                              "X5_PROBE: event=%lu boot_ms=%llu kind=%u conn=%u attr=%u value=%ld "
                              "len=%u copied=%u ingress_truncated=%u",
                              static_cast<unsigned long>(event.sequence),
                              static_cast<unsigned long long>(event.time_ms), unsigned(event.kind),
                              event.connection, event.attribute, static_cast<long>(event.value),
                              event.length, event.copied, event.length > event.copied);
  if (n < 0 || size_t(n) >= capacity)
    return 0;
  size_t used = size_t(n);
  if (private_hex && event.copied) {
    const unsigned count = event.copied;
    const int prefix =
        std::snprintf(line + used, capacity - used, " SENSITIVE hex_bytes=%u hex=", count);
    if (prefix < 0 || size_t(prefix) >= capacity - used)
      return 0;
    used += size_t(prefix);
    if (2 * count + 2 > capacity - used)
      return 0;
    const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < count; ++i) {
      line[used++] = digits[event.bytes[i] >> 4];
      line[used++] = digits[event.bytes[i] & 15];
    }
  }
  if (used + 2 > capacity)
    return 0;
  line[used++] = '\n';
  line[used] = 0;
  return used;
}
} // namespace x5_probe

#pragma once
#include <cstdint>

namespace ridesync {
class Clock;
// Calendar input: 2000..2099, Gregorian dates, seconds 0..59 (no leap seconds).
struct UtcDateTime {
  uint16_t year;
  uint8_t month, day, hour, minute, second;
  uint16_t millisecond;
};
// A copied anchor is never remapped by subsequent corrections or reset.
// utc_ms is signed POSIX milliseconds since 1970-01-01T00:00:00Z.
struct UtcAnchor {
  uint32_t sequence = 0;
  uint64_t receipt_ms = 0;
  int64_t utc_ms = 0;
  bool uncertainty_known = false;
  uint32_t uncertainty_ms = 0;
};
enum class AnchorQuality { Missing, Fresh, Expired };
enum class MonotonicQuality { Valid, InvalidSession, DurationExceeded };
struct RecordTimestamp {
  uint64_t session_id = 0;
  uint64_t monotonic_ms = 0;
  MonotonicQuality monotonic_quality = MonotonicQuality::InvalidSession;
  AnchorQuality anchor_quality = AnchorQuality::Missing;
  UtcAnchor anchor;
  uint64_t anchor_age_ms = 0;
  bool has_utc_estimate = false;
  int64_t utc_estimate_ms = 0;
};
// Single execution context. Sample via snapshot/anchor less than 2^32 ms apart.
// The raw Clock must advance modulo 2^32; reboot requires reset with a new ID.
// Zero IDs are invalid; callers must ensure IDs are unique across all sessions.
class SessionClock {
public:
  // A deliberately bounded session duration of 365 days.
  static constexpr uint64_t kMaxDurationMs = 31536000000ULL;
  SessionClock(Clock &clock, uint64_t session_id, uint32_t anchor_max_age_ms);
  // Reject zero/current IDs without modifying the session.
  bool reset(uint64_t new_session_id);
  RecordTimestamp snapshot();
  // UTC pertains to receipt now; known uncertainty must include transport delay.
  // Invalid input leaves the previous anchor intact, while still sampling time.
  bool anchor(const UtcDateTime &utc, bool uncertainty_known = false, uint32_t uncertainty_ms = 0);

private:
  Clock &clock_;
  uint64_t session_id_;
  uint32_t max_age_ms_;
  uint32_t last_raw_;
  uint64_t elapsed_ms_ = 0;
  bool duration_exceeded_ = false;
  UtcAnchor anchor_;
  void sample();
};
} // namespace ridesync

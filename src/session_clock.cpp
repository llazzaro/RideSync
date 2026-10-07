#include "session_clock.h"
#include "camera_manager.h"
#include <limits>
namespace ridesync {
constexpr uint64_t SessionClock::kMaxDurationMs;
namespace {
// POSIX milliseconds; leap seconds are deliberately rejected rather than folded.
bool utcMilliseconds(const UtcDateTime &d, int64_t &result) {
  if (d.year < 2000 || d.year > 2099 || d.month < 1 || d.month > 12 || d.day < 1 || d.hour > 23 ||
      d.minute > 59 || d.second > 59 || d.millisecond > 999)
    return false;
  const uint8_t monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = d.year % 4 == 0; // The accepted range excludes century exceptions.
  if (d.day > monthDays[d.month - 1] + (d.month == 2 && leap ? 1 : 0))
    return false;
  int64_t days = 10957; // 1970-01-01 to 2000-01-01.
  for (uint16_t year = 2000; year < d.year; ++year)
    days += year % 4 == 0 ? 366 : 365;
  for (uint8_t month = 1; month < d.month; ++month)
    days += monthDays[month - 1] + (month == 2 && leap ? 1 : 0);
  days += d.day - 1;
  result = (((days * 24 + d.hour) * 60 + d.minute) * 60 + d.second) * 1000 + d.millisecond;
  return true;
}
} // namespace
SessionClock::SessionClock(Clock &clock, uint64_t session_id, uint32_t age)
    : clock_(clock), session_id_(session_id), max_age_ms_(age), last_raw_(clock.now()) {}
bool SessionClock::reset(uint64_t id) {
  if (id == 0 || id == session_id_)
    return false;
  session_id_ = id;
  last_raw_ = clock_.now();
  elapsed_ms_ = 0;
  duration_exceeded_ = false;
  anchor_ = {};
  return true;
}
void SessionClock::sample() {
  const uint32_t raw = clock_.now();
  const uint32_t delta = raw - last_raw_;
  last_raw_ = raw;
  if (duration_exceeded_)
    return;
  if (delta > kMaxDurationMs - elapsed_ms_) {
    elapsed_ms_ = kMaxDurationMs;
    duration_exceeded_ = true;
    return;
  }
  elapsed_ms_ += delta;
}
RecordTimestamp SessionClock::snapshot() {
  sample();
  RecordTimestamp r;
  r.session_id = session_id_;
  r.monotonic_ms = elapsed_ms_;
  r.monotonic_quality = session_id_ == 0     ? MonotonicQuality::InvalidSession
                        : duration_exceeded_ ? MonotonicQuality::DurationExceeded
                                             : MonotonicQuality::Valid;
  r.anchor = anchor_;
  if (anchor_.sequence != 0) {
    r.anchor_age_ms = elapsed_ms_ - anchor_.receipt_ms;
    r.anchor_quality =
        r.anchor_age_ms <= max_age_ms_ ? AnchorQuality::Fresh : AnchorQuality::Expired;
    if (!duration_exceeded_ && r.anchor_quality == AnchorQuality::Fresh) {
      r.has_utc_estimate = true;
      r.utc_estimate_ms = anchor_.utc_ms + static_cast<int64_t>(r.anchor_age_ms);
    }
  }
  return r;
}
bool SessionClock::anchor(const UtcDateTime &utc, bool known, uint32_t uncertainty) {
  sample();
  int64_t ms;
  if (session_id_ == 0 || duration_exceeded_ ||
      anchor_.sequence == std::numeric_limits<uint32_t>::max() || !utcMilliseconds(utc, ms))
    return false;
  ++anchor_.sequence;
  anchor_.receipt_ms = elapsed_ms_;
  anchor_.utc_ms = ms;
  anchor_.uncertainty_known = known;
  anchor_.uncertainty_ms = known ? uncertainty : 0;
  return true;
}
} // namespace ridesync

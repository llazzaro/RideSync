#include "storage.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
namespace ridesync {
static_assert(ATOMIC_INT_LOCK_FREE == 2, "Storage requires lock-free 32-bit atomics");
static_assert(ATOMIC_BOOL_LOCK_FREE == 2, "Storage requires lock-free boolean atomics");
constexpr uint32_t Storage::kCapacity;
constexpr size_t Storage::kMaxRowBytes, Storage::kChunkBytes;
namespace {
bool token(const char *s, char *out) {
  if (!s)
    return false;
  size_t i = 0;
  for (; i < 49 && s[i]; ++i) {
    const char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
          c == '_' || c == '.'))
      return false;
    out[i] = c;
  }
  if (!i || i == 49)
    return false;
  out[i] = 0;
  return true;
}
bool metric(const GnssOptional<double> &v, double lo, double hi) {
  return !v.available || (std::isfinite(v.value) && v.value >= lo && v.value <= hi);
}
bool calendar(const GnssOptional<GnssDate> &v) {
  if (!v.available)
    return true;
  const auto &d = v.value;
  if (d.year < 2000 || d.year > 2099 || !d.month || d.month > 12 || !d.day)
    return false;
  const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return d.day <= days[d.month - 1] + (d.month == 2 && d.year % 4 == 0 ? 1 : 0);
}
struct Csv {
  char *data;
  size_t capacity, size = 0;
  bool ok = true;
  Csv(char *p, size_t n) : data(p), capacity(n) {}
  void append(const char *fmt, ...) {
    if (!ok)
      return;
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(data + size, capacity - size, fmt, args);
    va_end(args);
    if (n < 0 || static_cast<size_t>(n) >= capacity - size) {
      ok = false;
      return;
    }
    size += static_cast<size_t>(n);
  }
  void optional(const GnssOptional<double> &v) {
    if (v.available)
      append("%.6f", v.value);
    append(",");
  }
};
const char *header =
    "session_id,monotonic_ms,monotonic_quality,anchor_quality,anchor_sequence,anchor_receipt_ms,"
    "anchor_utc_ms,uncertainty_known,uncertainty_ms,anchor_age_ms,has_utc_estimate,utc_estimate_ms,"
    "modem_state,uart_health,gnss_power,receiver_ready,fix_validity,fix_age_ms,fix_valid,latitude_"
    "deg,longitude_deg,altitude_msl_m,speed_m_s,course_deg,source_utc_date,source_utc_time,fix_"
    "receipt_ms,satellites,fix_quality,accepted,dropped,rejected,lost,written,flushed\n";
} // namespace
void Storage::add(std::atomic<uint32_t> &c, uint32_t n) {
  // Each counter has exactly one writer; bounded load/store avoids CAS loops.
  uint32_t v = c.load(std::memory_order_relaxed);
  c.store(n > UINT32_MAX - v ? UINT32_MAX : v + n, std::memory_order_relaxed);
}
Storage::Storage(StorageSink &sink, const StorageConfig &c)
    : sink_(sink), session_(c.session_id), max_mounts_(c.mount_attempts),
      flush_records_(c.flush_records), valid_(false) {
  valid_ = session_ && max_mounts_ > 0 && max_mounts_ <= 3 && flush_records_ > 0 &&
           flush_records_ <= kCapacity && token(c.firmware, firmware_) &&
           token(c.provenance, provenance_);
  snprintf(path_, sizeof(path_), "/gps-%016llx.csv", static_cast<unsigned long long>(session_));
}
bool Storage::enqueue(const RecordTimestamp &t, const ModemSnapshot &s) {
  const auto &f = s.fix;
  const auto &time = f.utc_time.value;
  if (!valid_ || t.session_id != session_ || s.session_id != session_ ||
      t.monotonic_quality != MonotonicQuality::Valid ||
      t.monotonic_ms > SessionClock::kMaxDurationMs ||
      static_cast<unsigned>(t.anchor_quality) > 2 || static_cast<unsigned>(s.validity) > 4 ||
      static_cast<unsigned>(s.state) > 6 || static_cast<unsigned>(s.health) > 6 ||
      (t.anchor_quality != AnchorQuality::Missing &&
       (!t.anchor.sequence || t.anchor.receipt_ms > t.monotonic_ms ||
        t.anchor_age_ms != t.monotonic_ms - t.anchor.receipt_ms)) ||
      (t.anchor_quality == AnchorQuality::Missing && (t.anchor.sequence || t.has_utc_estimate)) ||
      (t.anchor.sequence &&
       (t.anchor.utc_ms < 946684800000LL || t.anchor.utc_ms > 4102444799999LL)) ||
      (t.has_utc_estimate &&
       (t.anchor_quality != AnchorQuality::Fresh ||
        t.utc_estimate_ms != t.anchor.utc_ms + static_cast<int64_t>(t.anchor_age_ms))) ||
      (s.age_available && (f.receipt_monotonic_ms > t.monotonic_ms ||
                           s.age_ms != t.monotonic_ms - f.receipt_monotonic_ms)) ||
      (s.validity == FixValidity::Valid && !f.valid) ||
      (f.valid && (!std::isfinite(f.latitude_degrees) || !std::isfinite(f.longitude_degrees) ||
                   std::abs(f.latitude_degrees) > 90 || std::abs(f.longitude_degrees) > 180 ||
                   f.receipt_monotonic_ms > t.monotonic_ms)) ||
      !metric(f.altitude_msl_metres, -100000, 100000) ||
      !metric(f.speed_metres_per_second, 0, 100000) || !metric(f.course_degrees, 0, 360) ||
      !calendar(f.utc_date) ||
      (f.utc_time.available &&
       (time.hour > 23 || time.minute > 59 || time.second > 59 || time.centisecond > 99))) {
    add(rejected_);
    return false;
  }
  if (terminal_.load(std::memory_order_acquire) || stop_.load(std::memory_order_acquire)) {
    add(dropped_);
    return false;
  }
  uint32_t w = write_.load(std::memory_order_relaxed), r = read_.load(std::memory_order_acquire);
  if (w - r == kCapacity) {
    add(dropped_);
    return false;
  }
  queue_[w % kCapacity] = {t, s};
  write_.store(w + 1, std::memory_order_release);
  add(accepted_);
  return true;
}
void Storage::requestStop() { stop_.store(true, std::memory_order_release); }
StorageHealth Storage::health() const {
  const auto accepted = accepted_.load(), flushed = flushed_.load();
  const bool terminal = terminal_.load();
  // Include a producer publication racing with terminal failure even when the
  // worker has already exited. Until saturation, every unflushed accepted record
  // is conservatively lost/uncertain on terminal media failure.
  return {accepted,         dropped_.load(),
          rejected_.load(), written_.load(),
          flushed,          terminal && accepted >= flushed ? accepted - flushed : lost_.load(),
          progress_.load(), terminal,
          stopped_.load()};
}
bool Storage::format(const Record &r) {
  Csv c(buffer_, sizeof(buffer_));
  const auto &t = r.timestamp;
  const auto &s = r.sample;
  const auto &f = s.fix;
  c.append("%llu,%llu,%u,%u,", (unsigned long long)t.session_id, (unsigned long long)t.monotonic_ms,
           (unsigned)t.monotonic_quality, (unsigned)t.anchor_quality);
  if (t.anchor.sequence) {
    c.append("%u,%llu,%lld,%u,", t.anchor.sequence, (unsigned long long)t.anchor.receipt_ms,
             (long long)t.anchor.utc_ms, t.anchor.uncertainty_known);
    if (t.anchor.uncertainty_known)
      c.append("%u", t.anchor.uncertainty_ms);
    c.append(",%llu,", (unsigned long long)t.anchor_age_ms);
  } else
    c.append(",,,0,,,");
  c.append("%u,", t.has_utc_estimate);
  if (t.has_utc_estimate)
    c.append("%lld", (long long)t.utc_estimate_ms);
  c.append(",%u,%u,%u,%u,%u,", (unsigned)s.state, (unsigned)s.health, s.gnss_power_enabled,
           s.receiver_ready, (unsigned)s.validity);
  if (s.age_available)
    c.append("%llu", (unsigned long long)s.age_ms);
  c.append(",%u,", f.valid);
  if (f.valid)
    c.append("%.8f,%.8f,", f.latitude_degrees, f.longitude_degrees);
  else
    c.append(",,");
  c.optional(f.altitude_msl_metres);
  c.optional(f.speed_metres_per_second);
  c.optional(f.course_degrees);
  if (f.utc_date.available)
    c.append("%04u-%02u-%02u", f.utc_date.value.year, f.utc_date.value.month, f.utc_date.value.day);
  c.append(",");
  if (f.utc_time.available)
    c.append("%02u:%02u:%02u.%02u", f.utc_time.value.hour, f.utc_time.value.minute,
             f.utc_time.value.second, f.utc_time.value.centisecond);
  c.append(",");
  if (s.age_available || f.valid || f.utc_date.available || f.utc_time.available)
    c.append("%llu", (unsigned long long)f.receipt_monotonic_ms);
  c.append(",");
  if (f.satellites.available)
    c.append("%u", f.satellites.value);
  c.append(",");
  if (f.fix_quality.available)
    c.append("%u", f.fix_quality.value);
  auto h = health();
  c.append(",%u,%u,%u,%u,%u,%u\n", h.accepted, h.dropped, h.rejected, h.lost, h.written, h.flushed);
  length_ = c.size;
  offset_ = 0;
  return c.ok;
}
void Storage::fail() {
  terminal_.store(true, std::memory_order_release);
  if (row_in_flight_) {
    add(lost_);
    row_in_flight_ = false;
  }
  add(lost_, cached_);
  cached_ = 0;
  stage_ = Stage::Close;
}
void Storage::writeChunk() {
  size_t n = length_ - offset_;
  if (n > kChunkBytes)
    n = kChunkBytes;
  if (sink_.write(buffer_ + offset_, n) != n) {
    fail();
    return;
  }
  offset_ += n;
  if (offset_ == length_) {
    length_ = offset_ = 0;
    if (row_in_flight_) {
      row_in_flight_ = false;
      add(written_);
      ++cached_;
    }
    stage_ = cached_ >= flush_records_ ? Stage::Flush : Stage::Rows;
  }
}
void Storage::workerStep() {
  if (!valid_) {
    terminal_.store(true);
    stopped_.store(true);
    stage_ = Stage::Done;
    return;
  }
  switch (stage_) {
  case Stage::Mount:
    ++mounts_;
    if (sink_.mount())
      stage_ = Stage::Open;
    else if (mounts_ >= max_mounts_)
      fail();
    break;
  case Stage::Open:
    if (!sink_.openExclusive(path_))
      fail();
    else {
      Csv c(buffer_, sizeof(buffer_));
      c.append(
          "#ridesync_gps,1\n#session,%llu,firmware,%s,provenance,%s\n#policy,queue_records,8,flush_"
          "records,%u,idle_flush,1,chunk_bytes,256,mount_attempts,%u,write_retries,0\n%s",
          (unsigned long long)session_, firmware_, provenance_, flush_records_, max_mounts_,
          header);
      length_ = c.size;
      offset_ = 0;
      if (!c.ok)
        fail();
      else
        stage_ = Stage::Header;
    }
    break;
  case Stage::Header:
    writeChunk();
    break;
  case Stage::Rows: {
    if (length_) {
      writeChunk();
      break;
    }
    const uint32_t r = read_.load(std::memory_order_relaxed),
                   w = write_.load(std::memory_order_acquire);
    if (r != w) {
      Record record = queue_[r % kCapacity];
      read_.store(r + 1, std::memory_order_release);
      row_in_flight_ = true;
      if (!format(record))
        fail();
      else
        writeChunk();
    } else if (cached_)
      stage_ = Stage::Flush;
    else if (stop_.load(std::memory_order_acquire)) {
      // Stop is published by the sole producer AFTER its last enqueue. Acquire
      // that publication before a final queue observation, never close from the
      // earlier empty snapshot which may precede the producer's final record.
      if (read_.load(std::memory_order_relaxed) == write_.load(std::memory_order_acquire))
        stage_ = Stage::Close;
    }
    break;
  }
  case Stage::Flush:
    if (!sink_.flush())
      fail();
    else {
      add(flushed_, cached_);
      cached_ = 0;
      stage_ = Stage::Rows;
    }
    break;
  case Stage::Close:
    sink_.close();
    stopped_.store(true);
    stage_ = Stage::Done;
    break;
  case Stage::Done:
    break;
  }
  if (terminal_.load()) {
    // Drain only published slots; a racing producer publication is picked up on
    // the next step. No queue slot or producer mutex is held across sink calls.
    uint32_t r = read_.load(), w = write_.load(std::memory_order_acquire);
    add(lost_, w - r);
    read_.store(w, std::memory_order_release);
  }
  add(progress_);
}
} // namespace ridesync

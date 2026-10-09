#include "storage.h"
#include "motion_estimator.h"
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
bool vectorFinite(const MotionVector &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool motionValid(const ImuEvidence &e, const MotionEvidence &m) {
  const auto &o = m.estimate;
  if (static_cast<unsigned>(m.state) > 3 || o.dynamic_lean_valid || o.dynamic_acceleration_valid)
    return false;
  if (m.state != MotionAdmission::Enabled)
    return !o.measurements_valid && !o.static_tilt_valid;
  if (!MotionEstimator::configValid(m.config) || !m.snapshot_max_age_ms ||
      m.snapshot_max_age_ms > 60000)
    return false;
  if (e.kind != RecordKind::ImuSample)
    return !o.measurements_valid && !o.static_tilt_valid && !m.reference.externally_stationary &&
           !m.reference.session_id && !m.reference.config_generation &&
           !m.reference.batch_sequence && !m.reference.declaration;
  const auto &v = e.config;
  if (o.measurements_valid) {
    if (!v.generation || !v.sensor_id || v.sensor_state != Qualification::Qualified ||
        v.mount_state != Qualification::Qualified ||
        v.calibration_state != Qualification::Qualified || v.mount_id != m.config.mount_id ||
        v.calibration_id != m.config.calibration_id ||
        v.accel_offset_compensation != m.config.accel_compensation ||
        v.gyro_offset_compensation != m.config.gyro_compensation || !v.calibration_offsets_known ||
        !v.calibration_gains_known || !v.accel_scale_numerator || !v.accel_scale_denominator ||
        !v.gyro_scale_numerator || !v.gyro_scale_denominator || (e.timing_flags & 8))
      return false;
    for (unsigned i = 0; i < 3; ++i)
      if (e.accel[i] <= -32767 || e.accel[i] >= 32767 || e.gyro[i] <= -32767 || e.gyro[i] >= 32767)
        return false;
  }
  if (o.measurements_valid &&
      (!vectorFinite(o.specific_force_mps2) || !vectorFinite(o.angular_rate_rad_s)))
    return false;
  if (o.static_tilt_valid &&
      (!o.measurements_valid || !std::isfinite(o.roll_rad) || !std::isfinite(o.pitch_rad) ||
       !m.reference.externally_stationary || !m.reference.declaration ||
       m.reference.session_id != e.session_id ||
       m.reference.config_generation != e.config.generation ||
       m.reference.batch_sequence != e.batch_sequence || !e.receipt_known || (e.timing_flags & 7)))
    return false;
  return true;
}
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
void formatMotion(Csv &c, const MotionEvidence &m) {
  c.append(",%u", static_cast<unsigned>(m.state));
  if (m.state != MotionAdmission::Enabled) {
    for (unsigned field = 1; field < 36; ++field) {
      c.append(",");
      if (field >= 24 && field <= 27)
        c.append("0");
    }
    return;
  }
  const auto &v = m.config;
  const auto &r = m.reference;
  const auto &o = m.estimate;
  c.append(",1,%u,%u,%u,%u,%u,%u,%u,%u", m.snapshot_max_age_ms, static_cast<unsigned>(v.convention),
           v.mount_qualified, v.residual_calibration_qualified, v.mount_id, v.calibration_id,
           v.accel_compensation, v.gyro_compensation);
  for (float x : v.sensor_to_body)
    c.append(",%.9g", double(x));
  c.append(",%u,%llu,%u,%u,%u,%u,%u,0,0", r.externally_stationary, (unsigned long long)r.session_id,
           r.config_generation, r.batch_sequence, r.declaration, o.measurements_valid,
           o.static_tilt_valid);
  const float values[] = {o.specific_force_mps2.x,
                          o.specific_force_mps2.y,
                          o.specific_force_mps2.z,
                          o.angular_rate_rad_s.x,
                          o.angular_rate_rad_s.y,
                          o.angular_rate_rad_s.z,
                          o.roll_rad,
                          o.pitch_rad};
  for (unsigned i = 0; i < 8; ++i) {
    c.append(",");
    if (i < 6 ? o.measurements_valid : o.static_tilt_valid)
      c.append("%.9g", double(values[i]));
  }
}
void formatTimestamp(Csv &c, const RecordTimestamp &t) {
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
}
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
    : format_(c.format), sink_(sink), session_(c.session_id), max_mounts_(c.mount_attempts),
      flush_records_(c.flush_records), valid_(false) {
  valid_ = (format_ == StorageFormat::GpsV1 || format_ == StorageFormat::MixedV2 ||
            format_ == StorageFormat::CameraV3 || format_ == StorageFormat::MotionV4) &&
           session_ && max_mounts_ > 0 && max_mounts_ <= 3 && flush_records_ > 0 &&
           flush_records_ <= kCapacity && token(c.firmware, firmware_) &&
           token(c.provenance, provenance_);
  snprintf(path_, sizeof(path_),
           format_ == StorageFormat::GpsV1 ? "/gps-%016llx.csv" : "/telemetry-%016llx.csv",
           static_cast<unsigned long long>(session_));
}
bool Storage::validTimestamp(const RecordTimestamp &t) const {
  return t.session_id == session_ &&
         !(t.monotonic_quality != MonotonicQuality::Valid ||
           t.monotonic_ms > SessionClock::kMaxDurationMs ||
           static_cast<unsigned>(t.anchor_quality) > 2 ||
           (t.anchor_quality != AnchorQuality::Missing &&
            (!t.anchor.sequence || t.anchor.receipt_ms > t.monotonic_ms ||
             t.anchor_age_ms != t.monotonic_ms - t.anchor.receipt_ms)) ||
           (t.anchor_quality == AnchorQuality::Missing &&
            (t.anchor.sequence || t.has_utc_estimate)) ||
           (t.anchor.sequence &&
            (t.anchor.utc_ms < 946684800000LL || t.anchor.utc_ms > 4102444799999LL)) ||
           (t.has_utc_estimate &&
            (t.anchor_quality != AnchorQuality::Fresh ||
             t.utc_estimate_ms != t.anchor.utc_ms + static_cast<int64_t>(t.anchor_age_ms))));
}
bool Storage::enqueue(const RecordTimestamp &t, const ModemSnapshot &s) {
  const auto &f = s.fix;
  const auto &time = f.utc_time.value;
  if (!valid_ || t.session_id != session_ || s.session_id != session_ || !validTimestamp(t) ||
      static_cast<unsigned>(s.validity) > 4 || static_cast<unsigned>(s.state) > 6 ||
      static_cast<unsigned>(s.health) > 6 ||
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
    add(kinds_[0].rejected);
    return false;
  }
  Record record;
  record.timestamp = t;
  record.gps = s;
  return publish(record, false);
}
void Storage::drop(RecordKind kind) {
  add(dropped_);
  if (kind < RecordKind::Count)
    add(kinds_[static_cast<unsigned>(kind)].dropped);
}
void Storage::droppedCamera(uint32_t count) {
  add(dropped_, count);
  add(kinds_[static_cast<unsigned>(RecordKind::Camera)].dropped, count);
}
void Storage::rejectedCamera(uint32_t count) {
  add(rejected_, count);
  add(kinds_[static_cast<unsigned>(RecordKind::Camera)].rejected, count);
}
bool Storage::publish(const Record &record, bool reserve) {
  auto &h = kinds_[static_cast<unsigned>(record.kind)];
  uint32_t w = write_.load(std::memory_order_relaxed), r = read_.load(std::memory_order_acquire);
  if (terminal_.load(std::memory_order_acquire) || stop_.load(std::memory_order_acquire) ||
      w - r >= kCapacity - (reserve ? 2 : 0)) {
    drop(record.kind);
    return false;
  }
  queue_[w % kCapacity] = record;
  write_.store(w + 1, std::memory_order_release);
  add(accepted_);
  add(h.accepted);
  return true;
}
bool Storage::enqueueImu(const RecordTimestamp &t, const ImuEvidence &e, bool reserve,
                         const MotionEvidence &motion) {
  const auto k = static_cast<unsigned>(e.kind);
  const auto &c = e.config;
  const bool kind_valid = k > 0 && k < 5;
  if (!valid_ || format_ == StorageFormat::GpsV1 || !kind_valid || !validTimestamp(t) ||
      (format_ == StorageFormat::MotionV4 && !motionValid(e, motion)) || e.session_id != session_ ||
      static_cast<unsigned>(c.sensor_state) > 2 || static_cast<unsigned>(c.mount_state) > 2 ||
      static_cast<unsigned>(c.calibration_state) > 2 || c.accel_offset_compensation > 2 ||
      c.gyro_offset_compensation > 2 || e.timing_flags > 15 || e.event_code > 9 ||
      e.event_length > 4 || e.sensor_time_ticks24 > 0xffffff ||
      ((c.accel_scale_numerator == 0) != (c.accel_scale_denominator == 0)) ||
      ((c.gyro_scale_numerator == 0) != (c.gyro_scale_denominator == 0)) ||
      (c.sensor_state == Qualification::Qualified && !c.sensor_id) ||
      (c.mount_state == Qualification::Qualified && !c.mount_id) ||
      (c.calibration_state == Qualification::Qualified && !c.calibration_id) ||
      (e.kind == RecordKind::ImuSample &&
       (e.event_code || e.event_length || e.sensor_time_present || e.event_count)) ||
      (e.sensor_time_present &&
       (e.kind != RecordKind::ImuControl || e.event_code != 5 || e.event_length != 3 ||
        e.sensor_time_ticks24 != (static_cast<uint32_t>(e.event_bytes[0]) |
                                  (static_cast<uint32_t>(e.event_bytes[1]) << 8) |
                                  (static_cast<uint32_t>(e.event_bytes[2]) << 16))))) {
    add(rejected_);
    if (kind_valid)
      add(kinds_[k].rejected);
    return false;
  }
  if (c.calibration_gains_known) {
    for (unsigned i = 0; i < 3; ++i) {
      if (!c.accel_gain_numerator[i] || !c.accel_gain_denominator[i] || !c.gyro_gain_numerator[i] ||
          !c.gyro_gain_denominator[i]) {
        add(rejected_);
        add(kinds_[k].rejected);
        return false;
      }
    }
  }
  Record record;
  record.kind = e.kind;
  record.timestamp = t;
  record.imu = e;
  record.motion = motion;
  return publish(record, reserve);
}
bool Storage::enqueueCamera(const RecordTimestamp &t, const CameraEvidence &e, bool reserve) {
  const bool valid_event =
      static_cast<unsigned>(e.kind) <= static_cast<unsigned>(CameraEventKind::Disconnected);
  const bool ack = e.kind == CameraEventKind::WireAck;
  const bool observation = e.kind == CameraEventKind::RecordingObserved;
  const bool request = e.kind == CameraEventKind::RequestAccepted ||
                       e.kind == CameraEventKind::RequestQueued ||
                       e.kind == CameraEventKind::RequestRefused;
  const bool setup_ack =
      e.ack_action == CameraAckAction::Pair || e.ack_action == CameraAckAction::Claim;
  if (!valid_ || (format_ != StorageFormat::CameraV3 && format_ != StorageFormat::MotionV4) ||
      !validTimestamp(t) || e.session_id != session_ || e.peer_slot >= kMaxCameras || !e.peer_id ||
      (e.event_receipt_known &&
       (e.event_receipt_age_ms > 60000 || e.event_receipt_ms > t.monotonic_ms ||
        t.monotonic_ms - e.event_receipt_ms != e.event_receipt_age_ms)) ||
      (!e.event_receipt_known && (e.event_receipt_ms || e.event_receipt_age_ms)) ||
      e.model == CameraModel::Unknown ||
      static_cast<unsigned>(e.model) > static_cast<unsigned>(CameraModel::HERO12_BLACK) ||
      !valid_event || static_cast<unsigned>(e.operation) > static_cast<unsigned>(Operation::Wake) ||
      static_cast<unsigned>(e.error) > static_cast<unsigned>(CameraError::InvalidPolicy) ||
      static_cast<unsigned>(e.recording) > static_cast<unsigned>(RecordingState::Recording) ||
      static_cast<unsigned>(e.ack_domain) > static_cast<unsigned>(CameraAckDomain::Protobuf) ||
      static_cast<unsigned>(e.ack_action) > static_cast<unsigned>(CameraAckAction::QueryEncoding) ||
      (ack && (!e.intent_id || !e.connection_generation || !e.operation_generation ||
               e.ack_domain == CameraAckDomain::None || e.ack_action == CameraAckAction::None)) ||
      (ack && ((setup_ack && e.ack_domain != CameraAckDomain::Protobuf) ||
               (!setup_ack && e.ack_domain != CameraAckDomain::Classic))) ||
      (!ack && (e.ack_domain != CameraAckDomain::None || e.ack_action != CameraAckAction::None)) ||
      (!observation && e.recording != RecordingState::Unknown) ||
      (observation && (e.recording == RecordingState::Unknown || !e.connection_generation)) ||
      (request &&
       ((e.kind == CameraEventKind::RequestRefused) == (e.error == CameraError::None))) ||
      (request && e.kind != CameraEventKind::RequestRefused && !e.intent_id) ||
      (request && (e.connection_generation || e.operation_generation || e.delivery_admitted)) ||
      (e.kind == CameraEventKind::Attempt &&
       (!e.intent_id || !e.connection_generation || !e.operation_generation ||
        (e.delivery_admitted ? e.error != CameraError::None
                             : e.error != CameraError::Transport))) ||
      (e.kind != CameraEventKind::Attempt && e.delivery_admitted)) {
    add(rejected_);
    add(kinds_[static_cast<unsigned>(RecordKind::Camera)].rejected);
    return false;
  }
  Record record;
  record.kind = RecordKind::Camera;
  record.timestamp = t;
  record.camera = e;
  return publish(record, reserve);
}
KindHealth Storage::kindHealth(RecordKind kind) const {
  if (kind >= RecordKind::Count)
    return {};
  const auto &h = kinds_[static_cast<unsigned>(kind)];
  const auto a = h.accepted.load(), f = h.flushed.load();
  return {a,
          h.dropped.load(),
          h.rejected.load(),
          h.written.load(),
          f,
          terminal_.load() && a >= f ? a - f : 0};
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
  if (r.kind == RecordKind::Camera)
    return formatCamera(r);
  if (r.kind != RecordKind::Gps)
    return formatImu(r);
  Csv c(buffer_, sizeof(buffer_));
  if (format_ != StorageFormat::GpsV1)
    c.append("gps,");
  const auto &s = r.gps;
  const auto &f = s.fix;
  formatTimestamp(c, r.timestamp);
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
  c.append(",%u,%u,%u,%u,%u,%u", h.accepted, h.dropped, h.rejected, h.lost, h.written, h.flushed);
  c.append("\n");
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
      const unsigned k = static_cast<unsigned>(in_flight_kind_);
      add(kinds_[k].written);
      ++cached_kinds_[k];
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
      c.append("%s\n#session,%llu,firmware,%s,provenance,%s\n#policy,queue_records,8,flush_"
               "records,%u,idle_flush,1,chunk_bytes,256,mount_attempts,%u,write_retries,0\n%s",
               format_ == StorageFormat::GpsV1      ? "#ridesync_gps,1"
               : format_ == StorageFormat::MixedV2  ? "#ridesync_telemetry,2"
               : format_ == StorageFormat::CameraV3 ? "#ridesync_telemetry,3"
                                                    : "#ridesync_telemetry,4",
               (unsigned long long)session_, firmware_, provenance_, flush_records_, max_mounts_,
               header);
      if (format_ != StorageFormat::GpsV1)
        c.append(format_ == StorageFormat::MotionV4
                     ? "#imu_layout,4,see_docs/motion_logging.md\n"
                     : "#imu_layout,2,see_docs/mixed_telemetry.md\n");
      if (format_ == StorageFormat::CameraV3 || format_ == StorageFormat::MotionV4)
        c.append("#camera_layout,3,see_docs/log_format.md\n");
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
      in_flight_kind_ = record.kind;
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
      for (unsigned k = 0; k < static_cast<unsigned>(RecordKind::Count); ++k) {
        add(kinds_[k].flushed, cached_kinds_[k]);
        cached_kinds_[k] = 0;
      }
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

namespace ridesync {
bool Storage::formatImu(const Record &r) {
  Csv c(buffer_, sizeof(buffer_));
  const auto &e = r.imu;
  const auto &v = e.config;
  const char *names[] = {"gps", "imu", "config", "health", "control"};
  c.append("%s,", names[static_cast<unsigned>(r.kind)]);
  formatTimestamp(c, r.timestamp);
  c.append(",%u,%u,%u,%u,%u,", e.batch_sequence, e.frame_sequence, e.byte_position, e.sensor_epoch,
           e.receipt_known);
  if (e.receipt_known)
    c.append("%u", e.receipt_millis32);
  c.append(",%u,", e.drain_known);
  if (e.drain_known)
    c.append("%u", e.drain_start_millis32);
  c.append(",");
  if (e.drain_known)
    c.append("%u", e.drain_end_millis32);
  c.append(",%u,0,,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,", e.timing_flags,
           v.generation, v.sensor_id, v.mount_id, v.calibration_id, (unsigned)v.sensor_state,
           (unsigned)v.mount_state, (unsigned)v.calibration_state, v.accel_range_mg,
           v.gyro_range_mdps, v.accel_scale_numerator, v.accel_scale_denominator,
           v.gyro_scale_numerator, v.gyro_scale_denominator, v.accel_odr_millihz,
           v.gyro_odr_millihz, v.accel_filter, v.gyro_filter, v.accel_offset_compensation,
           v.gyro_offset_compensation, v.calibration_method, v.calibration_time_known);
  if (v.calibration_time_known)
    c.append("%lld", (long long)v.calibration_utc_ms);
  c.append(",%u,", v.calibration_temperature_known);
  if (v.calibration_temperature_known)
    c.append("%d", v.calibration_temperature_millic);
  c.append(",%u", v.calibration_offsets_known);
  for (auto x : v.accel_offset) {
    c.append(",");
    if (v.calibration_offsets_known)
      c.append("%d", x);
  }
  for (auto x : v.gyro_offset) {
    c.append(",");
    if (v.calibration_offsets_known)
      c.append("%d", x);
  }
  c.append(",%u", v.calibration_gains_known);
  for (unsigned i = 0; i < 3; ++i) {
    c.append(",");
    if (v.calibration_gains_known)
      c.append("%u", v.accel_gain_numerator[i]);
    c.append(",");
    if (v.calibration_gains_known)
      c.append("%u", v.accel_gain_denominator[i]);
  }
  for (unsigned i = 0; i < 3; ++i) {
    c.append(",");
    if (v.calibration_gains_known)
      c.append("%u", v.gyro_gain_numerator[i]);
    c.append(",");
    if (v.calibration_gains_known)
      c.append("%u", v.gyro_gain_denominator[i]);
  }
  for (auto x : e.accel) {
    c.append(",");
    if (r.kind == RecordKind::ImuSample)
      c.append("%d", x);
  }
  for (auto x : e.gyro) {
    c.append(",");
    if (r.kind == RecordKind::ImuSample)
      c.append("%d", x);
  }
  c.append(",%u,%u,", e.event_code, e.event_length);
  for (uint8_t i = 0; i < e.event_length; ++i)
    c.append("%02x", e.event_bytes[i]);
  c.append(",%u,", e.sensor_time_present);
  if (e.sensor_time_present)
    c.append("%u", e.sensor_time_ticks24);
  c.append(",%u,%u", e.event_count, e.event_count_lower_bound);
  auto h = kindHealth(r.kind);
  c.append(",%u,%u,%u,%u,%u,%u", h.accepted, h.dropped, h.rejected, h.lost, h.written, h.flushed);
  if (format_ == StorageFormat::MotionV4)
    formatMotion(c, r.motion);
  c.append("\n");
  length_ = c.size;
  offset_ = 0;
  return c.ok;
}
bool Storage::formatCamera(const Record &r) {
  Csv c(buffer_, sizeof(buffer_));
  const auto &e = r.camera;
  c.append("camera,");
  formatTimestamp(c, r.timestamp);
  c.append(",%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,owner_admission,%u,", e.peer_slot, e.peer_id,
           static_cast<unsigned>(e.model), e.group_generation, e.intent_id, e.connection_generation,
           e.operation_generation, static_cast<unsigned>(e.kind),
           static_cast<unsigned>(e.operation), static_cast<unsigned>(e.error),
           static_cast<unsigned>(e.recording), static_cast<unsigned>(e.ack_domain),
           static_cast<unsigned>(e.ack_action), e.delivery_admitted, e.event_receipt_known);
  if (e.event_receipt_known)
    c.append("%llu", (unsigned long long)e.event_receipt_ms);
  c.append(",");
  if (e.event_receipt_known)
    c.append("%u", e.event_receipt_age_ms);
  c.append(",0,,0,,");
  auto h = kindHealth(RecordKind::Camera);
  c.append("%u,%u,%u,%u,%u,%u\n", h.accepted, h.dropped, h.rejected, h.lost, h.written, h.flushed);
  length_ = c.size;
  offset_ = 0;
  return c.ok;
}
} // namespace ridesync

#include "imu_manager.h"
#include <climits>
namespace ridesync {
namespace {
void add(uint32_t &value, uint32_t n = 1) {
  value = n > UINT32_MAX - value ? UINT32_MAX : value + n;
}
int16_t signedLe(const uint8_t *p) {
  const uint16_t u = uint16_t(p[0]) | (uint16_t(p[1]) << 8);
  return u < 32768 ? int16_t(u) : int16_t(int32_t(u) - 65536);
}
} // namespace
bool ImuFifoCodec::decode(const uint8_t *bytes, uint16_t length, ImuEvidence base,
                          ImuEmitter &out) {
  if (length > 112 || (!bytes && length)) {
    add(health_.unsupported);
    add(health_.discontinuities);
    base.kind = RecordKind::ImuControl;
    base.event_code = 1;
    base.timing_flags |= 1;
    out.emit(base);
    return false;
  }
  for (uint16_t pos = 0; pos < length;) {
    ImuEvidence e = base;
    e.byte_position = pos;
    e.kind = RecordKind::ImuControl;
    uint8_t header = bytes[pos++], n = 0;
    if (header == 0x80)
      return true;
    switch (header) {
    case 0x8c:
      n = 12;
      break;
    case 0x44:
      n = 3;
      break;
    case 0x40:
      n = 1;
      break;
    case 0x48:
      n = 4;
      break;
    default:
      add(health_.unsupported);
      add(health_.discontinuities);
      e.event_code = 1;
      e.event_length = 1;
      e.event_bytes[0] = header;
      e.timing_flags |= 1;
      out.emit(e);
      return false;
    }
    if (n > length - pos) {
      add(health_.partial);
      add(health_.discontinuities);
      e.event_code = 3;
      e.timing_flags |= 1;
      out.emit(e);
      return false;
    }
    const uint8_t *p = bytes + pos;
    if (header == 0x8c) {
      if (frame_ == UINT32_MAX) {
        e.event_code = 1;
        e.timing_flags |= 5;
        out.emit(e);
        return false;
      }
      e.kind = RecordKind::ImuSample;
      e.frame_sequence = ++frame_;
      add(health_.samples);
      for (unsigned axis = 0; axis < 3; ++axis) {
        e.gyro[axis] = signedLe(p + 2 * axis);
        e.accel[axis] = signedLe(p + 6 + 2 * axis);
        if (e.gyro[axis] == 32767 || e.gyro[axis] == -32767 || e.accel[axis] == 32767 ||
            e.accel[axis] == -32767)
          e.timing_flags |= 8;
      }
    } else {
      e.event_length = n;
      for (unsigned j = 0; j < n; ++j)
        e.event_bytes[j] = p[j];
      if (header == 0x44) {
        e.event_code = 5;
        e.sensor_time_present = true;
        e.sensor_time_ticks24 = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16);
        if (time_ && e.sensor_time_ticks24 == tick_)
          e.timing_flags |= 2;
        if (time_ && (e.sensor_time_ticks24 < tick_ || !base.receipt_known ||
                      uint32_t(base.receipt_millis32 - receipt_) >= 655360))
          e.timing_flags |= 4;
        receipt_ = base.receipt_millis32;
        tick_ = e.sensor_time_ticks24;
        time_ = true;
      } else if (header == 0x40) {
        e.event_code = 4;
        e.event_count = p[0];
        add(health_.skipped_lower_bound, p[0]);
        add(health_.discontinuities);
        e.event_count_lower_bound = p[0] == 255;
        e.timing_flags |= 1;
      } else {
        add(health_.discontinuities);
        e.event_code = 6;
        e.timing_flags |= 1;
        out.emit(e);
        return false;
      }
    }
    out.emit(e);
    pos += n;
  }
  return true;
}
void ImuFifoCodec::barrier() { time_ = false; }
ImuManager::ImuManager(ImuPort &p, ImuInbox &i, HealthProgress &h, uint64_t s, const ImuConfig &c)
    : port_(p), inbox_(i), progress_(h) {
  base_.session_id = s;
  base_.config = c;
  base_.sensor_epoch = 1;
}
void ImuManager::emit(const ImuEvidence &e) {
  if (e.event_code == 6 || e.event_code == 8)
    profile_unknown_ = true;
  batch_.records[batch_.count++] = e;
  if (batch_.count == ImuBatch::kRecords)
    publish();
}
void ImuManager::publish() {
  if (batch_.count) {
    inbox_.publish(batch_);
    batch_.count = 0;
  }
}
void ImuManager::step(uint32_t now) {
  if (state_ == State::Finished)
    return;
  base_.drain_known = false;
  base_.receipt_known = true;
  base_.receipt_millis32 = now;
  if (stop_ || inbox_.stopRequested()) {
    publish();
    inbox_.finish();
    state_ = State::Finished;
    progress_.finished();
    return;
  }
  if (state_ == State::Failed) {
    progress_.completed(DeviceHealth::RetryExhausted);
    return;
  }
  if (base_.batch_sequence == UINT32_MAX || base_.sensor_epoch == UINT32_MAX) {
    state_ = State::Failed;
    return;
  }
  ++base_.batch_sequence;
  base_.timing_flags = receipt_ && now == previous_ ? 2 : 0;
  previous_ = now;
  receipt_ = true;
  if (state_ == State::Initial) {
    bool ok = port_.begin(base_.config);
    if (!ok)
      add(health_.init_errors);
    state_ = ok ? State::Ready : State::Failed;
    ImuEvidence e = base_;
    e.kind = ok ? RecordKind::ImuConfig : RecordKind::ImuHealth;
    e.event_code = ok ? 0 : 2;
    emit(e);
    publish();
    progress_.completed(ok ? DeviceHealth::Ok : DeviceHealth::Missing);
    return;
  }
  if (state_ == State::Barrier) {
    const bool ok = port_.flush();
    if (ok)
      add(health_.recovery_flushes);
    else
      add(health_.recovery_errors);
    ImuEvidence e = base_;
    e.kind = RecordKind::ImuControl;
    e.event_code = ok ? 9 : 2;
    e.timing_flags |= 1;
    emit(e);
    publish();
    if (ok) {
      ++base_.sensor_epoch;
      codec_.barrier();
      state_ = State::Ready;
    } else
      state_ = State::Failed;
    progress_.completed(ok ? DeviceHealth::Ok : DeviceHealth::IoError);
    return;
  }
  uint16_t n = 0;
  uint8_t flags = 0;
  const bool ok = port_.read(bytes_, kBytes, n, flags);
  base_.drain_known = port_.drainTimes(base_.drain_start_millis32, base_.drain_end_millis32);
  if (!ok || n > kBytes) {
    add(health_.read_errors);
    ImuEvidence e = base_;
    e.kind = RecordKind::ImuHealth;
    port_.describeReadFailure(e);
    e.timing_flags |= 1;
    emit(e);
    state_ = ++failures_ >= 3 || profile_unknown_ ? State::Failed : State::Barrier;
  } else {
    if (flags) {
      ImuEvidence e = base_;
      e.kind = RecordKind::ImuHealth;
      e.event_length = 1;
      e.event_bytes[0] = flags;
      emit(e);
    }
    if (!codec_.decode(bytes_, n, base_, *this))
      state_ = ++failures_ >= 3 || profile_unknown_ ? State::Failed : State::Barrier;
    else if (n)
      failures_ = 0;
  }
  publish();
  progress_.completed(ok ? DeviceHealth::Ok : DeviceHealth::IoError);
}
} // namespace ridesync

#include "telemetry_admission.h"
namespace ridesync {
namespace {
constexpr uint32_t kCameraEventMaxAgeMs = 60000;
void increment(std::atomic<uint32_t> &counter, uint32_t amount = 1) {
  const uint32_t old = counter.load(std::memory_order_relaxed);
  counter.store(amount > UINT32_MAX - old ? UINT32_MAX : old + amount, std::memory_order_relaxed);
}
void admitCamera(SessionClock &clock, Storage &storage, CameraEvidence evidence) {
  uint32_t admission_raw;
  const auto timestamp = clock.snapshotWithRaw(admission_raw);
  if (evidence.event_receipt_known) {
    const uint32_t age = admission_raw - evidence.event_receipt_raw32;
    if (timestamp.monotonic_quality == MonotonicQuality::Valid && age <= kCameraEventMaxAgeMs &&
        age <= timestamp.monotonic_ms) {
      evidence.event_receipt_age_ms = age;
      evidence.event_receipt_ms = timestamp.monotonic_ms - age;
    } else {
      evidence.event_receipt_known = false;
      evidence.event_receipt_raw32 = 0;
    }
  }
  storage.enqueueCamera(timestamp, evidence);
}
} // namespace
constexpr uint32_t ImuInbox::kCapacity, CameraInbox::kCapacity;
constexpr uint8_t ImuBatch::kRecords, TelemetryAdmission::kQuota;
bool ImuInbox::publish(const ImuBatch &batch) {
  if (!batch.count || batch.count > ImuBatch::kRecords) {
    increment(rejected_);
    return false;
  }
  for (uint8_t i = 0; i < batch.count; ++i) {
    const auto kind = batch.records[i].kind;
    if (kind == RecordKind::Gps || kind >= RecordKind::Camera) {
      increment(rejected_);
      return false;
    }
  }
  const uint32_t w = write_.load(std::memory_order_relaxed);
  if (finished_.load(std::memory_order_acquire) ||
      w - read_.load(std::memory_order_acquire) == kCapacity) {
    for (uint8_t i = 0; i < batch.count; ++i)
      increment(dropped_[static_cast<unsigned>(batch.records[i].kind)]);
    return false;
  }
  slots_[w % kCapacity] = batch;
  write_.store(w + 1, std::memory_order_release);
  return true;
}
void ImuInbox::finish() { finished_.store(true, std::memory_order_release); }
bool ImuInbox::stopRequested() const { return stop_.load(std::memory_order_acquire); }
uint32_t ImuInbox::dropped(RecordKind kind) const {
  return kind < RecordKind::Camera ? dropped_[static_cast<unsigned>(kind)].load() : 0;
}
uint32_t ImuInbox::rejected() const { return rejected_.load(); }
bool CameraInbox::publish(const CameraEvidence &e) {
  if (!e.session_id || e.peer_slot >= kMaxCameras || !e.peer_id ||
      e.model == CameraModel::Unknown ||
      static_cast<unsigned>(e.kind) > static_cast<unsigned>(CameraEventKind::Disconnected)) {
    increment(rejected_);
    return false;
  }
  const uint32_t w = write_.load(std::memory_order_relaxed);
  if (finished_.load(std::memory_order_acquire) ||
      w - read_.load(std::memory_order_acquire) == kCapacity) {
    increment(dropped_);
    return false;
  }
  slots_[w % kCapacity] = e;
  write_.store(w + 1, std::memory_order_release);
  return true;
}
void CameraInbox::finish() { finished_.store(true, std::memory_order_release); }
bool CameraInbox::stopRequested() const { return stop_.load(std::memory_order_acquire); }
TelemetryAdmission::TelemetryAdmission(SessionClock &clock, Storage &storage, ImuInbox &inbox,
                                       CameraInbox *camera, const MotionAdmissionConfig &motion,
                                       StaticMotionReferenceSource *source)
    : clock_(clock), storage_(storage), inbox_(inbox), camera_(camera), motion_options_(motion),
      estimator_(motion.estimator), dynamic_(motion.dynamic), source_(source) {
  if (motion.requested)
    motion_state_ =
        motionAdmissionQualified(motion) ? MotionAdmission::Enabled : MotionAdmission::Refused;
}
void TelemetryAdmission::beginMotionPass() { current_ = {}; }
void TelemetryAdmission::withdrawMotionReference() {
  current_ = {};
  estimator_.reset();
}
void TelemetryAdmission::revokeMotion() {
  dynamic_.reset();
  dynamic_discontinuity_ = true;
  if (motion_state_ == MotionAdmission::Enabled)
    motion_state_ = MotionAdmission::Revoked;
  withdrawMotionReference();
}
void TelemetryAdmission::refuseMotion() {
  if (!imu_admission_started_ && motion_state_ == MotionAdmission::Enabled)
    motion_state_ = MotionAdmission::Refused;
  withdrawMotionReference();
}
MotionCurrentSnapshot TelemetryAdmission::motionSnapshot(uint32_t now) const {
  if (!current_.current || motion_state_ != MotionAdmission::Enabled)
    return {};
  const uint64_t elapsed = current_.admitted_at.monotonic_ms + uint32_t(now - admitted_raw_);
  const uint32_t age = now - current_.source.receipt_millis32;
  if (elapsed > SessionClock::kMaxDurationMs || age > motion_options_.snapshot_max_age_ms ||
      age > elapsed)
    return {};
  return current_;
}
bool TelemetryAdmission::admitImu(const ImuEvidence &e, MotionInputRoute route, bool reserve) {
  if (e.kind > RecordKind::Gps && e.kind < RecordKind::Camera) {
    imu_admission_started_ = true;
    if (route != motion_options_.route)
      revokeMotion();
  }
  uint32_t raw;
  const auto timestamp = clock_.snapshotWithRaw(raw);
  MotionEvidence motion;
  motion.state = motion_state_;
  withdrawMotionReference();
  if (motion_state_ == MotionAdmission::Enabled) {
    motion.config = motion_options_.estimator;
    motion.snapshot_max_age_ms = motion_options_.snapshot_max_age_ms;
    if (e.kind == RecordKind::ImuSample) {
      if (source_)
        motion.reference = source_->referenceFor(e);
      motion.estimate = estimator_.update(e, motion.reference);
    }
  }
  const uint32_t age = raw - e.receipt_millis32;
  motion.dynamic_requested = motion_options_.dynamic.enabled;
  motion.dynamic_cadence_us = motion_options_.dynamic_cadence_us;
  if (motion.dynamic_requested)
    motion.dynamic = updateDynamic(e, motion.estimate, motion.dynamic_declaration,
                                   e.receipt_known && age <= motion_options_.snapshot_max_age_ms &&
                                       timestamp.monotonic_quality == MonotonicQuality::Valid &&
                                       age <= timestamp.monotonic_ms);
  const bool accepted = storage_.enqueueImu(timestamp, e, reserve, motion);
  if (!accepted) {
    dynamic_.reset();
    dynamic_discontinuity_ = true;
  }
  if (accepted && motion.estimate.measurements_valid && e.receipt_known && !(e.timing_flags & 7) &&
      timestamp.monotonic_quality == MonotonicQuality::Valid &&
      age <= motion_options_.snapshot_max_age_ms && age <= timestamp.monotonic_ms) {
    current_.current = true;
    current_.estimate = motion.estimate;
    current_.admitted_at = timestamp;
    current_.source.session_id = e.session_id;
    current_.source.generation = e.config.generation;
    current_.source.batch = e.batch_sequence;
    current_.source.frame = e.frame_sequence;
    current_.source.byte_position = e.byte_position;
    current_.source.sensor_epoch = e.sensor_epoch;
    current_.source.receipt_millis32 = e.receipt_millis32;
    current_.source.receipt_known = e.receipt_known;
    current_.source.timing_flags = e.timing_flags;
    admitted_raw_ = raw;
  }
  return accepted;
}
DynamicMotionEstimate TelemetryAdmission::updateDynamic(const ImuEvidence &e,
                                                        const MotionEstimate &converted,
                                                        uint32_t &declaration, bool fresh) {
  if (motion_state_ != MotionAdmission::Enabled) {
    DynamicMotionEstimate refused;
    refused.fault = !motion_options_.dynamic_reference ? DynamicMotionFault::Reference
                                                       : DynamicMotionFault::Configuration;
    dynamic_.reset();
    return refused;
  }
  // FIFO end and read-boundary sensor-time records are metadata, not samples.
  // Preserve the segment across these benign controls without advancing its
  // modelled clock or treating their sensor-time snapshot as acquisition time.
  if (e.kind == RecordKind::ImuControl && (e.event_code == 0 || e.event_code == 5) &&
      !(e.timing_flags & 7) && !dynamic_discontinuity_) {
    DynamicMotionEstimate metadata;
    metadata.fault = DynamicMotionFault::None;
    return metadata;
  }
  DynamicMotionInput input;
  input.session_id = e.session_id;
  input.sensor_id = e.config.sensor_id;
  input.config_generation = e.config.generation;
  input.mount_id = e.config.mount_id;
  input.calibration_id = e.config.calibration_id;
  input.sensor_epoch = e.sensor_epoch;
  input.sequence = e.frame_sequence;
  input.measurements_valid =
      fresh && converted.measurements_valid && motion_state_ == MotionAdmission::Enabled;
  input.specific_force_mps2 = converted.specific_force_mps2;
  input.angular_rate_rad_s = converted.angular_rate_rad_s;
  input.discontinuity =
      dynamic_discontinuity_ || (e.timing_flags & 7) || e.kind != RecordKind::ImuSample;
  dynamic_discontinuity_ = false;
  const auto cadence = motion_options_.dynamic_cadence_us;
  if (cadence && cadence <= motion_options_.dynamic.max_step_us) {
    input.timing_source = DynamicTimingSource::ModelledCadence;
    dynamic_time_us_ += cadence;
  }
  input.sample_time_us = dynamic_time_us_;
  if (e.kind != RecordKind::ImuSample)
    return dynamic_.update(input);
  DynamicMotionReference reference;
  if (motion_options_.dynamic_reference)
    reference = motion_options_.dynamic_reference->referenceFor(e);
  declaration = reference.declaration;
  return declaration ? dynamic_.initialize(input, reference) : dynamic_.update(input);
}
bool TelemetryAdmission::gps(const ModemSnapshot &sample) { return gps(clock_.snapshot(), sample); }
bool TelemetryAdmission::gps(const RecordTimestamp &timestamp, const ModemSnapshot &sample) {
  if (stopping_) {
    storage_.drop(RecordKind::Gps);
    return false;
  }
  return storage_.enqueue(timestamp, sample);
}
bool TelemetryAdmission::event(const ImuEvidence &evidence) {
  if (stopping_) {
    storage_.drop(evidence.kind);
    return false;
  }
  return admitImu(evidence, MotionInputRoute::Direct, evidence.kind == RecordKind::ImuSample);
}
uint8_t TelemetryAdmission::tick() {
  if (stopped_)
    return 0;
  if (camera_) {
    const uint32_t dropped = camera_->dropped_.load(std::memory_order_acquire);
    const uint32_t rejected = camera_->rejected_.load(std::memory_order_acquire);
    storage_.droppedCamera(dropped - camera_dropped_seen_);
    storage_.rejectedCamera(rejected - camera_rejected_seen_);
    camera_dropped_seen_ = dropped;
    camera_rejected_seen_ = rejected;
  }
  uint32_t dropped = 0;
  for (unsigned kind = 0; kind < 5; ++kind)
    dropped += inbox_.dropped_[kind].load(std::memory_order_acquire);
  if (dropped != dynamic_dropped_seen_) {
    dynamic_.reset();
    dynamic_discontinuity_ = true;
    dynamic_dropped_seen_ = dropped;
  }
  uint8_t processed = 0;
  for (; processed < kQuota;) {
    bool handled = false;
    if (camera_ && camera_turn_) {
      const uint32_t r = camera_->read_.load(std::memory_order_relaxed);
      if (r != camera_->write_.load(std::memory_order_acquire)) {
        const auto evidence = camera_->slots_[r % CameraInbox::kCapacity];
        camera_->read_.store(r + 1, std::memory_order_release);
        admitCamera(clock_, storage_, evidence);
        handled = true;
        camera_turn_ = false;
      }
    }
    if (handled) {
      ++processed;
      continue;
    }
    const uint32_t r = inbox_.read_.load(std::memory_order_relaxed);
    if (r != inbox_.write_.load(std::memory_order_acquire)) {
      const auto &batch = inbox_.slots_[r % ImuInbox::kCapacity];
      admitImu(batch.records[index_], MotionInputRoute::Inbox, true);
      if (++index_ == batch.count) {
        index_ = 0;
        inbox_.read_.store(r + 1, std::memory_order_release);
      }
      handled = true;
      camera_turn_ = true;
    }
    if (!handled && camera_) {
      const uint32_t cr = camera_->read_.load(std::memory_order_relaxed);
      if (cr != camera_->write_.load(std::memory_order_acquire)) {
        const auto evidence = camera_->slots_[cr % CameraInbox::kCapacity];
        camera_->read_.store(cr + 1, std::memory_order_release);
        admitCamera(clock_, storage_, evidence);
        handled = true;
        camera_turn_ = false;
      }
    }
    if (!handled)
      break;
    ++processed;
  }
  if (stopping_ && inbox_.finished_.load(std::memory_order_acquire) &&
      inbox_.read_.load(std::memory_order_relaxed) ==
          inbox_.write_.load(std::memory_order_acquire) &&
      (!camera_ || (camera_->finished_.load(std::memory_order_acquire) &&
                    camera_->read_.load(std::memory_order_relaxed) ==
                        camera_->write_.load(std::memory_order_acquire)))) {
    if (camera_) {
      const uint32_t dropped = camera_->dropped_.load(std::memory_order_acquire);
      const uint32_t rejected = camera_->rejected_.load(std::memory_order_acquire);
      storage_.droppedCamera(dropped - camera_dropped_seen_);
      storage_.rejectedCamera(rejected - camera_rejected_seen_);
      camera_dropped_seen_ = dropped;
      camera_rejected_seen_ = rejected;
    }
    storage_.requestStop();
    stopped_ = true;
  }
  return processed;
}
void TelemetryAdmission::requestStop() {
  revokeMotion();
  stopping_ = true;
  inbox_.stop_.store(true, std::memory_order_release);
  if (camera_)
    camera_->stop_.store(true, std::memory_order_release);
}
} // namespace ridesync

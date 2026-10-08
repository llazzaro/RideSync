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
  return storage_.enqueueImu(clock_.snapshot(), evidence, evidence.kind == RecordKind::ImuSample);
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
      storage_.enqueueImu(clock_.snapshot(), batch.records[index_], true);
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
  stopping_ = true;
  inbox_.stop_.store(true, std::memory_order_release);
  if (camera_)
    camera_->stop_.store(true, std::memory_order_release);
}
} // namespace ridesync

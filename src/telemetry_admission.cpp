#include "telemetry_admission.h"
namespace ridesync {
namespace {
void increment(std::atomic<uint32_t> &counter, uint32_t amount = 1) {
  const uint32_t old = counter.load(std::memory_order_relaxed);
  counter.store(amount > UINT32_MAX - old ? UINT32_MAX : old + amount, std::memory_order_relaxed);
}
} // namespace
constexpr uint32_t ImuInbox::kCapacity;
constexpr uint8_t ImuBatch::kRecords, TelemetryAdmission::kQuota;
bool ImuInbox::publish(const ImuBatch &batch) {
  if (!batch.count || batch.count > ImuBatch::kRecords) {
    increment(rejected_);
    return false;
  }
  for (uint8_t i = 0; i < batch.count; ++i) {
    const auto kind = batch.records[i].kind;
    if (kind == RecordKind::Gps || kind >= RecordKind::Count) {
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
  return kind < RecordKind::Count ? dropped_[static_cast<unsigned>(kind)].load() : 0;
}
uint32_t ImuInbox::rejected() const { return rejected_.load(); }
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
  uint8_t processed = 0;
  for (; processed < kQuota; ++processed) {
    const uint32_t r = inbox_.read_.load(std::memory_order_relaxed);
    if (r == inbox_.write_.load(std::memory_order_acquire))
      break;
    const auto &batch = inbox_.slots_[r % ImuInbox::kCapacity];
    storage_.enqueueImu(clock_.snapshot(), batch.records[index_], true);
    if (++index_ == batch.count) {
      index_ = 0;
      inbox_.read_.store(r + 1, std::memory_order_release);
    }
  }
  if (stopping_ && inbox_.finished_.load(std::memory_order_acquire) &&
      inbox_.read_.load(std::memory_order_relaxed) ==
          inbox_.write_.load(std::memory_order_acquire)) {
    storage_.requestStop();
    stopped_ = true;
  }
  return processed;
}
void TelemetryAdmission::requestStop() {
  stopping_ = true;
  inbox_.stop_.store(true, std::memory_order_release);
}
} // namespace ridesync

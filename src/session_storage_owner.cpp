#include "session_storage_owner.h"
namespace ridesync {
IdentityAllocation SessionStorageOwner::allocation() const {
  return published_.load(std::memory_order_acquire) ? allocation_ : IdentityAllocation{};
}
bool SessionStorageOwner::bind(Storage &storage) {
  const auto a = allocation();
  if (control_bound_ || control_cancelled_ || a.status != IdentityStatus::Committed || !a.id ||
      !storage.configValid() || storage.sessionId() != a.id || !storage.usesSink(sink_) ||
      storage.health().terminal || storage.health().stopped)
    return false;
  control_bound_ = true;
  bound_.store(&storage, std::memory_order_release);
  return true;
}
void SessionStorageOwner::cancel() {
  control_cancelled_ = true;
  cancelled_.store(true, std::memory_order_release);
}
bool SessionStorageOwner::workerStep() {
  if (phase_ == Phase::Allocate) {
    if (cancelled_.load(std::memory_order_acquire)) {
      allocation_.status = IdentityStatus::IdentityUnavailable;
    } else if (!sink_.mount()) {
      allocation_.status = IdentityStatus::MediaError;
      allocation_.error = sink_.ioError();
    } else {
      allocation_ = allocator_.reserve();
    }
    published_.store(true, std::memory_order_release);
    phase_ = allocation_.status == IdentityStatus::Committed ? Phase::WaitBind : Phase::Done;
  }
  if (phase_ == Phase::WaitBind) {
    // Acquire the pointer before cancellation: control publishes bind before
    // cancel, and cancellation then drains the bound Storage rather than leaving
    // a published consumer with an unserviced queue.
    storage_ = bound_.load(std::memory_order_acquire);
    if (storage_)
      phase_ = Phase::Storage;
    else if (cancelled_.load(std::memory_order_acquire)) {
      // Recheck after acquire of cancel, which orders any preceding bind store.
      storage_ = bound_.load(std::memory_order_acquire);
      phase_ = storage_ ? Phase::Storage : Phase::Done;
    }
  }
  if (phase_ == Phase::Storage) {
    if (cancelled_.load(std::memory_order_acquire))
      storage_->requestStop();
    storage_->workerStep();
    if (!storage_->health().stopped)
      return false;
    // Storage's Close phase already released the mounted sink.
    phase_ = Phase::Done;
    finished_.store(true, std::memory_order_release);
    return true;
  }
  if (phase_ == Phase::Done) {
    sink_.close(); // Also release failed/partial mounts and unbound reservations.
    finished_.store(true, std::memory_order_release);
    return true;
  }
  return false;
}
} // namespace ridesync

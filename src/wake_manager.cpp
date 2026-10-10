#include "wake_manager.h"
#include <algorithm>
namespace ridesync {
bool sameWakeOperation(const WakeOperation &a, const WakeOperation &b) {
  return a.peer == b.peer && a.id == b.id && a.generation == b.generation;
}
namespace {
bool intentTerminal(WakePhase phase) {
  return phase == WakePhase::Ready || phase == WakePhase::Unsupported ||
         phase == WakePhase::Failed || phase == WakePhase::Cancelled || phase == WakePhase::Timeout;
}
bool validPolicy(const WakePolicy &p) {
  return p.total_ms && p.total_ms < 0x80000000u && p.slice_ms && p.slice_ms <= p.total_ms;
}
} // namespace
WakeManager::WakeManager(WakeRadio &radio, WakeRecovery *recovery)
    : radio_(radio), recovery_(recovery) {}
WakeManager::~WakeManager() {
  for (size_t peer = 0; peer < kWakePeers; ++peer)
    cancel(peer);
}
CameraError WakeManager::request(const WakeOperation &op, const WakePeerConfig &config,
                                 const WakePolicy &policy, uint32_t now) {
  if (op.peer >= kWakePeers)
    return CameraError::InvalidPeer;
  auto &slot = slots_[op.peer];
  if (slot.occupied && (!intentTerminal(slot.status.phase) || !slot.status.released))
    return CameraError::Busy;
  if (!op.id || op.id == UINT32_MAX || op.id <= last_ids_[op.peer] || !op.generation ||
      op.generation == UINT32_MAX || (generation_ && generation_ != op.generation) ||
      !validPolicy(policy))
    return CameraError::InvalidPolicy;
  if (!config.enabled)
    return CameraError::Disabled;
  if (!config.source_qualified)
    return CameraError::Unsupported;
  const auto encoding = insta360::encodeWake(config.profile, config.identifier.data(), 6);
  if (encoding.error != insta360::WakeCodecError::None)
    return encoding.error == insta360::WakeCodecError::InvalidIdentifier
               ? CameraError::InvalidPolicy
               : CameraError::Unsupported;
  slot = {};
  slot.occupied = true;
  slot.encoding = encoding;
  slot.policy = policy;
  slot.started = now;
  slot.status.operation = op;
  slot.status.phase = WakePhase::Queued;
  slot.status.error = CameraError::None;
  last_ids_[op.peer] = op.id;
  generation_ = op.generation;
  const auto observed = recovery_ ? recovery_->currentObserved(op.peer) : RecordingState::Unknown;
  if (observed == RecordingState::Stopped || observed == RecordingState::Recording) {
    slot.status.phase = WakePhase::Ready;
    slot.status.observed = observed;
  }
  return CameraError::None;
}
void WakeManager::updateReleaseOnly(Slot &slot) {
  slot.status.released = !slot.radio && !slot.recovery;
}
void WakeManager::seal(Slot &slot, WakePhase phase, CameraError error) {
  if (!slot.occupied || slot.sealed)
    return;
  slot.sealed = true;
  slot.status.phase = phase;
  slot.status.error = error;
  slot.status.observed = RecordingState::Unknown;
  if (slot.radio && !slot.radio_cancelled) {
    slot.radio_cancelled = true;
    radio_.cancel(slot.status.operation);
  }
  if (slot.recovery && !slot.recovery_cancelled) {
    slot.recovery_cancelled = true;
    recovery_->cancel(slot.status.operation);
  }
  updateReleaseOnly(slot);
}
void WakeManager::cancel(uint8_t peer) {
  if (peer < kWakePeers)
    seal(slots_[peer], WakePhase::Cancelled, CameraError::Cancelled);
}
void WakeManager::invalidate(uint32_t generation) {
  // Invalid/non-increasing generation permanently closes this owner. Reusing
  // a generation cannot make a delayed publication current again.
  for (auto &slot : slots_)
    seal(slot, WakePhase::Cancelled, CameraError::Cancelled);
  generation_ =
      generation && generation < UINT32_MAX && generation > generation_ ? generation : UINT32_MAX;
}
void WakeManager::recover(Slot &slot) {
  if (!recovery_) {
    seal(slot, WakePhase::Unsupported, CameraError::Unsupported);
    return;
  }
  const auto error = recovery_->begin(slot.status.operation, slot.started + slot.policy.total_ms);
  if (error != CameraError::None) {
    seal(slot, error == CameraError::Unsupported ? WakePhase::Unsupported : WakePhase::Failed,
         error);
    return;
  }
  slot.recovery = true;
  slot.status.phase = WakePhase::Recovering;
  updateReleaseOnly(slot);
}
void WakeManager::service(uint32_t now) {
  for (auto &slot : slots_)
    if (slot.occupied && !intentTerminal(slot.status.phase) &&
        uint32_t(now - slot.started) >= slot.policy.total_ms)
      seal(slot, WakePhase::Timeout, CameraError::Timeout);
  if (owner_ < kWakePeers) {
    auto &slot = slots_[owner_];
    const auto result = radio_.poll(slot.status.operation, now);
    if (sameWakeOperation(result.operation, slot.status.operation)) {
      if (!slot.sealed && result.sdk_error)
        seal(slot, WakePhase::Failed, CameraError::Transport);
      if (result.terminal) {
        if (!slot.sealed)
          slot.status.phase = WakePhase::Releasing;
        if (result.released) {
          slot.radio = false;
          owner_ = kWakePeers;
          updateReleaseOnly(slot);
          if (!slot.sealed) {
            if (result.submitted)
              recover(slot);
            else
              seal(slot, WakePhase::Failed, CameraError::Transport);
          }
        }
      }
    }
  }
  for (auto &slot : slots_) {
    if (!slot.recovery)
      continue;
    const auto result = recovery_->poll(slot.status.operation, now);
    if (!sameWakeOperation(result.operation, slot.status.operation) || !result.terminal)
      continue;
    if (result.released)
      slot.recovery = false;
    updateReleaseOnly(slot);
    if (slot.sealed)
      continue;
    if (result.error != CameraError::None)
      seal(slot,
           result.error == CameraError::Unsupported ? WakePhase::Unsupported : WakePhase::Failed,
           result.error);
    else if (!result.fresh || (result.observed != RecordingState::Stopped &&
                               result.observed != RecordingState::Recording))
      seal(slot, WakePhase::Failed, CameraError::NotConnected);
    else if (result.released) {
      slot.status.phase = WakePhase::Ready;
      slot.status.observed = result.observed;
    }
  }
  if (owner_ != kWakePeers)
    return;
  for (size_t offset = 0; offset < kWakePeers; ++offset) {
    const size_t peer = (cursor_ + offset) % kWakePeers;
    auto &slot = slots_[peer];
    if (!slot.occupied || slot.sealed || slot.status.phase != WakePhase::Queued)
      continue;
    cursor_ = (peer + 1) % kWakePeers;
    const uint32_t remaining = slot.policy.total_ms - uint32_t(now - slot.started);
    const auto result = radio_.begin(slot.status.operation, slot.encoding,
                                     now + std::min(remaining, slot.policy.slice_ms), now);
    if (result == WakeSubmit::Accepted) {
      slot.radio = true;
      owner_ = peer;
      slot.status.phase = WakePhase::Advertising;
      updateReleaseOnly(slot);
    } else if (result != WakeSubmit::Busy) {
      seal(slot, result == WakeSubmit::Unsupported ? WakePhase::Unsupported : WakePhase::Failed,
           result == WakeSubmit::Unsupported ? CameraError::Unsupported : CameraError::Transport);
    }
    break;
  }
}
WakeStatus WakeManager::status(uint8_t peer) const {
  if (peer < kWakePeers)
    return slots_[peer].status;
  WakeStatus result;
  result.error = CameraError::InvalidPeer;
  return result;
}
} // namespace ridesync

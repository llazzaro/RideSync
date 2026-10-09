#include "wake_preparation.h"
namespace ridesync {
WakePreparation::WakePreparation(WakeManager &manager, Clock &clock,
                                 const std::array<WakePeerConfig, kWakePeers> &configs,
                                 uint32_t generation, WakePolicy policy)
    : manager_(manager), clock_(clock), configs_(configs), generation_(generation),
      policy_(policy) {}
bool WakePreparation::matches(size_t peer) const {
  return peer < kWakePeers && peers_[peer].used &&
         sameWakeOperation(peers_[peer].operation, manager_.status(peer).operation);
}
CameraError WakePreparation::prepare(size_t peer) {
  if (peer >= kWakePeers)
    return CameraError::InvalidPeer;
  auto &slot = peers_[peer];
  if (slot.used && (!matches(peer) || !manager_.status(peer).released ||
                    (manager_.status(peer).phase != WakePhase::Ready && !slot.sealed &&
                     manager_.status(peer).error == CameraError::None)))
    return CameraError::Busy;
  if (!generation_ || generation_ == UINT32_MAX || slot.last_id >= UINT32_MAX - 1)
    return CameraError::InvalidPolicy;
  WakeOperation operation;
  operation.peer = peer;
  operation.generation = generation_;
  operation.id = ++slot.last_id;
  const auto error = manager_.request(operation, configs_[peer], policy_, clock_.now());
  if (error == CameraError::Busy)
    return error;
  slot.operation = operation;
  slot.immediate = error;
  slot.used = error == CameraError::None;
  slot.sealed = slot.qualified_link = false;
  return error;
}
RecordingPreparationResult WakePreparation::prepared(size_t peer) {
  RecordingPreparationResult result;
  if (peer >= kWakePeers) {
    result.error = CameraError::InvalidPeer;
    return result;
  }
  const auto &slot = peers_[peer];
  if (!slot.used) {
    result.error = slot.immediate;
    return result;
  }
  if (slot.sealed || !matches(peer)) {
    result.error = CameraError::Cancelled;
    return result;
  }
  const auto status = manager_.status(peer);
  result.error = status.error;
  result.pending = status.phase == WakePhase::Queued || status.phase == WakePhase::Advertising ||
                   status.phase == WakePhase::Releasing || status.phase == WakePhase::Recovering;
  if (!result.pending && result.error == CameraError::None && !commandReady(peer))
    result.error = CameraError::NotConnected;
  return result;
}
void WakePreparation::retire(size_t peer) {
  if (peer >= kWakePeers || !peers_[peer].used || peers_[peer].sealed)
    return;
  auto &slot = peers_[peer];
  // STOP may use an already-qualified live control link. A pending or stale
  // completion cannot acquire this permission after retirement. New prepare
  // clears it before any replacement Start; invalidate always revokes it.
  slot.qualified_link = commandReady(peer);
  slot.sealed = true;
  manager_.cancel(peer);
}
bool WakePreparation::commandReady(size_t peer) const {
  if (!matches(peer))
    return false;
  const auto &slot = peers_[peer];
  const auto status = manager_.status(peer);
  return status.released &&
         (slot.sealed ? slot.qualified_link
                      : status.phase == WakePhase::Ready && status.error == CameraError::None &&
                            status.observed != RecordingState::Unknown);
}
bool WakePreparation::released(size_t peer) const {
  return peer < kWakePeers &&
         (!peers_[peer].used || (matches(peer) && manager_.status(peer).released));
}
bool WakePreparation::retiring(size_t peer) const {
  return peer < kWakePeers && peers_[peer].used && !released(peer) &&
         (peers_[peer].sealed || !matches(peer) ||
          manager_.status(peer).error != CameraError::None);
}
void WakePreparation::invalidate(uint32_t generation) {
  for (size_t peer = 0; peer < kWakePeers; ++peer) {
    retire(peer);
    peers_[peer].qualified_link = false;
  }
  manager_.invalidate(generation);
  generation_ =
      generation && generation < UINT32_MAX && generation > generation_ ? generation : UINT32_MAX;
}
void WakePreparation::service() { manager_.service(clock_.now()); }
} // namespace ridesync

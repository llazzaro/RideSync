#include "camera_event_logger.h"
namespace ridesync {
bool CameraEventLogger::configurePeer(size_t peer, uint32_t id, CameraModel model) {
  if (sealed_ || peer >= identities_.size() || !id || model == CameraModel::Unknown)
    return false;
  for (size_t i = 0; i < identities_.size(); ++i)
    if (i != peer && identities_[i].id == id)
      return false;
  identities_[peer].id = id;
  identities_[peer].model = model;
  return true;
}
bool CameraEventLogger::readyFor(const CameraManager &manager) const {
  if (!session_id_ || !manager.size())
    return false;
  for (size_t i = 0; i < manager.size(); ++i) {
    const auto *camera = manager.configuredCamera(i);
    if (camera->enabled && (!identities_[i].id || identities_[i].model != camera->model))
      return false;
  }
  return true;
}
CameraEvidence CameraEventLogger::base(size_t peer, Operation operation, uint32_t intent_id) {
  CameraEvidence e;
  e.session_id = session_id_;
  e.peer_slot = static_cast<uint8_t>(peer);
  if (peer < identities_.size()) {
    e.peer_id = identities_[peer].id;
    e.model = identities_[peer].model;
  }
  e.group_generation = group_.status().generation;
  e.intent_id = intent_id;
  e.operation = operation;
  return e;
}
void CameraEventLogger::publish(const CameraEvidence &e) {
  sealed_ = true;
  // Logging failure is evidence in inbox counters; it never changes control.
  CameraEvidence copied = e;
  copied.event_receipt_known = true;
  copied.event_receipt_raw32 = raw_.now();
  inbox_.publish(copied);
}
void CameraEventLogger::request(size_t peer, Operation op, CameraError error, bool queued,
                                uint32_t intent_id) {
  auto e = base(peer, op, intent_id);
  e.kind = error != CameraError::None ? CameraEventKind::RequestRefused
           : queued                   ? CameraEventKind::RequestQueued
                                      : CameraEventKind::RequestAccepted;
  e.error = error;
  publish(e);
}
void CameraEventLogger::attempt(size_t peer, Operation op, Token token, uint32_t intent_id,
                                bool delivered) {
  auto e = base(peer, op, intent_id);
  e.kind = CameraEventKind::Attempt;
  e.connection_generation = token.connection;
  e.operation_generation = token.operation;
  e.delivery_admitted = delivered;
  e.error = delivered ? CameraError::None : CameraError::Transport;
  publish(e);
}
void CameraEventLogger::accepted(const Event &event, Operation op, uint32_t intent_id) {
  auto e = base(event.peer, op, intent_id);
  e.connection_generation = event.token.connection;
  if (event.kind != EventKind::RecordingObserved && event.kind != EventKind::Disconnected)
    e.operation_generation = event.token.operation;
  switch (event.kind) {
  case EventKind::RecordingObserved:
  case EventKind::CommandRecordingObserved:
    e.kind = CameraEventKind::RecordingObserved;
    e.recording = event.recording;
    if (event.kind == EventKind::RecordingObserved)
      e.intent_id = 0;
    break;
  case EventKind::Completed:
    e.kind = CameraEventKind::ManagerCompleted;
    break;
  case EventKind::Disconnected:
    e.kind = CameraEventKind::Disconnected;
    e.error = CameraError::Transport;
    e.intent_id = 0;
    break;
  case EventKind::Failed:
    e.kind = CameraEventKind::Failed;
    e.error = CameraError::Transport;
    break;
  }
  publish(e);
}
void CameraEventLogger::cancelled(size_t peer, Operation op, Token token, uint32_t intent_id) {
  auto e = base(peer, op, intent_id);
  e.kind = CameraEventKind::Cancelled;
  e.connection_generation = token.connection;
  e.operation_generation = token.operation;
  e.error = CameraError::Cancelled;
  publish(e);
}
void CameraEventLogger::failure(size_t peer, Operation op, Token token, uint32_t intent_id,
                                CameraError error) {
  auto e = base(peer, op, intent_id);
  e.kind = CameraEventKind::Failed;
  e.connection_generation = token.connection;
  e.operation_generation = token.operation;
  e.error = error;
  publish(e);
}
void CameraEventLogger::wireAck(size_t peer, Operation op, Token token, uint32_t intent_id,
                                CameraAckDomain domain, CameraAckAction action) {
  auto e = base(peer, op, intent_id);
  e.kind = CameraEventKind::WireAck;
  e.connection_generation = token.connection;
  e.operation_generation = token.operation;
  e.ack_domain = domain;
  e.ack_action = action;
  publish(e);
}
} // namespace ridesync

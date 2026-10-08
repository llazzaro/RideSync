#include "camera_manager.h"
#include <climits>
#include <utility>
namespace ridesync {
namespace {
constexpr uint32_t kCompletedObservationMs = 1000;
// Valid when tick() runs at least once per 2^31 milliseconds.
bool reached(uint32_t now, uint32_t deadline) { return now - deadline < 0x80000000UL; }
bool connected(Lifecycle l) {
  return l == Lifecycle::Ready || l == Lifecycle::Operating || l == Lifecycle::Backoff;
}
CapabilityState capability(const Capabilities &c, Operation op) {
  switch (op) {
  case Operation::Start:
    return c.start;
  case Operation::Stop:
    return c.stop;
  case Operation::Query:
    return c.query;
  case Operation::Wake:
    return c.wake;
  default:
    return CapabilityState::Unknown;
  }
}
} // namespace
CameraManager::CameraManager(Clock &c, CameraTransport &t, RetryPolicy p)
    : clock_(c), transport_(t), policy_(p) {}
bool CameraManager::validPolicy() const {
  return policy_.timeout_ms > 0 && policy_.timeout_ms <= INT32_MAX && policy_.backoff_ms > 0 &&
         policy_.backoff_ms <= INT32_MAX && policy_.max_attempts > 0 && policy_.max_attempts <= 5;
}
ConfigResult CameraManager::configure(const SourceConfig &c) {
  const auto result = validate(c);
  if (!result.ok())
    return result;
  // Unused source slots are outside validation and may contain arbitrary storage.
  // Prepare only validated entries before retiring the current registry.
  SourceConfig sanitized;
  sanitized.count = c.count;
  sanitized.capacity = c.capacity;
  for (size_t i = 0; i < c.count; ++i)
    sanitized.cameras[i] = c.cameras[i];
  reset();
  config_ = std::move(sanitized);
  for (size_t i = 0; i < config_.count; ++i)
    peers_[i].state.lifecycle = config_.cameras[i].enabled ? Lifecycle::Idle : Lifecycle::Disabled;
  return result;
}
CameraError CameraManager::request(size_t i, Operation op) {
  if (i >= size())
    return CameraError::InvalidPeer;
  auto &p = peers_[i];
  CameraError error = CameraError::None;
  if (!validPolicy())
    error = CameraError::InvalidPolicy;
  else if (!config_.cameras[i].enabled)
    error = CameraError::Disabled;
  else if (sealed(i))
    error = CameraError::Cancelled;
  if (error != CameraError::None) {
    if (audit_)
      audit_->request(i, op, error, false, 0);
    return error;
  }
  if (op == Operation::Connect) {
    if (p.active || connected(p.state.lifecycle))
      error = CameraError::Busy;
  } else {
    if (!connected(p.state.lifecycle) || (p.active && p.current == Operation::Connect))
      error = CameraError::NotConnected;
    else if (capability(p.state.capabilities, op) != CapabilityState::Supported)
      error = CameraError::Unsupported;
  }
  if (error == CameraError::None && p.active && p.queued == kQueueDepth)
    error = CameraError::QueueFull;
  if (error == CameraError::None && !p.state.token.hasRoom())
    error = CameraError::Busy;
  // Intent identifiers never wrap into an earlier request in this manager
  // lifetime; require an explicit new manager/session before exhaustion.
  if (error == CameraError::None && p.next_intent_id == UINT32_MAX)
    error = CameraError::Busy;
  if (error != CameraError::None) {
    if (audit_)
      audit_->request(i, op, error, false, 0);
    return error;
  }
  const uint32_t intent_id = ++p.next_intent_id;
  const bool queued = p.active;
  if (audit_)
    audit_->request(i, op, CameraError::None, queued, intent_id);
  if (op == Operation::Start)
    p.state.desired = RecordingState::Recording;
  if (op == Operation::Stop)
    p.state.desired = RecordingState::Stopped;
  if (p.active)
    p.queue[p.queued++] = {op, intent_id};
  else
    start(i, op, intent_id);
  return CameraError::None;
}
void CameraManager::start(size_t i, Operation op, uint32_t intent_id) {
  auto &p = peers_[i];
  p.current = op;
  p.active_intent_id = intent_id;
  p.active = true;
  p.state.attempts = 0;
  p.state.error = CameraError::None;
  attempt(i);
}
void CameraManager::attempt(size_t i) {
  auto &p = peers_[i];
  ++p.state.attempts;
  p.ack_mask = 0;
  p.completed_observation_open = false;
  Token::advance(p.state.token.operation);
  if (p.current == Operation::Connect) {
    Token::advance(p.state.token.connection);
    p.state.observed = RecordingState::Unknown;
    p.state.has_observation = false;
    p.state.capabilities = Capabilities{};
  }
  p.state.lifecycle =
      p.current == Operation::Connect ? Lifecycle::Connecting : Lifecycle::Operating;
  p.state.deadline_ms = clock_.now() + policy_.timeout_ms;
  const bool delivered = transport_.begin(i, config_.cameras[i], p.current, p.state.token);
  if (audit_)
    audit_->attempt(i, p.current, p.state.token, p.active_intent_id, delivered);
  if (!delivered)
    fail(i, CameraError::Transport);
}
void CameraManager::fail(size_t i, CameraError error) {
  auto &p = peers_[i];
  p.completed_observation_open = false;
  if (audit_)
    audit_->failure(i, p.current, p.state.token, p.active_intent_id, error);
  transport_.cancel(i, p.state.token);
  if (p.current == Operation::Connect || p.state.attempts >= policy_.max_attempts)
    transport_.close(i, p.state.token);
  Token::advance(p.state.token.operation); // Late response is invalid even during backoff.
  p.state.error = error;
  p.state.observed = RecordingState::Unknown;
  p.state.has_observation = false;
  if (p.state.attempts < policy_.max_attempts) {
    p.state.lifecycle = Lifecycle::Backoff;
    p.state.deadline_ms = clock_.now() + policy_.backoff_ms;
  } else {
    p.active = false;
    p.queued = 0;
    p.state.lifecycle = Lifecycle::Failed;
    p.state.capabilities = Capabilities{};
  }
}
void CameraManager::next(size_t i) {
  auto &p = peers_[i];
  if (p.queued == 0)
    return;
  const auto queued = p.queue[0];
  for (size_t j = 1; j < p.queued; ++j)
    p.queue[j - 1] = p.queue[j];
  --p.queued;
  start(i, queued.operation, queued.intent_id);
}
CameraError CameraManager::cancel(size_t i) {
  if (i >= size())
    return CameraError::InvalidPeer;
  auto &p = peers_[i];
  if (!config_.cameras[i].enabled)
    return CameraError::Disabled;
  if (p.active)
    transport_.cancel(i, p.state.token);
  if (audit_ && p.active)
    audit_->cancelled(i, p.current, p.state.token, p.active_intent_id);
  const bool link = p.state.lifecycle == Lifecycle::Ready ||
                    (connected(p.state.lifecycle) && p.current != Operation::Connect);
  if (p.active && !link)
    transport_.close(i, p.state.token);
  Token::advance(p.state.token.operation);
  p.active = false;
  p.completed_observation_open = false;
  p.queued = 0;
  p.state.error = CameraError::Cancelled;
  p.state.observed = RecordingState::Unknown;
  p.state.has_observation = false;
  p.state.lifecycle = link ? Lifecycle::Ready : Lifecycle::Idle;
  return CameraError::None;
}
CameraError CameraManager::seal(size_t i) {
  if (i >= size())
    return CameraError::InvalidPeer;
  if (!config_.cameras[i].enabled)
    return CameraError::Disabled;
  if (sealed(i))
    return CameraError::None;
  maintenance_[i] = true; // Before cancel/close: no old callback can admit work.
  const auto token = peers_[i].state.token;
  cancel(i);
  transport_.close(i, token);
  auto &state = peers_[i].state;
  Token::advance(state.token.connection);
  state.lifecycle = Lifecycle::Idle;
  state.desired = state.observed = RecordingState::Unknown;
  state.has_observation = false;
  state.capabilities = Capabilities{};
  return CameraError::None;
}
void CameraManager::reset() {
  Token::advance(resets_);
  for (size_t i = 0; i < kMaxCameras; ++i) {
    auto &p = peers_[i];
    if (audit_ && p.active)
      audit_->cancelled(i, p.current, p.state.token, p.active_intent_id);
    if (p.active)
      transport_.cancel(i, p.state.token);
    if (p.active || connected(p.state.lifecycle))
      transport_.close(i, p.state.token);
    Token t = p.state.token;
    const uint32_t next_intent_id = p.next_intent_id;
    Token::advance(t.connection);
    Token::advance(t.operation);
    p = Peer{};
    p.state.token = t;
    p.next_intent_id = next_intent_id;
    if (i < size() && !config_.cameras[i].enabled)
      p.state.lifecycle = Lifecycle::Disabled;
  }
}
void CameraManager::tick() {
  ++ticks_;
  for (size_t i = 0; i < size(); ++i) {
    auto &p = peers_[i];
    if (!p.active || !reached(clock_.now(), p.state.deadline_ms))
      continue;
    if (p.state.lifecycle == Lifecycle::Backoff)
      attempt(i);
    else
      fail(i, CameraError::Timeout);
  }
}
bool CameraManager::event(const Event &e) {
  if (e.peer >= size() || sealed(e.peer))
    return false;
  auto &p = peers_[e.peer];
  if (!p.state.token.valid())
    return false;
  const bool connectionEvent =
      e.kind == EventKind::Disconnected || e.kind == EventKind::RecordingObserved;
  if (e.token.connection != p.state.token.connection ||
      (!connectionEvent && e.token.operation != p.state.token.operation))
    return false;
  const auto l = p.state.lifecycle;
  const bool backoffConnectionEvent =
      l == Lifecycle::Backoff && connectionEvent && p.current != Operation::Connect;
  if (l != Lifecycle::Connecting && l != Lifecycle::Ready && l != Lifecycle::Operating &&
      !backoffConnectionEvent)
    return false;
  if (!connectionEvent && p.active && reached(clock_.now(), p.state.deadline_ms)) {
    fail(e.peer, CameraError::Timeout);
    return false;
  }
  if (e.kind == EventKind::CommandRecordingObserved && !p.active &&
      (!p.completed_observation_open || reached(clock_.now(), p.completed_observation_deadline_ms)))
    return false;
  // A command observation may legitimately arrive during the group's Confirm
  // phase after Completed retired the active operation. Its unchanged token
  // still belongs to that completed intent until a new attempt or cancellation.
  const uint32_t intent_id =
      !connectionEvent && (p.active || e.kind == EventKind::CommandRecordingObserved)
          ? p.active_intent_id
          : 0;
  const Operation operation = p.current;
  if (e.kind == EventKind::Completed) {
    if (!p.active)
      return false;
    if (p.current == Operation::Connect)
      p.state.capabilities = e.capabilities;
    p.completed_observation_open = p.current != Operation::Connect;
    p.completed_observation_deadline_ms = clock_.now() + kCompletedObservationMs;
    p.active = false;
    p.state.lifecycle = Lifecycle::Ready;
    p.state.error = CameraError::None;
    next(e.peer);
  } else if (e.kind == EventKind::RecordingObserved ||
             e.kind == EventKind::CommandRecordingObserved) {
    if (l == Lifecycle::Connecting)
      return false;
    p.state.observed = e.recording;
    p.state.last_observed_ms = clock_.now();
    p.state.has_observation = true;
  } else if (e.kind == EventKind::Disconnected) {
    if (p.active)
      transport_.cancel(e.peer, p.state.token);
    Token::advance(p.state.token.connection);
    Token::advance(p.state.token.operation);
    p.active = false;
    p.completed_observation_open = false;
    p.queued = 0;
    p.state.lifecycle = Lifecycle::Idle;
    p.state.error = CameraError::Transport;
    p.state.observed = RecordingState::Unknown;
    p.state.has_observation = false;
    p.state.capabilities = Capabilities{};
  } else if (e.kind == EventKind::Failed) {
    if (!p.active)
      return false;
    fail(e.peer, CameraError::Transport);
  } else
    return false;
  p.state.last_seen_ms = clock_.now();
  p.state.has_last_seen = true;
  if (audit_ && e.kind != EventKind::Failed)
    audit_->accepted(e, operation, intent_id);
  return true;
}
bool CameraManager::wireAck(size_t i, Token token, CameraAckDomain domain, CameraAckAction action) {
  if (i >= size() || domain == CameraAckDomain::None ||
      static_cast<unsigned>(domain) > static_cast<unsigned>(CameraAckDomain::Protobuf) ||
      action == CameraAckAction::None ||
      static_cast<unsigned>(action) > static_cast<unsigned>(CameraAckAction::QueryEncoding))
    return false;
  auto &p = peers_[i];
  if (!p.active || !p.state.token.valid() || p.state.token.connection != token.connection ||
      p.state.token.operation != token.operation ||
      (p.state.lifecycle != Lifecycle::Connecting && p.state.lifecycle != Lifecycle::Operating) ||
      reached(clock_.now(), p.state.deadline_ms))
    return false;
  const uint32_t bit = uint32_t(1) << static_cast<unsigned>(action);
  if (p.ack_mask & bit)
    return false;
  p.ack_mask |= bit;
  if (audit_)
    audit_->wireAck(i, p.current, token, p.active_intent_id, domain, action);
  return true;
}
const CameraState *CameraManager::state(size_t p) const {
  return p < size() ? &peers_[p].state : nullptr;
}
} // namespace ridesync

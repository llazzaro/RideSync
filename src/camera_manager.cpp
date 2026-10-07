#include "camera_manager.h"
#include <climits>
namespace ridesync {
namespace {
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
  reset();
  config_ = c;
  for (size_t i = 0; i < config_.count; ++i)
    peers_[i].state.lifecycle = config_.cameras[i].enabled ? Lifecycle::Idle : Lifecycle::Disabled;
  return result;
}
CameraError CameraManager::request(size_t i, Operation op) {
  if (i >= size())
    return CameraError::InvalidPeer;
  if (!validPolicy())
    return CameraError::InvalidPolicy;
  auto &p = peers_[i];
  if (!config_.cameras[i].enabled)
    return CameraError::Disabled;
  if (op == Operation::Connect) {
    if (p.active || connected(p.state.lifecycle))
      return CameraError::Busy;
  } else {
    if (!connected(p.state.lifecycle) || (p.active && p.current == Operation::Connect))
      return CameraError::NotConnected;
    if (capability(p.state.capabilities, op) != CapabilityState::Supported)
      return CameraError::Unsupported;
  }
  if (p.active && p.queued == kQueueDepth)
    return CameraError::QueueFull;
  if (op == Operation::Start)
    p.state.desired = RecordingState::Recording;
  if (op == Operation::Stop)
    p.state.desired = RecordingState::Stopped;
  if (p.active)
    p.queue[p.queued++] = op;
  else
    start(i, op);
  return CameraError::None;
}
void CameraManager::start(size_t i, Operation op) {
  auto &p = peers_[i];
  p.current = op;
  p.active = true;
  p.state.attempts = 0;
  p.state.error = CameraError::None;
  attempt(i);
}
void CameraManager::attempt(size_t i) {
  auto &p = peers_[i];
  ++p.state.attempts;
  ++p.state.token.operation;
  if (p.current == Operation::Connect) {
    ++p.state.token.connection;
    p.state.observed = RecordingState::Unknown;
    p.state.has_observation = false;
    p.state.capabilities = Capabilities{};
  }
  p.state.lifecycle =
      p.current == Operation::Connect ? Lifecycle::Connecting : Lifecycle::Operating;
  p.state.deadline_ms = clock_.now() + policy_.timeout_ms;
  if (!transport_.begin(i, config_.cameras[i], p.current, p.state.token))
    fail(i, CameraError::Transport);
}
void CameraManager::fail(size_t i, CameraError error) {
  auto &p = peers_[i];
  transport_.cancel(i, p.state.token);
  if (p.current == Operation::Connect || p.state.attempts >= policy_.max_attempts)
    transport_.close(i, p.state.token);
  ++p.state.token.operation; // Late response is invalid even during backoff.
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
  const auto op = p.queue[0];
  for (size_t j = 1; j < p.queued; ++j)
    p.queue[j - 1] = p.queue[j];
  --p.queued;
  start(i, op);
}
CameraError CameraManager::cancel(size_t i) {
  if (i >= size())
    return CameraError::InvalidPeer;
  auto &p = peers_[i];
  if (!config_.cameras[i].enabled)
    return CameraError::Disabled;
  if (p.active)
    transport_.cancel(i, p.state.token);
  const bool link = p.state.lifecycle == Lifecycle::Ready ||
                    (connected(p.state.lifecycle) && p.current != Operation::Connect);
  if (p.active && !link)
    transport_.close(i, p.state.token);
  ++p.state.token.operation;
  p.active = false;
  p.queued = 0;
  p.state.error = CameraError::Cancelled;
  p.state.observed = RecordingState::Unknown;
  p.state.has_observation = false;
  p.state.lifecycle = link ? Lifecycle::Ready : Lifecycle::Idle;
  return CameraError::None;
}
void CameraManager::reset() {
  for (size_t i = 0; i < kMaxCameras; ++i) {
    auto &p = peers_[i];
    if (p.active)
      transport_.cancel(i, p.state.token);
    if (p.active || connected(p.state.lifecycle))
      transport_.close(i, p.state.token);
    Token t = p.state.token;
    ++t.connection;
    ++t.operation;
    p = Peer{};
    p.state.token = t;
    if (i < size() && !config_.cameras[i].enabled)
      p.state.lifecycle = Lifecycle::Disabled;
  }
}
void CameraManager::tick() {
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
  if (e.peer >= size())
    return false;
  auto &p = peers_[e.peer];
  if (e.token.connection != p.state.token.connection ||
      e.token.operation != p.state.token.operation)
    return false;
  const auto l = p.state.lifecycle;
  const bool backoffDisconnect = l == Lifecycle::Backoff && e.kind == EventKind::Disconnected &&
                                 p.current != Operation::Connect;
  if (l != Lifecycle::Connecting && l != Lifecycle::Ready && l != Lifecycle::Operating &&
      !backoffDisconnect)
    return false;
  if (e.kind != EventKind::Disconnected && p.active && reached(clock_.now(), p.state.deadline_ms)) {
    fail(e.peer, CameraError::Timeout);
    return false;
  }
  if (e.kind == EventKind::Completed) {
    if (!p.active)
      return false;
    if (p.current == Operation::Connect)
      p.state.capabilities = e.capabilities;
    p.active = false;
    p.state.lifecycle = Lifecycle::Ready;
    p.state.error = CameraError::None;
    next(e.peer);
  } else if (e.kind == EventKind::RecordingObserved) {
    if (l == Lifecycle::Connecting)
      return false;
    p.state.observed = e.recording;
    p.state.last_observed_ms = clock_.now();
    p.state.has_observation = true;
  } else if (e.kind == EventKind::Disconnected) {
    if (p.active)
      transport_.cancel(e.peer, p.state.token);
    ++p.state.token.connection;
    ++p.state.token.operation;
    p.active = false;
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
  return true;
}
const CameraState *CameraManager::state(size_t p) const {
  return p < size() ? &peers_[p].state : nullptr;
}
} // namespace ridesync

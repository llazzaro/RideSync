#include "x5_wake.h"
namespace ridesync {
namespace {
bool due(uint32_t now, uint32_t deadline) { return now - deadline < 0x80000000u; }
} // namespace
RecordingState X5WakeRecovery::currentObserved(uint8_t peer) const {
  if (peer != 0 || runtime_.status().revoked ||
      (used_ && (!result_.terminal || !result_.released)) ||
      !runtime_.adapter().ready(Operation::Start))
    return RecordingState::Unknown;
  return runtime_.status().observed;
}
CameraError X5WakeRecovery::begin(const WakeOperation &op, uint32_t deadline) {
  if (op.peer != 0)
    return CameraError::Unsupported;
  if (!op.id || op.id == UINT32_MAX || !op.generation || op.generation == UINT32_MAX ||
      due(clock_.now(), deadline))
    return CameraError::InvalidPolicy;
  if (used_ && (!result_.terminal || !result_.released))
    return CameraError::Busy;
  const auto state = runtime_.status();
  if (!state.configured || state.revoked)
    return CameraError::Disabled;
  if (state.connected || state.active || !runtime_.adapter().connectionReleased())
    return CameraError::Busy;
  const auto error = runtime_.request(Operation::Connect);
  if (error != CameraError::None)
    return error;
  result_ = {};
  result_.operation = op;
  deadline_ = deadline;
  used_ = true;
  return CameraError::None;
}
void X5WakeRecovery::fail(CameraError error) {
  if (result_.terminal)
    return;
  result_.terminal = true;
  result_.fresh = false;
  result_.observed = RecordingState::Unknown;
  result_.error = error;
  runtime_.disconnect();
}
WakeRecoveryResult X5WakeRecovery::poll(const WakeOperation &op, uint32_t now) {
  if (!used_ || !sameWakeOperation(op, result_.operation))
    return {}; // A stale caller cannot progress or release another operation.
  if (!result_.terminal) {
    const auto state = runtime_.status();
    if (due(now, deadline_))
      fail(CameraError::Timeout);
    else if (state.revoked)
      fail(CameraError::Disabled);
    else if (state.error != CameraError::None || state.failure != X5Failure::None)
      fail(state.error == CameraError::None ? CameraError::Transport : state.error);
    else if (!state.active && state.connected && state.subscribed && state.age_known &&
             runtime_.adapter().ready(Operation::Start)) {
      // This observation belongs to the new connection, never pre-wake cache.
      result_.terminal = result_.released = result_.fresh = true;
      result_.observed = state.observed;
    }
  }
  if (result_.terminal && result_.error != CameraError::None)
    result_.released = runtime_.adapter().connectionReleased();
  return result_;
}
void X5WakeRecovery::cancel(const WakeOperation &op) {
  if (used_ && sameWakeOperation(op, result_.operation))
    fail(CameraError::Cancelled);
}
bool X5WakeControl::configure(const WakePeerConfig &config, WakePolicy policy) {
  if (attempted_)
    return false;
  attempted_ = true;
  if (!config.enabled || !config.source_qualified ||
      insta360::encodeWake(config.profile, config.identifier.data(), config.identifier.size())
              .error != insta360::WakeCodecError::None ||
      !policy.total_ms || policy.total_ms >= 0x80000000u || !policy.slice_ms ||
      policy.slice_ms > policy.total_ms)
    return false;
  config_ = config;
  policy_ = policy;
  configured_ = true;
  return true;
}
bool X5WakeControl::pending() const {
  const auto state = manager_.status(0);
  return id_ && (!state.released || state.phase == WakePhase::Queued ||
                 state.phase == WakePhase::Advertising || state.phase == WakePhase::Releasing ||
                 state.phase == WakePhase::Recovering);
}
CameraError X5WakeControl::request() {
  if (!configured_ || !runtime_.status().configured || runtime_.status().revoked)
    return CameraError::Disabled;
  if (pending() || runtime_.status().active || runtime_.status().connected ||
      !runtime_.adapter().connectionReleased())
    return CameraError::Busy;
  if (id_ >= UINT32_MAX - 1)
    return CameraError::InvalidPolicy;
  WakeOperation op;
  op.id = ++id_;
  op.generation = 1;
  return manager_.request(op, config_, policy_, clock_.now());
}
CameraError X5WakeControl::command(Operation op) {
  if (op == Operation::Wake)
    return request();
  if (pending())
    return CameraError::Busy;
  return runtime_.request(op);
}
void X5WakeControl::cancel() { manager_.cancel(0); }
void X5WakeControl::service() {
  if (runtime_.status().revoked)
    manager_.cancel(0);
  manager_.service(clock_.now());
}
WakeStatus X5WakeControl::status() const {
  auto result = manager_.status(0);
  if (!configured_) {
    result.error = CameraError::Disabled;
    return result;
  }
  if (result.phase == WakePhase::Ready)
    result.observed = runtime_.status().observed;
  return result;
}
} // namespace ridesync

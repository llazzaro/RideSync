#include "x5_runtime.h"
#include <cstdio>
namespace ridesync {
X5Runtime::X5Runtime(X5PeripheralPort &p, Clock &c)
    : adapter_(p, c), manager_(c, adapter_, X5Adapter::managerPolicy()) {
  adapter_.attach(manager_);
}
bool X5Runtime::configure(const X5Qualification &q, const SourceConfig &source) {
  if (attempted_ || revoked_)
    return false;
  attempted_ = true;
  refusal_ = X5Failure::Qualification;
  if (source.count != 1 || !validate(source).ok())
    return false;
  const auto &c = source.cameras[0];
  char identifier[18]{};
  std::snprintf(identifier, sizeof identifier, "%02X:%02X:%02X:%02X:%02X:%02X",
                q.identity.address[5], q.identity.address[4], q.identity.address[3],
                q.identity.address[2], q.identity.address[1], q.identity.address[0]);
  if (!c.enabled || c.family != CameraFamily::Insta360 || c.model != CameraModel::X5 ||
      c.gps_telemetry || !c.wake_identifier.empty() || c.identifier != identifier ||
      (q.identity.type == IdentityType::Public ? c.address_type != AddressType::Public
                                               : c.address_type != AddressType::Random))
    return false;
  if (!adapter_.configure(q)) {
    refusal_ = adapter_.failure();
    return false;
  }
  configured_ = manager_.configure(source).ok();
  error_ = configured_ ? CameraError::None : CameraError::Disabled;
  refusal_ = configured_ ? X5Failure::None : X5Failure::Qualification;
  return configured_;
}
void X5Runtime::service() {
  adapter_.service();
  manager_.tick();
}
CameraError X5Runtime::request(Operation op) {
  last_request_refusal_ = X5Failure::None;
  if (request_id_ == UINT32_MAX) {
    last_request_refusal_ = X5Failure::Exhausted;
    return last_request_error_ = CameraError::InvalidPolicy;
  }
  ++request_id_;
  if (!configured_ || revoked_) {
    last_request_refusal_ = X5Failure::Disabled;
    return last_request_error_ = CameraError::Disabled;
  }
  last_request_refusal_ = adapter_.refusal(op);
  if (last_request_refusal_ != X5Failure::None)
    return last_request_error_ = last_request_refusal_ == X5Failure::Busy
                                     ? CameraError::Busy
                                     : CameraError::Unsupported;
  last_request_error_ = manager_.request(0, op);
  if (last_request_error_ == CameraError::None) {
    accepted_id_ = request_id_;
    error_ = CameraError::None;
    refusal_ = X5Failure::None;
  }
  return last_request_error_;
}
void X5Runtime::disconnect() {
  manager_.reset(); // Retires intent and closes; no implicit STOP or reconnect.
  error_ = CameraError::Cancelled;
  refusal_ = X5Failure::Cancelled;
}
void X5Runtime::revoke() {
  if (revoked_)
    return;
  revoked_ = true;
  disconnect();
  error_ = CameraError::Disabled;
  refusal_ = X5Failure::Disabled;
}
X5RuntimeStatus X5Runtime::status() const {
  X5RuntimeStatus out;
  out.configured = configured_;
  out.revoked = revoked_;
  out.request_id = accepted_id_;
  out.last_request_id = request_id_;
  out.last_request_error = last_request_error_;
  out.last_request_refusal = last_request_refusal_;
  out.error = error_;
  out.failure = refusal_ != X5Failure::None ? refusal_ : adapter_.failure();
  out.connected = adapter_.connected();
  out.subscribed = adapter_.subscribed();
  out.active = adapter_.active();
  out.observed = adapter_.observed();
  out.age_known = adapter_.observationAge(out.age_ms);
  const auto *state = manager_.state(0);
  if (state) {
    out.lifecycle = state->lifecycle;
    if (state->error != CameraError::None && error_ == CameraError::None)
      out.error = state->error;
  }
  if (out.failure == X5Failure::Timeout && error_ == CameraError::None)
    out.error = CameraError::Timeout;
  return out;
}
} // namespace ridesync

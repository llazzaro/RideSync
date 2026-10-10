#pragma once
#include "profiles/insta360_x5.h"
namespace ridesync {
struct X5RuntimeStatus {
  bool configured = false, revoked = false, connected = false, subscribed = false, active = false,
       age_known = false;
  uint32_t request_id = 0, last_request_id = 0, age_ms = 0;
  CameraError last_request_error = CameraError::Disabled;
  X5Failure last_request_refusal = X5Failure::Disabled;
  RecordingState observed = RecordingState::Unknown;
  Lifecycle lifecycle = Lifecycle::Disabled;
  CameraError error = CameraError::Disabled;
  X5Failure failure = X5Failure::Disabled;
};
// Exactly one serialized owner. No BLE I/O in construction, no command queue.
class X5Runtime {
public:
  X5Runtime(X5PeripheralPort &port, Clock &clock);
  bool configure(const X5Qualification &, const SourceConfig &);
  void service();
  CameraError request(Operation);
  void disconnect();
  void revoke();
  X5RuntimeStatus status() const;
  X5Adapter &adapter() { return adapter_; }
  CameraManager &manager() { return manager_; }

private:
  X5Adapter adapter_;
  CameraManager manager_;
  bool attempted_ = false, configured_ = false, revoked_ = false;
  uint32_t request_id_ = 0, accepted_id_ = 0;
  CameraError last_request_error_ = CameraError::Disabled;
  X5Failure last_request_refusal_ = X5Failure::Disabled;
  CameraError error_ = CameraError::Disabled;
  X5Failure refusal_ = X5Failure::Disabled;
};
} // namespace ridesync

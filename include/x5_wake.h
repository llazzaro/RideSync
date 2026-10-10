#pragma once
#include "wake_manager.h"
#include "x5_runtime.h"
namespace ridesync {
// Serialized owner; recovery never emits shutter commands or retries a link.
class X5WakeRecovery final : public WakeRecovery {
public:
  X5WakeRecovery(X5Runtime &runtime, Clock &clock) : runtime_(runtime), clock_(clock) {}
  RecordingState currentObserved(uint8_t peer) const override;
  CameraError begin(const WakeOperation &, uint32_t deadline) override;
  WakeRecoveryResult poll(const WakeOperation &, uint32_t now) override;
  void cancel(const WakeOperation &) override;

private:
  X5Runtime &runtime_;
  Clock &clock_;
  WakeRecoveryResult result_;
  uint32_t deadline_ = 0;
  bool used_ = false;
  void fail(CameraError);
};
// One qualified X5, explicit WAKE followed by separately requested REC/STOP.
// Service the runtime first, then this owner. Nothing happens at construction.
class X5WakeControl {
public:
  X5WakeControl(X5Runtime &runtime, Clock &clock, WakeRadio &radio)
      : runtime_(runtime), clock_(clock), recovery_(runtime, clock), manager_(radio, &recovery_) {}
  bool configure(const WakePeerConfig &, WakePolicy = {});
  CameraError request();
  CameraError command(Operation);
  void cancel();
  void service();
  WakeStatus status() const;

private:
  X5Runtime &runtime_;
  Clock &clock_;
  X5WakeRecovery recovery_;
  WakeManager manager_;
  WakePeerConfig config_;
  WakePolicy policy_;
  uint32_t id_ = 0;
  bool attempted_ = false, configured_ = false;
  bool pending() const;
};
} // namespace ridesync

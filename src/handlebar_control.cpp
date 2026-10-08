#include "handlebar_control.h"
namespace ridesync {
HandlebarControl::HandlebarControl(Clock &c, Hero12Adapter &a, CameraManager &m,
                                   RecordingManager &g, ButtonInput &i, LedSink &s)
    : clock_(c), adapter_(a), manager_(m), group_(g), input_(i), led_(s) {}
HandlebarControl::~HandlebarControl() { group_.detachPreparation(*this); }
bool HandlebarControl::begin(const ButtonConfig &config) {
  if (begun_ || !validateButtonConfig(config) || !group_.attachPreparation(*this))
    return false;
  config_ = config;
  reset_generation_ = manager_.resets();
  begun_ = button_.begin(
      config, [](void *p, ButtonAction a) { static_cast<HandlebarControl *>(p)->submit(a); }, this);
  return begun_;
}
ControlAdmission HandlebarControl::submit(ButtonAction action) {
  if (!begun_ || status_.local.phase != TelemetryPhase::Running) {
    increment(status_.refused);
    return status_.admission = ControlAdmission::Inactive;
  }
  if (status_.local.camera != CameraAdmission::Admitted ||
      (action != ButtonAction::RecordingIntent && action != ButtonAction::WakeReconnect &&
       action != ButtonAction::Resync) ||
      !group_.status().enabled) {
    increment(status_.refused);
    return status_.admission = ControlAdmission::Refused;
  }
  if (count_ == actions_.size()) {
    increment(status_.overflow);
    return status_.admission = ControlAdmission::Overflow;
  }
  actions_[count_++] = action;
  increment(status_.accepted);
  return status_.admission = ControlAdmission::Admitted;
}
void HandlebarControl::beforeAdvance() {
  if (manager_.resets() != reset_generation_ || reset_generation_ == UINT32_MAX) {
    reset_generation_ = manager_.resets();
    reset();
    return;
  }
  if (status_.local.phase != TelemetryPhase::Running ||
      status_.local.camera != CameraAdmission::Admitted) {
    count_ = 0;
    return;
  }
  button_.poll(input_, clock_.now());
  // Drain the bounded FIFO before the sole advancement. A STOP in this batch
  // retires earlier preparation before any recovery can dispatch REC.
  for (size_t i = 0; i < count_; ++i) {
    if (actions_[i] == ButtonAction::RecordingIntent) {
      const auto target = recording_ ? RecordingState::Stopped : RecordingState::Recording;
      if (group_.request(target) == GroupError::None)
        recording_ = !recording_;
    } else {
      group_.resync();
    }
  }
  count_ = 0;
}
CameraError HandlebarControl::prepare(size_t i) {
  // No physical wake primitive is source-qualified for this route. BLE recovery
  // remains eligible independently; never label a reconnect as power-on proof.
  status_.wake[i] = CameraError::Unsupported;
  return adapter_.requestRecovery(i, false);
}
RecordingPreparationResult HandlebarControl::prepared(size_t i) {
  const auto phase = adapter_.recoveryState(i).phase;
  RecordingPreparationResult result;
  result.pending =
      phase == Hero12RecoveryPhase::Pending || phase == Hero12RecoveryPhase::Scanning ||
      phase == Hero12RecoveryPhase::Connecting || phase == Hero12RecoveryPhase::Observing;
  if (phase == Hero12RecoveryPhase::Ready && adapter_.recoveryReady(i))
    result.pending = !commandReady(i);
  else if (!result.pending) {
    result.error = phase == Hero12RecoveryPhase::Timeout       ? CameraError::Timeout
                   : phase == Hero12RecoveryPhase::Unsupported ? CameraError::Unsupported
                   : phase == Hero12RecoveryPhase::Cancelled   ? CameraError::Cancelled
                                                               : CameraError::NotConnected;
  }
  return result;
}
void HandlebarControl::retire(size_t i) { adapter_.cancelRecovery(i); }
bool HandlebarControl::commandReady(size_t i) const { return adapter_.commandReady(i); }
bool HandlebarControl::retiring(size_t i) const { return adapter_.linkRetiring(i); }
bool HandlebarControl::released(size_t i) const { return adapter_.linkReleased(i); }
bool HandlebarControl::binds(const Hero12Adapter &a, const CameraManager &m,
                             const RecordingManager &g) const {
  return &a == &adapter_ && &m == &manager_ && &g == &group_;
}
void HandlebarControl::revoke() {
  for (size_t i = 0; i < count_; ++i)
    increment(status_.discarded);
  count_ = 0;
  recording_ = false;
  status_.local.camera = CameraAdmission::Disabled;
}
void HandlebarControl::reset() {
  reset_generation_ = manager_.resets();
  revoke();
  button_.begin(
      config_, [](void *p, ButtonAction a) { static_cast<HandlebarControl *>(p)->submit(a); },
      this);
  group_.cancel();
}
void HandlebarControl::admission(const LocalTelemetryStatus &s) {
  if (s.phase != TelemetryPhase::Running || s.camera != CameraAdmission::Admitted) {
    if (status_.local.camera == CameraAdmission::Admitted)
      reset();
    else
      revoke();
  }
  status_.local = s;
}
void HandlebarControl::observe(const LocalTelemetryStatus &s) {
  admission(s);
  status_.group = group_.status();
  status_.health = {};
  status_.health.safe_mode = s.safe_mode;
  status_.health.application_fault = s.fault != TelemetryFault::None || status_.led_error != 0;
  for (size_t i = 0; i < manager_.size(); ++i) {
    status_.lifecycle[i] = manager_.state(i)->lifecycle;
    status_.recovery[i] = adapter_.recoveryState(i);
    status_.adapter_fault[i] = adapter_.fault(i);
    status_.health.adapter_recovery |= status_.group.peers[i].pending && retiring(i);
    const auto phase = status_.recovery[i].phase;
    status_.health.adapter_recovery |=
        phase == Hero12RecoveryPhase::Pending || phase == Hero12RecoveryPhase::Scanning ||
        phase == Hero12RecoveryPhase::Connecting || phase == Hero12RecoveryPhase::Observing;
  }
  auto &gps = status_.health.devices[0];
  gps.enabled = gps.qualified = gps.current = s.phase == TelemetryPhase::Running;
  gps.severity = LedSeverity::Partial;
  gps.outcome = s.gps.validity == FixValidity::Valid   ? DeviceHealth::Ok
                : s.gps.validity == FixValidity::NoFix ? DeviceHealth::NoFix
                                                       : DeviceHealth::Missing;
  auto &imu = status_.health.devices[1];
  imu.enabled = imu.qualified = imu.current = s.imu != SensorAdmission::Disabled;
  imu.severity = LedSeverity::Partial;
  imu.outcome = s.imu == SensorAdmission::Admitted ? s.imu_worker.outcome : DeviceHealth::Missing;
  auto &storage = status_.health.devices[2];
  storage.enabled = storage.qualified = storage.current = s.phase != TelemetryPhase::Inactive;
  storage.severity =
      s.storage.terminal || s.storage_error ? LedSeverity::Error : LedSeverity::Partial;
  storage.outcome = s.storage.terminal || s.storage_error ? DeviceHealth::IoError
                    : s.storage.dropped || s.storage.rejected || s.storage.lost
                        ? DeviceHealth::Missing
                        : DeviceHealth::Ok;
  status_.led = selectLedState(s.camera == CameraAdmission::Admitted ? &status_.group : nullptr,
                               status_.lifecycle, status_.health);
  const int error = led_.service(status_.led, clock_.now());
  if (error) {
    status_.led_error = error;
    status_.health.application_fault = true;
    status_.led = LedState::Error;
  }
}
} // namespace ridesync

#include "handlebar_control.h"
namespace ridesync {
namespace {
CameraError recoveryError(Hero12RecoveryPhase phase) {
  switch (phase) {
  case Hero12RecoveryPhase::Timeout:
    return CameraError::Timeout;
  case Hero12RecoveryPhase::Unsupported:
    return CameraError::Unsupported;
  case Hero12RecoveryPhase::Cancelled:
    return CameraError::Cancelled;
  default:
    return CameraError::NotConnected;
  }
}
} // namespace
HandlebarControl::HandlebarControl(Clock &c, CameraRuntimePort &a, CameraManager &m,
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
  if (action == ButtonAction::Bookmark) {
    status_.bookmark = bookmark_owner_ ? bookmark_owner_->bookmark(BookmarkSource::Physical,
                                                                   BookmarkGesture::Double)
                                       : BookmarkReceipt{};
    if (status_.bookmark.outcome == BookmarkOutcome::Admitted) {
      increment(status_.accepted);
      return status_.admission = ControlAdmission::Admitted;
    }
    if (status_.bookmark.outcome == BookmarkOutcome::Overflow) {
      increment(status_.overflow);
      return status_.admission = ControlAdmission::Overflow;
    }
    increment(status_.refused);
    return status_.admission = status_.bookmark.outcome == BookmarkOutcome::Inactive
                                   ? ControlAdmission::Inactive
                                   : ControlAdmission::Refused;
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
  if (status_.local.phase != TelemetryPhase::Running) {
    count_ = 0;
    return;
  }
  button_.poll(input_, clock_.now());
  if (status_.local.camera != CameraAdmission::Admitted) {
    count_ = 0;
    return;
  }
  // Serial commands share the same group. Remember their explicit choice
  // before a resync clears intent; Unknown must preserve the REC/STOP latch.
  const auto intent = group_.status().intent;
  if (intent != RecordingState::Unknown)
    recording_ = intent == RecordingState::Recording;
  // Drain the bounded FIFO before the sole advancement. A STOP in this batch
  // retires earlier preparation before any recovery can dispatch REC.
  for (size_t i = 0; i < count_; ++i) {
    if (actions_[i] == ButtonAction::RecordingIntent) {
      // Honor explicit group commands (including the console). Resync clears
      // that intent, so fall back to the last admitted button choice then.
      const auto intent = group_.status().intent;
      const bool recording =
          intent == RecordingState::Recording || (intent == RecordingState::Unknown && recording_);
      const auto target = recording ? RecordingState::Stopped : RecordingState::Recording;
      if (group_.request(target) == GroupError::None)
        recording_ = target == RecordingState::Recording;
    } else {
      group_.resync();
    }
  }
  count_ = 0;
}
CameraError HandlebarControl::prepare(size_t i) {
  // Dispatch each peer through its model-specific wake/recovery route.
  // Unsupported physical wake must not prevent an eligible BLE reconnect.
  status_.wake[i] = adapter_.wakeSupported(i) ? CameraError::None : CameraError::Unsupported;
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
    result.error = recoveryError(phase);
  }
  return result;
}
void HandlebarControl::retire(size_t i) { adapter_.cancelRecovery(i); }
bool HandlebarControl::commandReady(size_t i) const { return adapter_.commandReady(i); }
bool HandlebarControl::retiring(size_t i) const { return adapter_.linkRetiring(i); }
bool HandlebarControl::released(size_t i) const { return adapter_.linkReleased(i); }
bool HandlebarControl::binds(const CameraRuntimePort &a, const CameraManager &m,
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
namespace {
DeviceHealth storageHealth(const LocalTelemetryStatus &s) {
  if (s.storage.terminal || s.storage_error)
    return DeviceHealth::IoError;
  return s.storage.dropped || s.storage.rejected || s.storage.lost ? DeviceHealth::Missing
                                                                   : DeviceHealth::Ok;
}
bool pendingRecovery(Hero12RecoveryPhase phase) {
  return phase == Hero12RecoveryPhase::Pending || phase == Hero12RecoveryPhase::Scanning ||
         phase == Hero12RecoveryPhase::Connecting || phase == Hero12RecoveryPhase::Observing;
}
} // namespace
void HandlebarControl::observeDevices(const LocalTelemetryStatus &s) {
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
  storage.outcome = storageHealth(s);
}
void HandlebarControl::observe(const LocalTelemetryStatus &s) {
  admission(s);
  status_.group = group_.status();
  if (s.phase == TelemetryPhase::Running && s.camera == CameraAdmission::Admitted &&
      status_.group.intent != RecordingState::Unknown)
    recording_ = status_.group.intent == RecordingState::Recording;
  status_.health = {};
  status_.health.safe_mode = s.safe_mode;
  status_.health.camera_critical = s.camera_health.enabled && s.camera_health.critical;
  status_.health.camera_warning = s.camera_health.enabled && s.camera_health.warning;
  status_.health.required_worker_stall = s.worker_stalls != 0;
  status_.health.application_fault = s.supervision_fault || s.worker_refused ||
                                     s.fault != TelemetryFault::None || status_.led_error != 0 ||
                                     s.configuration == StartupState::ConfigTimedOut;
  for (size_t i = 0; i < manager_.size(); ++i) {
    status_.lifecycle[i] = manager_.state(i)->lifecycle;
    status_.recovery[i] = adapter_.recoveryState(i);
    status_.adapter_fault[i] = adapter_.fault(i);
    status_.health.adapter_recovery |= status_.group.peers[i].pending && retiring(i);
    const auto phase = status_.recovery[i].phase;
    status_.health.adapter_recovery |= pendingRecovery(phase);
  }
  observeDevices(s);
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

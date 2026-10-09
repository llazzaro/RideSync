#include "application_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "ble_esp32.h"
#include "nvs_boot_guard.h"
#include "pairing_proof_esp32.h"
#include <new>
namespace ridesync {
SupervisedEsp32Application::SupervisedEsp32Application(
    HardwareSerial &u, SPIClass &s, TwoWire &w, const QualifiedLocalTelemetry &q,
    const QualifiedHandlebar &h, const std::array<Hero12Qualification, kMaxCameras> &c,
    StaticMotionReferenceSource *source)
    : uart_(u), spi_(s), wire_(w), qualification_(q), handlebar_(h), cameras_(c),
      motion_source_(source) {}
SupervisedEsp32Application::~SupervisedEsp32Application() {
  // No waits or filesystem access. Boot-lifetime caller retains this object and
  // all its resources until the actual worker barriers permit destruction.
  if (telemetry_ && canRelease()) {
    if (control_)
      control_->~Esp32HandlebarControl();
    telemetry_->~Esp32LocalTelemetry();
  }
}
std::array<WorkerPolicy, 4> SupervisedEsp32Application::prepare(const SettingsSnapshot &settings,
                                                                bool safe, bool nvs,
                                                                HealthSupervisor &supervisor) {
  if (prepared_)
    return policy_;
  prepared_ = true;
  supervisor_ = &supervisor;
  configuration_ = !settings.completed  ? StartupState::ConfigTimedOut
                   : settings.effective ? StartupState::ConfigReady
                                        : StartupState::ConfigUnavailable;
  epoch_ = settings.epoch;
  generation_ = settings.generation;
  auto &q = qualification_;
  q.runtime.safe_mode = safe;
  safe_mode_ = safe;
  SettingsQualification proof;
  proof.cameras = q.runtime.cameras_qualified;
  proof.button = handlebar_.opt_in && handlebar_.acknowledge_qualification;
  proof.local_telemetry = q.runtime.opt_in;
  proof.safe_mode = safe;
  proof.nvs_allowed = nvs;
  const auto admission = admitSettings(settings, proof);
  camera_allowed_ = admission.cameras;
  const auto &saved_button = settings.settings.button_gpio;
  const auto &qualified_button = handlebar_.button;
  button_allowed_ = admission.button && camera_allowed_ && qualified_button.enabled &&
                    qualified_button.board_qualified && saved_button.pin == qualified_button.pin &&
                    saved_button.pull == qualified_button.pull &&
                    saved_button.active_low == qualified_button.active_low;
  if (camera_allowed_) {
    SourceConfig source;
    camera_allowed_ =
        expandSettings(settings.settings, source) && hero12Runtime().manager.configure(source).ok();
    for (size_t i = 0; camera_allowed_ && i < settings.peers.count; ++i) {
      const auto slot = settings.peers.entries[i].slot;
      camera_allowed_ = slot < kMaxCameras &&
                        settings.peers.entries[i].model == CameraModel::HERO12_BLACK &&
                        hero12Runtime().adapter.configurePeer(slot, cameras_[slot]);
    }
    q.runtime.peers = settings.peers;
  }
  button_allowed_ = button_allowed_ && camera_allowed_;
  handlebar_.actions = settings.settings.button;
  // Saved GPIO cannot replace independently qualified physical routing.
  const bool local =
      admission.local_telemetry && q.runtime.gps_qualified && q.modem.pins_qualified &&
      q.modem.documentary_profile_opt_in && q.modem_already_powered &&
      q.runtime.power_timing.qualified && q.sd.opt_in && q.sd.wiring_card_qualified &&
      q.sd.exclusive_volume && q.sd.namespace_commissioned && q.sd.commissioned_namespace &&
      (!q.runtime.imu_enabled || (q.runtime.imu_qualified && q.imu.enabled && q.imu.dedicated_bus &&
                                  q.imu.electrically_qualified && q.imu.sensor_id));
  for (size_t i = 0; i < policy_.size(); ++i) {
    auto &p = policy_[i];
    p.enabled = local && (i != static_cast<unsigned>(Worker::Ble) || camera_allowed_) &&
                (i != static_cast<unsigned>(Worker::Imu) || (q.runtime.imu_enabled && !safe));
    p.required = p.qualified = p.enabled;
    p.deadline_ms = HealthSupervisor::kMaxDeadlineMs;
    p.startup_grace_ms = HealthSupervisor::kMaxGraceMs;
  }
  return policy_;
}
void SupervisedEsp32Application::launch() {
  if (!prepared_ || launched_)
    return;
  launched_ = true;
  telemetry_ = new (&telemetry_memory_)
      Esp32LocalTelemetry(uart_, spi_, wire_, qualification_, motion_source_);
  telemetry_->owner().configurationStartup(configuration_);
  telemetry_->owner().currentAdmission(camera_allowed_, qualification_.runtime.safe_mode);
  if (button_allowed_) {
    control_ = new (&control_memory_) Esp32HandlebarControl(*telemetry_, handlebar_);
    if (!control_->begin()) {
      camera_allowed_ = false;
      telemetry_->owner().currentAdmission(false, qualification_.runtime.safe_mode);
    }
  }
  const bool local = telemetry_->start();
  if (local && camera_allowed_)
    ble_started_ = hero12Runtime().adapter.start(true, true);
  if (!ble_started_ && policy_[static_cast<unsigned>(Worker::Ble)].enabled) {
    supervisor_->progress(Worker::Ble).refused();
    camera_allowed_ = false;
    telemetry_->owner().currentAdmission(false, qualification_.runtime.safe_mode);
  }
  if (!local)
    for (unsigned i = 0; i < policy_.size(); ++i)
      if (policy_[i].enabled)
        supervisor_->progress(static_cast<Worker>(i)).refused();
}
void SupervisedEsp32Application::current(const SettingsSnapshot &s, bool safe, bool nvs) {
  safe_mode_ = safe;
  // Any new settings generation retires this session's control authorization;
  // a fresh owner is required. No remap or replay in an active session.
  camera_allowed_ = camera_allowed_ && s.completed && s.effective && s.peers_valid &&
                    s.epoch == epoch_ && s.generation == generation_ && !safe && nvs;
  reset_current_ = camera_allowed_;
  if (!reset_current_)
    revokeReset();
  if (telemetry_)
    telemetry_->owner().currentAdmission(camera_allowed_, safe);
}
bool SupervisedEsp32Application::resetNow(uint32_t &now) {
  if (!telemetry_ || !telemetry_->owner().session())
    return false;
  return telemetry_->owner().session()->clock().snapshotWithRaw(now).monotonic_quality ==
         MonotonicQuality::Valid;
}
bool SupervisedEsp32Application::resetAdmitted() const {
  return launched_ && camera_allowed_ && reset_current_ && ble_started_ && !safe_mode_ &&
         telemetry_ && telemetry_->status().phase == TelemetryPhase::Running &&
         nvsBootStatus().persistenceAllowed() &&
         Esp32BleHost::instance().state() == BleHostState::Ready &&
         Esp32BleHost::instance().fault() == BleFault::None;
}
BondResetSubmission SupervisedEsp32Application::requestPairingReset(uint8_t slot, uint32_t peer_id,
                                                                    uint32_t epoch,
                                                                    uint64_t generation,
                                                                    uint32_t operation,
                                                                    uint32_t timeout_ms) {
  if (!reset_.releasable)
    return BondResetSubmission::Busy;
  if (!operation || operation <= last_reset_)
    return BondResetSubmission::Stale;
  uint32_t now = 0;
  if (!resetAdmitted() || epoch != epoch_ || generation != generation_ || !peer_id ||
      slot >= kMaxCameras || !timeout_ms || timeout_ms > 5000 || !resetNow(now) ||
      host_operation_ == UINT32_MAX || hero12Runtime().manager.sealed(slot))
    return BondResetSubmission::Refused;
  const auto &q = cameras_[slot];
  if (!q.source_qualified || !q.classic_profile_confirmed || !q.firmware_size ||
      !q.identity.verified ||
      (q.identity.type != IdentityType::Public && q.identity.type != IdentityType::RandomStatic))
    return BondResetSubmission::Refused;
  unsigned matched = 0;
  for (size_t i = 0; i < qualification_.runtime.peers.count; ++i) {
    const auto &p = qualification_.runtime.peers.entries[i];
    if (p.slot == slot && p.id == peer_id && p.model == CameraModel::HERO12_BLACK)
      ++matched;
    if (p.slot != slot && p.slot < kMaxCameras && cameras_[p.slot].identity.verified &&
        cameras_[p.slot].identity.type == q.identity.type &&
        cameras_[p.slot].identity.address == q.identity.address)
      return BondResetSubmission::Refused;
  }
  BleStoreProof proof;
  if (matched != 1 || !Esp32BleHost::instance().admittedProof(proof) ||
      proof.qualification_record == UINT32_MAX)
    return BondResetSubmission::Refused;
  reset_ = {};
  reset_.operation = last_reset_ = operation;
  reset_.slot = slot;
  reset_.peer_id = peer_id;
  reset_.epoch = epoch;
  reset_.generation = generation;
  reset_.identity = q.identity;
  reset_.qualification_record = proof.qualification_record;
  reset_.releasable = false;
  reset_.phase = ApplicationResetPhase::Retiring;
  reset_identity_ = q.identity;
  reset_proof_ = proof;
  reset_deadline_ = now + timeout_ms;
  proof_pending_ = host_pending_ = retried_ = retry_wait_ = host_cancelled_ = false;
  hero12Runtime().group.seal(slot);
  hero12Runtime().adapter.sealForMaintenance(slot);
  return BondResetSubmission::Queued;
}
void SupervisedEsp32Application::revokeReset(bool timeout) {
  if (reset_.releasable)
    return;
  reset_.finished = true;
  reset_.timed_out = reset_.timed_out || timeout;
  reset_.cancelled = reset_.cancelled || !timeout;
  reset_.outcome = reset_.mutation ? BondOutcome::Indeterminate : BondOutcome::Refused;
  if (proof_pending_)
    pairingProofMaintenance().cancel(reset_.operation);
  if (host_pending_ && !host_cancelled_) {
    Esp32BleHost::instance().cancelBondReset(reset_host_operation_);
    host_cancelled_ = true;
  }
}
bool SupervisedEsp32Application::cancelPairingReset(uint32_t operation) {
  if (reset_.releasable || reset_.operation != operation)
    return false;
  revokeReset();
  return true;
}
void SupervisedEsp32Application::finishReset() {
  if (proof_pending_ || host_pending_ || !hero12Runtime().adapter.maintenanceReleased(reset_.slot))
    return;
  reset_.finished = reset_.releasable = true;
  reset_.phase = ApplicationResetPhase::Finished;
  hero12Runtime().adapter.finishMaintenanceDrain();
}
bool SupervisedEsp32Application::serviceResetProof() {
  if (!proof_pending_)
    return true;
  const auto result = pairingProofMaintenance().result(reset_.operation);
  if (!result.releasable)
    return false;
  reset_.proof_denied = result.durable;
  reset_.proof_write_attempted = result.write_attempted;
  reset_.requalification_required = result.durable || result.write_attempted;
  reset_.error = result.error;
  pairingProofMaintenance().release(reset_.operation);
  proof_pending_ = false;
  if (result.cancelled || result.timed_out || !result.durable) {
    if (result.cancelled || result.timed_out)
      revokeReset(result.timed_out);
    else {
      reset_.finished = true;
      reset_.outcome = BondOutcome::Refused;
    }
    finishReset();
    return false;
  }
  reset_.phase = ApplicationResetPhase::Submitting;
  return true;
}

bool SupervisedEsp32Application::serviceResetHost(uint32_t now) {
  if (!host_pending_)
    return true;
  const auto result = Esp32BleHost::instance().bondResetResult(reset_host_operation_, now);
  reset_.mutation = reset_.mutation || result.mutation;
  reset_.requalification_required =
      reset_.requalification_required || result.requalification_required;
  if (result.cancelled || result.timed_out)
    revokeReset(result.timed_out);
  // Revocation can precede our first observation of an admitted mutation.
  // Merge that evidence before either held or final host-resource release.
  if (reset_.finished && reset_.mutation)
    reset_.outcome = BondOutcome::Indeterminate;
  if (!result.releasable)
    return false;
  host_pending_ = false;
  reset_.error = result.error;
  if (!reset_.finished && result.outcome == BondOutcome::Busy && !retried_) {
    retried_ = true;
    retry_wait_ = true;
    retry_at_ = now + 100;
    reset_.phase = ApplicationResetPhase::Submitting;
    return true;
  }
  if (!reset_.finished)
    reset_.outcome = result.outcome;
  reset_.finished = true;
  finishReset();
  return false;
}

void SupervisedEsp32Application::serviceResetPhase(uint32_t now) {
  if (reset_.finished) {
    finishReset();
    return;
  }
  if (retry_wait_) {
    if (now - retry_at_ >= 0x80000000UL)
      return;
    retry_wait_ = false;
  }
  if (reset_.phase == ApplicationResetPhase::Retiring) {
    if (!hero12Runtime().adapter.maintenanceReleased(reset_.slot))
      return;
    const auto result =
        pairingProofMaintenance().request(reset_.operation, reset_proof_, reset_deadline_, now);
    if (result == BondResetSubmission::Queued) {
      proof_pending_ = true;
      reset_.phase = ApplicationResetPhase::RevokingProof;
      return;
    }
    if (result == BondResetSubmission::Busy && !retried_) {
      retried_ = true;
      retry_wait_ = true;
      retry_at_ = now + 100;
      return;
    }
    reset_.outcome = result == BondResetSubmission::Busy ? BondOutcome::Busy : BondOutcome::Refused;
    reset_.finished = true;
    finishReset();
    return;
  }
  if (reset_.phase == ApplicationResetPhase::Submitting) {
    if (host_operation_ == UINT32_MAX) {
      revokeReset();
      finishReset();
      return;
    }
    reset_host_operation_ = ++host_operation_;
    const auto result = Esp32BleHost::instance().requestBondReset(
        reset_identity_, reset_host_operation_, reset_deadline_, now);
    if (result == BondResetSubmission::Queued) {
      host_pending_ = true;
      reset_.phase = ApplicationResetPhase::WaitingHost;
      return;
    }
    if (result == BondResetSubmission::Busy && !retried_) {
      retried_ = true;
      retry_wait_ = true;
      retry_at_ = now + 100;
      return;
    }
    reset_.outcome = result == BondResetSubmission::Busy ? BondOutcome::Busy : BondOutcome::Refused;
    reset_.finished = true;
    finishReset();
  }
}

void SupervisedEsp32Application::serviceReset() {
  if (reset_.releasable)
    return;
  uint32_t now = 0;
  if (!resetNow(now) || !resetAdmitted())
    revokeReset();
  else if (now - reset_deadline_ < 0x80000000UL)
    revokeReset(true);
  if (!serviceResetProof())
    return;
  if (!serviceResetHost(now))
    return;
  serviceResetPhase(now);
}
void SupervisedEsp32Application::publish(Worker w, uint32_t n, DeviceHealth outcome, bool done,
                                         bool refused) {
  if (!policy_[static_cast<unsigned>(w)].enabled)
    return;
  auto &p = supervisor_->progress(w);
  p.observe(n, outcome);
  if (refused)
    p.refused();
  if (done)
    p.finished();
}
void SupervisedEsp32Application::service(uint8_t stalls, uint8_t refused, bool fault) {
  if (!telemetry_)
    return;
  telemetry_->owner().supervision(stalls, refused, fault);
  if (stalls || refused || fault)
    telemetry_->owner().currentAdmission(false, safe_mode_);
  if (stalls || refused || fault) {
    reset_current_ = false;
    revokeReset();
  }
  serviceReset();
  telemetry_->service(); // Exactly one bound application/camera/admission pass.
  const auto s = telemetry_->status();
  const bool no_session =
      s.phase == TelemetryPhase::Stopping || s.phase == TelemetryPhase::Finished;
  publish(Worker::At, s.at_completed,
          s.gps.validity == FixValidity::Valid    ? DeviceHealth::Ok
          : s.gps.validity == FixValidity::NoFix  ? DeviceHealth::NoFix
          : s.gps.health == UartHealth::Exhausted ? DeviceHealth::RetryExhausted
          : (s.gps.state == ModemState::Desynchronized || s.gps.health == UartHealth::InvalidClock)
              ? DeviceHealth::Desynchronized
              : DeviceHealth::Missing,
          (no_session || s.gps.health == UartHealth::InvalidClock) && s.at_completed != 0,
          no_session && s.at_completed == 0);
  publish(Worker::Sd, s.sd_completed, s.storage_error ? DeviceHealth::IoError : DeviceHealth::Ok,
          s.storage_worker_finished);
  publish(Worker::Imu, s.imu_worker.completed, s.imu_worker.outcome, s.imu_worker.finished,
          s.imu == SensorAdmission::TaskRefused || s.imu == SensorAdmission::SafeModeRefused ||
              (no_session && s.imu == SensorAdmission::Pending));
  if (ble_started_) {
    const auto &p = hero12Runtime().adapter.progress();
    publish(Worker::Ble, p.generation(), p.outcome(), p.isFinished());
  }
}
} // namespace ridesync
extern "C" ridesync::SupervisedEsp32Application &ridesync_supervised_application(
    HardwareSerial &uart, SPIClass &spi, TwoWire &wire, const ridesync::QualifiedLocalTelemetry &q,
    const ridesync::QualifiedHandlebar &h,
    const std::array<ridesync::Hero12Qualification, ridesync::kMaxCameras> &c,
    ridesync::StaticMotionReferenceSource *source) {
  static ridesync::SupervisedEsp32Application application(uart, spi, wire, q, h, c, source);
  return application;
}
#endif
#if defined(ARDUINO_ARCH_ESP32)
struct ApplicationPairingResetSymbols {
  decltype(&ridesync::SupervisedEsp32Application::requestPairingReset) request;
  decltype(&ridesync::SupervisedEsp32Application::cancelPairingReset) cancel;
  decltype(&ridesync::SupervisedEsp32Application::pairingResetStatus) result;
};
// Data-only opt-in link proof; no default activation or operation is performed.
extern "C" const ApplicationPairingResetSymbols ridesync_application_pairing_reset_backend = {
    &ridesync::SupervisedEsp32Application::requestPairingReset,
    &ridesync::SupervisedEsp32Application::cancelPairingReset,
    &ridesync::SupervisedEsp32Application::pairingResetStatus};
#endif

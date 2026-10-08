#include "application_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include <new>
namespace ridesync {
SupervisedEsp32Application::SupervisedEsp32Application(
    HardwareSerial &u, SPIClass &s, TwoWire &w, const QualifiedLocalTelemetry &q,
    const QualifiedHandlebar &h, const std::array<Hero12Qualification, kMaxCameras> &c)
    : uart_(u), spi_(s), wire_(w), qualification_(q), handlebar_(h), cameras_(c) {}
SupervisedEsp32Application::~SupervisedEsp32Application() {
  // No waits or filesystem access. Boot-lifetime caller retains this object and
  // all its resources until the actual worker barriers permit destruction.
  if (telemetry_ && telemetry_->canRelease()) {
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
  telemetry_ = new (&telemetry_memory_) Esp32LocalTelemetry(uart_, spi_, wire_, qualification_);
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
  if (telemetry_)
    telemetry_->owner().currentAdmission(camera_allowed_, safe);
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
    const std::array<ridesync::Hero12Qualification, ridesync::kMaxCameras> &c) {
  static ridesync::SupervisedEsp32Application application(uart, spi, wire, q, h, c);
  return application;
}
#endif

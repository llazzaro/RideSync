#include "local_telemetry_runtime.h"
#include "handlebar_control.h"
#include <new>
namespace ridesync {
LocalTelemetryRuntime::Active::Active(Clock &raw, StorageSink &sink, ModemUart &uart,
                                      Hero12Adapter &adapter, CameraManager &manager,
                                      RecordingManager &group, uint64_t id,
                                      const LocalTelemetryConfig &config, GnssPowerControl *power)
    : session(raw, sink, adapter, manager, group, id, config.firmware, config.provenance),
      modem(uart, config.modem), gps(session.clock(), modem, power, config.power_timing) {}
LocalTelemetryRuntime::LocalTelemetryRuntime(Clock &raw, ModemUart &uart,
                                             TelemetryStorageWorker &sd, TelemetryImuWorker &imu,
                                             Hero12Adapter &adapter, CameraManager &manager,
                                             RecordingManager &group,
                                             const LocalTelemetryConfig &config,
                                             GnssPowerControl *power)
    : raw_(raw), uart_(uart), sd_(sd), imu_(imu), adapter_(adapter), manager_(manager),
      group_(group), config_(config), power_(power) {
  current_safe_mode_ = config.safe_mode;
  status_.safe_mode = config.safe_mode;
}
LocalTelemetryRuntime::~LocalTelemetryRuntime() {
  // Caller must observe canRelease first; never wait or touch a filesystem here.
  if (active_)
    active_->~Active();
}
void LocalTelemetryRuntime::refuseStart(TelemetryFault fault) {
  if (started_)
    return;
  started_ = true;
  status_.phase = TelemetryPhase::Refused;
  status_.fault = fault;
}
bool LocalTelemetryRuntime::start() {
  if (started_)
    return false;
  started_ = true;
  if (!config_.opt_in || !config_.gps_qualified ||
      (config_.imu_enabled && !config_.imu_qualified) || !config_.gps_record_ms ||
      config_.gps_record_ms >= 0x80000000UL || !config_.power_timing.qualified ||
      (power_ &&
       (!config_.power_timing.key_active_ms || !config_.power_timing.settle_ms ||
        config_.power_timing.key_active_ms > 60000 || config_.power_timing.settle_ms > 60000))) {
    status_.phase = TelemetryPhase::Refused;
    status_.fault = TelemetryFault::Qualification;
    return false;
  }
  if (!sd_.start()) {
    status_.phase = TelemetryPhase::Refused;
    status_.fault = TelemetryFault::StorageTask;
    return false;
  }
  startup_ms_ = raw_.now();
  sd_started_ = true;
  status_.releasable = false;
  status_.phase = TelemetryPhase::Allocating;
  status_.imu = config_.imu_enabled ? SensorAdmission::Pending : SensorAdmission::Disabled;
  return true;
}
void LocalTelemetryRuntime::service() {
  if (servicing_)
    return;
  servicing_ = true;
  if (!sd_started_ || status_.phase == TelemetryPhase::Finished) {
    if (control_)
      control_->observe(status_);
    servicing_ = false;
    return;
  }
  status_.identity = sd_.allocation();
  if (status_.phase == TelemetryPhase::Allocating &&
      raw_.now() - startup_ms_ >= HealthSupervisor::kMaxGraceMs) {
    status_.fault = TelemetryFault::StartupTimeout;
    requestStop(); // Live blocked worker is retained; never destroy or wait here.
  }
  if (status_.phase == TelemetryPhase::Allocating &&
      status_.identity.status != IdentityStatus::Pending) {
    if (status_.identity.status != IdentityStatus::Committed || !status_.identity.id) {
      status_.fault = TelemetryFault::Identity;
      requestStop();
    } else {
      active_ = new (&memory_) Active(raw_, sd_.sink(), uart_, adapter_, manager_, group_,
                                      status_.identity.id, config_, power_);
      auto &s = active_->session;
      const auto t = s.clock().snapshot();
      if (!s.storage().configValid() || active_->modem.snapshot(t).state == ModemState::Disabled) {
        status_.fault = TelemetryFault::Configuration;
        requestStop();
      } else if (!sd_.bind(s.storage())) {
        status_.fault = TelemetryFault::Bind;
        requestStop();
      } else {
        bound_ = true;
        bool camera = config_.cameras_qualified && current_camera_ && !current_safe_mode_ &&
                      config_.peers.count && config_.peers.count <= kMaxCameras;
        if (camera) {
          for (size_t i = 0; i < config_.peers.count; ++i) {
            const auto &p = config_.peers.entries[i];
            camera = s.configurePeer(p.slot, p.id, p.model) && camera;
          }
          camera = camera && s.activate();
        }
        status_.camera = camera                      ? CameraAdmission::Admitted
                         : config_.cameras_qualified ? CameraAdmission::Refused
                                                     : CameraAdmission::Disabled;
        if (!camera)
          s.activateLocal(); // Same valid Storage, never a second logger/clock.
        if (config_.imu_enabled) {
          // Preserve the actual safe-mode policy, not an eligibility shortcut.
          imu_started_ = imu_.start(s.imuInbox(), status_.identity.id, config_.safe_mode);
          status_.imu = imu_started_        ? SensorAdmission::Admitted
                        : config_.safe_mode ? SensorAdmission::SafeModeRefused
                                            : SensorAdmission::TaskRefused;
        }
        if (!imu_started_) {
          s.finishImu();
          imu_finished_ = true;
        }
        status_.phase = TelemetryPhase::Running;
      }
    }
  }
  if (active_ && bound_) {
    auto &s = active_->session;
    if (s.storage().health().terminal)
      requestStop();
    if (status_.phase == TelemetryPhase::Running) {
      active_->gps.tick();
      status_.at_completed = active_->gps.completed(); // Only after an actual AT tick returns.
      auto t = s.clock().snapshot();
      auto sample = active_->modem.snapshot(t);
      const auto &fix = sample.fix;
      if ((sample.validity == FixValidity::Valid || sample.validity == FixValidity::NoFix) &&
          fix.utc_date.available && fix.utc_time.available &&
          (!anchored_ || anchored_receipt_ != fix.receipt_monotonic_ms)) {
        const auto &d = fix.utc_date.value;
        const auto &u = fix.utc_time.value;
        if (s.clock().anchorAtReceipt({d.year, d.month, d.day, u.hour, u.minute, u.second,
                                       static_cast<uint16_t>(u.centisecond * 10)},
                                      fix.receipt_monotonic_ms)) {
          anchored_ = true;
          anchored_receipt_ = fix.receipt_monotonic_ms;
        }
        t = s.clock().snapshot();
        sample = active_->modem.snapshot(t);
      }
      status_.timestamp = t;
      status_.gps = sample;
      if (!logged_ || t.monotonic_ms - logged_ms_ >= config_.gps_record_ms) {
        s.admission().gps(t, sample); // Same snapshot for age validation/CSV.
        logged_ = true;
        logged_ms_ = t.monotonic_ms;
      }
    }
    if (imu_started_) {
      status_.imu_worker = imu_.observation();
      if (status_.imu_worker.finished && !imu_finished_) {
        s.finishImu(); // Acquire final worker publication before inbox finish.
        imu_finished_ = true;
      }
    }
    status_.safe_mode = current_safe_mode_;
    if (!current_camera_ || current_safe_mode_)
      status_.camera =
          config_.cameras_qualified ? CameraAdmission::Refused : CameraAdmission::Disabled;
    if (status_.phase == TelemetryPhase::Stopping && imu_finished_ && !session_stop_requested_) {
      s.requestStop(); // AFTER final IMU publication and actual wrapper access.
      session_stop_requested_ = true;
    }
    if (control_)
      control_->admission(status_);
    s.service(status_.phase == TelemetryPhase::Running ? control_ : nullptr);
    // Sole adapter/group/manager/admission advancement this pass.
  }
  observe();
  servicing_ = false;
}
void LocalTelemetryRuntime::requestStop() {
  if (!sd_started_ || status_.phase == TelemetryPhase::Finished ||
      status_.phase == TelemetryPhase::Stopping)
    return;
  status_.phase = TelemetryPhase::Stopping;
  if (control_)
    control_->revoke();
  if (status_.camera == CameraAdmission::Admitted) {
    group_.cancel();
    adapter_.stop();
  }
  if (active_)
    active_->gps.cancel();
  if (imu_started_)
    imu_.requestStop();
  if (!active_ || !bound_)
    sd_.cancel(); // Unbound owner only; no producer or Storage queue exists.
}
bool LocalTelemetryRuntime::attachControl(HandlebarControl &c) {
  if (control_ || started_ || !c.binds(adapter_, manager_, group_))
    return false;
  control_ = &c;
  return true;
}
void LocalTelemetryRuntime::currentAdmission(bool cameras, bool safe_mode) {
  // A revocation is terminal for camera operations in this session. Returning
  // settings/safe mode cannot replay queued work or re-enable the old snapshot.
  if ((!cameras || safe_mode) && current_camera_) {
    current_camera_ = false;
    status_.camera =
        config_.cameras_qualified ? CameraAdmission::Refused : CameraAdmission::Disabled;
    if (control_)
      control_->reset();
    else
      group_.cancel();
    if (active_ && bound_ && config_.cameras_qualified)
      adapter_.stop();
  }
  current_safe_mode_ = safe_mode;
  status_.safe_mode = safe_mode;
}
void LocalTelemetryRuntime::supervision(uint8_t stalls, uint8_t refused, bool fault) {
  status_.worker_stalls = stalls;
  status_.worker_refused = refused;
  status_.supervision_fault = fault;
}
void LocalTelemetryRuntime::detachControl(HandlebarControl &c) {
  if (control_ == &c && canRelease())
    control_ = nullptr;
}
void LocalTelemetryRuntime::observe() {
  status_.sd_completed = sd_.completed();
  status_.storage_error = sd_.ioError();
  status_.storage_worker_finished = sd_.workerFinished();
  if (active_) {
    auto &s = active_->session;
    status_.storage = s.storage().health();
    for (unsigned i = 0; i < static_cast<unsigned>(RecordKind::Count); ++i)
      status_.kinds[i] = s.storage().kindHealth(static_cast<RecordKind>(i));
    for (unsigned i = 0; i < 5; ++i)
      status_.imu_dropped[i] = s.imuInbox().dropped(static_cast<RecordKind>(i));
    status_.imu_rejected = s.imuInbox().rejected();
  }
  if (status_.phase == TelemetryPhase::Stopping && status_.storage_worker_finished &&
      (!imu_started_ || imu_finished_) && (!bound_ || active_->session.stopped())) {
    status_.phase = TelemetryPhase::Finished;
    status_.releasable = true;
  }
  if (control_)
    control_->observe(status_);
}
CameraEventSession *LocalTelemetryRuntime::session() {
  return active_ ? &active_->session : nullptr;
}
bool LocalTelemetryRuntime::binds(const Hero12Adapter &a, const CameraManager &m,
                                  const RecordingManager &g) const {
  return &a == &adapter_ && &m == &manager_ && &g == &group_;
}
} // namespace ridesync

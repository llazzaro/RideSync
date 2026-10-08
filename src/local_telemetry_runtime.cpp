#include "local_telemetry_runtime.h"
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
      group_(group), config_(config), power_(power) {}
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
  sd_started_ = true;
  status_.releasable = false;
  status_.phase = TelemetryPhase::Allocating;
  status_.imu = config_.imu_enabled ? SensorAdmission::Pending : SensorAdmission::Disabled;
  return true;
}
void LocalTelemetryRuntime::service() {
  if (!sd_started_ || status_.phase == TelemetryPhase::Finished)
    return;
  status_.identity = sd_.allocation();
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
        bool camera = config_.cameras_qualified && !config_.safe_mode && config_.peers.count &&
                      config_.peers.count <= kMaxCameras;
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
    s.service(); // Sole adapter/group/manager/admission advancement this pass.
  }
  observe();
}
void LocalTelemetryRuntime::requestStop() {
  if (!sd_started_ || status_.phase == TelemetryPhase::Finished ||
      status_.phase == TelemetryPhase::Stopping)
    return;
  status_.phase = TelemetryPhase::Stopping;
  if (active_)
    active_->gps.cancel();
  if (imu_started_)
    imu_.requestStop();
  if (active_ && bound_)
    active_->session.requestStop();
  else
    sd_.cancel(); // Unbound owner only; no producer or Storage queue exists.
}
void LocalTelemetryRuntime::observe() {
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
}
CameraEventSession *LocalTelemetryRuntime::session() {
  return active_ ? &active_->session : nullptr;
}
bool LocalTelemetryRuntime::binds(const Hero12Adapter &a, const CameraManager &m,
                                  const RecordingManager &g) const {
  return &a == &adapter_ && &m == &manager_ && &g == &group_;
}
} // namespace ridesync

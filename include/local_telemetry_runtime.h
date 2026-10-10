#pragma once
#include "application_startup.h"
#include "camera_event_session.h"
#include "gps_manager.h"
#include "imu_manager.h"
#include "session_identity.h"
#include <type_traits>
namespace ridesync {
class HandlebarControl;
// Worker boundaries, not additional loggers. All control methods below run on
// the application owner. Only worker implementations access filesystem/IMU IO.
class TelemetryStorageWorker {
public:
  virtual ~TelemetryStorageWorker() = default;
  virtual bool start() = 0;
  virtual IdentityAllocation allocation() const = 0;
  virtual StorageSink &sink() = 0;
  virtual bool bind(Storage &) = 0;
  virtual void cancel() = 0;
  virtual bool workerFinished() const = 0;
  virtual int ioError() const = 0;
  virtual uint32_t completed() const { return 0; }
};
struct ImuWorkerObservation {
  DeviceHealth outcome = DeviceHealth::Missing;
  uint32_t completed = 0;
  bool finished = false;
  // Read the manager/codec details only after the worker's final access.
  ImuManagerHealth manager;
  ImuCodecHealth codec;
};
class TelemetryImuWorker {
public:
  virtual ~TelemetryImuWorker() = default;
  virtual bool start(ImuInbox &, uint64_t session_id, bool safe_mode) = 0;
  virtual void requestStop() = 0;
  virtual ImuWorkerObservation observation() const = 0;
};
enum class TelemetryPhase { Inactive, Allocating, Running, Stopping, Finished, Refused };
enum class TelemetryFault {
  None,
  Qualification,
  StorageTask,
  Identity,
  Configuration,
  Bind,
  CameraRoute,
  StartupTimeout
};
enum class SensorAdmission { Disabled, Pending, Admitted, SafeModeRefused, TaskRefused };
enum class CameraAdmission { Disabled, Admitted, Refused };
struct LocalTelemetryConfig {
  bool opt_in = false, gps_qualified = false, imu_enabled = false, imu_qualified = false;
  bool safe_mode = false, cameras_qualified = false;
  uint32_t gps_record_ms = 1000;
  bool motion_enabled = false;
  MotionEstimatorConfig motion_config;
  DynamicMotionConfig dynamic_motion;
  uint32_t dynamic_cadence_us = 0;
  DynamicMotionReferenceSource *dynamic_reference = nullptr;
  uint32_t motion_snapshot_max_age_ms = 0;
  ModemConfig modem;
  // Qualified already-powered modem is supported without any GPIO operations.
  QualifiedPowerTiming power_timing;
  // Optional copied GNSS producer only. BLE/command owner services the consumer
  // separately with current control priority. Consumer outlives canRelease().
  GpsSnapshotConsumer *gps_forwarding_consumer = nullptr;
  const char *firmware = nullptr, *provenance = nullptr;
  CameraPeers peers;
};
struct LocalTelemetryStatus {
  TelemetryPhase phase = TelemetryPhase::Inactive;
  bool safe_mode = false;
  TelemetryFault fault = TelemetryFault::None;
  SensorAdmission imu = SensorAdmission::Disabled;
  CameraAdmission camera = CameraAdmission::Disabled;
  MotionAdmission motion_admission = MotionAdmission::Disabled;
  bool motion_current = false;
  MotionEstimate motion_estimate;
  MotionSampleIdentity motion_source;
  IdentityAllocation identity;
  RecordTimestamp timestamp;
  ModemSnapshot gps;
  StorageHealth storage{};
  KindHealth kinds[static_cast<unsigned>(RecordKind::Count)]{};
  ImuWorkerObservation imu_worker;
  uint32_t imu_dropped[5]{}, imu_rejected = 0;
  int storage_error = 0;
  bool storage_worker_finished = false, releasable = true;
  uint32_t at_completed = 0, sd_completed = 0;
  uint8_t worker_stalls = 0, worker_refused = 0;
  bool supervision_fault = false;
  StartupState configuration = StartupState::WaitingConfig;
};
// ONE serialized application owner: start/service/requestStop/status, all GPS,
// clock, camera, admission and future control operations. No application task
// is created. Copy status only on that owner (mailbox it for other contexts).
// Hardware/worker ports, trio, UART and strings outlive canRelease(). No retry
// or terminal Storage reset: each boot/session needs a fresh runtime + SD owner.
class LocalTelemetryRuntime {
public:
  LocalTelemetryRuntime(Clock &, ModemUart &, TelemetryStorageWorker &, TelemetryImuWorker &,
                        CameraRuntimePort &, CameraManager &, RecordingManager &,
                        const LocalTelemetryConfig &, GnssPowerControl *power = nullptr,
                        StaticMotionReferenceSource *source = nullptr);
  ~LocalTelemetryRuntime();
  LocalTelemetryRuntime(const LocalTelemetryRuntime &) = delete;
  LocalTelemetryRuntime &operator=(const LocalTelemetryRuntime &) = delete;
  bool start();
  // Concrete resource admission can refuse before any worker exists. Terminal.
  void refuseStart(TelemetryFault);
  void service();
  void requestStop();
  void withdrawMotionReference();
  LocalTelemetryStatus status() const;
  bool canRelease() const { return status_.releasable; }
  bool binds(const CameraRuntimePort &, const CameraManager &, const RecordingManager &) const;
  // Control-owner access for #41. Never tick managers/admission again after
  // service(). Null before allocation; resources are retained through Finished.
  CameraEventSession *session();
  bool attachControl(HandlebarControl &);
  // Persistent current admission, evaluated before every composed control pass.
  void currentAdmission(bool cameras, bool safe_mode);
  void supervision(uint8_t stalls, uint8_t refused, bool fault);
  void configurationStartup(StartupState s) { status_.configuration = s; }
  void detachControl(HandlebarControl &);

private:
  struct Active {
    CameraEventSession session;
    ModemGnss modem;
    GpsManager gps;
    Active(Clock &, StorageSink &, ModemUart &, CameraRuntimePort &, CameraManager &,
           RecordingManager &, uint64_t, const LocalTelemetryConfig &, GnssPowerControl *,
           StaticMotionReferenceSource *);
  };
  Clock &raw_;
  ModemUart &uart_;
  TelemetryStorageWorker &sd_;
  TelemetryImuWorker &imu_;
  CameraRuntimePort &adapter_;
  CameraManager &manager_;
  RecordingManager &group_;
  const LocalTelemetryConfig config_;
  GnssPowerControl *power_;
  StaticMotionReferenceSource *source_;
  typename std::aligned_storage<sizeof(Active), alignof(Active)>::type memory_;
  Active *active_ = nullptr;
  HandlebarControl *control_ = nullptr;
  LocalTelemetryStatus status_;
  bool started_ = false, sd_started_ = false, bound_ = false, imu_started_ = false;
  bool imu_finished_ = false, logged_ = false, anchored_ = false, servicing_ = false;
  bool current_camera_ = true, current_safe_mode_ = false, session_stop_requested_ = false;
  bool motion_revoked_ = false;
  void revokeMotion();
  uint32_t startup_ms_ = 0;
  uint64_t logged_ms_ = 0, anchored_receipt_ = 0;
  void observe();
};
} // namespace ridesync

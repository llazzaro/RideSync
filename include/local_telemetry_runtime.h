#pragma once
#include "camera_event_session.h"
#include "config_bootstrap.h"
#include "gps_manager.h"
#include "imu_manager.h"
#include "session_identity.h"
#include <type_traits>
namespace ridesync {
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
  CameraRoute
};
enum class SensorAdmission { Disabled, Pending, Admitted, SafeModeRefused, TaskRefused };
enum class CameraAdmission { Disabled, Admitted, Refused };
struct LocalTelemetryConfig {
  bool opt_in = false, gps_qualified = false, imu_enabled = false, imu_qualified = false;
  bool safe_mode = false, cameras_qualified = false;
  uint32_t gps_record_ms = 1000;
  ModemConfig modem;
  // Qualified already-powered modem is supported without any GPIO operations.
  QualifiedPowerTiming power_timing;
  const char *firmware = nullptr, *provenance = nullptr;
  CameraPeers peers;
};
struct LocalTelemetryStatus {
  TelemetryPhase phase = TelemetryPhase::Inactive;
  TelemetryFault fault = TelemetryFault::None;
  SensorAdmission imu = SensorAdmission::Disabled;
  CameraAdmission camera = CameraAdmission::Disabled;
  IdentityAllocation identity;
  RecordTimestamp timestamp;
  ModemSnapshot gps;
  StorageHealth storage{};
  KindHealth kinds[static_cast<unsigned>(RecordKind::Count)]{};
  ImuWorkerObservation imu_worker;
  uint32_t imu_dropped[5]{}, imu_rejected = 0;
  int storage_error = 0;
  bool storage_worker_finished = false, releasable = true;
};
// ONE serialized application owner: start/service/requestStop/status, all GPS,
// clock, camera, admission and future control operations. No application task
// is created. Copy status only on that owner (mailbox it for other contexts).
// Hardware/worker ports, trio, UART and strings outlive canRelease(). No retry
// or terminal Storage reset: each boot/session needs a fresh runtime + SD owner.
class LocalTelemetryRuntime {
public:
  LocalTelemetryRuntime(Clock &, ModemUart &, TelemetryStorageWorker &, TelemetryImuWorker &,
                        Hero12Adapter &, CameraManager &, RecordingManager &,
                        const LocalTelemetryConfig &, GnssPowerControl *power = nullptr);
  ~LocalTelemetryRuntime();
  LocalTelemetryRuntime(const LocalTelemetryRuntime &) = delete;
  LocalTelemetryRuntime &operator=(const LocalTelemetryRuntime &) = delete;
  bool start();
  // Concrete resource admission can refuse before any worker exists. Terminal.
  void refuseStart(TelemetryFault);
  void service();
  void requestStop();
  LocalTelemetryStatus status() const { return status_; }
  bool canRelease() const { return status_.releasable; }
  bool binds(const Hero12Adapter &, const CameraManager &, const RecordingManager &) const;
  // Control-owner access for #41. Never tick managers/admission again after
  // service(). Null before allocation; resources are retained through Finished.
  CameraEventSession *session();

private:
  struct Active {
    CameraEventSession session;
    ModemGnss modem;
    GpsManager gps;
    Active(Clock &, StorageSink &, ModemUart &, Hero12Adapter &, CameraManager &,
           RecordingManager &, uint64_t, const LocalTelemetryConfig &, GnssPowerControl *);
  };
  Clock &raw_;
  ModemUart &uart_;
  TelemetryStorageWorker &sd_;
  TelemetryImuWorker &imu_;
  Hero12Adapter &adapter_;
  CameraManager &manager_;
  RecordingManager &group_;
  const LocalTelemetryConfig config_;
  GnssPowerControl *power_;
  typename std::aligned_storage<sizeof(Active), alignof(Active)>::type memory_;
  Active *active_ = nullptr;
  LocalTelemetryStatus status_;
  bool started_ = false, sd_started_ = false, bound_ = false, imu_started_ = false;
  bool imu_finished_ = false, logged_ = false, anchored_ = false;
  uint64_t logged_ms_ = 0, anchored_receipt_ = 0;
  void observe();
};
} // namespace ridesync

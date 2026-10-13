#pragma once
#include "power_shutdown_report.h"
#include "recording_manager.h"
#if __has_include("camera_health.h")
#define RIDESYNC_POWER_FIXTURE_HEALTH 1
#include "camera_health.h"
#endif
#if __has_include("camera_marker.h")
#define RIDESYNC_POWER_FIXTURE_BOOKMARK 1
#endif
namespace ridesync {
#if defined(RIDESYNC_POWER_FIXTURE_HEALTH)
class CameraHealthTransitionSink {
public:
  virtual ~CameraHealthTransitionSink() = default;
};
class CameraHealthSerialPort {
public:
  virtual ~CameraHealthSerialPort() = default;
  virtual size_t writable() const = 0;
  virtual size_t write(const char *, size_t) = 0;
};
class CameraHealthSerialSink : public CameraHealthTransitionSink {
public:
  explicit CameraHealthSerialSink(CameraHealthSerialPort &) {}
  uint32_t dropped() const { return 0; }
};
inline const char *cameraHealthReasonName(HealthReason) { return "fixture"; }
struct FakeHealth {
  CameraHealthPeer p;
  const CameraHealthPeer *snapshot(size_t) const { return &p; }
  HealthSeverity severity(size_t, HealthReason) const { return HealthSeverity::None; }
};
struct FakeSession {
  FakeHealth evaluator;
  const FakeHealth &health() const { return evaluator; }
};
#endif
extern unsigned marks;
class LocalTelemetryRuntime {
public:
#if defined(RIDESYNC_POWER_FIXTURE_HEALTH)
  FakeSession *session() { return nullptr; }
#endif
#if defined(RIDESYNC_POWER_FIXTURE_BOOKMARK)
  BookmarkReceipt bookmark(BookmarkSource, BookmarkGesture, uint64_t = 0) {
    ++marks;
    return {};
  }
#endif
};
enum class TelemetryPhase { Running, Stopping };
enum class CameraAdmission { Admitted };
struct FakeTelemetryStatus {
  TelemetryPhase phase = TelemetryPhase::Running;
  CameraAdmission camera = CameraAdmission::Admitted;
  unsigned fault = 0, imu = 0, motion_admission = 0, worker_stalls = 0, worker_refused = 0;
  bool safe_mode = false, supervision_fault = false, releasable = false;
  int storage_error = 0;
  struct {
    unsigned validity = 0;
  } gps;
  StorageHealth storage{};
#if defined(RIDESYNC_POWER_FIXTURE_HEALTH)
  struct {
    bool enabled = false, critical = false, warning = false;
    unsigned peer = 0, dropped = 0, coalesced = 0, wire_failures = 0, wire_coalesced = 0;
    HealthReason reason = HealthReason::UnexpectedStop;
  } camera_health;
#endif
  KindHealth kinds[static_cast<unsigned>(RecordKind::Count)]{};
};
struct FakeTelemetry {
  FakeTelemetryStatus s;
  LocalTelemetryRuntime runtime;
  unsigned stops = 0;
  FakeTelemetryStatus status() { return s; }
  void requestStop() { ++stops; }
  LocalTelemetryRuntime &owner() { return runtime; }
};
struct FakeControl {
  struct {
    struct {
#if defined(RIDESYNC_POWER_FIXTURE_BOOKMARK)
      BookmarkReceipt bookmark;
#endif
    } control;
  } s;
  decltype(s) status() const { return s; }
};
struct SupervisedEsp32Application {
  PowerReportSnapshot s;
  FakeTelemetry *t = nullptr;
  unsigned requests = 0;
  FakeTelemetry *telemetry() { return t; }
  FakeControl *control() { return nullptr; }
  bool requestShutdown() {
    ++requests;
    s.policy.sealed = true;
    s.policy.cause = PowerShutdownCause::Manual;
    s.policy.state = PowerShutdownState::Clean;
    s.policy.outcome = PowerShutdownOutcome::Clean;
    ++s.generation;
    return true;
  }
  bool startupReady() { return !s.policy.sealed; }
  struct Status {
    PowerShutdownStatus policy;
    PowerObservation observation;
    PowerBackendStatus backend;
    StorageHealth storage{};
    unsigned telemetry_fault = 0;
    uint32_t uncertain_rows = 0, report_generation = 0;
    int32_t reset_reason = 0, storage_error = 0;
    bool startup_refused = false;
  };
  Status powerStatus() const {
    Status out;
    out.policy = s.policy;
    out.observation = s.observation;
    out.backend = s.backend;
    out.storage = s.storage;
    out.storage_error = s.storage_error;
    out.report_generation = s.generation;
    out.reset_reason = s.reset_reason;
    return out;
  }
};
extern SupervisedEsp32Application *application;
struct Provider {
  bool supplied = true, configured = true, spi_initialized = false, i2c_initialized = false;
  int error = 0;
};
inline Provider benchApplicationStatus() { return {}; }
inline SupervisedEsp32Application *benchApplicationOwner() { return application; }
extern unsigned stationary, marks;
inline bool benchArmStationaryReference() {
  ++stationary;
  return true;
}

} // namespace ridesync

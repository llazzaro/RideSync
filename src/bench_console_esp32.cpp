#if defined(ARDUINO_ARCH_ESP32) && defined(RIDESYNC_BENCH_APPLICATION)
#include "bench_application.h"
#include "bench_console.h"
#include "power_shutdown_report.h"
#include "profiles/mixed_camera_esp32.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
namespace ridesync {
namespace {
class ArduinoHealthPort final : public CameraHealthSerialPort {
public:
  size_t writable() const override { return static_cast<size_t>(Serial.availableForWrite()); }
  size_t write(const char *s, size_t n) override {
    return Serial.write(reinterpret_cast<const uint8_t *>(s), n);
  }
} health_port;
CameraHealthSerialSink health_serial(health_port);
BenchConsole console;
bool waiting = false;
bool clear_requested = false;
BenchCommand current_command = BenchCommand::Status;
PowerReportBuffer power_report;
uint32_t power_report_generation = 0;
bool ordinary_report_requested = false;
void reportDetails();
void capturePowerReport(SupervisedEsp32Application *application) {
  PowerReportSnapshot snapshot;
  if (application) {
    const auto status = application->powerStatus();
    snapshot.available = true;
    snapshot.policy = status.policy;
    snapshot.observation = status.observation;
    snapshot.backend = status.backend;
    snapshot.storage = status.storage;
    snapshot.storage_error = status.storage_error;
    snapshot.telemetry_fault = unsigned(status.telemetry_fault);
    snapshot.uncertain_rows = status.uncertain_rows;
    snapshot.generation = status.report_generation;
    snapshot.reset_reason = status.reset_reason;
    snapshot.startup_refused = status.startup_refused;
  }
  power_report.capture(snapshot);
}
void writePowerReport() {
  const int available = Serial.availableForWrite();
  if (available <= 0 || !power_report.pending())
    return;
  size_t bytes = power_report.pending();
  if (bytes > static_cast<size_t>(available))
    bytes = static_cast<size_t>(available);
  if (bytes > 64)
    bytes = 64;
  const auto written = Serial.write(reinterpret_cast<const uint8_t *>(power_report.data()), bytes);
  power_report.consume(written < bytes ? written : bytes);
}
void servicePowerReport() {
  auto *application = benchApplicationOwner();
  if (application) {
    const auto generation = application->powerStatus().report_generation;
    if (generation != power_report_generation) {
      power_report_generation = generation;
      capturePowerReport(application);
    }
  }
  writePowerReport();
  if (!power_report.pending() && ordinary_report_requested) {
    ordinary_report_requested = false;
    reportDetails();
  }
}
const char *observed(RecordingState state) {
  return state == RecordingState::Recording ? "Recording"
         : state == RecordingState::Stopped ? "Stopped"
                                            : "Unknown";
}
void report() {
  auto *application = benchApplicationOwner();
  capturePowerReport(application);
  const auto power = application ? application->powerStatus().policy : PowerShutdownStatus{};
  ordinary_report_requested = !application || (!power.enabled && !power.sealed);
}
void reportDetails() {
  const auto provider = benchApplicationStatus();
  auto *application = benchApplicationOwner();
  if (application) {
    const auto power = application->powerStatus().policy;
    if (power.enabled || power.sealed)
      return; // Protected mode and sealed drains require only bounded power evidence.
  }
  Serial.printf("BENCH provider=%u configured=%u provider_error=%u spi=%u i2c=%u\n",
                unsigned(provider.supplied), unsigned(provider.configured),
                unsigned(provider.error), unsigned(provider.spi_initialized),
                unsigned(provider.i2c_initialized));
  Serial.printf(
      "BENCH internal_free=%u internal_largest=%u internal_minimum=%u loop_stack_words=%u\n",
      unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(uxTaskGetStackHighWaterMark(nullptr)));
  if (!application || !application->telemetry())
    return;
  const auto status = application->telemetry()->status();
  const auto &health = status.camera_health;
  Serial.printf(
      "HEALTH enabled=%u critical=%u warning=%u highest_peer=%u reason=%s "
      "dropped=%lu coalesced=%lu wire_coalesced=%lu wire_failures=%lu serial_dropped=%lu\n",
      unsigned(health.enabled), unsigned(health.critical), unsigned(health.warning),
      unsigned(health.peer), cameraHealthReasonName(health.reason),
      static_cast<unsigned long>(health.dropped), static_cast<unsigned long>(health.coalesced),
      static_cast<unsigned long>(health.wire_coalesced),
      static_cast<unsigned long>(health.wire_failures),
      static_cast<unsigned long>(health_serial.dropped()));
  Serial.printf(
      "BENCH phase=%u fault=%u camera=%u gps_validity=%u imu=%u motion=%u storage_error=%d "
      "sd_dropped=%u sd_lost=%u stalls=%u refused=%u releasable=%u\n",
      unsigned(status.phase), unsigned(status.fault), unsigned(status.camera),
      unsigned(status.gps.validity), unsigned(status.imu), unsigned(status.motion_admission),
      status.storage_error, status.storage.dropped, status.storage.lost,
      unsigned(status.worker_stalls), unsigned(status.worker_refused), unsigned(status.releasable));
  const auto &bookmarks = status.kinds[static_cast<unsigned>(RecordKind::Bookmark)];
  Serial.printf("BENCH bookmarks accepted=%u dropped=%u rejected=%u written=%u flushed=%u lost=%u; "
                "admission is RAM acceptance, not durability.\n",
                bookmarks.accepted, bookmarks.dropped, bookmarks.rejected, bookmarks.written,
                bookmarks.flushed, bookmarks.lost);
  if (application->control()) {
    const auto control = application->control()->status();
    Serial.printf("BENCH inputs record_ready=%u mark_ready=%u\n", unsigned(control.input_ready),
                  unsigned(control.mark_input_ready));
    const auto marker = control.control.bookmark;
    Serial.printf("BENCH physical_marker outcome=%u session=%llu id=%u monotonic_ms=%llu\n",
                  unsigned(marker.outcome), static_cast<unsigned long long>(marker.session_id),
                  marker.bookmark_id,
                  static_cast<unsigned long long>(marker.timestamp.monotonic_ms));
  }
  const auto runtime = mixedCameraRuntime();
  const auto group = runtime.group.status();
  Serial.printf("BENCH intent=%s enabled=%u ready=%u observed_recording=%u observed_stopped=%u "
                "unknown=%u pending=%u errors=%u generation=%u\n",
                observed(group.intent), unsigned(group.enabled), unsigned(group.ready),
                unsigned(group.recording), unsigned(group.stopped), unsigned(group.unknown),
                unsigned(group.pending), unsigned(group.errors), group.generation);
  for (size_t i = 0; i < runtime.manager.size(); ++i) {
    const auto &state = *runtime.manager.state(i);
    const auto &peer = group.peers[i];
    Serial.printf("BENCH peer=%u model=%u lifecycle=%u observed=%s acknowledged=%u pending=%u "
                  "error=%u adapter_fault=%u query=%u wake=%u gps=%u hilight=%u\n",
                  unsigned(i), unsigned(runtime.manager.configuredCamera(i)->model),
                  unsigned(state.lifecycle), observed(state.observed), unsigned(peer.acknowledged),
                  unsigned(peer.pending), unsigned(peer.error), unsigned(runtime.adapter.fault(i)),
                  unsigned(state.capabilities.query), unsigned(state.capabilities.wake),
                  unsigned(state.capabilities.gps), unsigned(state.capabilities.hilight));
    auto *session = application->telemetry()->owner().session();
    if (health.enabled && session) {
      const auto *current = session->health().snapshot(i);
      const auto &recording = current->fields[static_cast<size_t>(HealthField::Recording)];
      Serial.printf("HEALTH peer=%u episode=%lu current_recording=%s validity=%u historical=%u\n",
                    unsigned(i), static_cast<unsigned long>(current->episode),
                    observed(recording.validity == HealthValidity::Valid
                                 ? static_cast<RecordingState>(recording.value)
                                 : RecordingState::Unknown),
                    unsigned(recording.validity), unsigned(recording.historical));
      for (size_t f = 0; f < static_cast<size_t>(HealthField::Count); ++f) {
        const auto &field = current->fields[f];
        Serial.printf("HEALTH peer=%u field=%u capability=%u validity=%u value=%llu unit=%u "
                      "historical=%u receipt_ms=%lu\n",
                      unsigned(i), unsigned(f), unsigned(field.capability),
                      unsigned(field.validity), static_cast<unsigned long long>(field.value),
                      unsigned(field.unit), unsigned(field.historical),
                      static_cast<unsigned long>(field.received_ms));
      }
      for (size_t r = 0; r < static_cast<size_t>(HealthReason::Count); ++r) {
        const auto reason = static_cast<HealthReason>(r);
        const auto severity = session->health().severity(i, reason);
        if (severity != HealthSeverity::None || current->alerts[r] != HealthSeverity::None)
          Serial.printf("HEALTH peer=%u reason=%s current_severity=%u historical_severity=%u\n",
                        unsigned(i), cameraHealthReasonName(reason), unsigned(severity),
                        unsigned(current->alerts[r]));
      }
    }
  }
}
bool admitted(SupervisedEsp32Application *application) {
  if (!application || !application->telemetry())
    return false;
  const auto power = application->powerStatus().policy;
  if (power.sealed || (power.enabled && !power.admission_allowed))
    return false;
  const auto state = application->telemetry()->status();
  return state.phase == TelemetryPhase::Running && state.camera == CameraAdmission::Admitted &&
         !state.safe_mode && !state.supervision_fault && !state.worker_stalls &&
         !state.worker_refused;
}
void executePeers(BenchCommand command, const MixedCameraRuntime &runtime, unsigned &accepted,
                  unsigned &refused) {
  for (size_t i = 0; i < runtime.manager.size(); ++i) {
    if (!runtime.manager.configuredCamera(i)->enabled)
      continue;
    const auto error = command == BenchCommand::Connect
                           ? runtime.manager.request(i, Operation::Connect)
                           : runtime.adapter.requestRecovery(i, false);
    accepted += error == CameraError::None;
    refused += error != CameraError::None;
    if (error != CameraError::None)
      Serial.printf("BENCH admission peer=%u error=%u\n", unsigned(i), unsigned(error));
  }
}
void executeCameraCommand(BenchCommand command, const MixedCameraRuntime &runtime,
                          unsigned &accepted, unsigned &refused) {
  if (command == BenchCommand::Record || command == BenchCommand::Stop ||
      command == BenchCommand::Query) {
    const auto result =
        command == BenchCommand::Query
            ? runtime.group.resync()
            : runtime.group.request(command == BenchCommand::Record ? RecordingState::Recording
                                                                    : RecordingState::Stopped);
    accepted = result == GroupError::None;
    refused = !accepted;
  } else {
    executePeers(command, runtime, accepted, refused);
  }
}
void execute(BenchCommand command) {
  auto *application = benchApplicationOwner();
  if (command == BenchCommand::Status) {
    report();
    return;
  }
  if (command == BenchCommand::Shutdown) {
    if (application) {
      waiting = application->requestShutdown();
      current_command = command;
      capturePowerReport(application);
    } else {
      capturePowerReport(nullptr);
    }
    return;
  }
  if (application && application->powerStatus().policy.sealed) {
    capturePowerReport(application);
    return; // No CLEAR, stationarity, bookmark or camera intent after sealing.
  }
  if (command == BenchCommand::Clear) {
    clear_requested = true;
    Serial.println("BENCH safe-mode clear requested; retired camera admission is not reopened.");
    return;
  }
  if (command == BenchCommand::Stationary) {
    Serial.printf("BENCH stationary declaration admitted=%u; next qualified sample initializes "
                  "experimental motion.\n",
                  unsigned(benchArmStationaryReference()));
    return;
  }
  if (command == BenchCommand::Mark) {
    auto *telemetry = application ? application->telemetry() : nullptr;
    const auto marker = submitBenchBookmark(telemetry ? &telemetry->owner() : nullptr);
    Serial.printf("BENCH marker outcome=%u session=%llu id=%u monotonic_ms=%llu; "
                  "admitted=%u means RAM queue acceptance, not durable storage or camera tag.\n",
                  unsigned(marker.outcome), static_cast<unsigned long long>(marker.session_id),
                  marker.bookmark_id,
                  static_cast<unsigned long long>(marker.timestamp.monotonic_ms),
                  unsigned(marker.outcome == BookmarkOutcome::Admitted));
    return;
  }
  if (!admitted(application)) {
    Serial.println("BENCH command refused: commissioned running camera owner required.");
    report();
    return;
  }
  const auto runtime = mixedCameraRuntime();
  unsigned accepted = 0, refused = 0;
  executeCameraCommand(command, runtime, accepted, refused);
  Serial.printf("BENCH command=%u accepted=%u refused=%u; acceptance is not recording proof.\n",
                unsigned(command), accepted, refused);
  waiting = accepted != 0;
  current_command = command;
  report();
}
bool commandsFinished(const MixedCameraRuntime &runtime) {
  bool done;
  done = runtime.group.status().pending == 0;
  for (size_t i = 0; done && i < runtime.manager.size(); ++i) {
    const auto state = runtime.manager.state(i)->lifecycle;
    done = state != Lifecycle::Connecting && state != Lifecycle::Operating &&
           state != Lifecycle::Backoff;
    if (current_command == BenchCommand::Wake) {
      const auto recovery = runtime.adapter.recoveryState(i).phase;
      done = done && recovery != Hero12RecoveryPhase::Pending &&
             recovery != Hero12RecoveryPhase::Scanning &&
             recovery != Hero12RecoveryPhase::Connecting &&
             recovery != Hero12RecoveryPhase::Observing;
    }
  }
  return done;
}
void terminal() {
  if (!waiting)
    return;
  auto *application = benchApplicationOwner();
  if (application && application->powerStatus().policy.sealed) {
    if (application->powerStatus().policy.outcome != PowerShutdownOutcome::None)
      waiting = false;
    return; // Transition report is bounded and owns power-shutdown terminal evidence.
  }
  if (!application || !application->telemetry()) {
    waiting = false;
    return;
  }
  const auto status = application->telemetry()->status();
  bool done = status.phase != TelemetryPhase::Running;
  if (current_command == BenchCommand::Shutdown)
    done = status.releasable;
  else if (!done) {
    const auto runtime = mixedCameraRuntime();
    done = commandsFinished(runtime);
  }
  if (done) {
    Serial.printf("BENCH terminal command=%u; inspect errors and camera observations.\n",
                  unsigned(current_command));
    report();
    waiting = false;
  }
}
} // namespace
CameraHealthTransitionSink *benchHealthSink() { return &health_serial; }
uint32_t benchHealthSerialDropped() { return health_serial.dropped(); }
void benchConsoleService() {
  servicePowerReport();
  // One bounded line slot, no buffered command replay. Drain the current burst
  // before dispatch so two complete pasted commands lose intent admission.
  for (unsigned i = 0; i < 64 && Serial.available(); ++i)
    console.push(static_cast<uint8_t>(Serial.read()));
  if (Serial.available()) {
    // Continue draining in bounded slices before any intent can be admitted.
    terminal();
    return;
  }
  BenchCommand command;
  BenchConsoleError error;
  if (console.take(command, error)) {
    if (error == BenchConsoleError::None)
      execute(command);
    else {
      auto *application = benchApplicationOwner();
      if (application && application->powerStatus().policy.sealed)
        capturePowerReport(application);
      else
        Serial.printf("BENCH input refused error=%u; send one complete uppercase command.\n",
                      unsigned(error));
    }
  }
  terminal();
  servicePowerReport();
}
bool benchConsoleTakeClear() {
  const bool requested = clear_requested;
  clear_requested = false;
  return requested;
}
} // namespace ridesync
#endif

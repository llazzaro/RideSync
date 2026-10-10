#if defined(ARDUINO_ARCH_ESP32) && defined(RIDESYNC_BENCH_APPLICATION)
#include "bench_application.h"
#include "bench_console.h"
#include "profiles/mixed_camera_esp32.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
namespace ridesync {
namespace {
BenchConsole console;
bool waiting = false;
bool clear_requested = false;
BenchCommand current_command = BenchCommand::Status;
const char *observed(RecordingState state) {
  return state == RecordingState::Recording ? "Recording"
         : state == RecordingState::Stopped ? "Stopped"
                                            : "Unknown";
}
void report() {
  const auto provider = benchApplicationStatus();
  auto *application = benchApplicationOwner();
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
  Serial.printf(
      "BENCH phase=%u fault=%u camera=%u gps_validity=%u imu=%u motion=%u storage_error=%d "
      "sd_dropped=%u sd_lost=%u stalls=%u refused=%u releasable=%u\n",
      unsigned(status.phase), unsigned(status.fault), unsigned(status.camera),
      unsigned(status.gps.validity), unsigned(status.imu), unsigned(status.motion_admission),
      status.storage_error, status.storage.dropped, status.storage.lost,
      unsigned(status.worker_stalls), unsigned(status.worker_refused), unsigned(status.releasable));
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
                  "error=%u adapter_fault=%u query=%u wake=%u gps=%u\n",
                  unsigned(i), unsigned(runtime.manager.configuredCamera(i)->model),
                  unsigned(state.lifecycle), observed(state.observed), unsigned(peer.acknowledged),
                  unsigned(peer.pending), unsigned(peer.error), unsigned(runtime.adapter.fault(i)),
                  unsigned(state.capabilities.query), unsigned(state.capabilities.wake),
                  unsigned(state.capabilities.gps));
  }
}
bool admitted(SupervisedEsp32Application *application) {
  if (!application || !application->telemetry())
    return false;
  const auto state = application->telemetry()->status();
  return state.phase == TelemetryPhase::Running && state.camera == CameraAdmission::Admitted &&
         !state.safe_mode && !state.supervision_fault && !state.worker_stalls &&
         !state.worker_refused;
}
void execute(BenchCommand command) {
  auto *application = benchApplicationOwner();
  if (command == BenchCommand::Status) {
    report();
    return;
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
  if (command == BenchCommand::Shutdown) {
    if (application && application->telemetry()) {
      application->telemetry()->requestStop();
      waiting = true;
      current_command = command;
      Serial.println("BENCH shutdown requested; wait for releasable=1 before removing power.");
    } else {
      Serial.println("BENCH command refused: application unavailable.");
    }
    return;
  }
  if (!admitted(application)) {
    Serial.println("BENCH command refused: commissioned running camera owner required.");
    report();
    return;
  }
  const auto runtime = mixedCameraRuntime();
  unsigned accepted = 0, refused = 0;
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
  Serial.printf("BENCH command=%u accepted=%u refused=%u; acceptance is not recording proof.\n",
                unsigned(command), accepted, refused);
  waiting = accepted != 0;
  current_command = command;
  report();
}
void terminal() {
  if (!waiting)
    return;
  auto *application = benchApplicationOwner();
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
  }
  if (done) {
    Serial.printf("BENCH terminal command=%u; inspect errors and camera observations.\n",
                  unsigned(current_command));
    report();
    waiting = false;
  }
}
} // namespace
void benchConsoleService() {
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
    else
      Serial.printf("BENCH input refused error=%u; send one complete uppercase command.\n",
                    unsigned(error));
  }
  terminal();
}
bool benchConsoleTakeClear() {
  const bool requested = clear_requested;
  clear_requested = false;
  return requested;
}
} // namespace ridesync
#endif

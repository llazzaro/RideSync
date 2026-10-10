#include "profiles/insta360_x5_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "nvs_boot_guard.h"
#if defined(RIDESYNC_X5_WAKE_MILESTONE)
#include "x5_wake_esp32.h"
#endif
#if defined(RIDESYNC_X5_STORE_INSPECT)
#include "ble_esp32.h"
#endif
#include "pairing_proof_esp32.h"
#include "x5_serial_control.h"
#include <Arduino.h>
#include <freertos/task.h>
#if defined(RIDESYNC_X5_PRIVATE_HEADER)
#include RIDESYNC_X5_PRIVATE_HEADER
#define RIDESYNC_HAS_X5_PROVIDER 1
#elif defined(__has_include) && !defined(RIDESYNC_X5_NO_PRIVATE_HEADER)
#if __has_include("x5_commissioning.local.h")
#include "x5_commissioning.local.h"
#define RIDESYNC_HAS_X5_PROVIDER 1
#endif
#endif
extern "C" ridesync::X5PeripheralPort *ridesync_x5_peripheral_backend();
namespace ridesync {
namespace {
class ArduinoClock final : public Clock {
public:
  uint32_t now() const override { return millis(); }
};
std::atomic<bool> proof_ready{false};
bool begun = false, finalized = false, provider_present = false;
uint32_t begun_ms = 0;
X5Qualification qualification;
SourceConfig source;
#if defined(RIDESYNC_X5_WAKE_MILESTONE)
WakePeerConfig wake_config;
WakePolicy wake_policy;
bool wake_provider_present = false;
X5WakeControl &wakeControl() {
  static ArduinoClock clock;
  static X5WakeControl control(x5Runtime(), clock, x5WakeRadio());
  return control;
}
#endif
void proofTask(void *) {
  pairingProofMaintenance().beginOwner(); // The only NVS configuration owner in this image.
#if defined(RIDESYNC_X5_STORE_INSPECT)
  const auto observation = Esp32BleHost::instance().inspectStore(false);
  Serial.printf("X5_STORE complete=%u error=%d counts=", unsigned(observation.complete),
                observation.error);
  for (unsigned i = 0; i < observation.snapshot.counts.size(); ++i)
    Serial.printf("%s%u", i ? "," : "", observation.snapshot.counts[i]);
  Serial.printf(" digest=");
  for (auto byte : observation.snapshot.digest)
    Serial.printf("%02x", unsigned(byte));
  Serial.println("");
#endif
  proof_ready.store(true, std::memory_order_release);
  // Retain the owner; no task deletion or later proof mutations in this milestone.
  for (;;)
    vTaskDelay(pdMS_TO_TICKS(100));
}
const char *recordingName(RecordingState state) {
  return state == RecordingState::Recording ? "Recording"
         : state == RecordingState::Stopped ? "Stopped"
                                            : "Unknown";
}
const char *failureName(X5Failure failure) {
  const char *names[] = {"None",         "Disabled",     "Qualification", "Busy",     "Host",
                         "Subscription", "UnknownState", "WrongMode",     "Stale",    "Timeout",
                         "Transport",    "Cancelled",    "LostInput",     "Exhausted"};
  const auto n = static_cast<unsigned>(failure);
  return n < sizeof names / sizeof *names ? names[n] : "Invalid";
}
void report(const X5RuntimeStatus &s, const char *action, uint32_t rejected) {
  Serial.printf("X5 id=%lu action=%s configured=%u revoked=%u link=%u subscribed=%u active=%u "
                "observed=%s age_known=%u age_ms=%lu lifecycle=%u error=%u failure=%s last_id=%lu "
                "last_error=%u last_refusal=%s rejected=%lu\n",
                static_cast<unsigned long>(s.request_id), action, unsigned(s.configured),
                unsigned(s.revoked), unsigned(s.connected), unsigned(s.subscribed),
                unsigned(s.active), recordingName(s.observed), unsigned(s.age_known),
                static_cast<unsigned long>(s.age_ms), unsigned(s.lifecycle), unsigned(s.error),
                failureName(s.failure), static_cast<unsigned long>(s.last_request_id),
                unsigned(s.last_request_error), failureName(s.last_request_refusal),
                static_cast<unsigned long>(rejected));
}
class SerialPort final : public X5CommandPort {
public:
  CameraError request(Operation op) override {
#if defined(RIDESYNC_X5_WAKE_MILESTONE)
    return wakeControl().command(op);
#else
    return x5Runtime().request(op);
#endif
  }
  void disconnect() override {
#if defined(RIDESYNC_X5_WAKE_MILESTONE)
    wakeControl().cancel();
#endif
    x5Runtime().disconnect();
  }
  void status() override {} // The caller reports one copied snapshot after dispatch.
};
X5SerialControl &serialControl() {
  // Construct only when this milestone is serviced. A global port/parser would
  // root its virtual methods and retain the entire X5 backend in other images.
  static SerialPort port;
  static X5SerialControl serial(port);
  return serial;
}
#if defined(RIDESYNC_X5_WAKE_MILESTONE)
WakeStatus previous_wake;
#endif
X5RuntimeStatus previous;
bool have_previous = false;
bool changed(const X5RuntimeStatus &a, const X5RuntimeStatus &b) {
  return a.request_id != b.request_id || a.last_request_id != b.last_request_id ||
         a.last_request_error != b.last_request_error ||
         a.last_request_refusal != b.last_request_refusal || a.connected != b.connected ||
         a.subscribed != b.subscribed || a.active != b.active || a.observed != b.observed ||
         a.age_known != b.age_known || a.lifecycle != b.lifecycle || a.error != b.error ||
         a.failure != b.failure || a.revoked != b.revoked || a.configured != b.configured;
}
} // namespace
X5Runtime &x5Runtime() {
  static ArduinoClock clock;
  static X5Runtime runtime(*ridesync_x5_peripheral_backend(), clock);
  return runtime;
}
void x5MilestoneBegin(bool safe_mode) {
  if (begun)
    return;
  begun = true;
  begun_ms = millis();
#ifdef RIDESYNC_HAS_X5_PROVIDER
  provider_present = ridesyncPrivateX5Qualification(qualification, source);
#if defined(RIDESYNC_X5_WAKE_MILESTONE)
  wake_provider_present = ridesyncPrivateX5WakeConfig(wake_config, wake_policy);
#endif
#endif
#if defined(RIDESYNC_X5_STORE_INSPECT)
  provider_present = true; // Read-only diagnostic, never commissions a camera.
#endif
  if (!provider_present || safe_mode || !nvsBootStatus().persistenceAllowed()) {
    finalized = true;
    x5Runtime().revoke();
    Serial.println("X5 commissioning refused: missing private provider, safe mode or NVS fault.");
    return;
  }
  TaskHandle_t task = nullptr;
  if (xTaskCreate(proofTask, "x5_config", 12288, nullptr, 1, &task) != pdPASS) {
    finalized = true;
    x5Runtime().revoke();
    Serial.println("X5 commissioning refused: configuration owner unavailable.");
  }
#if defined(RIDESYNC_X5_STORE_INSPECT)
  finalized = true;
  x5Runtime().revoke();
  Serial.println("X5 private store inspection only; camera commands disabled.");
#else
  Serial.println("X5 commands: CONNECT REC STOP QUERY STATUS DISCONNECT WAKE. Boot is idle.");
#endif
}
void x5MilestoneService(bool admission_allowed) {
  if (!begun)
    return;
  auto &runtime = x5Runtime();
  if (!admission_allowed || !nvsBootStatus().persistenceAllowed())
    runtime.revoke();
  if (!finalized) {
    // Bound handshake acceptance before reading a possibly late ready publication.
    if (millis() - begun_ms >= 1000 || runtime.status().revoked) {
      runtime.revoke();
      finalized = true;
      Serial.println("X5 commissioning refused: owner deadline/admission.");
    } else if (proof_ready.load(std::memory_order_acquire)) {
      finalized = true;
      if (!runtime.configure(qualification, source))
        Serial.println("X5 commissioning refused: qualification/profile mismatch.");
#if defined(RIDESYNC_X5_WAKE_MILESTONE)
      if (!wake_provider_present || !wakeControl().configure(wake_config, wake_policy))
        Serial.println("X5 wake commissioning refused: missing or unqualified wake provider.");
#endif
    }
  }
  runtime.service(); // Deadline/receive progress precedes serial dispatch.
#if defined(RIDESYNC_X5_WAKE_MILESTONE)
  wakeControl().service();
#endif
  auto &serial = serialControl();
  for (unsigned n = 0; n < 32 && Serial.available(); ++n)
    serial.consume(static_cast<char>(Serial.read()));
  const auto command = serial.service();
  const auto snapshot = runtime.status();
  bool wake_changed = false;
#if defined(RIDESYNC_X5_WAKE_MILESTONE)
  const auto wake = wakeControl().status();
  wake_changed = wake.operation.id != previous_wake.operation.id ||
                 wake.phase != previous_wake.phase || wake.error != previous_wake.error ||
                 wake.released != previous_wake.released || wake.observed != previous_wake.observed;
#endif
  if (command != X5SerialCommand::None || !have_previous || changed(snapshot, previous) ||
      wake_changed) {
    const char *actions[] = {"event",  "CONNECT",    "REC",  "STOP",        "QUERY",
                             "STATUS", "DISCONNECT", "WAKE", "invalid-line"};
    report(snapshot, actions[static_cast<unsigned>(command)], serial.rejected());
#if defined(RIDESYNC_X5_WAKE_MILESTONE)
    previous_wake = wake;
    Serial.printf("X5_WAKE id=%lu phase=%u error=%u released=%u observed=%s command_error=%u\n",
                  static_cast<unsigned long>(wake.operation.id), unsigned(wake.phase),
                  unsigned(wake.error), unsigned(wake.released), recordingName(wake.observed),
                  unsigned(serial.error()));
#endif
    previous = snapshot;
    have_previous = true;
  }
}
} // namespace ridesync
extern "C" ridesync::X5Runtime *ridesync_x5_runtime() { return &ridesync::x5Runtime(); }
#endif

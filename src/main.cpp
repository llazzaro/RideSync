#include "config_storage.h"
#include "health_supervisor.h"
#include "nvs_boot_guard.h"
#include "status_led.h"
#include <Arduino.h>
#include <cstring>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
using namespace ridesync;
// Raw bytes avoid C++ constructors initializing RTC_NOINIT on every warm boot.
RTC_NOINIT_ATTR uint8_t retained_boot[sizeof(BootRecord)];
HealthSupervisor supervisor;
// Source-only wiring profile remains disabled until board/electrical qualification.
Esp32LedGpio led_gpio;
GpioLedSink led_sink(led_gpio);
StatusLed status_led(led_sink);
std::atomic<bool> supervision_fault{false};
BootRecovery recovery;
std::atomic<bool> clear_safe_mode{false}, safe_mode{false};
std::atomic<WatchdogState> watchdog_state{WatchdogState::NotStarted};
std::atomic<int> watchdog_error{0};
std::atomic<uint8_t> stalled_workers{0};

// One explicit owning service isolates blocking NVS calls from control loops.
// No SDK init/deinit/erase may run while this owner is active. Future lifecycle
// work must stop/quiesce it first. Configuration requests belong in a bounded
// owner mailbox; no UI or control producer is admitted in this milestone.
NvsConfigStore config_store;
ConfigPersistence config_persistence(config_store);
SourceConfig ram_settings; // owner-private; complete settings only, no runtime intent
std::atomic<PersistStatus> config_status{PersistStatus::Defaults};
std::atomic<int32_t> config_error{0};
void configTask(void *) {
  auto result = safe_mode.load() ? PersistResult{PersistStatus::Refused}
                                 : config_persistence.load(ram_settings);
  config_error.store(result.code);
  config_status.store(result.status);
  TickType_t next = xTaskGetTickCount();
  for (;;) {
    // service() checks the SDK gate on every operation and drops pending work
    // after refusal. No automatic default/migration requests or driver begin.
    result = config_persistence.service(millis());
    config_error.store(result.code);
    config_status.store(result.status);
    vTaskDelayUntil(&next, pdMS_TO_TICKS(100));
  }
}

class SdkWatchdog : public WatchdogPort {
public:
  Subscription status() override {
    switch (esp_task_wdt_status(nullptr)) {
    case ESP_OK:
      return Subscription::Present;
    case ESP_ERR_NOT_FOUND:
      return Subscription::Missing;
    case ESP_ERR_INVALID_STATE:
      return Subscription::Uninitialized;
    default:
      return Subscription::Error;
    }
  }
  int addCurrent() override { return esp_task_wdt_add(nullptr); }
  int feedCurrent() override { return esp_task_wdt_reset(); }
  int removeCurrent() override { return esp_task_wdt_delete(nullptr); }
};
ResetClass resetClass(esp_reset_reason_t reason) {
  switch (reason) {
  case ESP_RST_POWERON:
    return ResetClass::Cold;
  case ESP_RST_BROWNOUT:
    return ResetClass::Brownout;
  case ESP_RST_PANIC:
    return ResetClass::Panic;
  case ESP_RST_INT_WDT:
  case ESP_RST_TASK_WDT:
  case ESP_RST_WDT:
    return ResetClass::Watchdog;
  case ESP_RST_SW:
    return ResetClass::Software;
  case ESP_RST_EXT:
    return ResetClass::Operator;
  case ESP_RST_DEEPSLEEP:
    return ResetClass::DeepSleep;
  default:
    return ResetClass::Unknown;
  }
}
void retainBoot() { std::memcpy(retained_boot, &recovery.record(), sizeof(BootRecord)); }
void healthTask(void *) {
  SdkWatchdog sdk;
  HealthWatchdog watchdog(sdk);
  if (!watchdog.begin()) {
    watchdog_error.store(watchdog.error());
    watchdog_state.store(watchdog.state());
    if (watchdog.state() != WatchdogState::ExistingSubscription) {
      vTaskDelete(nullptr); // Current task is not subscribed on these failed paths.
      return;
    }
    // Foreign subscription: keep its task alive, never adopt/delete/feed it.
    // Preserve the SDK's failure escalation rather than leave a dangling handle.
  }
  watchdog_state.store(watchdog.state());
  TickType_t next = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    if (clear_safe_mode.exchange(false)) {
      recovery.operatorClear(now);
      retainBoot();
      safe_mode.store(false);
    }
    // Fixed, lock-free snapshots; no UART/BLE/FS calls or manager mutations.
    const auto decision = supervisor.evaluate(now);
    stalled_workers.store(decision.stalled);
    if (recovery.execution(now, decision.stable_candidate))
      retainBoot();
    watchdog.service(decision);
    watchdog_error.store(watchdog.error());
    watchdog_state.store(watchdog.state());
    vTaskDelayUntil(&next, pdMS_TO_TICKS(HealthSupervisor::kCadenceMs));
  }
}
} // namespace

void setup() {
  Serial.begin(115200);
  led_sink.begin(LedWiring{}, false);
  const auto sdk_reason = esp_reset_reason(); // Capture once; never overwrite SDK hints.
  BootRecord retained;
  std::memcpy(&retained, retained_boot, sizeof(retained));
  const auto boot = recovery.begin(retained, resetClass(sdk_reason), millis());
  safe_mode.store(boot.safe_mode);
  retainBoot();
  Serial.printf("RideSync: sdk_reset=%u app_cause=%u retention_valid=%u safe_mode=%u\n",
                static_cast<unsigned>(sdk_reason), static_cast<unsigned>(boot.previous_app_cause),
                static_cast<unsigned>(boot.retention_valid), static_cast<unsigned>(boot.safe_mode));
  const auto nvs = nvsBootStatus();
  const bool config_ble_admission = !boot.safe_mode && nvs.persistenceAllowed();
  Serial.printf("RideSync: nvs_observed=%u nvs_first_failure=%d nvs_last_result=%d "
                "nvs_format_refused=%u nvs_refusal_error=%d config_ble_admission=%u\n",
                static_cast<unsigned>(nvs.init_observed), nvs.first_init_failure,
                nvs.last_init_result, static_cast<unsigned>(nvs.format_refused), nvs.refusal_error,
                static_cast<unsigned>(config_ble_admission));
  if (!config_ble_admission)
    Serial.println("RideSync: config/BLE blocked; diagnostic/RAM mode; no save/format/retry.");
  if (config_ble_admission &&
      xTaskCreatePinnedToCore(configTask, "config", 12288, nullptr, 1, nullptr, 1) != pdPASS)
    Serial.println("RideSync: config task creation failed; RAM defaults only.");
  // No BLE driver exists yet; future BLE owner needs independent restoration
  // evidence and overflow refusal as well as SDK admission.
  // NVS failure is device/config health, never a fabricated worker stall. Qualified
  // standalone GNSS/SD/IMU lifetimes must remain independent of camera/NVS readiness.
  Serial.println("Optional AT/BLE/SD/IMU disabled; no pins qualified. Serial C clears safe mode.");
  // There are no feature-qualified workers/admissions yet, including in safe mode.
  // Future composition must suppress optional startup/admission when safe_mode is true.
  if (!supervisor.begin({}, millis())) {
    supervision_fault.store(true);
    Serial.println("RideSync: health configuration failed");
    return;
  }
  // Preserve framework 5-second panic TWDT and CPU0 idle ownership. No init/deinit,
  // enableLoopWDT, implicit peripheral activation or automatic application restart.
  if (xTaskCreatePinnedToCore(healthTask, "health", 4096, nullptr, 2, nullptr, 1) != pdPASS) {
    supervision_fault.store(true);
    Serial.println("RideSync: supervisor task creation failed");
  }
}

void loop() {
  static PersistStatus reported_config = PersistStatus::Defaults;
  const auto settings_status = config_status.load();
  if (settings_status != reported_config) {
    Serial.printf("RideSync: config status=%u error=%d\n", static_cast<unsigned>(settings_status),
                  config_error.load());
    reported_config = settings_status;
  }
  static WatchdogState reported = WatchdogState::NotStarted;
  const auto state = watchdog_state.load();
  if (state != reported) {
    Serial.printf("RideSync: watchdog status=%u error=%d\n", static_cast<unsigned>(state),
                  watchdog_error.load());
    reported = state;
  }
  static uint8_t reported_stalls = 0;
  const uint8_t stalls = stalled_workers.load();
  if (stalls != reported_stalls) {
    Serial.printf("RideSync: stalled worker mask=%u\n", static_cast<unsigned>(stalls));
    reported_stalls = stalls;
  }
  // Only this application scheduler owns LED service. Read published observations;
  // no camera group is composed, and no watchdog/health policy is invoked here.
  LedHealth led_health;
  led_health.safe_mode = safe_mode.load();
  led_health.required_worker_stall = stalls != 0;
  led_health.application_fault =
      supervision_fault.load() || state == WatchdogState::ExistingSubscription ||
      state == WatchdogState::Uninitialized || state == WatchdogState::StatusFailed ||
      state == WatchdogState::AddFailed || state == WatchdogState::FeedFailed ||
      state == WatchdogState::RemoveFailed;
  const int led_error = status_led.service(selectLedState(nullptr, {}, led_health), millis());
  static int reported_led_error = 0;
  if (led_error != reported_led_error) {
    Serial.printf("RideSync: LED backend error=%d\n", led_error);
    reported_led_error = led_error;
  }
  // Only the supervisor owns recovery metadata. One-character bounded mailbox.
  if (Serial.available() && Serial.read() == 'C')
    clear_safe_mode.store(true);
  delay(1);
}

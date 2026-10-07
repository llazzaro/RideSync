#include "health_supervisor.h"
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
BootRecovery recovery;
std::atomic<bool> clear_safe_mode{false}, safe_mode{false};
std::atomic<WatchdogState> watchdog_state{WatchdogState::NotStarted};
std::atomic<int> watchdog_error{0};
std::atomic<uint8_t> stalled_workers{0};

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
  const auto sdk_reason = esp_reset_reason(); // Capture once; never overwrite SDK hints.
  BootRecord retained;
  std::memcpy(&retained, retained_boot, sizeof(retained));
  const auto boot = recovery.begin(retained, resetClass(sdk_reason), millis());
  safe_mode.store(boot.safe_mode);
  retainBoot();
  Serial.printf("RideSync: sdk_reset=%u app_cause=%u retention_valid=%u safe_mode=%u\n",
                static_cast<unsigned>(sdk_reason), static_cast<unsigned>(boot.previous_app_cause),
                static_cast<unsigned>(boot.retention_valid), static_cast<unsigned>(boot.safe_mode));
  Serial.println("Optional AT/BLE/SD/IMU disabled; no pins qualified. Serial C clears safe mode.");
  // There are no feature-qualified workers/admissions yet, including in safe mode.
  // Future composition must suppress optional startup/admission when safe_mode is true.
  if (!supervisor.begin({}, millis())) {
    Serial.println("RideSync: health configuration failed");
    return;
  }
  // Preserve framework 5-second panic TWDT and CPU0 idle ownership. No init/deinit,
  // enableLoopWDT, implicit peripheral activation or automatic application restart.
  if (xTaskCreatePinnedToCore(healthTask, "health", 4096, nullptr, 2, nullptr, 1) != pdPASS)
    Serial.println("RideSync: supervisor task creation failed");
}

void loop() {
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
  // Only the supervisor owns recovery metadata. One-character bounded mailbox.
  if (Serial.available() && Serial.read() == 'C')
    clear_safe_mode.store(true);
  delay(1);
}

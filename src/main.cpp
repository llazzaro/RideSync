#if defined(RIDESYNC_X5_SERIAL_MILESTONE)
#include "health_supervisor.h"
#include "profiles/insta360_x5_esp32.h"
#include <Arduino.h>
#include <cstring>
#include <esp_attr.h>
#include <esp_system.h>
namespace {
RTC_NOINIT_ATTR uint8_t x5_retained_boot[sizeof(ridesync::BootRecord)];
ridesync::BootRecovery x5_recovery;
bool x5_safe_mode = true;
} // namespace
void setup() {
  Serial.begin(115200);
  ridesync::BootRecord retained;
  std::memcpy(&retained, x5_retained_boot, sizeof retained);
  const auto reason = esp_reset_reason();
  const auto reset =
      reason == ESP_RST_POWERON     ? ridesync::ResetClass::Cold
      : reason == ESP_RST_BROWNOUT  ? ridesync::ResetClass::Brownout
      : reason == ESP_RST_SW        ? ridesync::ResetClass::Software
      : reason == ESP_RST_EXT       ? ridesync::ResetClass::Operator
      : reason == ESP_RST_DEEPSLEEP ? ridesync::ResetClass::DeepSleep
      : reason == ESP_RST_PANIC     ? ridesync::ResetClass::Panic
      : reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT
          ? ridesync::ResetClass::Watchdog
          : ridesync::ResetClass::Unknown;
  x5_safe_mode = x5_recovery.begin(retained, reset, millis()).safe_mode;
  std::memcpy(x5_retained_boot, &x5_recovery.record(), sizeof retained);
  ridesync::x5MilestoneBegin(x5_safe_mode);
}
void loop() {
  ridesync::x5MilestoneService(!x5_safe_mode);
  delay(1);
}
#else
#include "application_startup.h"
#include "health_supervisor.h"
#include "nvs_boot_guard.h"
#include "pairing_proof_esp32.h"
#include "status_led.h"
#include <Arduino.h>
#include <cstring>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>

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
std::atomic<uint16_t> worker_health_masks{0};

// One explicit owning service isolates blocking NVS calls from control loops.
// No SDK init/deinit/erase may run while this owner is active. Future lifecycle
// work must stop/quiesce it first. Configuration requests belong in a bounded
// owner mailbox; no UI or control producer is admitted in this milestone.
NvsConfigStore config_store;
ConfigPersistence config_persistence(config_store);
// Boot-local epoch: all envelopes and endpoints are destroyed at reboot. This is
// not a durable session ID; future endpoint restart needs a new nonzero epoch.
constexpr uint32_t kSettingsEpoch = 1;
SettingsPublication settings_publication;
SettingsRequests settings_requests; // no production save producer admitted
typename std::aligned_storage<sizeof(ConfigBootstrap), alignof(ConfigBootstrap)>::type
    config_memory;
ConfigBootstrap *config_bootstrap = nullptr;
ApplicationSettings application_settings(kSettingsEpoch); // loop-owned immutable copy
constexpr uint32_t kConfigStartupMs = 1000;
StartupState startup_state = StartupState::WaitingConfig;
StartupState config_startup = StartupState::WaitingConfig;
uint32_t config_started = 0, supervisor_started = 0;
ApplicationWorkers *workers = nullptr;
bool policy_fixed = false, workers_launched = false;
SettingsSnapshot received_settings; // loop-only scratch; no large loop stack envelope
void configTask(void *) {
  pairingProofMaintenance().beginOwner(); // Existing exclusive NVS owner, before publication.
  config_bootstrap->start(safe_mode.load(), config_store.allowed());
  TickType_t next = xTaskGetTickCount();
  for (;;) {
    config_bootstrap->service(millis(), safe_mode.load(), config_store.allowed());
    pairingProofMaintenance().service();
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
    worker_health_masks.store(static_cast<uint16_t>(decision.stalled) |
                              (static_cast<uint16_t>(decision.refused) << 8));
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
  workers = commissionedApplication();
  config_bootstrap = new (&config_memory)
      ConfigBootstrap(config_persistence, settings_publication, settings_requests, kSettingsEpoch,
                      workers ? workers->peers() : CameraPeers{});
  config_started = millis();
  if (!config_ble_admission) {
    // No worker exists: setup is the sole owner on this terminal startup path.
    config_bootstrap->unavailable(PersistStatus::Refused);
  } else if (xTaskCreatePinnedToCore(configTask, "config", 12288, nullptr, 1, nullptr, 1) !=
             pdPASS) {
    config_bootstrap->unavailable({PersistStatus::ReadError, -1});
    Serial.println("RideSync: config task creation failed; settings unavailable.");
  }
  Serial.println("Optional workers require explicit commissioning. Serial C clears safe mode.");
}

void loop() {
  // Admission is bounded by receipt on this owner, even if publication becomes
  // ready before the first post-deadline pass. Never accept that late envelope.
  if (!policy_fixed && millis() - config_started >= kConfigStartupMs)
    config_startup = StartupState::ConfigTimedOut;
  if (settings_publication.take(received_settings) &&
      config_startup != StartupState::ConfigTimedOut &&
      application_settings.accept(received_settings)) {
    const auto &settings = application_settings.snapshot();
    Serial.printf(
        "RideSync: config ready=%u effective=%u epoch=%u generation=%llu "
        "load=%u outcome=%u error=%d cameras=%u peers=%u\n",
        static_cast<unsigned>(settings.completed), static_cast<unsigned>(settings.effective),
        settings.epoch, static_cast<unsigned long long>(settings.generation),
        static_cast<unsigned>(settings.load.status), static_cast<unsigned>(settings.outcome.status),
        settings.outcome.code, static_cast<unsigned>(settings.settings.count),
        static_cast<unsigned>(settings.peers_valid));
  }
  if (!policy_fixed && (application_settings.snapshot().completed ||
                        millis() - config_started >= kConfigStartupMs)) {
    // Timeout is terminal for this boot. Keep the permanent NVS owner/resources;
    // consume but never apply a late snapshot. No erase/reinit/retry is admitted.
    config_startup = !application_settings.snapshot().completed  ? StartupState::ConfigTimedOut
                     : application_settings.snapshot().effective ? StartupState::ConfigReady
                                                                 : StartupState::ConfigUnavailable;
    Serial.printf("RideSync: configuration startup outcome=%u\n",
                  static_cast<unsigned>(config_startup));
    std::array<WorkerPolicy, 4> policy{};
    if (workers)
      policy = workers->prepare(application_settings.snapshot(), safe_mode.load(),
                                config_store.allowed(), supervisor);
    policy_fixed = true;
    startup_state = StartupState::WaitingSupervisor;
    supervisor_started = millis();
    if (!supervisor.begin(policy, millis()) ||
        xTaskCreatePinnedToCore(healthTask, "health", 4096, nullptr, 2, nullptr, 1) != pdPASS) {
      supervision_fault.store(true);
      startup_state = StartupState::SupervisionFailed;
      Serial.println("RideSync: supervisor task creation failed or invalid policy");
    }
  }
  if (policy_fixed && startup_state != StartupState::SupervisionFailed && !workers_launched) {
    const auto watchdog = watchdog_state.load();
    // Bound admission on this owner before accepting a ready handshake. A late
    // Running publication cannot bypass expiry, including across millis wrap.
    if (millis() - supervisor_started >= HealthSupervisor::kMaxGraceMs) {
      supervision_fault.store(true);
      startup_state = StartupState::SupervisionFailed;
    } else if (watchdog == WatchdogState::Running) {
      workers_launched = true;
      startup_state = StartupState::Running;
      if (workers) {
        workers->current(application_settings.snapshot(), safe_mode.load(), config_store.allowed());
        workers->launch();
      }
    } else if (watchdog != WatchdogState::NotStarted) {
      supervision_fault.store(true);
      startup_state = StartupState::SupervisionFailed;
    }
  }
  static WatchdogState reported = WatchdogState::NotStarted;
  const auto state = watchdog_state.load();
  if (state != reported) {
    Serial.printf("RideSync: watchdog status=%u error=%d\n", static_cast<unsigned>(state),
                  watchdog_error.load());
    reported = state;
  }
  static uint8_t reported_stalls = 0;
  const uint16_t masks = worker_health_masks.load();
  const uint8_t stalls = masks & 255, refused = masks >> 8;
  if (stalls != reported_stalls) {
    Serial.printf("RideSync: stalled worker mask=%u\n", static_cast<unsigned>(stalls));
    reported_stalls = stalls;
  }
  if (workers && workers_launched) {
    workers->current(application_settings.snapshot(), safe_mode.load(), config_store.allowed());
    workers->service(stalls, refused, supervision_fault.load() || state != WatchdogState::Running);
  }
  // Only this application scheduler owns LED service. Read published observations;
  // no camera group is composed, and no watchdog/health policy is invoked here.
  LedHealth led_health;
  led_health.safe_mode = safe_mode.load();
  led_health.required_worker_stall = stalls != 0;
  led_health.application_fault = refused != 0 || config_startup == StartupState::ConfigTimedOut;
  led_health.application_fault =
      led_health.application_fault || supervision_fault.load() ||
      state == WatchdogState::ExistingSubscription || state == WatchdogState::Uninitialized ||
      state == WatchdogState::StatusFailed || state == WatchdogState::AddFailed ||
      state == WatchdogState::FeedFailed || state == WatchdogState::RemoveFailed;
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

#endif

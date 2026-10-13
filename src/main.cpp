#include "health_supervisor.h"
#include <esp_system.h>
namespace {
ridesync::ResetClass resetClass(decltype(esp_reset_reason()) reason) {
  switch (reason) {
  case ESP_RST_POWERON:
    return ridesync::ResetClass::Cold;
  case ESP_RST_BROWNOUT:
    return ridesync::ResetClass::Brownout;
  case ESP_RST_PANIC:
    return ridesync::ResetClass::Panic;
  case ESP_RST_INT_WDT:
  case ESP_RST_TASK_WDT:
  case ESP_RST_WDT:
    return ridesync::ResetClass::Watchdog;
  case ESP_RST_SW:
    return ridesync::ResetClass::Software;
  case ESP_RST_EXT:
    return ridesync::ResetClass::Operator;
  case ESP_RST_DEEPSLEEP:
    return ridesync::ResetClass::DeepSleep;
  default:
    return ridesync::ResetClass::Unknown;
  }
}
} // namespace
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
  const auto reset = resetClass(reason);
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
#if defined(RIDESYNC_SETUP_APPLICATION)
#include "setup_prerequisites.h"
#include "setup_service.h"
#if defined(RIDESYNC_BENCH_APPLICATION)
#include "ble_esp32.h"
#include "profiles/qualification.h"
#include "setup_discovery.h"
#endif
#endif
#if defined(RIDESYNC_BENCH_APPLICATION)
#include "bench_application.h"
#include "bench_console.h"
#endif
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
SettingsRequests settings_requests;
#if defined(RIDESYNC_SETUP_APPLICATION)
SettingsSavePublication setup_save_results;
SetupService setup_service;
uint32_t setup_challenge = 0;
// The storage task alone publishes acknowledgements. Each new pause has a
// fresh token, so an acknowledgement from a previous lease can never be reused.
std::atomic<uint32_t> config_pause_requested{0}, config_pause_ack{0};
uint32_t config_pause_sequence = 0;
bool requestConfigPause() {
  if (config_pause_requested.load(std::memory_order_acquire))
    return true;
  if (config_pause_sequence == UINT32_MAX)
    return false;
  config_pause_requested.store(++config_pause_sequence, std::memory_order_release);
  return true;
}
bool configPauseAcknowledged() {
  const auto request = config_pause_requested.load(std::memory_order_acquire);
  return request && config_pause_ack.load(std::memory_order_acquire) == request;
}
SetupSaveDispatch setup_dispatch;
SettingsSaveResult setup_result;
char setup_reply[kSetupFrameBytes + 1]{};
char setup_report[769]{};
SourceConfig setup_report_source;
#if defined(RIDESYNC_BENCH_APPLICATION)
const BenchApplicationConfig *setup_configuration = nullptr;
SetupDiscovery setup_discovery(Esp32BleHost::instance());
bool setup_host_initializing = false;
std::array<SetupIdentityMapping, kBlePeers> setup_mappings{};
void setupDiscoveryMappings() {
  if (!setup_configuration)
    return;
  const auto &source = setup_configuration->source;
  const auto &q = setup_configuration->cameras;
  size_t count = 0;
  for (size_t i = 0; i < source.count && i < kBlePeers; ++i) {
    const auto &camera = source.cameras[i];
    const BondIdentity *id = nullptr;
    switch (camera.model) {
    case CameraModel::X5:
      break; // CE80 camera-initiated route is not central discovery.
    case CameraModel::ONE_RS:
      id = &q.one_rs[i].identity;
      break;
    case CameraModel::GO3S:
      id = &q.go3s[i].identity;
      break;
    case CameraModel::HERO12_BLACK:
      id = &q.hero12[i].identity;
      break;
    default:
      id = &q.dji[i].identity;
      break;
    }
    if (!id || !qualification::resolvedIdentity(*id) ||
        !qualification::identifierAddress(camera.identifier, *id, true) ||
        camera.address_type !=
            (id->type == IdentityType::Public ? AddressType::Public : AddressType::Random))
      continue;
    auto &mapping = setup_mappings[count++];
    mapping.observed = id->address;
    mapping.observed_type = id->type == IdentityType::Public ? 0 : 1;
    mapping.identity = *id;
  }
  setup_discovery.setIdentityMappings(setup_mappings.data(), count);
}

#endif
#endif
typename std::aligned_storage<sizeof(ConfigBootstrap), alignof(ConfigBootstrap)>::type
    config_memory;
ConfigBootstrap *config_bootstrap = nullptr;
ApplicationSettings application_settings(kSettingsEpoch); // loop-owned immutable copy
constexpr uint32_t kConfigStartupMs = 1000;
StartupState startup_state = StartupState::WaitingConfig;
StartupState config_startup = StartupState::WaitingConfig;
uint32_t config_started = 0, supervisor_started = 0;
ApplicationWorkers *workers = nullptr;
bool configuration_fixed = false, policy_fixed = false, workers_launched = false;
SettingsSnapshot received_settings; // loop-only scratch; no large loop stack envelope
#if defined(RIDESYNC_SETUP_APPLICATION) && defined(RIDESYNC_BENCH_APPLICATION)
void setupDiscoveryInitialization(uint32_t now) {
  if (setup_discovery.initializationRequested()) {
    const auto *proof = setup_configuration ? &setup_configuration->setup_store : nullptr;
    if (proof && !proof->qualification_record)
      proof = &setup_configuration->cameras.x5.store; // Reuse an actual reviewed receipt.
    const bool qualified = proof && proof->qualification_record && !safe_mode.load() &&
                           config_store.allowed() && application_settings.snapshot().effective;
    if (!qualified) {
      setup_discovery.initialize(false, now);
      return;
    }
    if (!requestConfigPause()) {
      setup_discovery.initialize(false, now);
      return;
    }
    if (!configPauseAcknowledged())
      return;
    const bool restore = Esp32BleHost::instance().configureRestore(*proof);
    setup_host_initializing = restore;
    setup_discovery.initialize(restore, now);
    if (!restore) {
      config_pause_requested.store(0, std::memory_order_release);
    }
  }
  if (!setup_host_initializing && !setup_discovery.initializationRequested() &&
      !setup_service.restartRequested() && config_pause_requested.load()) {
    // Entry expiry/cancel before the host lease was taken must release the pause.
    config_pause_requested.store(0, std::memory_order_release);
  }
  if (setup_host_initializing && setup_discovery.hostSettled()) {
    setup_host_initializing = false;
    config_pause_requested.store(0, std::memory_order_release);
  }
}
#endif
void configTask(void *) {
  pairingProofMaintenance().beginOwner(); // Existing exclusive NVS owner, before publication.
  config_bootstrap->start(safe_mode.load(), config_store.allowed());
  TickType_t next = xTaskGetTickCount();
  for (;;) {
#if defined(RIDESYNC_SETUP_APPLICATION)
    const auto pause = config_pause_requested.load(std::memory_order_acquire);
    if (pause) {
      config_pause_ack.store(pause, std::memory_order_release);
      vTaskDelayUntil(&next, pdMS_TO_TICKS(100));
      continue;
    }
    config_pause_ack.store(0, std::memory_order_release);
#endif
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
void receiveSettings() {
  // Admission is bounded by receipt on this owner, even if publication becomes
  // ready before the first post-deadline pass. Never accept that late envelope.
  if (!configuration_fixed && millis() - config_started >= kConfigStartupMs)
    config_startup = StartupState::ConfigTimedOut;
  if (settings_publication.take(received_settings) &&
      config_startup != StartupState::ConfigTimedOut &&
      application_settings.accept(received_settings)) {
    const auto &settings = application_settings.snapshot();
#if defined(RIDESYNC_SETUP_APPLICATION)
    setup_service.setSnapshot(settings);
#endif
    Serial.printf(
        "RideSync: config ready=%u effective=%u epoch=%u generation=%llu "
        "load=%u outcome=%u error=%d cameras=%u peers=%u\n",
        static_cast<unsigned>(settings.completed), static_cast<unsigned>(settings.effective),
        settings.epoch, static_cast<unsigned long long>(settings.generation),
        static_cast<unsigned>(settings.load.status), static_cast<unsigned>(settings.outcome.status),
        settings.outcome.code, static_cast<unsigned>(settings.settings.count),
        static_cast<unsigned>(settings.peers_valid));
  }
}

void fixWorkerPolicy() {
  if (!configuration_fixed && (application_settings.snapshot().completed ||
                               millis() - config_started >= kConfigStartupMs)) {
    // Timeout is terminal for this boot. Keep the permanent NVS owner/resources;
    // consume but never apply a late snapshot. No erase/reinit/retry is admitted.
    config_startup = !application_settings.snapshot().completed  ? StartupState::ConfigTimedOut
                     : application_settings.snapshot().effective ? StartupState::ConfigReady
                                                                 : StartupState::ConfigUnavailable;
    Serial.printf("RideSync: configuration startup outcome=%u\n",
                  static_cast<unsigned>(config_startup));
    configuration_fixed = true;
  }
  if (configuration_fixed && !policy_fixed) {
    if (workers && !workers->startupReady())
      return; // Qualified power wait precedes supervisor/worker grace clocks.
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
}

void launchWorkers() {
  if (!policy_fixed || startup_state == StartupState::SupervisionFailed || workers_launched)
    return;
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

void serviceLed(uint8_t stalls, uint8_t refused, WatchdogState state) {
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
}

#if defined(RIDESYNC_SETUP_APPLICATION)
void serviceSetup() {
  const uint32_t now = millis();
  // This image never constructs/launches workers; entry cannot retire a live camera.
  setup_service.setIsolationAvailable(!workers && application_settings.snapshot().completed &&
                                      config_startup != StartupState::ConfigTimedOut);
  setup_service.tick(now);
  // tick may have published an asynchronous save/entry result. Empty its slot
  // before reading a new legitimate host request; retain unread UART bytes.
  if (setup_service.takeResponse(setup_reply, sizeof setup_reply))
    Serial.println(setup_reply);
  for (unsigned i = 0; i < 64 && Serial.available(); ++i)
    setup_service.feed(static_cast<char>(Serial.read()), now);
  if (setup_service.isolationRequested())
    setup_service.acknowledgeIsolation(!workers);
#if defined(RIDESYNC_BENCH_APPLICATION)
  setupDiscoveryInitialization(now);
#endif
  if (setup_service.takeSave(setup_dispatch)) {
    setup_dispatch.save.session = setup_dispatch.session;
    setup_dispatch.save.request = setup_dispatch.request;
    if (config_pause_requested.load() || !settings_requests.put(setup_dispatch.save))
      setup_service.completeSave(setup_dispatch.session, setup_dispatch.request, SaveOutcome::Busy,
                                 PersistStatus::Pending, kSettingsEpoch,
                                 application_settings.snapshot().generation);
  }
  if (setup_save_results.take(setup_result))
    setup_service.completeSave(setup_result.session, setup_result.request, setup_result.outcome,
                               setup_result.result, setup_result.epoch, setup_result.generation);
  // Read-only reports use actual private declarations when available; no store,
  // radio or driver action is invoked to manufacture a prerequisite observation.
  static uint64_t report_generation = UINT64_MAX;
  static bool report_effective = false;
  const auto &saved = application_settings.snapshot();
  if ((report_generation != saved.generation || report_effective != saved.effective) &&
      expandSettings(saved.settings, setup_report_source)) {
    report_generation = saved.generation;
    report_effective = saved.effective;
    SetupPrerequisiteEvidence evidence;
#if defined(RIDESYNC_BENCH_APPLICATION)
    const auto provider = benchApplicationStatus();
    const auto state = setup_configuration                              ? SetupProvider::Available
                       : !provider.supplied                             ? SetupProvider::Missing
                       : provider.error == BenchProviderError::Declined ? SetupProvider::Declined
                                                                        : SetupProvider::Refused;
    evidence = setupQualificationEvidence(
        setup_report_source, setup_configuration ? &setup_configuration->cameras : nullptr, state);
#endif
    auto report = setupPrerequisites(setup_report_source, evidence);
    if (!application_settings.snapshot().effective) {
      report.settings = SetupEvidence::Refused;
      report.overall = SetupReadiness::Refused;
    }
    if (encodePrerequisiteReport(report, setup_report, sizeof setup_report))
      setup_service.setPrerequisiteReport(setup_report);
  }
  if (setup_service.takeResponse(setup_reply, sizeof setup_reply))
    Serial.println(setup_reply);
#if defined(RIDESYNC_BENCH_APPLICATION)
  setup_service.ownerReleased(setup_discovery.released() && !setup_host_initializing);
#else
  setup_service.ownerReleased(true);
#endif
  if (setup_service.restartRequested()) {
    if (requestConfigPause() && configPauseAcknowledged())
      esp_restart(); // Explicit request, durable result and acknowledged owner barrier.
  }
}
#endif

void reportWatchdog(WatchdogState state) {
  static WatchdogState reported = WatchdogState::NotStarted;
  if (state != reported) {
    Serial.printf("RideSync: watchdog status=%u error=%d\n", static_cast<unsigned>(state),
                  watchdog_error.load());
    reported = state;
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
#if defined(RIDESYNC_SETUP_APPLICATION)
  setup_challenge = esp_random();
  if (!setup_challenge)
    setup_challenge = 1;
  uint32_t session_nonce = esp_random();
  if (!session_nonce)
    session_nonce = 1;
  setup_service.reset(millis(), kSettingsEpoch, setup_challenge, session_nonce);
  workers = nullptr;
#if defined(RIDESYNC_BENCH_APPLICATION)
  setup_configuration = benchSetupConfiguration();
  setupDiscoveryMappings();
  setup_service.setDiscovery(&setup_discovery);
#endif
#else
  workers = commissionedApplication();
#endif
  const SourceConfig *startup_defaults = nullptr;
#if defined(RIDESYNC_BENCH_APPLICATION)
#if defined(RIDESYNC_SETUP_APPLICATION)
  startup_defaults = setup_configuration ? &setup_configuration->source : nullptr;
#else
  startup_defaults = benchApplicationDefaults();
#endif
#endif
  config_bootstrap = new (&config_memory)
      ConfigBootstrap(config_persistence, settings_publication, settings_requests, kSettingsEpoch,
                      workers ? workers->peers() : CameraPeers{}, startup_defaults
#if defined(RIDESYNC_SETUP_APPLICATION)
                      ,
                      &setup_save_results
#endif
      );
  config_started = millis();
  if (!config_ble_admission) {
    // No worker exists: setup is the sole owner on this terminal startup path.
    config_bootstrap->unavailable(PersistStatus::Refused);
  } else if (xTaskCreatePinnedToCore(configTask, "config", 12288, nullptr, 1, nullptr, 1) !=
             pdPASS) {
    config_bootstrap->unavailable({PersistStatus::ReadError, -1});
    Serial.println("RideSync: config task creation failed; settings unavailable.");
  }
#if defined(RIDESYNC_SETUP_APPLICATION)
  Serial.println("Parked USB setup-only image: use the host setup wizard within 30 seconds. "
                 "Camera and peripheral workers remain isolated.");
#elif defined(RIDESYNC_BENCH_APPLICATION)
  Serial.println(
      "Bench commands: STATUS CONNECT REC STOP QUERY WAKE SHUTDOWN CLEAR STATIONARY MARK. "
      "Boot is idle.");
#else
  Serial.println("Optional workers require explicit commissioning. Serial C clears safe mode.");
#endif
}

void loop() {
  receiveSettings();
  fixWorkerPolicy();
  launchWorkers();
  const auto state = watchdog_state.load();
  reportWatchdog(state);
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
  serviceLed(stalls, refused, state);
  // Only the supervisor owns recovery metadata. One-character bounded mailbox.
#if defined(RIDESYNC_SETUP_APPLICATION)
  serviceSetup();
#elif defined(RIDESYNC_BENCH_APPLICATION)
  benchConsoleService();
  if (benchConsoleTakeClear())
    clear_safe_mode.store(true);
#else
  if (Serial.available() && Serial.read() == 'C')
    clear_safe_mode.store(true);
#endif
  delay(1);
}

#endif

// Actual main/facade scheduler; only NVS/SDK IO and task scheduling are synthetic.
#include "application_esp32.h"
#include "config_storage.h"
#include "nvs_boot_guard.h"
static ConfigRecord saved_config;
namespace ridesync {
NvsBootStatus nvsBootStatus() {
  NvsBootStatus s;
  s.init_observed = true;
  s.format_refused = nvs_revoked;
  return s;
}
bool NvsConfigStore::allowed() const { return true; }
StoreResult NvsConfigStore::read(unsigned slot, ConfigRecord &out) {
  if (!slot && saved_config.size) {
    out = saved_config;
    return {};
  }
  return {StoreStatus::Missing};
}
StoreResult NvsConfigStore::write(unsigned, const ConfigRecord &) {
  assert(false);
  return {};
}
static ApplicationWorkers *provided = nullptr;
ApplicationWorkers *commissionedApplication() { return provided; }
} // namespace ridesync
#include "src/main.cpp"
static void taskPass(void (*fn)(void *)) {
  assert(fn);
  try {
    fn(nullptr);
  } catch (const std::runtime_error &) {
  }
}
#include "reset_cases.inc"
int mainApplication(const std::string &mode) {
  HardwareSerial uart;
  SPIClass spi;
  TwoWire wire;
  auto q = qualified();
  q.runtime.cameras_qualified = true;
  q.runtime.peers.count = 1;
  q.runtime.peers.entries[0].id = 301;
  q.runtime.peers.entries[0].model = CameraModel::HERO12_BLACK;
  SourceConfig source;
  source.count = 1;
  source.cameras[0].name = "hero";
  source.cameras[0].model = CameraModel::HERO12_BLACK;
  source.cameras[0].family = CameraFamily::GoPro;
  source.cameras[0].identifier = "01:02:03:04:05:06";
  source.cameras[0].address_type = AddressType::Public;
  source.button_gpio.enabled = source.button_gpio.board_qualified = true;
  source.button_gpio.pin = 32;
  source.button_gpio.pull = ButtonPull::Up;
  const bool reset_case = mode.find("main-reset-") == 0 && mode != "main-reset-seal";
  if (reset_case) {
    uart.auto_reply = true;
    source.count = 2;
    source.cameras[1] = source.cameras[0];
    source.cameras[1].name = "other";
    source.cameras[1].identifier = "02:02:03:04:05:06";
    q.runtime.peers.count = 2;
    q.runtime.peers.entries[1].id = 302;
    q.runtime.peers.entries[1].slot = 1;
    q.runtime.peers.entries[1].model = CameraModel::HERO12_BLACK;
  }
  assert(encodeConfig(source, 1, saved_config).status == PersistStatus::Encoded);
  QualifiedHandlebar handlebar;
  handlebar.opt_in = handlebar.acknowledge_qualification = true;
  handlebar.button = source.button_gpio;
  if (mode == "main-button-unqualified")
    handlebar.button.pin = 33;
  std::array<Hero12Qualification, kMaxCameras> proof{};
  proof[0].source_qualified = proof[0].classic_profile_confirmed = true;
  proof[0].identity.verified = true;
  proof[0].identity.type = IdentityType::Public;
  proof[0].identity.address = {{1, 2, 3, 4, 5, 6}};
  proof[0].firmware_size = 1;
  proof[0].firmware[0] = 'x';
  if (reset_case) {
    proof[1] = proof[0];
    proof[1].identity.address[0] = 2;
  }
  if (mode == "main-reset-ambiguous")
    proof[1].identity = proof[0].identity;
  commission();
  wire.payload[0] = 0x8c;
  wire.payload[1] = 7;
  wire.payload[7] = 6;
  if (mode == "main-qualification")
    q.modem.pins_qualified = false;
  if (mode == "main-ble-refusal")
    host_failure = true;
  if (mode == "main-sd-refusal")
    fail_sd = true;
  if (mode == "main-imu-refusal")
    fail_imu = true;
  if (mode == "main-supervisor-refusal")
    sdk_add_error = 17;
  if (mode == "main-health-task-refusal")
    fail_health_task = true;
  if (mode == "main-config-task-refusal")
    fail_config_task = true;
  if (mode == "main-blocked-allocation")
    block_ledger.store(true);
  if (mode == "main-blocked-close")
    block_close.store(true);
  SupervisedEsp32Application application(uart, spi, wire, q, handlebar, proof);
  provided = &application;
  if (mode == "main-safe-mode") {
    BootRecovery record;
    record.begin({}, ResetClass::Cold, 0);
    for (unsigned i = 0; i < 3; ++i)
      record.begin(record.record(), ResetClass::Watchdog, 0);
    std::memcpy(retained_boot, &record.record(), sizeof(BootRecord));
    test_reset_reason = ESP_RST_TASK_WDT;
  }
  const bool supervisor_rollover = mode == "main-supervisor-exact-rollover" ||
                                   mode == "main-supervisor-beyond-rollover" ||
                                   mode == "main-supervisor-timely-rollover";
  if (supervisor_rollover)
    raw_time.store(UINT32_MAX - 1500);
  const bool config_refused = fail_config_task || mode == "main-safe-mode";
  setup();
  assert((config_task || mode == "main-safe-mode") && !health_task && worker_threads.empty() &&
         !application.telemetry());
  loop();
  if (!config_refused)
    assert(!health_task && worker_threads.empty());
  if (mode == "main-config-timeout" || mode == "main-late-config") {
    raw_time.fetch_add(1000);
    if (mode == "main-late-config")
      taskPass(config_task);
    loop();
    assert(config_startup == StartupState::ConfigTimedOut && health_task);
  } else if (!config_refused) {
    raw_time.fetch_add(900);
    loop();
    assert(!health_task);
    taskPass(config_task);
    loop();
    assert(health_task && worker_threads.empty());
  }
  if (fail_health_task) {
    assert(startup_state == StartupState::SupervisionFailed && worker_threads.empty());
    return 0;
  }
  if (mode == "main-supervisor-timeout") {
    raw_time.fetch_add(1000);
    loop();
    assert(startup_state == StartupState::SupervisionFailed && worker_threads.empty());
    taskPass(health_task);
    loop();
    assert(worker_threads.empty());
    return 0;
  }
  const bool late_supervisor =
      mode == "main-supervisor-exact" || mode == "main-supervisor-beyond" ||
      mode == "main-supervisor-exact-rollover" || mode == "main-supervisor-beyond-rollover";
  if (late_supervisor || mode == "main-supervisor-timely" ||
      mode == "main-supervisor-timely-rollover") {
    const uint32_t elapsed =
        late_supervisor
            ? HealthSupervisor::kMaxGraceMs + (mode.find("beyond") != std::string::npos ? 1 : 0)
            : HealthSupervisor::kMaxGraceMs - 1;
    raw_time.store(supervisor_started + elapsed);
    assert(millis() - supervisor_started == elapsed);
    if (supervisor_rollover)
      assert(millis() < supervisor_started);
  }
  taskPass(health_task); // Real success is ready before main first observes the deadline.
  assert(watchdog_state.load() ==
         (sdk_add_error ? WatchdogState::AddFailed : WatchdogState::Running));
  loop();
  if (late_supervisor) {
    assert(startup_state == StartupState::SupervisionFailed && supervision_fault.load() &&
           worker_threads.empty() && !application.telemetry());
    clear_safe_mode.store(true);
    taskPass(health_task);
    loop();
    assert(startup_state == StartupState::SupervisionFailed && worker_threads.empty() &&
           !application.telemetry());
    return 0;
  }
  if (sdk_add_error) {
    assert(startup_state == StartupState::SupervisionFailed && worker_threads.empty());
    return 0;
  }
  assert(startup_state == StartupState::Running && application.telemetry());
  if (mode == "main-config-timeout" || mode == "main-late-config") {
    assert(!application_settings.snapshot().completed);
    taskPass(config_task);
    loop();
    assert(!application_settings.snapshot().completed);
  }
  if (mode == "main-qualification" || mode == "main-sd-refusal") {
    assert(application.telemetry()->canRelease());
    assert(application.telemetry()->status().phase == TelemetryPhase::Refused &&
           worker_threads.empty());
    if (mode == "main-ble-refusal")
      host_failure = true;
    if (mode == "main-sd-refusal") {
      const auto d = supervisor.evaluate(millis());
      assert(d.refused && !d.stalled && !d.stable_candidate && !d.execution_healthy);
    }
    return 0;
  }
  if (mode == "main-early-stop") {
    application.telemetry()->requestStop();
    assert(imu_tasks == 0 && uart.writes == 0 && !application.telemetry()->canRelease());
  } else if (mode == "main-blocked-allocation") {
    for (unsigned i = 0; i < 1000 && !ledger_entered.load(); ++i)
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    assert(ledger_entered.load());
    raw_time.fetch_add(1001);
    loop();
    auto d = supervisor.evaluate(millis());
    assert(d.stalled & (1U << static_cast<unsigned>(Worker::Sd)));
    assert(supervisor.progress(Worker::Sd).generation() == 0);
    application.telemetry()->requestStop();
    for (unsigned i = 0; i < 100; ++i)
      loop();
    assert(!application.telemetry()->canRelease() && uart.writes == 0 && imu_tasks == 0);
    release_ledger.store(true);
  } else {
    for (unsigned i = 0; i < 400; ++i) {
      loop();
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    auto s = application.telemetry()->status();
    assert(s.phase == TelemetryPhase::Running && s.at_completed > 0 && s.sd_completed > 0);
    if (mode == "main-config-timeout" || mode == "main-late-config" || config_refused)
      assert(s.camera != CameraAdmission::Admitted && !application.control());
    else if (mode == "main-ble-refusal") {
      assert(application.control() && s.camera == CameraAdmission::Refused);
      assert(supervisor.progress(Worker::Ble).isRefused());
      assert(supervisor.progress(Worker::Ble).generation() == 0);
    } else if (mode == "main-button-unqualified") {
      assert(!application.control() && button_configs == 0);
    } else {
      assert(s.camera == CameraAdmission::Admitted && application.control());
      const auto copied = application.control()->status().control;
      assert(copied.local.at_completed == s.at_completed);
      const auto ticks = hero12Runtime().manager.ticks(),
                 advances = hero12Runtime().group.advancements();
      loop();
      assert(hero12Runtime().manager.ticks() == ticks + 1 &&
             hero12Runtime().group.advancements() == advances + 1);
      if (reset_case)
        resetCases(application, mode);
      if (mode == "main-reset-seal") {
        const auto snapshot = application_settings.snapshot();
        assert(application.requestPairingReset(0, 301, snapshot.epoch, snapshot.generation, 1,
                                               1000) == BondResetSubmission::Queued);
        assert(hero12Runtime().manager.sealed(0));
        assert(hero12Runtime().manager.request(0, Operation::Query) == CameraError::Cancelled);
        assert(application.cancelPairingReset(1));
        loop();
        loop();
        assert(application.pairingResetStatus().releasable);
      }
      if (mode == "main-terminal-at") {
        assert(application.telemetry()->owner().session()->clock().reset(42));
        loop();
        assert(application.telemetry()->status().gps.health == UartHealth::InvalidClock);
        assert(supervisor.progress(Worker::At).isFinished());
        assert(supervisor.progress(Worker::At).outcome() == DeviceHealth::Desynchronized);
        const auto count = supervisor.progress(Worker::At).generation();
        raw_time.fetch_add(2001);
        loop();
        const auto d = supervisor.evaluate(millis());
        assert(!(d.stalled & (1U << static_cast<unsigned>(Worker::At))));
        assert(supervisor.progress(Worker::At).generation() == count);
      }
      if (mode == "main-runtime-safe-mode") {
        safe_mode.store(true);
        loop();
        assert(application.telemetry()->status().safe_mode);
        assert(application.telemetry()->status().camera == CameraAdmission::Refused);
        safe_mode.store(false);
        loop();
        assert(!application.telemetry()->status().safe_mode);
        assert(application.telemetry()->status().camera == CameraAdmission::Refused);
      }
      if (mode == "main-current-gate") {
        SettingsSnapshot stale = application_settings.snapshot();
        stale.effective = false; // duplicate generation cannot revoke current settings
        assert(settings_publication.put(stale));
        loop();
        assert(application.telemetry()->status().camera == CameraAdmission::Admitted);
        --stale.epoch;
        ++stale.generation;
        assert(settings_publication.put(stale));
        loop();
        assert(application.telemetry()->status().camera == CameraAdmission::Admitted);
        SettingsSnapshot changed = application_settings.snapshot();
        ++changed.generation;
        changed.effective = false;
        assert(settings_publication.put(changed));
        loop();
        assert(application.telemetry()->status().camera == CameraAdmission::Refused);
        changed.effective = true;
        ++changed.generation;
        assert(settings_publication.put(changed));
        loop();
        assert(application.telemetry()->status().camera == CameraAdmission::Refused);
        assert(application.control()->status().control.local.camera == CameraAdmission::Refused);
      }
    }
    if (mode == "main-safe-mode") {
      assert(s.imu == SensorAdmission::SafeModeRefused && imu_tasks == 0 && safe_mode.load());
      clear_safe_mode.store(true);
      taskPass(health_task);
      loop();
      assert(!safe_mode.load() &&
             application.telemetry()->status().camera != CameraAdmission::Admitted);
      assert(application.telemetry()->status().imu == SensorAdmission::SafeModeRefused);
    }
    if (mode == "main-imu-refusal") {
      auto d = supervisor.evaluate(millis());
      assert(d.refused == (1U << static_cast<unsigned>(Worker::Imu)) && !d.stable_candidate);
      loop();
      taskPass(health_task);
      loop();
      assert(application.control()->status().control.health.application_fault);
    }
    if (mode == "main-imu-final-barrier")
      pause_imu_final.store(true);
    if (mode == "main-sd-final-barrier")
      pause_sd_final.store(true);
    application.telemetry()->requestStop();
    if (mode == "main-imu-final-barrier" || mode == "main-sd-final-barrier") {
      auto &entered = mode == "main-imu-final-barrier" ? imu_final_entered : sd_final_entered;
      auto &release = mode == "main-imu-final-barrier" ? release_imu_final : release_sd_final;
      for (unsigned i = 0; i < 1000 && !entered.load(); ++i) {
        loop();
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
      assert(entered.load());
      for (unsigned i = 0; i < 100; ++i)
        loop();
      assert(!application.telemetry()->canRelease());
      if (mode == "main-imu-final-barrier") {
        assert(!application.telemetry()->status().imu_worker.finished && !unmounts);
        assert(!application.telemetry()->owner().session()->stopped());
      } else {
        assert(application.telemetry()->owner().session()->stopped());
        assert(!application.telemetry()->status().storage_worker_finished);
      }
      release.store(true);
    }
  }
  for (unsigned i = 0; i < 1000 && !application.telemetry()->canRelease() && !close_entered.load();
       ++i) {
    if (reset_case) {
      Esp32BleHost::instance().driveRetirements();
      Esp32BleHost::instance().driveProcedures();
    }
    loop();
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  if (block_close.load()) {
    assert(close_entered.load() && !application.telemetry()->canRelease());
    loop(); // acquire the last returned pre-close step before sampling health
    const auto before = supervisor.progress(Worker::Sd).generation();
    supervisor.evaluate(millis());
    raw_time.fetch_add(2001);
    for (unsigned i = 0; i < 100; ++i)
      loop();
    auto d = supervisor.evaluate(millis());
    assert(d.stalled & (1U << static_cast<unsigned>(Worker::Sd)));
    assert(supervisor.progress(Worker::Sd).generation() == before);
    assert(!application.telemetry()->status().storage_worker_finished);
    release_close.store(true);
  }
  for (unsigned i = 0; i < 1000 && !application.telemetry()->canRelease(); ++i) {
    loop();
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  assert(application.canRelease());
  for (auto &t : worker_threads)
    t.join();
  const auto old_id = application.telemetry()->status().identity.id;
  q.runtime.cameras_qualified = false;
  Esp32LocalTelemetry fresh(uart, spi, wire, q);
  assert(fresh.start()); // full route unbound by exact owner before a new owner
  for (unsigned i = 0; i < 1000 && fresh.status().phase != TelemetryPhase::Running; ++i)
    pass(fresh);
  assert(fresh.status().phase == TelemetryPhase::Running && fresh.status().identity.id);
  if (old_id)
    assert(fresh.status().identity.id != old_id);
  fresh.requestStop();
  for (unsigned i = 0; i < 1000 && !fresh.canRelease(); ++i)
    pass(fresh);
  assert(fresh.canRelease());
  for (auto &t : worker_threads)
    if (t.joinable())
      t.join();
  return 0;
}

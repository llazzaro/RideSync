// Actual application facade/SD/IMU stop path; power logic inputs are synthetic.
#include "power_shutdown_esp32.h"
struct FixturePower final : PowerEventBackend {
  PowerObservation input;
  PowerBackendStatus result;
  unsigned initializes = 0, reads = 0, offs = 0;
  bool fail_initialize = false, fail_read = false, fail_off = false;
  bool initialize() override {
    ++initializes;
    result.attempted = true;
    result.ready = !fail_initialize;
    result.refused = fail_initialize;
    result.initialization_error = fail_initialize ? 71 : 0;
    return result.ready;
  }
  PowerObservation observe(uint32_t) override {
    ++reads;
    result.read_error = fail_read ? 72 : 0;
    return fail_read ? PowerObservation{} : input;
  }
  bool requestOff() override {
    ++offs;
    if (!result.ready)
      return false;
    result.off_attempted = true;
    result.off_succeeded = !fail_off;
    result.off_error = fail_off ? 73 : 0;
    return result.off_succeeded;
  }
  PowerBackendStatus status() const override { return result; }
};
static void powerHero12Fixture(QualifiedLocalTelemetry &q, SourceConfig &source,
                               MixedCameraQualifications &cameras) {
  // Same independently qualified synthetic source/identity as main_app.inc.
  q.runtime.cameras_qualified = true;
  q.runtime.peers.count = 1;
  q.runtime.peers.entries[0].id = 301;
  q.runtime.peers.entries[0].model = CameraModel::HERO12_BLACK;
  source.count = 1;
  source.cameras[0].name = "hero";
  source.cameras[0].model = CameraModel::HERO12_BLACK;
  source.cameras[0].family = CameraFamily::GoPro;
  source.cameras[0].identifier = "06:05:04:03:02:01";
  source.cameras[0].address_type = AddressType::Public;
  auto &proof = cameras.hero12[0];
  proof.source_qualified = proof.classic_profile_confirmed = true;
  proof.identity.verified = true;
  proof.identity.type = IdentityType::Public;
  proof.identity.address = {{1, 2, 3, 4, 5, 6}};
  proof.firmware_size = 1;
  proof.firmware[0] = 'x';
  pairingProofMaintenance().beginOwner();
}
static bool powerShutter(const BleCommand &command, uint8_t state) {
  // Existing fixture/protocol route: command endpoint3, payload begins at2;
  // command1's value is payload[2]. Inspect real submissions, not fake effects.
  return command.phase == BlePhase::Write && command.handle == 3 && command.size >= 5 &&
         command.bytes[2] == 1 && command.bytes[4] == state;
}
int powerApplication(const std::string &mode) {
  HardwareSerial uart;
  SPIClass spi;
  TwoWire wire;
  auto q = qualified();
  const bool imu_barrier = mode == "power-imu-final-barrier";
  const bool leave_recording = mode == "power-camera-leave-recording";
  q.runtime.imu_enabled = imu_barrier;
  q.runtime.cameras_qualified = false;
  SourceConfig source;
  MixedCameraQualifications cameras;
  if (leave_recording)
    powerHero12Fixture(q, source, cameras);
  FixturePower power;
  power.input.acc = PowerSignal::Active;
  power.input.power_fail = PowerSignal::Inactive;
  power.input.reserve = PowerReserve::Ready;
  power.input.supply = PowerSupply::Usable;
  power.fail_initialize = mode == "power-init-refusal";
  power.fail_read = mode == "power-read-refusal";
  power.fail_off = mode == "power-off-failure";
  PowerShutdownConfig config;
  config.enabled = config.qualified = config.off_qualified = true;
  config.startup_ms = 20;
  config.acc_off_ms = 10;
  config.drain_ms = 50;
  const bool manual = mode == "power-manual-disabled";
  if (manual)
    config = {};
  SupervisedEsp32Application app(uart, spi, wire, q, QualifiedHandlebar{}, cameras, nullptr, config,
                                 &power);
  if (mode == "power-init-refusal" || mode == "power-read-refusal") {
    for (unsigned i = 0; i < 3; ++i) {
      assert(!app.startupReady());
      raw_time.fetch_add(20);
    }
    const auto startup = app.powerStatus();
    assert(startup.policy.state == PowerShutdownState::BootGuard);
    assert(!startup.policy.protected_ready && !startup.policy.admission_allowed);
    assert(worker_threads.empty() && !app.telemetry() && !mounts && !uart.writes);
    assert(startup.backend.attempted && power.initializes == 1);
    if (mode == "power-init-refusal")
      assert(startup.backend.refused && !startup.backend.ready &&
             startup.backend.initialization_error == 71 && !power.reads);
    else
      assert(startup.backend.ready && startup.backend.read_error == 72 && power.reads == 3);
    assert(app.requestShutdown());
    const auto stopped = app.powerStatus();
    assert(stopped.policy.sealed && stopped.policy.teardown_complete && stopped.policy.io_error);
    assert(stopped.policy.outcome == PowerShutdownOutcome::Failed && app.canRelease());
    assert(!app.startupReady() && worker_threads.empty());
    return 0;
  }
  if (mode == "power-reserve-unknown" || mode == "power-reserve-not-ready") {
    power.input.reserve =
        mode == "power-reserve-unknown" ? PowerReserve::Unknown : PowerReserve::NotReady;
    assert(!app.startupReady());
    raw_time.fetch_add(2000);
    assert(!app.startupReady());
    const auto parked = app.powerStatus();
    assert(parked.policy.state == PowerShutdownState::BootGuard && !parked.policy.sealed);
    assert(!parked.policy.protected_ready && !parked.policy.admission_allowed);
    assert(worker_threads.empty() && !app.telemetry() && !mounts && !uart.writes && !power.offs);
    power.input.reserve = PowerReserve::Ready;
    assert(!app.startupReady());
    raw_time.fetch_add(19);
    assert(!app.startupReady());
    raw_time.fetch_add(1);
    assert(app.startupReady());
    assert(app.powerStatus().policy.protected_ready && app.powerStatus().policy.admission_allowed);
    assert(worker_threads.empty() && !app.telemetry() && !power.offs);
    return 0;
  }
  if (mode == "power-boot-off" || mode == "power-manual-prelaunch") {
    power.input.acc = PowerSignal::Inactive;
    assert(!app.startupReady());
    if (mode == "power-manual-prelaunch")
      assert(app.requestShutdown());
    raw_time.fetch_add(2000);
    if (mode == "power-manual-prelaunch")
      power.input.acc = PowerSignal::Active;
    for (unsigned i = 0; i < 25; ++i) {
      assert(!app.startupReady());
      raw_time.fetch_add(1);
    }
    assert(worker_threads.empty() && !app.telemetry() && power.offs <= 1);
    return 0;
  }
  if (!manual) {
    assert(!app.startupReady());
    raw_time.fetch_add(20);
  }
  assert(app.startupReady());
  SettingsSnapshot settings;
  settings.completed = settings.effective = true;
  settings.epoch = 1;
  settings.generation = 1;
  assert(copySettings(source, settings.settings));
  if (leave_recording) {
    settings.peers = q.runtime.peers;
    settings.peers_valid = true;
  }
  HealthSupervisor sup;
  auto policy = app.prepare(settings, false, true, sup);
  assert(sup.begin(policy, millis()));
  if (mode == "power-launch-loss") {
    power.input.power_fail = PowerSignal::Active;
    app.launch();
    assert(worker_threads.empty() && !app.telemetry());
    assert(app.powerStatus().policy.sealed);
    return 0;
  }
  commission();
  if (mode == "power-sd-refusal")
    fail_sd = true;
  app.launch();
  if (mode == "power-sd-refusal") {
    assert(app.telemetry() && app.telemetry()->status().phase == TelemetryPhase::Refused);
    assert(app.telemetry()->status().fault == TelemetryFault::StorageTask);
    assert(worker_threads.empty() && sd_tasks == 1 && !mounts && app.canRelease());
    assert(app.requestShutdown());
    const auto stopped = app.powerStatus();
    assert(stopped.policy.sealed && stopped.policy.teardown_complete && stopped.policy.io_error);
    assert(stopped.policy.outcome == PowerShutdownOutcome::Failed);
    assert(stopped.telemetry_fault == TelemetryFault::StorageTask && power.offs == 1);
    app.service(0, 0, false);
    assert(app.powerStatus().policy.outcome == PowerShutdownOutcome::Failed && power.offs == 1);
    assert(!app.startupReady());
    return 0;
  }
  for (unsigned i = 0; i < 500 && app.telemetry()->status().phase != TelemetryPhase::Running; ++i) {
    app.service(0, 0, false);
    raw_time.fetch_add(1);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  if (app.telemetry()->status().phase != TelemetryPhase::Running) {
    const auto failed = app.telemetry()->status();
    std::fprintf(stderr, "power fixture %s: phase=%u fault=%u storage_error=%d terminal=%d\n",
                 mode.c_str(), unsigned(failed.phase), unsigned(failed.fault), failed.storage_error,
                 failed.storage.terminal);
  }
  assert(app.telemetry()->status().phase == TelemetryPhase::Running);
  size_t camera_seal_boundary = 0;
  if (leave_recording) {
    auto runtime = mixedCameraRuntime();
    auto &host = Esp32BleHost::instance();
    assert(app.telemetry()->status().camera == CameraAdmission::Admitted);
    assert(runtime.manager.request(0, Operation::Connect) == CameraError::None);
    auto pump = [&]() {
      host.driveProcedures();
      host.driveRetirements();
      app.service(0, 0, false);
    };
    for (unsigned i = 0; i < 200 && !runtime.adapter.commandReady(0); ++i)
      pump();
    assert(runtime.adapter.commandReady(0) && host.links[0]);
    assert(runtime.group.request(RecordingState::Recording) == GroupError::None);
    for (unsigned i = 0; i < 200 && (!host.encoding[0] || runtime.manager.state(0)->observed !=
                                                              RecordingState::Recording);
         ++i)
      pump();
    assert(host.encoding[0] && runtime.manager.state(0)->observed == RecordingState::Recording);
    bool video_prepared = false, start_submitted = false;
    for (const auto &command : host.commands) {
      if (command.phase == BlePhase::Write && command.handle == 3 && command.size >= 3 &&
          command.bytes[2] == 0x3e)
        video_prepared = true;
      start_submitted = start_submitted || powerShutter(command, 1);
      assert(!powerShutter(command, 0));
    }
    assert(video_prepared && start_submitted);
    camera_seal_boundary = host.commands.size();
  }
  if (imu_barrier) {
    assert(app.telemetry()->status().imu == SensorAdmission::Admitted && imu_tasks == 1);
    pause_imu_final.store(true);
  }
  if (mode == "power-blocked-close")
    block_close.store(true);
  if (manual)
    assert(app.requestShutdown());
  else if (mode == "power-running-read-failure") {
    power.fail_read = true;
    app.service(0, 0, false);
    const auto failed = app.powerStatus();
    assert(failed.policy.outcome == PowerShutdownOutcome::Failed && failed.policy.io_error);
    assert(failed.policy.cause == PowerShutdownCause::ObservationInvalid);
    assert(failed.backend.read_error == 72 && power.offs == 1);
    power.fail_read = false;
  } else {
    power.input.power_fail = PowerSignal::Active;
    app.service(0, 0, false);
  }
  assert(app.powerStatus().policy.sealed &&
         app.telemetry()->status().phase != TelemetryPhase::Running);
  if (leave_recording) {
    assert(!mixedCameraRuntime().adapter.canDestroy() && !app.canRelease());
    assert(Esp32BleHost::instance().encoding[0]);
  }
  power.input.power_fail = PowerSignal::Inactive;
  power.input.acc = PowerSignal::Active;
  if (imu_barrier) {
    for (unsigned i = 0; i < 1000 && !imu_final_entered.load(); ++i) {
      app.service(0, 0, false);
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    assert(imu_final_entered.load() && !app.canRelease());
    assert(!app.telemetry()->status().imu_worker.finished && !unmounts);
    assert(!app.telemetry()->owner().session()->stopped());
    assert(!app.powerStatus().policy.teardown_complete && !power.offs);
    raw_time.fetch_add(50);
    app.service(0, 0, false);
    const auto timed_out = app.powerStatus();
    assert(timed_out.policy.outcome == PowerShutdownOutcome::TimedOut);
    assert(!timed_out.policy.teardown_complete && !app.canRelease() && power.offs == 1);
    assert(!app.startupReady());
    release_imu_final.store(true);
  }
  if (mode == "power-blocked-close") {
    for (unsigned i = 0; i < 1000 && !close_entered.load(); ++i) {
      app.service(0, 0, false);
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    assert(close_entered.load() && !app.canRelease());
    raw_time.fetch_add(50);
    app.service(0, 0, false);
    assert(app.powerStatus().policy.outcome == PowerShutdownOutcome::TimedOut);
    assert(!app.canRelease() && power.offs == 1);
    release_close.store(true);
  }
  for (unsigned i = 0; i < 1000 && !app.canRelease(); ++i) {
    if (leave_recording) {
      Esp32BleHost::instance().driveRetirements();
      Esp32BleHost::instance().driveProcedures();
    }
    app.service(0, 0, false);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  assert(app.canRelease());
  app.service(0, 0, false);
  if (mode == "power-off-failure") {
    const auto failed = app.powerStatus();
    assert(failed.policy.outcome == PowerShutdownOutcome::Failed && failed.policy.io_error);
    assert(failed.policy.teardown_complete && failed.backend.off_attempted);
    assert(!failed.backend.off_succeeded && failed.backend.off_error == 73);
    assert(failed.report_generation > 0);
    for (unsigned i = 0; i < 3; ++i)
      app.service(0, 0, false);
    assert(app.powerStatus().policy.outcome == PowerShutdownOutcome::Failed && power.offs == 1);
  } else if (mode == "power-running-read-failure") {
    assert(app.powerStatus().policy.outcome == PowerShutdownOutcome::Failed);
    assert(app.powerStatus().policy.io_error && app.powerStatus().policy.teardown_complete);
  } else if (mode == "power-blocked-close" || imu_barrier)
    assert(app.powerStatus().policy.outcome == PowerShutdownOutcome::TimedOut);
  else
    assert(app.powerStatus().policy.outcome == PowerShutdownOutcome::Clean);
  assert(power.offs == (manual ? 0U : 1U));
  assert(!app.startupReady()); // Returning ACC never opens the old session.
  if (leave_recording) {
    auto &host = Esp32BleHost::instance();
    assert(mixedCameraRuntime().adapter.canDestroy() && host.releases > 0);
    assert(host.encoding[0]); // Transport disconnection did not stop the camera.
    for (const auto &command : host.commands)
      assert(!powerShutter(command, 0));
    for (size_t i = camera_seal_boundary; i < host.commands.size(); ++i)
      assert(host.commands[i].phase != BlePhase::Connect && !powerShutter(host.commands[i], 1));
    const auto completed_commands = host.commands.size();
    for (unsigned i = 0; i < 3; ++i) {
      assert(!app.startupReady());
      app.service(0, 0, false);
      host.driveRetirements();
      host.driveProcedures();
    }
    assert(host.commands.size() == completed_commands && host.encoding[0] && power.offs == 1);
  }
  for (auto &thread : worker_threads)
    thread.join();
  assert(proof_sets == 0); // No collapse-time recovery persistence.
  return 0;
}

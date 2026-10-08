#include "ble_esp32.h"
// Actual main/application/adapter/central/config-owner; host SDK event scheduler
// is synthetic here. Exact pinned SDK semantics remain the independent 103 cases.
static void resetCases(SupervisedEsp32Application &app, const std::string &mode) {
  auto runtime = hero12Runtime();
  auto &host = Esp32BleHost::instance();
  const auto settings = application_settings.snapshot();
  auto request = [&](uint32_t operation = 1, uint32_t timeout = 1000) {
    return app.requestPairingReset(0, 301, settings.epoch, settings.generation, operation, timeout);
  };
  auto pump = [&]() {
    host.driveProcedures();
    host.driveRetirements();
    loop();
  };
  auto independentProgress = [&](bool query) {
    const auto before = app.telemetry()->status();
    const auto observed = runtime.manager.state(1)->last_observed_ms;
    if (query)
      assert(runtime.manager.request(1, Operation::Query) == CameraError::None);
    for (unsigned i = 0; i < 100; ++i) {
      raw_time.fetch_add(1);
      pump();
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    const auto after = app.telemetry()->status();
    assert(after.at_completed > before.at_completed && after.sd_completed > before.sd_completed &&
           after.imu_worker.completed > before.imu_worker.completed);
    if (query)
      assert(runtime.manager.state(1)->has_observation &&
             runtime.manager.state(1)->last_observed_ms > observed &&
             runtime.adapter.commandReady(1));
  };
  if (mode == "main-reset-stale" || mode == "main-reset-ambiguous" ||
      mode == "main-reset-admission") {
    if (mode == "main-reset-stale") {
      assert(app.requestPairingReset(0, 301, settings.epoch, settings.generation + 1, 1, 1000) ==
             BondResetSubmission::Refused);
      assert(app.requestPairingReset(0, 999, settings.epoch, settings.generation, 1, 1000) ==
             BondResetSubmission::Refused);
      assert(request(1, 5001) == BondResetSubmission::Refused);
    } else if (mode == "main-reset-admission") {
      nvs_revoked = true;
      assert(request() == BondResetSubmission::Refused);
      nvs_revoked = false;
    } else
      assert(request() == BondResetSubmission::Refused);
    assert(!runtime.manager.sealed(0) && !proof_sets && !host.reset_submissions);
    return;
  }
  if (mode != "main-reset-scan")
    assert(runtime.manager.request(0, Operation::Connect) == CameraError::None);
  assert(runtime.manager.request(1, Operation::Connect) == CameraError::None);
  for (unsigned i = 0;
       i < 200 && ((mode != "main-reset-scan" && !runtime.adapter.commandReady(0)) ||
                   !runtime.adapter.commandReady(1));
       ++i)
    pump();
  assert((mode == "main-reset-scan" || runtime.adapter.commandReady(0)) &&
         runtime.adapter.commandReady(1));
  if (mode == "main-reset-scan") {
    assert(runtime.adapter.requestRecovery(0, true, Hero12PowerCondition::BleConnectable) ==
           CameraError::None);
    loop();
    for (size_t i = 0; i < host.commands.size(); ++i)
      if (host.commands[i].phase == BlePhase::Scan)
        host.held = host.contexts[i];
    assert(host.held);
  }
  const auto old = runtime.manager.state(0)->token;
  if (mode == "main-reset-query" || mode == "main-reset-rec" || mode == "main-reset-stop") {
    host.auto_camera = false;
    const auto operation = mode == "main-reset-query" ? Operation::Query
                           : mode == "main-reset-rec" ? Operation::Start
                                                      : Operation::Stop;
    assert(runtime.manager.request(0, operation) == CameraError::None);
    loop();
    host.driveProcedures();
    loop();
    for (unsigned i = 0; i < CameraManager::kQueueDepth; ++i)
      runtime.manager.request(0, Operation::Start);
    assert(runtime.manager.request(0, Operation::Stop) == CameraError::QueueFull);
    host.auto_camera = true;
  }
  if (mode == "main-reset-recovery") {
    runtime.adapter.requestRecovery(0, true, Hero12PowerCondition::BleConnectable);
    loop();
  }
  if (mode == "main-reset-retained")
    host.held = host.links[0];
  if (mode == "main-reset-wrap")
    raw_time.store(UINT32_MAX - 100);
  const auto at = app.telemetry()->status().at_completed;
  const auto ticks = runtime.manager.ticks(), advances = runtime.group.advancements();
  assert(request(1, mode == "main-reset-busy-deadline" ? 100 : 1000) ==
         BondResetSubmission::Queued);
  assert(app.pairingResetStatus().epoch == settings.epoch &&
         app.pairingResetStatus().generation == settings.generation &&
         app.pairingResetStatus().qualification_record == 42 &&
         app.pairingResetStatus().identity.address[0] == 1);
  assert(runtime.manager.sealed(0) &&
         runtime.manager.state(0)->observed == RecordingState::Unknown);
  assert(runtime.manager.request(0, Operation::Start) == CameraError::Cancelled);
  assert(request(2) == BondResetSubmission::Busy);
  loop();
  assert(runtime.manager.ticks() == ticks + 1 && runtime.group.advancements() == advances + 1);
  Event late(0, old, EventKind::RecordingObserved);
  late.recording = RecordingState::Recording;
  runtime.manager.event(late);
  assert(runtime.manager.state(0)->observed == RecordingState::Unknown);
  if (mode == "main-reset-cancel" || mode == "main-reset-retained" || mode == "main-reset-scan")
    assert(app.cancelPairingReset(1));
  if (mode == "main-reset-timeout" || mode == "main-reset-wrap")
    raw_time.fetch_add(1000);
  if (mode == "main-reset-config") {
    auto changed = settings;
    ++changed.generation;
    assert(settings_publication.put(changed));
  }
  if (mode == "main-reset-safe")
    safe_mode.store(true);
  if (mode == "main-reset-retained" || mode == "main-reset-scan") {
    for (unsigned i = 0; i < 4; ++i)
      pump();
    assert(app.pairingResetStatus().finished && !app.pairingResetStatus().releasable &&
           !proof_sets && !host.reset_submissions);
    independentProgress(true);
    assert(!app.pairingResetStatus().releasable);
    host.held = nullptr;
  }
  for (unsigned i = 0; i < 10 && app.pairingResetStatus().phase == ApplicationResetPhase::Retiring;
       ++i)
    pump();
  if (mode == "main-reset-cancel" || mode == "main-reset-retained" || mode == "main-reset-scan" ||
      mode == "main-reset-timeout" || mode == "main-reset-wrap" || mode == "main-reset-config" ||
      mode == "main-reset-safe") {
    pump();
    assert(app.pairingResetStatus().epoch == settings.epoch &&
           app.pairingResetStatus().generation == settings.generation);
    assert(app.pairingResetStatus().finished && app.pairingResetStatus().releasable &&
           !proof_sets && !host.reset_submissions);
    return;
  }
  assert(app.pairingResetStatus().phase == ApplicationResetPhase::RevokingProof &&
         !host.reset_submissions);
  if (mode == "main-reset-proof-set-error")
    proof_set_error = 17;
  if (mode == "main-reset-proof-commit-error")
    proof_commit_error = 18;
  if (mode == "main-reset-proof-read-error")
    proof_read_error = 19;
  const bool blocked_proof = mode == "main-reset-proof-blocked" || mode == "main-reset-proof-final";
  if (blocked_proof) {
    std::atomic<bool> entered{false}, release{false};
    auto hook = [&]() {
      entered.store(true);
      while (!release.load())
        std::this_thread::yield();
    };
    if (mode == "main-reset-proof-final")
      proof_close_hook = [&]() {
        if (proof_commits)
          hook();
      };
    else
      proof_set_hook = hook;
    std::thread worker([&]() { taskPass(config_task); });
    for (unsigned i = 0; i < 10000 && !entered.load(); ++i)
      std::this_thread::yield();
    assert(entered.load());
    assert(app.cancelPairingReset(1));
    for (unsigned i = 0; i < 10; ++i) {
      loop();
      raw_time.fetch_add(1);
    }
    assert(app.pairingResetStatus().finished && !app.pairingResetStatus().releasable &&
           !host.reset_submissions && !app.canRelease());
    independentProgress(true);
    assert(!app.pairingResetStatus().releasable && !host.reset_submissions);
    release.store(true);
    worker.join();
    proof_close_hook = {};
    proof_set_hook = {};
    loop();
    assert(app.pairingResetStatus().releasable && !host.reset_submissions &&
           app.pairingResetStatus().requalification_required);
    return;
  }
  taskPass(config_task); // Real continuous sole NVS owner, actual set+commit+readback+close.
  if (mode == "main-reset-busy" || mode == "main-reset-busy-deadline")
    host.reset_busy = 2;
  loop();
  if (mode.find("main-reset-proof-") == 0) {
    assert(app.pairingResetStatus().finished && app.pairingResetStatus().releasable &&
           !host.reset_submissions);
    assert(app.pairingResetStatus().requalification_required);
    assert(!app.pairingResetStatus().cancelled && !app.pairingResetStatus().timed_out);
    return;
  }
  if (mode == "main-reset-busy-deadline") {
    assert(host.reset_submissions == 1);
    raw_time.fetch_add(100);
    loop();
    assert(host.reset_submissions == 1 && app.pairingResetStatus().timed_out &&
           app.pairingResetStatus().releasable);
    return;
  }
  if (mode == "main-reset-busy") {
    assert(host.reset_submissions == 1);
    raw_time.fetch_add(99);
    loop();
    assert(host.reset_submissions == 1);
    raw_time.fetch_add(1);
    loop();
    assert(host.reset_submissions == 2 && app.pairingResetStatus().outcome == BondOutcome::Busy &&
           app.pairingResetStatus().releasable);
    return;
  }
  assert(host.reset_submissions == 1 && host.reset_phase == 1 &&
         app.pairingResetStatus().proof_denied);
  if (mode == "main-reset-host-blocked" || mode == "main-reset-host-timeout" ||
      mode == "main-reset-host-late" || mode == "main-reset-host-nvs" ||
      mode == "main-reset-host-supervision") {
    host.driveReset(false);
    if (mode == "main-reset-host-late")
      host.reset_result.mutation = true;
    assert(runtime.manager.request(1, Operation::Query) == CameraError::None);
    loop();
    assert(!runtime.manager.sealed(1));
    const auto prior_observation = runtime.manager.state(1)->last_observed_ms;
    independentProgress(false);
    if (mode == "main-reset-host-timeout")
      raw_time.fetch_add(1000);
    else if (mode == "main-reset-host-nvs")
      nvs_revoked = true;
    else if (mode == "main-reset-host-supervision")
      supervision_fault.store(true);
    else
      assert(app.cancelPairingReset(1));
    loop();
    assert(app.pairingResetStatus().finished && !app.pairingResetStatus().releasable);
    host.driveReset();
    loop();
    assert(
        app.pairingResetStatus().releasable &&
        app.pairingResetStatus().outcome ==
            (mode == "main-reset-host-late" ? BondOutcome::Indeterminate : BondOutcome::Refused));
    nvs_revoked = false;
    supervision_fault.store(false);
    pump();
    pump();
    if (mode != "main-reset-host-nvs" && mode != "main-reset-host-supervision")
      assert(runtime.adapter.commandReady(1) &&
             runtime.manager.state(1)->last_observed_ms > prior_observation);
    return;
  }
  if (mode == "main-reset-terminal-busy") {
    host.reset_outcome = BondOutcome::Busy;
    host.driveReset();
    loop();
    assert(host.reset_submissions == 1);
    raw_time.fetch_add(100);
    loop();
    assert(host.reset_submissions == 2 && host.reset_phase == 1);
    host.driveReset();
    loop();
    assert(app.pairingResetStatus().outcome == BondOutcome::Busy &&
           app.pairingResetStatus().releasable);
    return;
  }
  if (mode == "main-reset-host-error")
    host.reset_outcome = BondOutcome::Indeterminate;
  host.driveReset();
  loop();
  auto result = app.pairingResetStatus();
  assert(result.finished && result.releasable && result.proof_denied &&
         result.requalification_required);
  assert(result.outcome ==
         (mode == "main-reset-host-error" ? BondOutcome::Indeterminate : BondOutcome::Removed));
  assert(runtime.manager.request(1, Operation::Query) == CameraError::None);
  pump();
  pump();
  assert(runtime.adapter.commandReady(1) &&
         runtime.manager.state(0)->observed == RecordingState::Unknown);
  assert(app.telemetry()->status().at_completed >= at &&
         app.telemetry()->status().sd_completed > 0);
  assert(request() == BondResetSubmission::Stale);
  assert(request(2) == BondResetSubmission::Refused); // sealed target never reopens/replays.
  assert(app.requestPairingReset(1, 302, settings.epoch, settings.generation, 2, 1000) ==
         BondResetSubmission::Queued);
  assert(app.cancelPairingReset(2));
  for (unsigned i = 0; i < 5; ++i)
    pump();
  assert(app.pairingResetStatus().releasable);
}

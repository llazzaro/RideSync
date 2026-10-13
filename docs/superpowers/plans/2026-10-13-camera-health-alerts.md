# Camera health alerts implementation plan

> Future implementation only. Execute task by task after this software feature is
> requested; this plan does not authorize firmware work as part of investigation #53.
> Follow repository workflow on `main` and use the executing-plans skill.

**Goal:** Warn of lost recording evidence and unexpected stops, with qualified
HERO12 resource alerts, without changing shutter/recovery policy.

**Architecture:** A fixed-memory evaluator consumes model-qualified observations
in the existing serialized owner. Its coherent status feeds serial/LED output
and a separate bounded transition log. Optional HERO12 queries yield to control.

**Tech stack:** Existing C++11/PlatformIO, fake clocks/transports, pinned ESP32
and NimBLE host, typed telemetry and Python log tooling. No new dependency/task.

**Spec:** [Camera health investigation](../../camera_health_alerts.md), dated
2026-10-13, includes sources, exact thresholds, freshness and exclusions.

## Global constraints

Opt-in; eight registry slots/four live BLE peers; <=4096 incremental RAM bytes;
one background transaction per peer; one aggregate request per 500 ms; no extra
record/reconnect/wake/mode command or automatic shutter retry. Preserve Unknown,
Unsupported and Unavailable. Keep existing log formats 1–5 unchanged. Hardware
checks remain #46/#22/#31; new health support remains Experimental / Not tested.

Each task starts with independent failing behavior/fixture tests, implements the
minimum contract, runs the focused suites and finishes with a reviewed commit.
Do not execute these tasks to close the documentation investigation.

## 1. Qualified snapshots and passive evaluator

- [ ] Add `include/camera_health.h` and `src/camera_health.cpp`: typed per-field
  capability/validity, source profile, connection/receipt/age, intent episodes,
  thresholds, bounded alert transitions and saturating loss counters. No IO.
- [ ] Extend `include/camera_profile.h`, `include/camera_manager.h`,
  `src/camera_manager.cpp` and adapter owner interfaces as needed to preserve
  validated observation receipt age and expose admitted group/direct peer intents.
  Existing control API behavior and operation/connection generation rules remain.
- [ ] Add `test/test_camera_health/test_main.cpp` with independent state timelines:
  ACK-only, initial unconfirmed, Recording->Stopped, refused/queued/direct Stop,
  multiple peers, age boundaries/wrap, duplicate/out-of-order, invalid fields,
  hysteresis, absent battery, unsupported mode, reset/seal/reconnect/no replay.
  Reuse qualified X5/DJI state paths; do not enable their resource thresholds.
- [ ] Run `pio test -e native -f test_camera_health -f test_camera_manager
  -f test_recording_manager` and affected adapter suites. Add sizeof assertions
  for <=256 bytes per peer and the declared fixed transition storage.

**Completion boundary:** Pure evaluator and qualification/intent plumbing pass
focused tests; resources may remain Unknown. No output/physical alert claim yet.

## 2. HERO12 resource codec and bounded refresh

- [ ] Extend `include/protocol/gopro_codec.h`, `src/protocol/gopro_codec.cpp` with
  targeted health status Get/Register and typed fields 1/6/10/35/54/60/70.
  Preserve Busy/Ready/Encoding decoding. Unknown IDs/lengths/ranges never publish
  partly parsed health; uint64 storage retains documented kilobytes.
- [ ] Extend `include/profiles/gopro_hero12.h`, `src/profiles/gopro_hero12.cpp`
  and mixed runtime dispatch: negotiated qualified polling floor, one transaction,
  connection-scoped registration, fair aggregate scheduler, control/keep-alive
  precedence and no retry burst. Reuse Encoding results without counting ACKs.
  Route optional health rejection separately from control failure; unsupported
  fields must not tear down an otherwise functioning recording link.
- [ ] Add independently literal expected fixtures under `test/fixtures/gopro/`
  referencing the pinned official SDK source in the spec. Extend
  `test/test_gopro_codec` and `test/test_hero12_adapter` with exact widths/endian,
  malformed fragments/TLVs, missing battery, unsupported IDs, registration vs
  shutter interleaving, stale notifications, long/invalid poll period, rejected
  optional query, flood loss, deadline/cancel/generation and missing-peer progress.
- [ ] Run those suites plus `test_camera_health`, existing central/dispatcher
  suites affected by scheduling, and pinned `hero12_adapter_compile` and
  `bench_application` builds. Inspect target RAM/map/stack against <=4096 bytes.
  Refresh the code graph with `graphify update .` after source changes.

**Completion boundary:** Source/synthetic HERO12 health observations and bounded
background IO work without control regressions. Physical status remains untested;
GO 3S/RS and other resource profiles are not silently enabled.

## 3. Coherent outputs and versioned event delivery

- [ ] Compose evaluator after validated adapter/manager ingestion in
  `include/local_telemetry_runtime.h`, `src/local_telemetry_runtime.cpp` and
  application wiring. Extend `include/handlebar_control.h`,
  `src/handlebar_control.cpp`, `include/status_led.h`, `src/status_led.cpp` and
  `src/bench_application.cpp` serial status with the same owner snapshot.
  Preserve existing gestures; Critical/Error, Recovery, Warning/Partial ordering
  follows the spec. No acknowledgement gesture or haptic driver in v1.
- [ ] Add an explicit unoccupied health format/version in
  `include/telemetry_record.h`, `include/storage.h`/`src/storage.cpp`, bounded health inbox
  in `include/telemetry_admission.h`/`src/telemetry_admission.cpp`, and logger
  alongside `include/camera_event_logger.h`/`src/camera_event_logger.cpp`.
  Extend `test/telemetry_parser.py` and independent fixtures under
  `test/fixtures/telemetry/`. Never reinterpret a CameraV3 ACK/observation as health.
- [ ] Test serial/LED precedence, active alert retained when logs drop, once-only
  transitions, complete identity/episode correlations, no private identifiers,
  reset retirement versus recovery, flood coalescing, independent GPS/control
  admission, new layout parse/reject and old layout byte equality. Extend
  `test/test_handlebar_control`, `test/test_status_led`,
  `test/test_camera_event_logging`, `test/test_local_telemetry` and Python parser
  tests; add a focused health logging suite if separate.
- [ ] Run the affected native/parser suites, `scripts/check_pipeline.sh`, formatting
  and the relevant pinned application/HERO12 builds, then wait for CI. Update
  `docs/log_format.md`, `docs/handlebar_control.md`, commissioning/testing docs
  and support matrix with precise implemented versus physical evidence labels.

**Completion boundary:** Opt-in end-to-end software alerts delivered via serial,
existing LED and bounded logs, with passing checks and measured resource budget.
No camera is hardware-confirmed by this step.

## 4. Physical observation handoff, no expanded campaign

- [ ] When equipped/enabled, annotate existing #46/#22/#31 rows per the spec:
  model/firmware/configuration/revision, actual recording/manual Stop, silence/
  disconnect, representative resource readings, alert latency, playback,
  missing-peer control and ongoing logging. Record unsafe/unavailable cases
  Not run/Blocked. Do not add a soak or require thermal shutdown.

**Affected modules:** Documentation/evidence only unless an observed defect
requires a scoped corrective change and rerun of its affected cases.
**Verification:** Actual recorded observations with existing acceptance owners.
**Completion boundary:** Promote only the exact model/feature whose physical
criteria pass; blocked later models do not invalidate the software delivery.

## Review focus

Review delayed owner receipt versus acquisition, real-world notification cadence,
mode changes/GoPro encoding ambiguity, background starvation behind control,
and incomplete alert history under floods. Synthetic tests do not establish
camera firmware semantics, physical rider notice, or footage integrity.

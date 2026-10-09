# Qualified Static Motion Logging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking. Execution method awaits the user's plan review; native execution in this session is recommended.

**Goal:** Record qualified body-frame specific force/angular rate and externally qualified static roll/pitch through the existing local telemetry runtime, preserving raw evidence and honest invalidity.

**Architecture:** The serialized `TelemetryAdmission` owner runs the existing estimator and copies raw plus motion evidence into one Storage queue item. Runtime commissioning supplies immutable optional configuration and a caller-owned reference source. An explicitly selected MotionV4 layout adds a 36-column suffix to raw IMU rows; existing v1–v3 bytes and worker ownership remain intact.

**Tech Stack:** C++11, PlatformIO native/Unity, pinned Arduino ESP32/NimBLE/BMI270, Python unittest and independent CSV parser, clang-format 18.

**Spec:** [Approved motion logging design](../specs/2026-10-09-motion-logging-design.md).

## Global Constraints

- Work directly on `main`; commit/push verified changes without branches, worktrees or PRs, per `AGENTS.md`.
- `MotionAdmission`: `Disabled=0`, `Refused=1`, `Enabled=2`, `Revoked=3`. Motion defaults disabled, unqualified configuration, age limit zero.
- Explicit snapshot age: `1..60000 ms`; supplied reference is caller-owned, owner-only, bounded and alive until `canRelease()`.
- The source performs no I/O, allocation, wait, lock, exception throwing, unbounded search or runtime/admission reentry. Declarations bind the exact session/generation/batch, increase across resets/rejected updates and never wrap. Exhaustion retires the lifetime.
- The motion route is immutable: runtime uses Inbox, standalone direct sessions select Direct; wrong-route IMU input permanently revokes motion without refusing raw logging. Status freshness cannot detect an unobserved full 32-bit clock wrap; preserve the qualified owner-service/lifetime premise.
- One paired queue admission per raw record, no new RecordKind, queue, task, heap allocation or IMU-worker estimator. Existing quota stays two attempts per tick; inbox reserves all IMU records, direct events reserve only samples.
- `ImuEvidence <=240`, `ImuBatch <=968`, `ImuInbox <=3912`, proposed `MotionEvidence <=160`, `TelemetryRecord <=704`, Storage row buffer `3072` bytes, eight queue slots, write chunks `256` bytes. Do not relax failed limits.
- V4 IMU/config/health/control rows: unchanged 88-column raw prefix plus 36-column suffix, 124 total. GPS remains 36 columns; camera retains v3 schema. Preserve v1–v3 header/row bytes.
- Invalid numeric outputs are blank; finite floats use `%.9g`; dynamic validity flags always false. Historical numeric conversion and current freshness are separate facts.
- Proper right-handed `R_BS`, qualified residual calibration/compensation and independent stationary declarations are required. No invented acquisition epochs, stationary detection, gravity removal, dynamic lean, integration or accuracy claims.
- This integration does not close #13's unresolved dynamic goals or #31 physical/reference qualification. Apply the finite acceptance policy and reuse unchanged evidence.

## Review Focus

1. A sample arriving during terminal drain must not restore motion or call the source; Task 4 tests worker finish before session drain and blocked final access.
2. A caller mixing direct and inbox inputs must not regain validity from an older queued sample; Task 3 tests both route directions and preservation of raw admissions.
3. An admitted row queued before reference withdrawal must retain its historical estimate while current status immediately clears; Tasks 3/4 test immutable copying and withdrawal independently of disk progress.
4. Clock rollover and late status reads must not manufacture fresh acquisition time; Task 4 tests modulo receipt age, invalid session quality and the declared full-wrap lifetime premise.
5. Refused optional configuration, malformed floats and absent fields must not corrupt raw logging or masquerade as zero measurements; Tasks 2/4/5 test refusal, row presence and independent parser rejection.

## File Responsibilities and Shared Interfaces

- Create `include/motion_types.h`: existing plain calibration/config/reference/vector/estimate definitions plus `MotionAdmission` and copied `MotionEvidence`; no dependency on telemetry or virtual source.
- Create `include/motion_admission.h`: source interface, immutable options, route enum and current-snapshot identity, including `motion_estimator.h` for evidence/core types.
- Modify `include/motion_estimator.h`, `src/motion_estimator.cpp`: move plain types and expose the existing structural qualification check. Keep formulas/declaration ordering.
- Modify `include/telemetry_record.h`, `include/storage.h`, `src/storage.cpp`: paired payload and V4 validation/formatting; sink/worker/counters remain here.
- Modify `include/telemetry_admission.h`, `src/telemetry_admission.cpp`: fixed estimator, source invocation, route ordering, current snapshot and paired admission.
- Modify `include/camera_event_session.h`, `src/camera_event_session.cpp`: select format and construct admission once before worker binding.
- Modify `include/local_telemetry_runtime.h`, `src/local_telemetry_runtime.cpp`: qualification, status, age, terminal ordering and reference withdrawal.
- Modify `include/local_telemetry_esp32.h`, `src/local_telemetry_esp32.cpp`, `include/application_esp32.h`, `src/application_esp32.cpp`: constructor/factory pointer forwarding only; existing workers and commissioning remain authoritative.
- Create `test/fixtures/motion/evidence.h`: synthetic independent literal counts and qualified metadata, with attribution to repository-authored fixtures; no physical evidence claim.
- Modify native motion/telemetry/local-runtime/session suites, Python parser/disk/stop/ESP32 harnesses and manual compile lists affected by estimator linkage.
- Create `test/test_motion_disk.py`: actual formatter fixture plus independent parser/ABI assertions; standard unittest discovery runs it in CI.
- Modify `docs/motion_logging.md`, `docs/motion_estimator.md`, `docs/local_telemetry.md`, `docs/log_format.md`, `docs/testing.md`, `README.md`: final behavior, field list, measured resources and remaining gates.

New interfaces are additive and retain default arguments:

```cpp
// motion_types.h, after moving the existing five plain definitions unchanged
enum class MotionAdmission : uint8_t { Disabled=0, Refused=1, Enabled=2, Revoked=3 };
struct MotionEvidence {
  MotionAdmission state = MotionAdmission::Disabled;
  MotionEstimatorConfig config;
  uint32_t snapshot_max_age_ms = 0;
  StaticMotionReference reference;
  MotionEstimate estimate;
};
// MotionEstimator public member
static bool configValid(const MotionEstimatorConfig &);

// motion_admission.h
enum class MotionInputRoute : uint8_t { Direct, Inbox };
struct MotionAdmissionConfig {
  bool requested = false, imu_qualified = false;
  MotionEstimatorConfig estimator;
  uint32_t snapshot_max_age_ms = 0;
  MotionInputRoute route = MotionInputRoute::Direct;
};
class StaticMotionReferenceSource {
public:
  virtual ~StaticMotionReferenceSource() = default;
  virtual StaticMotionReference referenceFor(const ImuEvidence &) = 0;
};
struct MotionSampleIdentity {
  uint64_t session_id = 0;
  uint32_t generation = 0, batch = 0, frame = 0, byte_position = 0;
  uint32_t sensor_epoch = 0, receipt_millis32 = 0;
  bool receipt_known = false;
  uint8_t timing_flags = 0;
};
struct MotionCurrentSnapshot {
  bool current = false;
  MotionEstimate estimate;
  MotionSampleIdentity source;
  RecordTimestamp admitted_at;
};
```

All new default constructors remain valid C++11. `MotionEvidence` has no pointer/source; source pointers exist only in owner objects. `MotionCurrentSnapshot` is owner status, not additional queued evidence.

## Task 1: Plain Types and Reusable Structural Qualification

**Files:** Create `include/motion_types.h`, `include/motion_admission.h`, `test/fixtures/motion/evidence.h`; modify `include/motion_estimator.h`, `src/motion_estimator.cpp`, `include/telemetry_record.h`, `test/test_motion_estimator/test_main.cpp`.

**Consumes:** existing `ImuEvidence`, core formulas and seven native core tests.
**Produces:** the shared types above, `MotionEstimator::configValid`, `TelemetryRecord::motion`, and independent fixture helpers `motion_fixture::config()` / `motion_fixture::sample(id, raw)`.

- [ ] Write the new configuration checks and literal fixture below. Register the test with Unity; the helper belongs in the new fixture header with `#pragma once` and `#include "motion_estimator.h"`.

```cpp
namespace motion_fixture {
inline ridesync::MotionEstimatorConfig config() {
  ridesync::MotionEstimatorConfig c;
  c.mount_qualified = c.residual_calibration_qualified = true;
  c.mount_id = 7; c.calibration_id = 9;
  c.convention = ridesync::MotionCalibrationConvention::ResidualCountsOffsetThenGain;
  c.accel_compensation = c.gyro_compensation = 1;
  return c;
}
inline ridesync::ImuEvidence sample(uint64_t id, uint32_t raw) {
  ridesync::ImuEvidence e;
  e.session_id = id; e.config.generation = 3; e.config.sensor_id = 5;
  e.config.mount_id = 7; e.config.calibration_id = 9;
  e.config.sensor_state = e.config.mount_state = e.config.calibration_state =
      ridesync::Qualification::Qualified;
  e.config.accel_scale_numerator = 1; e.config.accel_scale_denominator = 2048;
  e.config.gyro_scale_numerator = 125; e.config.gyro_scale_denominator = 2048;
  e.config.accel_offset_compensation = e.config.gyro_offset_compensation = 1;
  e.config.calibration_offsets_known = e.config.calibration_gains_known = true;
  for (unsigned i=0; i<3; ++i) {
    e.config.accel_gain_numerator[i] = e.config.accel_gain_denominator[i] = 1;
    e.config.gyro_gain_numerator[i] = e.config.gyro_gain_denominator[i] = 1;
  }
  e.batch_sequence = 13; e.frame_sequence = 2; e.byte_position = 12;
  e.receipt_known = true; e.receipt_millis32 = raw;
  e.accel[2] = 2048;
  return e;
}
}
void reusable_motion_config_check_rejects_unqualified_or_improper_rotation() {
  auto c = motion_fixture::config();
  TEST_ASSERT_TRUE(MotionEstimator::configValid(c));
  c.sensor_to_body[0] = -1;
  TEST_ASSERT_FALSE(MotionEstimator::configValid(c)); // reflection
  c = motion_fixture::config(); c.calibration_id = 0;
  TEST_ASSERT_FALSE(MotionEstimator::configValid(c));
  c = motion_fixture::config(); c.gyro_compensation = 0;
  TEST_ASSERT_FALSE(MotionEstimator::configValid(c));
}
```

- [ ] Run `.venv/bin/pio test -e native -f test_motion_estimator`; require compile failure for the missing public API, not an unrelated fixture error.
- [ ] Move the existing five plain definitions unchanged. Add `MotionEvidence motion` to `TelemetryRecord`, `MotionV4` to `StorageFormat`, trivial-copy assertions and the exact 160/704-byte ceilings. Add the source/options/status types above. Reuse `rotationValid` through `configValid`: require qualified flags, nonzero mount/calibration IDs, convention 1, compensation 1 or 2 and finite orthonormal proper rotation. `update` calls it and retains existing per-evidence comparisons and declaration consumption before rejection.
- [ ] Add cases for NaN/infinity, nonorthogonal/scaled matrices, unsupported convention, each missing qualified flag/ID and both compensation states. Preserve existing static angle/count, residual, saturation, timing and turn-ambiguity tests unchanged.
- [ ] Rerun the focused core suite and format the touched C++ files with `.venv/bin/clang-format -i`. Probe sizes before proceeding; failure is a design/resource gate, not permission to increase a bound.
- [ ] Commit verified files on `main`: `refactor(motion): expose copied types and configuration qualification`.

## Task 2: Paired V4 Storage and Strict Presence Rules

**Files:** Modify `include/storage.h`, `src/storage.cpp`, `test/test_telemetry/test_main.cpp`, `test/test_telemetry_disk.py`, `test/telemetry_parser.py`; create `test/test_motion_disk.py`.

**Consumes:** Task 1's `MotionEvidence`, existing raw validation and by-value `TelemetryRecord`.
**Produces:** `StorageFormat::MotionV4`, 3072-byte buffer and additive `bool enqueueImu(const RecordTimestamp &, const ImuEvidence &, bool reserve=false, const MotionEvidence &motion={})`.

- [ ] Add a failing native formatter fixture using the existing telemetry `Sink`, `TestClock` and `drain` helpers:

```cpp
void v4_copies_paired_evidence_before_caller_mutation() {
  Sink sink;
  Storage s(sink, {42,"fw","synthetic",2,4,StorageFormat::MotionV4});
  TestClock raw; SessionClock clock(raw,42,1000);
  auto e = motion_fixture::sample(42,0);
  MotionEvidence m;
  m.state = MotionAdmission::Enabled; m.config = motion_fixture::config();
  m.snapshot_max_age_ms = 100;
  m.estimate.measurements_valid = true;
  m.estimate.specific_force_mps2.z = 9.80665f;
  TEST_ASSERT_TRUE(s.enqueueImu(clock.snapshot(),e,false,m));
  e.accel[2] = -1; m.estimate.specific_force_mps2.z = -99;
  drain(s);
  TEST_ASSERT_NOT_EQUAL(std::string::npos,sink.bytes.find("#ridesync_telemetry,4\n"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos,sink.bytes.find("#imu_layout,4,see_docs/motion_logging.md\n"));
  TEST_ASSERT_EQUAL(std::string::npos,sink.bytes.find("-99"));
  TEST_ASSERT_EQUAL_UINT32(1,s.kindHealth(RecordKind::ImuSample).accepted);
}
```

- [ ] Run `.venv/bin/pio test -e native -f test_telemetry`; expect missing overload or V4 refusal. Capture the behavioral failure after the API compiles.
- [ ] Refactor raw validation into one path shared by old and paired calls; retain rejection/counter/reservation order. The accepted `Record` owns `record.imu=e; record.motion=motion;` and is published once. V4-only payload validation rejects out-of-range state, invalid enabled config/age, false-presence contradictions, nonfinite valid numerics, dynamic-valid flags, static-valid without measurements/context, and results on non-samples. Old callers supplying the default payload retain their raw validation and bytes.
- [ ] Extend constructor and camera enqueue format admission to V4. Header chooses v4, IMU marker 4 and camera marker 3; append the spec's exact 36 fields after the six raw counters, before the final LF. Leave legacy format branches byte-identical. For inactive states populate only state and four zero flags; for Enabled serialize immutable config/age, exact sample reference (zero reference on non-samples), four flags and valid-only finite vectors/angles. Do not serialize unqualified NaN configuration.

```cpp
// In the V4 suffix formatter: exact inactive-state field positions.
if (m.state != MotionAdmission::Enabled) {
  c.append(",%u", static_cast<unsigned>(m.state));
  for (unsigned field = 1; field < 36; ++field) {
    c.append(",");
    if (field >= 24 && field <= 27) c.append("0");
  }
  return c.ok;
}
```

- [ ] Test all four states and each IMU kind, accepted queue copying, queue saturation/GPS reservation, zero/short writes and flush failure. No second motion counter or persistence ACK exists. Invalid derived config must be represented as Refused upstream; raw malformed samples continue their original raw rejection semantics.
- [ ] Build `test_motion_disk.py` using the independent literal helper and real Storage/session clock/core source files, capturing stdout CSV. Its first independent assertions are `len(imu_values)==124`, `len(gps_values)==36`, unchanged camera prefix, marker presence and raw `accel_z == '2048'`. Add old v1/v2/v3 byte comparisons to the retained fixtures. Python manual compile lists that now link admission/storage must include `src/motion_estimator.cpp`; do not weaken tests to avoid symbols.
- [ ] Add the independent parser's V4 branch before committing this task: transcribe the spec's 36 fields independently, require both layout markers and exact counts, check enum/boolean/presence/finite-value rules, reject dynamic validity, and validate enabled qualification/rotation/age plus static-reference context. Preserve the v2/v3 branches. Exercise each inactive state and valid/invalid Enabled sample in the new disk suite; Task 5 expands malformed-input and resource coverage.
- [ ] Run `.venv/bin/pio test -e native -f test_telemetry -f test_storage` and `.venv/bin/python -m unittest discover -s test -p 'test_telemetry_disk.py' -v`, then `test_motion_disk.py`. Commit as `feat(storage): add paired static motion v4 rows`.

## Task 3: Serialized Motion Admission, Route Discipline and Historical Copies

**Files:** Modify `include/telemetry_admission.h`, `src/telemetry_admission.cpp`, `test/test_telemetry/test_main.cpp`, `test/test_telemetry_stop.py`; expand `test/test_motion_disk.py`.

**Consumes:** paired Storage overload and shared options/source types.
**Produces:** additive admission constructor arguments after `CameraInbox *camera=nullptr`: `const MotionAdmissionConfig &motion={}`, `StaticMotionReferenceSource *source=nullptr`; public `beginMotionPass()`, `withdrawMotionReference()`, `revokeMotion()`, `refuseMotion()`, `motionAdmission() const`, and `motionSnapshot(uint32_t now) const` returning `MotionCurrentSnapshot`.

- [ ] Add a test source in the existing native test file. It is synthetic independent stationarity evidence, never a production auto-detector:

```cpp
struct ReferenceSource : StaticMotionReferenceSource {
  unsigned calls = 0;
  uint32_t declaration = 0;
  bool stationary = true;
  StaticMotionReference referenceFor(const ImuEvidence &e) override {
    ++calls;
    StaticMotionReference r;
    r.externally_stationary = stationary;
    r.session_id=e.session_id; r.config_generation=e.config.generation;
    r.batch_sequence=e.batch_sequence; r.declaration=++declaration;
    return r;
  }
};
void selected_inbox_cannot_be_restored_after_direct_input() {
  Sink sink; Storage s(sink,{42,"fw","synthetic",2,4,StorageFormat::MotionV4});
  TestClock raw; SessionClock clock(raw,42,1000); ImuInbox inbox;
  MotionAdmissionConfig m; m.requested=m.imu_qualified=true;
  m.estimator=motion_fixture::config(); m.snapshot_max_age_ms=100;
  m.route=MotionInputRoute::Inbox; ReferenceSource source;
  TelemetryAdmission a(clock,s,inbox,nullptr,m,&source);
  ImuBatch b; b.count=1; b.records[0]=motion_fixture::sample(42,0);
  TEST_ASSERT_TRUE(inbox.publish(b));
  TEST_ASSERT_TRUE(a.event(b.records[0])); // raw event survives wrong route
  TEST_ASSERT_EQUAL_INT(MotionAdmission::Revoked,a.motionAdmission());
  TEST_ASSERT_EQUAL_UINT8(1,a.tick());
  TEST_ASSERT_EQUAL_UINT32(0,source.calls);
  TEST_ASSERT_FALSE(a.motionSnapshot(raw.time).current);
  TEST_ASSERT_EQUAL_UINT32(2,s.kindHealth(RecordKind::ImuSample).accepted);
}
```

- [ ] Run the telemetry suite for RED. Implement the fixed estimator/source/state/current fields in `TelemetryAdmission`, initialized from copied options. Requested=false is Disabled; requested but ineligible/config-invalid/age-invalid is Refused; otherwise Enabled. No allocation or source call in construction.
- [ ] Route `event()` and the inbox branch through one private `admitImu(timestamp,evidence,route,reserve)` helper. Wrong-route IMU permanently revokes before processing; GPS/camera do not. Enabled selected-route samples call source once (or use default reference), call `update`, copy MotionEvidence and make one enqueue attempt. Non-samples reset and copy zero reference/results. Disabled/refused/revoked records preserve raw admission with inactive suffixes. Failed raw/queue admission clears current state.
- [ ] Preserve exact reservation and two-attempt fairness: direct sample=true, direct config/health/control=false, inbox all=true. Keep camera turns and acquired final publication recheck unchanged. Add estimator source to test stop fixture's C++ link command; keep its deterministic scheduling barrier.
- [ ] Implement `beginMotionPass` to clear current estimate/source without clearing declaration history; selected processing can republish in the same pass. `withdrawMotionReference` resets output/history snapshot but keeps highest declaration; `revokeMotion` changes only Enabled to Revoked and clears; `refuseMotion` changes only Enabled to Refused before initial producer admission. Neither Disabled nor Refused becomes Revoked/Enabled. `requestStop` revokes immediately before final draining.
- [ ] Publish current only after successful enqueue, valid conversion, known receipt, clear timing bits 0..2, valid admission monotonic quality and modulo age bounded by configured age and admission elapsed time. `motionSnapshot(now)` extends elapsed from its retained admission raw time, bounds it by SessionClock's allowed duration and returns a zeroed result on failure. It never ticks/mutates SessionClock or calls source. Store admission raw time and identity, not a fabricated sample epoch. Full-wrap ambiguity remains a documented caller lifetime premise.
- [ ] Add independent literals for upright and `(0,700,1924)` roll=0.349066 within 0.001; without a source vectors remain valid and tilt false. Exercise direct route separately, reverse wrong-route revocation, source-context mismatch, reused declarations across withdrawal/control/reset, rejected samples consuming fresh declarations, endpoint/timing vetoes, null/default source, nonzero biases/gains/rotation, queue-copy mutation and two-record quotas including failures. A unit-norm force without external stationarity must never yield dynamic/static lean.
- [ ] In the disk fixture, enqueue a valid row, withdraw/revoke before worker drain, then assert the old row's literal estimate survives while later inactive rows have blank numerics. Test full queue and short-write/flush loss with no additional derived admission.
- [ ] Run telemetry/stop/disk affected checks and commit `feat(telemetry): admit ordered static motion evidence`.

## Task 4: Runtime Freshness, Terminal Ordering and Concrete Forwarding

**Files:** Modify session/runtime/ESP32/application header/source pairs listed above, `test/test_local_telemetry/test_main.cpp`, `test/test_camera_event_logging/test_main.cpp`, `test/test_local_telemetry_esp32.py`, `test/fixtures/local_telemetry/runtime.cpp`, `test/fixtures/local_telemetry/main_app.inc`.

**Consumes:** admission methods, immutable options/source.
**Produces:** LocalTelemetryConfig fields and status fields from the spec; additive final source pointer on constructors/factories, runtime `withdrawMotionReference()` and a non-inline age-checking `LocalTelemetryStatus status() const`.

- [ ] Extend the existing runtime `Rig` test, using Task 1's fixture and Task 3's `ReferenceSource`. Include the fixture header and source definition in this native suite.

```cpp
void real_runtime_admits_motion_then_clears_without_new_sample() {
  Rig f; auto c=qualified();
  c.motion_enabled=true; c.motion_config=motion_fixture::config();
  c.motion_snapshot_max_age_ms=100; ReferenceSource source;
  LocalTelemetryRuntime r(f.raw,f.uart,f.sd,f.imu,f.adapter,f.manager,f.group,c,nullptr,&source);
  TEST_ASSERT_TRUE(r.start());
  for (unsigned i=0; i<20 && r.status().phase!=TelemetryPhase::Running; ++i) f.pass(r);
  TEST_ASSERT_EQUAL_INT(TelemetryPhase::Running,r.status().phase);
  ImuBatch b; b.count=1;
  b.records[0]=motion_fixture::sample(r.status().identity.id,f.raw.value);
  TEST_ASSERT_TRUE(f.imu.inbox->publish(b));
  f.pass(r,false);
  const auto before=r.status();
  TEST_ASSERT_TRUE(before.motion_current);
  TEST_ASSERT_TRUE(before.motion_estimate.static_tilt_valid);
  TEST_ASSERT_FLOAT_WITHIN(0.00001f,9.80665f,before.motion_estimate.specific_force_mps2.z);
  f.pass(r,false);
  TEST_ASSERT_FALSE(r.status().motion_current);
  TEST_ASSERT_EQUAL_UINT32(1,source.calls);
  f.finish(r);
  TEST_ASSERT_NOT_EQUAL(std::string::npos,f.sd.fs.bytes.find("#ridesync_telemetry,4\n"));
}
```

- [ ] Run local-runtime native suite for RED. Add `motion_enabled=false`, `motion_config`, `motion_snapshot_max_age_ms=0` to LocalTelemetryConfig. Add status `motion_admission`, `motion_current`, `motion_estimate`, `motion_source` (the copied MotionSampleIdentity). Append source pointer after existing `power=nullptr`; copy config and keep caller-owned source lifetime.
- [ ] Append session arguments `const MotionAdmissionConfig &motion={}`, `StaticMotionReferenceSource *source=nullptr`. Its Storage selects MotionV4 whenever requested, even if refused; otherwise CameraV3. Active construction supplies requested/eligible config with Inbox route before `sd_.bind`. Sensor admission failure calls `refuseMotion` before the first tick; absent/unqualified IMU motion refuses without altering raw startup rules. Rejected startup before Active still publishes requested Refused status; default disabled remains Disabled. Existing raw qualification may still refuse the whole runtime for its original reasons.
- [ ] Start every owner service pass with `beginMotionPass`. Observe terminal storage/sensor outcome and current safe mode before `s.service` drains inbox; revoke before the drain on stop, terminal media, finished/failed IMU or safe mode. Control/health records alone reset, allowing a genuine in-session recovery with fresh declaration. `requestStop`, `currentAdmission` safe-mode entry and `supervision` IMU-bit/global-fault branches revoke immediately; unrelated camera withdrawal/refusal preserves independent motion.
- [ ] `observe` copies admission state and snapshot. `status()` checks `raw_.now()` again through `motionSnapshot` and returns zeroed estimate flags/numerics when stale, without changing the owner clock. `withdrawMotionReference()` forwards only on the owner; preserve queued historical payloads and source lifetime through `canRelease`.
- [ ] Forward the optional source through Esp32LocalTelemetry, SupervisedEsp32Application, `ridesync_local_telemetry_runtime` and `ridesync_supervised_application`. Their first call fixes resources/config/source; subsequent factory arguments never replace it. Add default null trailing arguments to declarations and definitions; existing calls in main/fixtures continue compiling. Expose withdrawal via the existing `owner()`; no new ISR/worker callback or firmware activation.
- [ ] Add runtime tests for disabled, missing calibration, bad transform/age 0/60001, safe-mode refusal, IMU task failure, null source, withdrawal, source mismatch/reuse, sensor finish with final sample pending, media terminal fault, requestStop before producer finish, camera-only/empty pass, later same-pass health/control record and older queued rows. Age tests cover age==limit, age==limit+1, raw rollover and receipt predating the session; status alone must expire. Camera refusal leaves motion/GPS/raw alive; global/IMU supervision faults revoke permanently.
- [ ] Extend actual ESP32/main harness modes to cover optional source forwarding, default-null behavior, V4 same-sink rows, motion refusal with continued raw/GPS, first-call source binding and final source noninvocation. Retain existing control advancement, final-access barriers and startup task policies; do not replace concrete runtime with a mock estimator wrapper. Update manual C++ compile lists for any new out-of-line core dependencies.
- [ ] Add a `motion-csv` mode to `test/fixtures/local_telemetry/runtime.cpp` that commissions qualified synthetic metadata, feeds the literal counts/reference, services the actual concrete runtime to final close and prints its sink bytes. In `test_local_telemetry_esp32.py`, run that mode with the already-built harness and pass its stdout through the independent parser. This is the actual runtime/admission/Storage disk acceptance fixture; the lower-level formatter fixture alone cannot satisfy runtime integration.

```python
run = subprocess.run([str(exe), 'motion-csv'], capture_output=True, timeout=10)
self.assertEqual(0, run.returncode, run.stderr.decode())
rows = parse(run.stdout.decode('ascii'))
sample = next(row for row in rows if row['kind'] == 'imu')
self.assertEqual('2048', sample['accel_z'])
self.assertEqual('1', sample['motion_measurements_valid'])
self.assertAlmostEqual(9.80665, float(sample['motion_force_z_mps2']), places=5)
self.assertEqual('0', sample['motion_dynamic_lean_valid'])
```

Import `parse` from `telemetry_parser` in this Python harness; use Task 2's V4 parser and keep the real runtime fixture in the passing Task 4 gate.
- [ ] Run native local-runtime/camera-event suites, `.venv/bin/python -m unittest discover -s test -p 'test_local_telemetry_esp32.py' -v`, then pinned default and retained HERO12 builds. Commit `feat(runtime): compose qualified static motion logging`.

## Task 5: Independent Parser, Resource Gates and Final Evidence

**Files:** Modify `test/telemetry_parser.py`, `test/test_motion_disk.py`, affected disk/parser tests, documentation listed above and this plan's completed checkboxes. Add an ABI probe in the existing ESP32 harness or a test-only compiled translation unit; do not add production telemetry/tasks for measurement.

**Consumes:** the actual runtime/formatter outputs, approved field order and fixed limits.
**Produces:** independently validated V4 logs, exact native/Xtensa sizes/compiler frames, documented behavior and verified main commit.

- [ ] Audit Task 2's independently defined 36-field `MOTION` suffix against the approved spec, not the formatter. Accept only v2/v3/v4, require v4 IMU and camera markers, preserve existing v2/v3 branches and reject unknown versions/partial final rows. Confirm V4 parser validates exact row counts, enum/boolean ranges, blank/present rules, finite ASCII numeric syntax, qualified rotation/convention/compensation/IDs and age 1..60000. Valid tilt requires measurements, stationary context matching raw session/generation/batch and nonzero declaration, known receipt and clear timing bits; both dynamic flags must be zero. Add the rejection tests below before tightening any missed check.

```python
# In test_motion_disk.py, using parsed actual fixture stdout:
rows = parse(data)
sample = next(row for row in rows if row['kind'] == 'imu')
self.assertEqual('2048', sample['accel_z'])
self.assertEqual('1', sample['motion_measurements_valid'])
self.assertAlmostEqual(9.80665, float(sample['motion_force_z_mps2']), places=5)
self.assertEqual('0', sample['motion_dynamic_lean_valid'])
self.assertEqual('0', sample['motion_dynamic_acceleration_valid'])
# Mutate one field in a CSV row at a time and call parse on the full file.
# Every mutation below must raise ValueError, retaining original header/markers.
```

- [ ] Add parser rejection cases: wrong version/markers/124-column count; false flag with populated vector/angle; true flag with blank/NaN/Inf/exponent overflow; reflected matrix, unsupported convention, unknown compensation, zero/too-large age; mismatched stationary session/generation/batch; dynamic flag true; missing receipt/stale/discontinuous tilt; inactive state containing configuration/reference/numerics. Do not use the estimator as expected truth or formatter-owned field constants as the parser's schema.
- [ ] Verify worst-case raw prefix and enabled/disabled suffixes independently: 1848 raw bound plus 36*21=756 gives 2604 before termination margin, fitting 3072. Emit maximum legal integer widths, finite positive/negative float extremes for valid numeric conversion, every optional raw field and header metadata; actual rows/headers stay below the buffer. Count 256-byte sink chunks. Do not assert static tilt for extreme vectors.
- [ ] Print native sizes in the disk/runtime fixture and assert ceilings:

```cpp
static_assert(sizeof(ImuEvidence)<=240 && sizeof(ImuBatch)<=968 && sizeof(ImuInbox)<=3912,
              "raw transport budget changed");
static_assert(sizeof(MotionEvidence)<=160 && sizeof(TelemetryRecord)<=704,
              "motion payload budget failed");
static_assert(Storage::kCapacity==8 && Storage::kMaxRowBytes==3072 && Storage::kChunkBytes==256,
              "storage resource contract changed");
std::fprintf(stderr,"motion=%zu record=%zu storage=%zu admission=%zu runtime=%zu\n",
  sizeof(MotionEvidence),sizeof(TelemetryRecord),sizeof(Storage),
  sizeof(TelemetryAdmission),sizeof(LocalTelemetryRuntime));
```

- [ ] Use the pinned Xtensa compiler with the actual target headers/flags to compile a probe containing arrays sized by `sizeof` each payload/queue/Storage/admission/CameraEventSession/LocalTelemetryRuntime/Esp32LocalTelemetry/SupervisedEsp32Application. Inspect symbol sizes with target `nm -S`. Capture private Active sizes by a test-only friend/probe or compiler debug layout; no size guesses or summed parent/child objects. Compare with the pre-change main ABI using the same compiler/flags. Report eight-slot delta, 1024-byte buffer delta, estimator/status additions and compiler `.su` frames for enqueue, admission/tick, runtime service/status and wrapper calls. Peak stack/latency/heap remain unmeasured.
- [ ] Run the focused final commands below once. Fix real failures; broaden/rerun only affected checks. Inspect retained ELF symbols for the concrete composition and compare default activation with baseline; linking alone is not hardware proof.

```sh
.venv/bin/pio test -e native -f test_motion_estimator -f test_telemetry -f test_storage -f test_local_telemetry -f test_camera_event_logging
.venv/bin/python -m unittest discover -s test -p 'test_motion_disk.py' -v
.venv/bin/python -m unittest discover -s test -p 'test_telemetry_disk.py' -v
.venv/bin/python -m unittest discover -s test -p 'test_telemetry_stop.py' -v
.venv/bin/python -m unittest discover -s test -p 'test_local_telemetry_esp32.py' -v
.venv/bin/python scripts/check_format.py
.venv/bin/pio run -e lilygo_t_a7670e_r2 -e hero12_adapter_compile
.venv/bin/python scripts/check_config_stack.py
git diff --check
```

- [ ] Write the actual V4 schema and commands to `docs/motion_logging.md`; update estimator/runtime/log-format/testing/README descriptions to reflect integration and default-off behavior, exact measured resource evidence and current/historical differences. Preserve the independent reference plan and unresolved original dynamic goals. No hardware result, achieved error bound or issue closure is claimed.
- [ ] Commit final evidence as `test(motion): verify v4 runtime logs and fixed resource bounds`, push verified commits directly to `origin/main`, then wait for that head's CI and inspect failures if any. Keep #13 open absent owner scope resolution/dynamic aiding. Mark this integration complete only after all tasks and measurements pass; the overarching open-issues goal remains incomplete.

## Self-Review and Execution Handoff

Coverage: types/qualification (Task 1), copied paired admission/schema (Tasks 2/3), source ordering/reference lifecycle (Task 3), real runtime/status/terminal/supervision/factory composition (Task 4), independent parsing/legacy/worst-case/ABI/stack/docs/CI (Task 5). The five Review Focus cases have explicit owning tests above. No additional hardware campaign, protocol feature or estimator retuning is introduced.

The user approved the architectural spec. This plan still awaits their review and execution-method choice. Recommend native execution in the current session: five sequential tasks share exact type, queue, lifetime and status interfaces, so one implementer avoids parallel ownership conflicts. The direct-to-main preference is already established and does not need reconfirmation.

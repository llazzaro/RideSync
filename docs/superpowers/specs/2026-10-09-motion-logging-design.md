# Qualified static motion logging in the existing runtime

Date: 2026-10-09. Status: proposed architectural specification for user review.
The user approved writing this specification from the design outline. This is
not approval to implement it or an implementation plan. Work stays on `main`;
this document creates no hardware qualification or ticket-completion claim.

## Intent and scope

Connect the existing `MotionEstimator` to the real local telemetry composition
so qualified raw samples produce recorded body-frame specific force and angular
rate, and independently qualified stationary samples can produce static roll and
pitch. Keep raw evidence, camera control, GPS admission and storage ownership
intact. A host fixture must exercise the actual runtime/admission/storage path,
not just another isolated estimator wrapper.

The original #13 dynamic motorcycle lean/pitch and gravity-free acceleration
goals remain unmet. This integration does not redefine #13 as complete. The
[feasibility plan](../../motion_estimator_plan.md) requires an explicit owner
scope resolution or adequate aiding for those goals. No gyro propagation,
GNSS-course aiding, gravity subtraction, moving bias adaptation or dynamic
accuracy claim is added. Both dynamic validity flags remain false. Physical
accuracy, installed axes/calibration and the existing independent-reference
campaign remain #31 work; no additional physical campaign or issue is created.

The existing [static core contract](../../motion_estimator.md),
[raw evidence contract](../../raw_imu.md),
[mixed rows](../../mixed_telemetry.md),
[camera rows](../../log_format.md), and
[acceptance policy](../../acceptance_policy.md) govern unchanged behavior.
Specific force includes gravity response; it is not vehicle acceleration with
gravity removed. All enabled outputs remain Experimental.

## Selected approach and alternatives

Use one copied raw-plus-motion queue item, processed by the existing admission
owner, with an explicitly selected version-4 log layout. This preserves pairing
and admission accounting without another queue or producer.

A separate motion row/queue was rejected because it doubles admission work and
can orphan derived data when one of the two admissions fails. Computing in the
IMU worker was rejected because external reference ownership would cross tasks
and the worker must continue publishing untouched raw evidence. The selected
approach adds fixed payload and formatting memory; explicit measurement gates
below address that cost.

## Types, configuration and composition

Move the existing plain `MotionEstimatorConfig`, `StaticMotionReference`,
`MotionVector`, `MotionEstimate` and calibration-convention enum definitions to
`include/motion_types.h`. `motion_estimator.h` includes those types and
`telemetry_record.h`; the latter includes only the plain motion types. This
removes the otherwise circular dependency without changing the static formulas.

Add `MotionAdmission` with numeric values `Disabled=0`, `Refused=1`, `Enabled=2`,
`Revoked=3`. Add a fixed copied `MotionEvidence` payload to `TelemetryRecord`,
not `ImuEvidence`. It contains admission state, qualified estimator configuration,
the snapshot-age limit, supplied reference and result. No pointer, string or
reference-source object enters a queued record. There is no new `RecordKind`.

Add to `LocalTelemetryConfig`: `motion_enabled` (default false),
`motion_config` (existing `MotionEstimatorConfig`, default unqualified), and
`motion_snapshot_max_age_ms` (default zero). Enabling motion requires enabled,
qualified IMU admission, a structurally valid qualified motion configuration and
an explicit age limit in 1..60000 ms. The limit is an operator-selected freshness
policy, not a measured sensor latency or accuracy guarantee. Invalid motion
configuration produces `Refused` without refusing the existing local telemetry
start. Motion refusal never changes an existing GPS/IMU startup prerequisite.

Validate configuration through a reusable static-core configuration check rather
than duplicating matrix/convention checks. Match sample mount/calibration IDs,
compensation, gains and scales through the existing per-evidence core checks.
The configuration is copied once and immutable for the session. Delivered
`ImuEvidence.config` is also copied as received, never modified to match the
requested motion configuration. Physical calibration evidence and persistence
remain caller responsibilities. Changing the calibration, transform or their
meaning requires a fresh qualified runtime/estimator lifetime and new faithfully
assigned IDs/generations, not mutation behind an old ID.

The existing `QualifiedLocalTelemetry.runtime` carries these options through
`Esp32LocalTelemetry` to `LocalTelemetryRuntime`. `LocalTelemetryRuntime::Active`
constructs its `CameraEventSession` with the optional motion settings before
`sd_.bind(s.storage())`. The session owns the fixed estimator/admission state;
`TelemetryAdmission` is its sole execution owner. Standalone session callers
retain default arguments and existing behavior. No additional application task,
sensor start, GPIO operation or logger is introduced. The supervised boot
composition continues to opt in only from actual commissioning evidence. Default
firmware activation remains disabled.

## External stationary reference and lifetime

Introduce a caller-owned `StaticMotionReferenceSource` with a bounded owner-only
method `StaticMotionReference referenceFor(const ImuEvidence &)`. An optional
source pointer is supplied when constructing `LocalTelemetryRuntime`; the ESP32
wrapper forwards it from its constructor, rather than persisting a pointer in
configuration settings. Forward the same optional constructor argument through
`SupervisedEsp32Application` and its existing `ridesync_supervised_application`
factory, and through `ridesync_local_telemetry_runtime`; default null preserves
existing callers. The first call to each boot-lifetime factory fixes the source
as well as resource references/configuration. The caller retains the source
until `canRelease()`.
Neither a worker, ISR nor storage formatter calls it. Its method performs no I/O,
allocation, wait, lock, exception throwing or unbounded search, and must not
reenter runtime/admission methods. A fixture/operator controller must
supply independent evidence for the exact sample context; a near-1g/low-rate
check, GNSS speed or handlebar state cannot implement this qualification.

A null source or absent evidence returns the default reference. Qualified
specific-force/rate conversion still works, while static tilt stays invalid.
There is no retained stationary boolean and no automatic stationary detector.
A source may return an externally stationary declaration only for the exact
session, configuration generation and batch being processed. Declaration IDs
must strictly increase per update across every reset, including rejected updates;
the existing estimator consumes a fresh declaration even when the sample fails.
IDs do not wrap. Exhaustion requires retiring the lifetime and obtaining a fresh
external reference, rather than clearing the declaration counter.

Expose owner-only `withdrawMotionReference()` on the runtime and forward it to
admission. It immediately resets the estimate/current snapshot without resetting
the highest declaration. The source must cease supplying withdrawn declarations;
a later qualified declaration can reestablish static tilt. This call neither
stops raw acquisition nor changes immutable calibration. No callback is made for
non-samples, disabled/refused/revoked motion, stop draining or finished producers.

The source is responsible for qualifying evidence acquisition, including old
queued samples; host receipt alone cannot prove a sample was stationary. The
integration does not backdate nominal 200 Hz sample epochs or turn FIFO boundary
ticks into acquisition timestamps.

## Ordered admission and processing

All motion work runs at the current serialized `TelemetryAdmission` boundary.
Both `event()` and inbox consumption use one helper that updates/resets the core,
copies raw and motion payloads together, and makes one Storage enqueue attempt.
GPS and camera rows never invoke the estimator or source. Preserve today's exact
reservation behavior: every inbox record uses `reserve=true`; direct `event()`
reserves only `RecordKind::ImuSample`. The fair two-record tick quota includes
failed attempts and remains unchanged. Formatting and sink I/O stay on the
storage worker.

The runtime selects the inbox route for motion. Any direct IMU `event()` in this
motion-enabled runtime permanently revokes motion for that session before that
event is processed, resets its current output, and leaves raw event admission
working. This prevents direct owner events from being treated as globally ordered
with older pending inbox evidence. For a standalone direct-admission session,
select the direct route explicitly at construction; consuming any inbox IMU
record similarly revokes motion. The selected route is immutable. Direct callers
must supply every sample/configuration/health/control record in source order;
opaque raw sequence values are not a global ordering oracle. There is no merge,
sequence wrap inference or buffering to repair mixed routes.

For the selected route, every non-sample record resets the estimate and creates
an invalid motion result; no source reference is requested. Each sample replaces
the prior result through `MotionEstimator::update()`. Its existing timing,
endpoint, compensation, scale/gain, reference-context and singularity rules apply.
Raw storage validation remains authoritative for raw acceptance. If raw admission
is refused, its paired motion payload is also refused; no independent derived
admission occurs. Clear the current motion snapshot after any such failure.
A failed row cannot be represented as successfully logged motion in status.

## Current status, historical rows and terminal behavior

Extend `LocalTelemetryStatus` with `MotionAdmission motion_admission`,
`bool motion_current`, a current `MotionEstimate`, and copied source identity
(session, generation, batch, frame, byte position, sensor epoch and receipt).
Only the serialized owner reads/publishes it; other contexts receive a mailbox
copy. `motion_current` means a qualified conversion from the most recent
successfully admitted sample is eligible as a current snapshot, not that the
sample has a known acquisition epoch or measured physical accuracy.

At the beginning of every service pass, clear the current snapshot. Publish a
new one only if a sample was processed in that pass, its paired queue admission
succeeded, motion remains enabled, receipt is known, flags 0..2 are clear, and
its modulo-32 receipt age is within `motion_snapshot_max_age_ms` and fits within
session elapsed time with valid monotonic quality. A later non-sample or rejected
IMU record in the same pass clears it again. A pass that consumes only camera
records or no IMU data leaves it invalid. These conservative rules do not alter
historical numeric conversion validity.

`status()` returns a copy and rechecks receipt age against the shared raw clock,
so an old snapshot cannot remain current merely because `service()` stopped.
When it fails that check, return `motion_current=false`, false estimate flags and
zero numeric fields. Clock reads do not mutate `SessionClock` or call workers.
As with existing camera receipt translation, modulo age requires a qualified
lifetime/owner-service premise preventing an unobserved full 32-bit wrap; this
API cannot detect a 49-day lapse. Missing/ambiguous receipt never yields a current
snapshot. A queued historical row may still contain valid calibrated vectors
with stale/missing receipt because `measurements_valid` means numeric conversion,
not freshness; its retained raw timing fields expose that limitation.

Stop, terminal storage failure, IMU worker finish/failure, safe-mode entry and
wrong-route input permanently set `Revoked` for this session and clear/reset
current motion before further inbox draining. No source calls or new derived
estimates occur during that drain: final raw records still pass through the
existing queue, with a revoked/invalid motion suffix. In particular,
`LocalTelemetryRuntime::service()` currently observes worker finish before
`s.service()` drains final publication; that finish must not allow a final
sample to reestablish current validity. Previously accepted historical rows keep
their copied estimates when eventually written. A sensor health/control record
that is followed by a genuine in-session recovery resets tilt but is not itself
permanent revocation; a fresh declaration is required afterward.

`currentAdmission(cameras, safe_mode)` revokes motion on safe-mode entry
independently of raw IMU continuation. Camera-only admission withdrawal does not
revoke motion. Leaving safe mode cannot reactivate revoked motion. The existing `supervision(stalls, refused, fault)` revokes motion when either
mask contains `1 << static_cast<unsigned>(Worker::Imu)` or the global fault flag
is true; unrelated camera refusal alone does not. None of these motion policies cancel camera/GPS progress or change the
existing raw stop/drain/resource-release protocol. `Disabled` stays disabled;
`Refused` stays refused; neither calls the reference source or produces derived
values. Optional motion cannot turn an otherwise valid raw logger into a startup
failure.

## Version 4 on-disk contract

Add `StorageFormat::MotionV4`. Runtime selects it when `motion_enabled` was
requested, including a refused request; otherwise it continues selecting
`CameraV3`. Standalone v1/v2/v3 selections preserve their headers and bytes.
V4 exclusively creates the existing unique telemetry path and starts with
`#ridesync_telemetry,4`, retains existing bounded firmware/provenance/storage
policy and GPS header, and emits `#imu_layout,4,see_docs/motion_logging.md` plus
`#camera_layout,3,see_docs/log_format.md`. The same sink, session identity,
exclusive-create and flush policy apply. Existing files are never reopened or
upgraded. Readers reject unknown versions and require the declared markers.

V4 GPS rows remain 36 columns and camera rows retain their exact v3 schema.
Each `imu`, `config`, `health`, or `control` row consists of the exact 88-column
v2 raw row prefix, including its six counters, followed by this **36-column**
suffix in this exact order, for **124 columns total**:

```text
motion_state,motion_algorithm,motion_snapshot_max_age_ms,motion_convention,motion_mount_qualified,motion_residual_calibration_qualified,motion_mount_id,motion_calibration_id,motion_accel_compensation,motion_gyro_compensation,motion_r_bs_00,motion_r_bs_01,motion_r_bs_02,motion_r_bs_10,motion_r_bs_11,motion_r_bs_12,motion_r_bs_20,motion_r_bs_21,motion_r_bs_22,motion_reference_stationary,motion_reference_session_id,motion_reference_generation,motion_reference_batch,motion_reference_declaration,motion_measurements_valid,motion_static_tilt_valid,motion_dynamic_lean_valid,motion_dynamic_acceleration_valid,motion_force_x_mps2,motion_force_y_mps2,motion_force_z_mps2,motion_rate_x_rad_s,motion_rate_y_rad_s,motion_rate_z_rad_s,motion_roll_rad,motion_pitch_rad
```

`motion_state` uses the enum values above. `motion_algorithm=1` denotes the
existing static-core algorithm/convention contract, not an accuracy grade.
`motion_convention=1` denotes `ResidualCountsOffsetThenGain` (zero unsupported).
Qualified configuration and all nine row-major rotation coefficients are
repeated on every enabled row so dropping a configuration event cannot orphan
interpretation. The raw prefix retains actual delivered configuration, including
residual coefficients and compensation states; the suffix identifies the
separately qualified configuration against which those counts were checked.

For `Enabled` rows, all configuration fields are populated. Sample reference
fields retain the source return exactly, including a default all-zero reference;
non-samples use an all-zero reference. The four result flags are always 0 or 1.
The six measured vector components are populated only when
`motion_measurements_valid=1`. Roll/pitch are populated only when
`motion_static_tilt_valid=1`. Invalid numeric outputs are blank, never serialized
as meaningful measured zero. Both dynamic flags are always 0. Non-samples have
all result flags 0 and all result numeric fields blank.

For `Disabled`, `Refused` or `Revoked`, only `motion_state` and the four zero
result flags are populated; all other suffix fields are blank. In particular,
an invalid/NaN requested transform is not serialized as qualified configuration.
Reasons for refusal/revocation are owner status diagnostics, not invented raw
sensor events. Disabled/refused/revoked suffixes preserve complete raw rows.

ASCII decimal integers and finite floats formatted with `%.9g`, comma separators
and LF termination are used; no quoting, locale-dependent decimal separator,
NaN or infinity is accepted. Parser validation checks exact counts, enums,
booleans, presence rules, proper qualified rotation, positive age limit,
reference/context requirements for valid tilt, false dynamic flags and finite
values. A partial final row is invalid and is reported/discarded under the
existing policy. Queue counters remain per raw kind; paired motion has no
separate admission count or persistence acknowledgement.

## Fixed resource limits and measurement gates

No extra FIFO, queue, task or heap allocation is introduced. `ImuEvidence` stays
within its existing 240-byte bound, `ImuBatch` within 968 bytes, and `ImuInbox`
within 3912 bytes. Storage keeps eight by-value slots and 256-byte write chunks.
The estimator performs a fixed number of arithmetic operations per admitted
record; one source call is permitted per selected-route sample. The reference
source's boundedness is a caller contract, not an inferred property of an
arbitrary virtual implementation.

Proposed, **unmeasured** ceilings are 160 bytes for `MotionEvidence`, 704 bytes
for the enlarged `TelemetryRecord`, and 3072 bytes for the Storage row buffer.
Do not silently weaken these limits if a size probe fails. Measure exact native
and pinned ESP32 ABI sizes of payload, queue, Storage, admission, active runtime
and concrete wrapper; document the resulting incremental fixed RAM and enqueue/
worker temporaries before accepting implementation. No growth is charged to the
transport inbox. Relative to the previous <=544-byte record assertion, eight
slots can grow by at most 1280 bytes; buffer growth is 1024 bytes. These are
ceiling-based accounting bounds, not measured allocation or stack peaks.

The existing conservative raw-row bound is 1848 bytes. At most 20 characters
per new numeric field plus one delimiter gives 36*21=756 additional bytes;
2604 bytes fits the proposed 3072-byte buffer including termination margin.
Finite IEEE float `%.9g` output fits the 20-character allowance. Independently
verify header and worst-case row formatting bounds, not just typical fixtures.
Formatting failures remain terminal media/formatter failures under Storage's
existing contract. The new field list must not increase again without updating
the bound and review.

Existing 8192-byte worker stacks are capacities, not measured peak stack or
latency guarantees. Pinned compile/size/stack-usage evidence checks temporary
costs; actual peak stack, CPU/service latency and 200 Hz throughput under load
remain unmeasured #31 observations. This design adds no achieved performance
claim or extra physical acceptance campaign.

## Focused validation and acceptance

Use independent literal raw counts/reference fixtures and an independent v4
parser; do not share the formatter's field list or use the estimator as expected
truth. Exercise actual `LocalTelemetryRuntime`, its real `CameraEventSession`,
`TelemetryAdmission` and Storage with the existing fake worker/sink boundaries.

- Qualified counts without a source produce literal expected calibrated vectors,
  invalid static/dynamic tilt, unchanged raw counts and retained configuration.
  Exact external references produce literal known static poses and associations.
- Nonzero offsets/gains and a proper rotation survive queue copying. Mutating the
  caller's input/config/reference after admission cannot alter accepted rows.
  Wrong IDs/compensation, invalid rotation, malformed scales and absent calibration
  refuse derived validity while preserving the existing raw validation outcome.
- Reused declarations across reset, reference withdrawal, context mismatch,
  controls/health, saturation, missing/stale/ambiguous timing and unit-norm
  sustained-acceleration ambiguity exercise the real owner path. Dynamic flags
  remain false even when the source qualifies static fixtures.
- Direct and inbox routes each work independently. A mixed-route event permanently
  revokes motion without losing raw admission, bypassing reservation policy or
  allowing older inbox samples to restore tilt.
- Empty/camera-only passes, status reads after the configured age, later control
  records, stop, safe mode, sensor finish/failure and terminal media faults clear
  current output. Final raw drain cannot restore it or call the source. Older
  queued valid rows retain their historical by-value estimates.
- Disabled/refused motion starts no new worker and leaves raw/GPS/camera admission
  functioning. Source invocation count is bounded by selected-route samples, the
  two-record quota/fairness remains intact, and full queues/short writes/flush
  failure use existing per-kind loss semantics without a second derived item.
- Independently parsed v4 rows have 124 IMU fields, 36 GPS fields, unchanged v3
  camera fields, exact presence rules and finite unit-bearing values. Unknown
  versions/missing markers/partial trailing rows fail closed. Existing v1-v3
  golden bytes remain unchanged. Worst-case rows/header and exact ABI size probes
  enforce the proposed budgets.

Run the focused affected native estimator, telemetry, local-runtime and session
suites; the independent telemetry disk/parser/stop and concrete ESP32 wrapper
checks; clang-format 18 and whitespace/document checks. Build pinned
`lilygo_t_a7670e_r2` and `hero12_adapter_compile`, the latter retaining the concrete
local/supervised composition that default linking can discard. Run the relevant
CI checks and report exact results/failures. Do not infer successful activation
from linking. Broader repeated suites need a new failure or affected dependency;
unchanged verified evidence is reused under the acceptance policy.

Software acceptance of this integration requires the implementation, focused
behavior/error fixtures, explicit measured ABI bounds and pinned build/CI checks.
It does not close #13's unresolved original dynamic goals, certify an installed
sensor, establish reference availability or achieve the unmeasured accuracy and
timing targets in the feasibility plan. The written specification must be
reviewed and approved before preparing an implementation plan; that plan must
then be reviewed and its execution method selected before implementation.

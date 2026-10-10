# Experimental static motion core

`MotionEstimator` consumes the existing `ImuEvidence` raw-count contract. It is
fixed-size, performs no allocation, and is connected to opt-in serialized
[MotionV4 telemetry](motion_logging.md#implemented-static-motion-logging-v4).
This implements the feasible static portion of the
[estimator plan](motion_estimator_plan.md); #13 remains open for resolution of its dynamic lean/gravity-free acceleration goal. Physical accuracy
is unmeasured; independent reference measurements belong to #31. The separate
[bounded dynamic replay experiment](dynamic_motion_estimator.md) computes
unreliable numeric attitude/body acceleration without promoting trusted validity.

## Frames and calibration

Body axes are right-handed: X forward, Y left, Z up. The caller qualifies the
row-major sensor-to-body rotation `R_BS` and its `mount_id`; the implementation
checks finite orthonormal rows (tolerance 0.0001) and positive determinant.
An opaque mount ID alone does not supply a transform. Specific force is in m/s²,
angular rate in rad/s. At rest, an upright sensor reports +9.80665 m/s² body Z.
Specific force includes gravity response; it is not gravity-free acceleration.

The supported software convention is explicitly
`ResidualCountsOffsetThenGain`: each sensor-axis delivered count is converted as
`(raw - residual_offset_counts) * gain_numerator/gain_denominator * scale`.
Acceleration scale is the retained rational g/count multiplied by 9.80665;
gyro scale is rational degrees/s/count multiplied by pi/180. `R_BS` then rotates
both vectors. Positive finite rational scales/gains, known offsets/gains, and
qualified sensor/mount/calibration metadata are required. Endpoint counts
(-32768, -32767, +32767) or the recorded endpoint flag invalidate both vectors.
This detects software-visible endpoints, not undocumented prior clipping.
Undefined timing flag bits (4..7) invalidate both measurements and tilt, matching
the retained IMU storage schema. A rejected record clears the previous output
and consumes any fresh stationary declaration; recovery needs a new declaration.

The caller must independently qualify this residual convention for the specified
calibration ID and the exact enabled/disabled device offset-compensation states.
Residual offsets describe counts *after* the declared device compensation; they
must not reapply biases already removed by hardware. Unknown compensation states
are rejected. `calibration_method` and other IDs remain opaque: nonzero values do
not establish this convention. Changing a calibration/transform requires a new
qualified configuration/estimator; IDs/generations must faithfully identify the
configuration rather than being reused for changed meanings.

## Static reference and validity

Each update replaces the previous result. Converted measurements can be valid
without tilt. `measurements_valid` means qualified numeric conversion of the
supplied evidence, not live freshness or a current reading. The core has no
clock/expiry. The caller must retain the input identity/time and reset or replace
the snapshot on terminal, no-new-data or withdrawn external reference;
`estimate()` must not be treated as indefinitely live-valid. Static tilt
additionally requires the caller's externally known
stationary declaration for the exact session, configuration generation and FIFO
batch. A declaration has a caller-owned strictly increasing nonzero ID per
update, retained across `reset()`. Reusing an ID cannot reestablish tilt after a
reset, terminal/control record, rejected sample or lost reference. A new estimator
starts a new declaration lifetime; do not reuse old declarations with it. This is
a caller ordering contract, not authentication. Before the finite ID is exhausted,
end that estimator lifetime and obtain a fresh external stationary reference.

Feed evidence records in order, including configuration, health and control
records; non-sample records clear the estimate. Call `reset()` at terminal/session
boundaries or whenever external stationarity is lost. A fresh declaration is
required on the next update; reset does not preserve an angle. Session/generation/
batch mismatches reject a static reference. There is no smoothing or gap bridging.

Known host receipt and absence of recorded discontinuity/stale/wrap ambiguity
flags are required for tilt. Receipt/drain timestamps are host observations;
repeated receipt timestamps within a FIFO batch are accepted. Sensor-time control
boundaries are not per-frame acquisition epochs, so neither those boundaries nor
host timestamps enable gyro integration or supply a missing receipt.

Under the external static declaration, force norm within 10% of standard gravity
and rate norm at most 0.05 rad/s are sanity vetoes. They cannot establish
stationarity or exclude a steady turn/translation. They are experimental rejection
thresholds, not measured accuracy bounds or vibration qualification. Static roll
is `atan2(f_y, f_z)` and pitch is `atan2(-f_x, hypot(f_y, f_z))`, in radians. Roll
is invalid at the pitch singularity (`hypot(f_y,f_z) < 0.000001*g`). No yaw is
estimated. Zero numeric fields with false validity flags must never be interpreted
as measured zero attitude.

`dynamic_lean_valid` and `dynamic_acceleration_valid` remain false. The core
does not infer external stationarity, propagate attitude using fabricated sample
epochs, subtract gravity during motion, or claim that unit-norm low-rate force is
gravity. Motorcycle turning and sustained acceleration remain ambiguous with the
current evidence. Static telemetry/configuration/runtime integration is implemented
with default-off commissioning; any dynamic estimator remains within #13's
unresolved scope.

## Verification

`test/test_motion_estimator` supplies independent literal static poses (upright,
±20° roll/pitch, ±45° roll), nonzero residual biases/gains, a proper axis rotation,
known 125°/s rate conversion, explicit compensation/convention rejection, reset
and context mismatch, repeated/missing host receipt, endpoint saturation,
singularity, and acceleration/turn ambiguity. These host fixtures verify software
behavior and quantization tolerance; they do not qualify an installed sensor.

Sequence fixtures also cover 1,000 samples over 100 seconds of uncompensated
3.90625 degrees/s gyro bias, alternating literal 0.75g/1.25g vibration samples,
missing-sample health records, recovery with a fresh stationary declaration, and
declaration-counter exhaustion across reset. Bias must remain visible as angular
rate without inventing integrated attitude; qualified residual compensation can
restore a static observation. Rejected vibration samples must clear tilt even
when their average is 1g. These synthetic checks do not measure physical drift,
vibration tolerance or dynamic accuracy.

October 9, 2026 verification: the selected native suite passed all seven tests
(final run 1.879 s). The pinned `lilygo_t_a7670e_r2` SDK build compiled the new
core and passed (17.813 s; RAM 37,816 bytes, flash 353,437 bytes). Application
activation is unchanged, so this is compilation evidence rather than integration
or a claim that the core is retained in the default firmware after linker garbage
collection. Clang-format 18 dry-run, tracked diff whitespace check, and explicit
new-file trailing-whitespace/final-newline checks passed. No hardware test or
whole-suite rerun was performed.

October 10, 2026 sequence-test reconciliation: all 12 focused estimator cases
passed, followed by `scripts/check_pipeline.sh` (58 Python tests, the three
bench runners, 481 native cases and the pinned formatting check). Temporary
mutations removing per-update clearing or the angular-rate veto caused the new
sequence tests to fail; the production source was restored before the passing
pipeline. Independent review verified calibration identity changes and nonzero
initial tilt fixtures. No production source/dependency changed; pinned firmware
build evidence is reused from passing [baseline CI](https://github.com/llazzaro/RideSync/actions/runs/38047663566)
at `3bcb10d`. No hardware test was needed for this test/documentation change.
#13 remains open for dynamic scope resolution; physical reference acceptance
remains unperformed in #31.

October 10, 2026 undefined-flag regression: the core previously accepted timing
flag `0x10` and published valid measurements/tilt, although the storage schema
rejects undefined bits 4..7. The new regression failed on that first value before
the fix. All 14 focused estimator tests now pass, covering every undefined flag
value (16..255), previous-output clearing, consumed declarations, fresh recovery,
and all 16 defined flag combinations. The repository pipeline passed 58 Python
tests, all three bench runners, 483 native cases and formatting. Independent code
review found no actionable issues. The pinned `lilygo_t_a7670e_r2` firmware build passed (RAM 38,576 bytes,
flash 358,249 bytes). This is software validation; no hardware test is needed for
the flag check. Existing physical reference checks remain in #31 and are collected
in #46. Dynamic estimation remains unresolved in #13.

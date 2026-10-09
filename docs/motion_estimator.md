# Experimental static motion core

`MotionEstimator` consumes the existing `ImuEvidence` raw-count contract. It is
fixed-size, performs no allocation, and is connected to opt-in serialized
[MotionV4 telemetry](motion_logging.md#implemented-static-motion-logging-v4).
This implements the feasible static portion of the
[estimator plan](motion_estimator_plan.md); #13 remains open for resolution of its dynamic lean/gravity-free acceleration goal. Physical accuracy
is unmeasured; independent reference measurements belong to #31.

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

October 9, 2026 verification: the selected native suite passed all seven tests
(final run 1.879 s). The pinned `lilygo_t_a7670e_r2` SDK build compiled the new
core and passed (17.813 s; RAM 37,816 bytes, flash 353,437 bytes). Application
activation is unchanged, so this is compilation evidence rather than integration
or a claim that the core is retained in the default firmware after linker garbage
collection. Clang-format 18 dry-run, tracked diff whitespace check, and explicit
new-file trailing-whitespace/final-newline checks passed. No hardware test or
whole-suite rerun was performed.

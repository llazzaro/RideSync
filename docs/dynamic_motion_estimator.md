# Experimental bounded dynamic motion replay (#13)

This opt-in software experiment computes attitude and body-frame gravity-free
acceleration after an independently declared stationary initialization. Numeric
output has **Unreliable** quality: it is not validated general motorcycle lean,
a measured accuracy result or a live default firmware feature. Trusted dynamic
lean/acceleration validity stays false. #13 retains its original dynamic goals;
physical reference acceptance stays in #31 and is collected through #46.

## What is calculated

The input is calibrated body-frame specific force in m/s² and angular rate in
rad/s, with explicit identity, ordered sample sequence and relative sample time.
Body axes are +X forward, +Y left, +Z up. At upright rest, specific force is
`(0, 0, 9.80665)`. Counts, mounting and residual/device compensation must first
be qualified using the [static conversion contract](motion_estimator.md).

The quaternion uses `(w,x,y,z)` and maps body coordinates into a local frame with
Z up. Initial roll/pitch come from externally stationary gravity; initial yaw is
set to zero as a coordinate gauge, not a heading observation. For the current
sample's angular rate held over the elapsed sample interval, propagation is

```text
q_next = normalize(q * exp((0, omega_body * dt / 2)))
gravity_body = 9.80665 * R(q_next)^T * (0, 0, 1)
acceleration_body = specific_force_body - gravity_body
```

Quaternion multiplication order matters: right multiplication applies a
body-frame rate. This integration convention is a declared numerical model;
it does not interpolate an unknown within-interval trajectory. Roll/pitch are
local gravity-relative attitude estimates. Body-frame gravity subtraction does
not require knowing absolute yaw, but earth-frame ENU acceleration would.

There is no accelerometer correction during propagation, moving gyro-bias
adaptation or GNSS correction. This avoids treating turn/translation force as a
fresh gravity measurement. Gyro bias and timing errors still accumulate; a finite
number alone does not establish trustworthy attitude. A maximum two-second
propagation window bounds this experiment, not its error. Unknown timing, gaps,
identity/configuration changes and failed initialization refuse or invalidate the
affected sequence rather than bridge it. The SI input has no raw clipping
detector: the caller must mark saturated/unqualified measurements invalid. Recovery requires a
new independently stationary initialization.

## Timing and replay

The host replay accepts normalized calibrated SI samples with explicitly
provided sample times. The optional MotionV4 conversion uses an operator-provided
nominal cadence for relative time; it must not reinterpret host receipt/drain
milliseconds as acquisition epochs. All such output remains modelled and
Unreliable. Calibrated SI input also needs its own acquisition-time provenance;
writing times into a CSV does not qualify them.

From the repository root, run the actual C++ core through the host wrapper:

```sh
.venv/bin/python scripts/replay_dynamic_motion.py --experimental \
  --input INPUT.csv --output NEW.csv \
  --max-step-us 20000 --horizon-us 2000000
```

A local `c++` or `g++` compiler is required. The wrapper compiles the repository
core in temporary storage; there is no Python filter substitute or new library
dependency. The output path must be new. Malformed input aborts without publishing
an output CSV; semantically invalid samples instead produce rows describing the
core's refusal. Input/output captures remain on the host; this command neither
uploads firmware nor activates hardware.

For a runnable independent analytic fixture, use
`tools/motion_replay/fixtures/analytic_roll.csv` as the input. It describes a
90-degree roll over one second with a known 2 m/s² body-X translation. The
expected final roll is pi/2, quaternion w=x=sqrt(0.5), and linear acceleration
(2, 0, 0) m/s². This is synthetic evidence, not a hardware capture.

Normalized input has this exact header and unquoted comma-separated fields:

```csv
session_id,sensor_id,config_generation,mount_id,calibration_id,sensor_epoch,sequence,sample_time_us,timing_source,measurements_valid,discontinuity,stationary,declaration,force_x_mps2,force_y_mps2,force_z_mps2,rate_x_rad_s,rate_y_rad_s,rate_z_rad_s
1,1,1,1,1,1,0,0,modelled,1,0,1,1,0,0,9.80665,0,0,0
1,1,1,1,1,1,1,10000,modelled,1,0,0,0,2,0,9.80665,0,0,0
1,1,1,1,1,1,2,20000,modelled,1,0,0,0,0,0,9.80665,0.5,0,0
```

These three literal rows are a synthetic usage example, not qualified sensor
captures. They initialize upright, inject forward acceleration and then a small
roll-rate step. All identity fields, including `sensor_epoch`, must be positive;
sequence and relative time may start at zero. Subsequent sequence values must
increase by exactly one and time must strictly increase within the configured
step limit. Keep session/sensor/configuration/mount/calibration/epoch and timing
source unchanged within a segment.

`timing_source` is exactly `unknown`, `modelled` or `qualified`. Unknown timing
refuses numeric estimation. Qualified describes caller-supplied acquisition-time
provenance; it does not turn these unmeasured outputs into trusted estimates.
Boolean fields are `0` or `1`. A stationary anchor has `stationary=1` and a
strictly increasing nonzero `declaration`. A nonstationary row uses
`stationary=0,declaration=0`. Any nonzero declaration triggers initialization,
even when stationarity is false, so do not carry an old nonce on ordinary rows.
The initial force norm must be within 10% of gravity and rate at most 0.05 rad/s;
these vetoes do not establish external stationarity.

After refusal/horizon expiry, ordinary rows remain uninitialized. A fresh
independently stationary anchor with a larger declaration can start a new window.
Rejected anchors consume fresh declarations too; reset does not allow reuse.
Before the finite declaration counter is exhausted, start a new explicitly
qualified estimator lifetime. A snapshot has no wall-clock expiry and must not
be presented as a live reading.

Output includes `quality`, `fault`, `numeric_available`, `angles_available`,
elapsed time, quaternion, gravity/body linear acceleration and roll/pitch.
Unavailable numeric fields are blank, never measured zeros. Near ±90° pitch,
Euler angles are unavailable while quaternion/vector output can remain available.
Every numeric row is `unreliable`; both trusted dynamic flags remain zero.

### Existing MotionV4 capture conversion

If a capture already contains calibrated static MotionV4 evidence, prepare one
explicitly selected segment:

```sh
.venv/bin/python scripts/prepare_dynamic_motion_replay.py \
  --input MOTION_V4.csv --output NORMALIZED_NEW.csv \
  --anchor-frame N --timing-model nominal-odr
.venv/bin/python scripts/replay_dynamic_motion.py --experimental \
  --input NORMALIZED_NEW.csv --output REPLAY_NEW.csv
```

Choose `N` from a matching externally stationary, valid static-tilt record; the
adapter does not infer stationarity or invent calibration. It requires equal
nonzero accel/gyro ODR and uses that declared cadence for relative time.
Conversion preserves identity/sequence and marks observed controls, context
changes, losses and timing flags as discontinuities where appropriate. MotionV4
has no global evidence sequence: omitted controls cannot always be detected.
The adapter therefore cannot certify capture completeness or acquisition timing;
inspect capture provenance and retain modelled/Unreliable quality. Subsequent
stationary references are not automatically reused to restart the selected
segment. Choose a fresh qualified anchor for another replay.

## A finite later test

Use the existing [#31 worksheet](release_bench_checklist.md) and
[finite independent reference plan](motion_estimator_plan.md#finite-independent-reference-plan-for-31).
This experiment adds no campaign, mandatory long soak or required purchase.
Keep actual captures, private filenames, identities and locations outside Git;
publish only reviewed summaries and evidence references.

1. Record the exact software revision, sensor/settings, mounting/calibration,
   time source and reference uncertainty before replay. Confirm the chosen input
   convention, units and body axes. Record excluded or unavailable claims.
2. Start a replay segment only from independently known stationary data. A
   parked motorcycle on its sidestand can supply a nonzero initial roll; do not
   silently zero its physical tilt. Near-1g/low-rate data alone cannot establish
   stationarity.
3. For each already planned static/straight/controlled-turn reference sequence,
   compare only the initialized bounded windows. Inspect numeric roll/pitch and
   body acceleration together with status/quality, elapsed time and refusal
   reasons. Leave intervals without qualified initialization or timing invalid.
4. Check that gaps, saturation, changed configuration and elapsed horizon clear
   numeric availability; obtain a fresh static reference before restarting.
   Synthetic fault fixtures establish software behavior, not physical sensor
   performance. Use actual observed faults only where the declared campaign can
   produce and measure them.
5. Score physical errors only when the independent reference and time alignment
   meet the existing conditional #28 targets. Otherwise retain Experimental /
   Unreliable and mark scoring Blocked. Do not use this estimator, another
   unqualified IMU or a coordinated-turn proxy as its own ground truth.

A bounded replay can be useful to inspect how motion computation behaves before
hardware qualification. A successful run or visually plausible curve does not
confirm riding accuracy. Sustained turns beyond the initialized horizon require
an aided estimator or a separately justified operating envelope; raising the
window simply to make a ride trace continuous would discard the experiment's
boundary.

## Math and source provenance

The implementation is independently authored quaternion gyro propagation and
gravity subtraction; no Fusion, VQF or MotoNav dependency/code is included.
[The GitHub comparison](motion_estimator_plan.md#github-implementation-comparison-october-10-2026)
records inspected pinned implementations and their licenses. Mahony et al.'s
[attitude-observer publication](https://doi.org/10.1109/TAC.2008.923738) provides
algorithm context; the current experiment implements no corrective observer or
claimed motorcycle model.

Bosch's [BMI270 datasheet BST-BMI270-DS000-08](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf#page=36),
page 36, identifies FIFO sensortime as a read-boundary snapshot; page 32,
section 4.6.15 documents the sampling grid. Those specifications support explicit
timing models and later cadence qualification, not invented per-frame observed
acquisition epochs. Physical accuracy, clock alignment and useful propagation
error budget remain unmeasured.

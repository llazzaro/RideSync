# Motion estimator feasibility and reference plan (#28)

Inspected 2026-10-09. This records the design decision; no physical reference
campaign has run. The later [static core](motion_estimator.md) implements only
the qualified static subset, with host tests and opt-in
[runtime/MotionV4 logging](motion_logging.md#implemented-static-motion-logging-v4).
Dynamic estimation remains incomplete. BMI270 activation, exact mount and electrical setup remain
unqualified ([raw IMU path](raw_imu.md)).

## Decision and #13 scope

Use a small six-axis, Mahony-style complementary observer only for static
gravity roll/pitch and bounded gyro propagation from a separately qualified
initial attitude. Emit calibrated body-frame specific force and angular rate
as separate measured channels. Do not claim general dynamic motorcycle lean or
pitch from this IMU-only route. The BMI270 has accel and gyro but no absolute
heading reference; yaw drifts under integration. Mahony et al. describe
low-cost-IMU attitude observers and time-varying gyro bias, but do not validate a
motorcycle model ([2008 paper, DOI 10.1109/TAC.2008.923738](https://doi.org/10.1109/TAC.2008.923738)). No paper code is reused. Existing
driver notices remain SparkFun MIT and Bosch BSD-3-Clause
([sources.md](sources.md)); Bosch's datasheet specifies the sensor, not an
attitude guarantee ([BMI270 datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf)).

The current GNSS path does not supply adequate aiding. Its documentary
`CGPSINFO` schema has optional speed/course but no quality or uncertainty; the
parser timestamps receipt, not measurement acquisition. The software defaults
to 1 s polling and a 3 s stale threshold, both caller-configurable. Structurally
valid coordinates do not establish accuracy; recorded receiver observations
were NoFix ([GNSS profile](gps_protocol.md), [SIMCom manual V1.09 §24.2.11](https://files.waveshare.com/wiki/A7670E-Cat-1-GNSS-HAT/A76XX_Series_AT_Command_Manual_V1.09.pdf)).

With synchronized, qualified speed/course, `dv/dt` and `v·d(course)/dt` could
approximate path acceleration; `atan(a_lateral/g)` is a bank-equilibrium proxy
only for a steady, level, coordinated no-slip turn. Course-over-ground is not
body heading or frame roll, and the model fails during turn entry/exit,
braking, camber, sideslip or body motion. The current receipt-time, nominal 1 Hz
and no-uncertainty path cannot support that proxy as motorcycle lean.

**Explicit #13 resolution:** a future implementation can cover specific force,
angular rate, externally qualified static gravity roll/pitch, and bounded gyro
propagation with validity flags. General dynamic lean/pitch and earth-frame
dynamic acceleration remain unmet: specific force alone does not remove
gravity, and current GNSS cannot reliably provide the missing correction.
Keep those outputs invalid/Experimental when unobservable. #13 must remain open
(or receive an owner-approved scope change) until the original dynamic goals
are adequately aided and validated or explicitly removed. Static/raw outputs do
not complete those goals.

## Estimator contract

Use right-handed motorcycle body frame B (+X forward, +Y left, +Z up) and local
East-North-Up frame N. Record measured sensor-to-body rotation `R_BS` after
physical axis identification. With `R_NB` mapping B to N, define calibrated
specific force `f_B = R_NBᵀ(a_N - g_N)` in m/s², where `g_N=(0,0,-9.80665)`;
angular rate `ω_B` is rad/s. A stationary sensor reports upward specific
force; do not label it gravity-removed vehicle acceleration.

Calibrate warm gyro bias while externally known stationary and accelerometer
offset/scale using a known six-position fixture. Persist calibration version,
full scales, units, sensor axes and mount rotation. A low-rate/near-1g test is
only a quality indicator: constant acceleration or a slow coordinated turn can
pass it. It cannot establish stationarity or gravity. Initialize valid tilt only
when a fixture/operator supplies independent stationary evidence and the
quality checks pass. No moving gyro-bias adaptation. Yaw initializes unknown.

Two seconds is a proposed maximum gyro-only propagation horizon after qualified
initialization, not a guarantee of accuracy. Before exposing a valid propagated
attitude, bench measurements must establish its error budget; otherwise mark it
unreliable at all propagated times. Reset/invalidate on acquisition-generation
change, sensor reinit, FIFO flush/discontinuity, missing/non-monotonic time,
calibration change, saturation, or lost stationary reference. Never bridge
sample gaps. GNSS course remains disabled as aiding until acquisition epoch,
quality and uncertainty are available.

The estimated filter state is fixed-size (quaternion, gyro bias and validity/
time metadata), O(1) per 200 Hz sample, with no heap or extra FIFO. This is an
estimate, not a benchmark. The existing raw path budgets 112-byte FIFO input,
968-byte publication batch, 240-byte base evidence and 8192-byte worker-stack
capacity; peak stack, CPU and latency are unmeasured ([raw IMU contract](raw_imu.md)).

## Finite independent reference plan for #31

Minimum is one controlled sequence each: (1) level and known 0°/±10°/±20°
roll/pitch fixture poses, (2) one straight acceleration/braking sequence, and
(3) one controlled turn labeled entry/steady/exit. Add repeats only if a stated
uncertainty or variance question requires them. Collection belongs to #31.
Reference-instrument availability is unknown; this plan neither assumes access
nor requires purchase.

Use a calibrated inclinometer/fixture for static tilt. PRO360 is one published
example (0.1° resolution, ±0.1° accuracy at 20°C), not a selected purchase.
Static target: absolute roll/pitch error ≤0.5° after settling within 1 s,
provided the combined reference expanded uncertainty is ≤0.2°
([specification](https://www.leveldevelopments.com/products/inclinometers/digital-inclinometers/pro360-pro-360-digital-protractor-range-360-resolution-0-1/)).
For dynamic acceleration/attitude, calibrated optical tracking of a rigid marker
constellation at the IMU mount is one possible independent method. Characterize
its actual attitude and trajectory-derived acceleration uncertainty for the
setup. A reference IMU alone is insufficient without its own calibration,
uncertainty and observability evidence.

Proposed straight-event 0.2–2 Hz earth-frame acceleration RMSE is ≤0.1g, scored
only with reference expanded uncertainty ≤0.03g. For any future estimator that
claims turn validity, proposed roll/pitch RMSE is ≤1.5° and p95 absolute error
≤3°, scored only with reference expanded attitude uncertainty ≤0.5°. Timing
error must be ≤20 ms, measured using a common sync event and start/end offset
check. These are unmeasured targets; without an available reference that
demonstrates the uncertainty and timing bounds, dynamic scoring is blocked.
The present turn sequence checks that lean validity remains false; it does not
score a lean estimate.

RideSync raw records currently expose receipt/drain boundaries, not calibrated
acquisition-to-world-clock time ([raw timing limits](raw_imu.md)); the ≤20 ms
target is unmeasured. An optical reference is only an example method: it must
demonstrate the stated uncertainty and timing bounds in the actual setup.

## Invalidity conditions

Only externally qualified stationary periods may yield gravity-referenced tilt.
Mark propagated attitude and derived earth-frame acceleration unreliable when
there is sustained turn/non-gravitational acceleration, GNSS absence/no-fix/
staleness or unqualified timing, saturation/clipping, vibration that defeats
quality checks, gyro drift beyond the measured horizon, or any sample/time
discontinuity. Unknown mount rotation, calibration, road camber, tire slip,
wheel lift or body articulation also defeats a motorcycle-lean interpretation.
These limits invalidate angle/gravity separation, not the recorded specific-
force and angular-rate bytes; BMI270 endpoint checks cannot prove no prior
clipping ([raw-path evidence](raw_imu.md)).

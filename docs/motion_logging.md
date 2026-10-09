# Motion logging hardware decision (#27)

Inspected 2026-10-07. This is a research decision only; no sensor is confirmed
in hand, purchased, wired, or tested on the RideSync unit.

## Decision

Provisional candidate: the **standard SparkFun 6DoF BMI270 Qwiic breakout**
(not the Micro form factor), as distinguished in SparkFun's [hardware overview](https://docs.sparkfun.com/SparkFun_Qwiic_6DoF_BMI270/hardware_overview/),
using the Bosch BMI270 sensor API through SparkFun BMI270 Arduino Library
v1.0.3. This names a preferred form factor only; the fitted board and exact PCB
revision remain unknown, as do availability, schematic fit and pin access. It
combines
raw tri-axis acceleration and angular-rate measurements, programmable ranges,
a sensor FIFO and interrupt support. SparkFun's wrapper exposes FIFO setup,
watermark and read functions; the driver is MIT-licensed. This is enough to
select a candidate for later physical qualification, not to authorize purchase
or integration. The exact fitted breakout, board revision, availability,
breakout schematic fit and pin access are unresolved.

One comparison was retained: Adafruit LSM6DSOX breakout and Adafruit_LSM6DS
library. ST specifies 208 Hz batch rate, FIFO watermark/overrun interrupts and
programmable accel/gyro ranges; Adafruit's library is BSD-licensed. It is a
credible alternative, but its documented high-level API is centered on
individual sensor events and its driver path was not assessed for RideSync's
FIFO/raw-frame needs. No third candidate adds enough evidence to justify the
extra comparison.

GitHub repository metadata checked 2026-10-08 shows both upstreams are
unarchived. The [SparkFun repository](https://github.com/sparkfun/SparkFun_BMI270_Arduino_Library)
latest commit/release was `21ea234` / v1.0.3 on 2024-06-26; the [Adafruit
repository](https://github.com/adafruit/Adafruit_LSM6DS) latest commit/release
was `379a520` / 4.7.4 on 2024-12-03. These dates are activity observations, not
a support guarantee; neither was recently updated at the time checked, and the
Adafruit repo is more recent by this limited indicator. Maintenance evidence
therefore does not favor BMI270; its provisional selection remains based on the
documented FIFO/raw timing fit. Recheck upstream status and notices before a
later dependency update.

| Candidate | Evidence relevant to RideSync | Decision |
|---|---|---|
| SparkFun 6DoF BMI270 Qwiic (standard form factor; PCB revision unverified) | BMI270: 16-bit accel and gyro; accel ±2/4/8/16 g, gyro ±125…2000 dps; ODR covers 200 Hz; 2 KiB FIFO with sensor-time support; data-ready and watermark interrupts. Breakout has I²C/SPI pins and interrupt pin(s), on-board I²C pull-ups, selectable address/SPI strap, 1.71–3.6 V sensor supply. SparkFun wrapper v1.0.3 exposes FIFO reads/configuration and reports converted g, dps and sensor time. | Preferred provisional module/driver pair. FIFO API and sensor time are a useful fit for buffered local logging. |
| Adafruit LSM6DSOX breakout | ST sensor supports 208 Hz ODR/batch, ±2/4/8/16 g and ±125/250/500/1000/2000 dps, programmable FIFO and interrupts. Adafruit breakout presents 3–5 V power/logic through regulator/level shifting; Adafruit driver is BSD. | Credible fallback, but review actual FIFO support and its treatment of sample timing before selecting it for raw logging. |

The breakout is a module, not the bare sensor. BMI270 bare-sensor VDD is
1.71–3.6 V and VDDIO 1.2–3.6 V; the SparkFun BMI270 Qwiic documentation gives
1.71–3.6 V supply/logic limits. Treat it as **3.3 V compatible, not 5 V
tolerant**. Do not infer a 3.3 V regulator or level shifter from the bare-chip
specification. The Adafruit LSM6DSOX breakout differs: its board documentation
specifies regulated/level-shifted breakout interfaces. Verify the exact module
schematic and revision before wiring either choice.

## Proposed acquisition starting point

Begin with paired accelerometer and gyroscope output at 200 Hz and the widest
listed ranges, ±16 g and ±2000 degrees/second, then inspect a stationary engine
and controlled ride dataset for clipping and noise before narrowing ranges or
changing rate. This is a measurable starting configuration, not a claim that
200 Hz captures every vibration of interest. At this setting one paired sample
is due every 5 ms. Preserve raw signed sensor counts plus the configured range
and scale; also log converted acceleration in g (optionally m/s²) and angular
rate in degrees/second (optionally radians/second). BMI270's 16-bit output at
the proposed widest ranges is nominally 2048 counts/g and 16.4 counts/(degree/s);
conversion must follow configured full scale and the selected sensor API.

Use data-ready/FIFO watermark interrupt to wake the collector, drain complete
frames, and retain the BMI270 sensor-time value or reconstruct per-frame sensor
time from FIFO timing metadata. Timestamp FIFO-drain and SD-write boundaries
with the ESP32 monotonic clock as separate host-side timing evidence. Proposed
optional measurement example for a declared #31 campaign: over a 10-minute 200 Hz run, account for all
120,000 expected paired samples, record zero FIFO overruns or unexplained
sequence gaps, and keep the 99th-percentile data-ready-to-buffered-record delay
below 10 ms. Measure actual inter-sample timing from sensor timestamps; report
sensor-time quantization and host scheduling jitter separately. These limits
are unmeasured planning targets, not software closure criteria or an additional
mandatory campaign. #31 fixes the actual finite campaign before execution.

The BMI270 FIFO is 2 KiB (Bosch notes a larger 6 KiB mode in an application
note); do not assume the larger mode without checking its separate setup
requirements. A FIFO only absorbs temporary host delay: it cannot protect
against sustained SD stalls. The logger must count FIFO overrun, read errors,
sequence gaps, SD write failures and dropped buffers. The ESP32 SD SPI example
already assigns SCK/MISO/MOSI/CS to GPIO 14/2/15/13, while example I²C is
GPIO 21/22. Those pin definitions are candidates pending physical board and
schematic verification. I²C on 21/22 could reuse the candidate bus if pull-up
voltage/resistance, bus speed, electrical loading and device addresses are
checked; the BMI270 default 0x68 can be changed to 0x69. For SPI, the IMU needs
a free CS and accessible SCK/MISO/MOSI plus interrupt GPIO; do not share the SD
CS or presume any header pin is free. GPIO2 and GPIO15 are ESP32 strapping pins.
No exact pin assignment is approved by this research.

Expected resource costs are estimates from the inspected source/interface, not
measurements. In the pinned Bosch BMI270 source, the configuration image is an
8 KiB `const` array in the firmware image and initialization uploads it through
32-byte callbacks. That implies roughly 256 32-byte transfer chunks in the
configuration phase, before register setup and readback; this is a callback
chunk size, not a measured transfer time or a separate staging allocation. The
2 KiB sensor FIFO is additional on-device storage. The selected logger path later budgets
112 bytes of FIFO input, 968 bytes for a publication batch, 240 bytes of copied
base evidence and an 8192-byte worker stack capacity; the stack number is a
chosen capacity, not measured use ([raw IMU path](raw_imu.md#profile-fifo-and-evidence)).

For the LSM6DSOX alternative, the inspected comparison sources document 208 Hz
batching, FIFO/watermark operation, programmable ranges and the Adafruit driver
interface, but this baseline gives no comparable FIFO capacity, firmware-image
size, initialization transfer size, or RideSync buffer/stack estimate. Its
expected firmware-transfer and RAM cost are therefore **unquantified**, not
assumed to be zero or lower. The BMI270 has a known 8 KiB configuration-image
cost and documented 2 KiB FIFO; these facts support a bounded estimate but do
not predict boot time, final linked image growth, runtime peak RAM, or sampling
performance. Those remain unmeasured and belong to later implementation and
qualification.

## Mounting, calibration and angle claims

Mount the board rigidly to the motorcycle frame near the vehicle centerline,
with its axes marked relative to the motorcycle: +X forward, +Y left, +Z up
(right-handed). Document the actual PCB silkscreen-to-vehicle rotation and
keep the sensor PCB supported without flex. Separate the sensor from engine
heat and avoid a soft mount that changes vibration response. Verify axis sign
by hand before any ride.

For bench calibration, warm the mounted unit to operating temperature, hold it
stationary and collect a long gyro-bias record; repeat after power cycle. Use
six static orientations with each accelerometer axis alternately aligned with
gravity to estimate offset and scale, then check a known level and 90-degree
fixture. Record calibration values, temperature and mounting alignment. The
known fixture/inclinometer is an independent static reference. Later dynamic
validation should compare to a separately calibrated reference IMU rigidly
mounted at the same point or to a documented optical/angle reference in a
controlled test, with synchronized timestamps and stated uncertainty.

Raw accelerometer and gyroscope channels are measurable sensor outputs. A
stationary accelerometer can estimate gravity direction; during motorcycle
motion, translational acceleration contaminates gravity-based tilt, while
gyro-integrated attitude drifts. Therefore this board alone does not directly
or accurately measure dynamic motorcycle lean/pitch. Any reported lean/pitch
requires a separately specified estimator, calibration and dynamic validation
(tracked separately from this hardware choice); do not label accel-only tilt or
an unspecified fusion output as validated lean angle.

## Open qualification gates

- Physically identify the T-A7670E PCB revision, matching schematic and safe
  accessory connections; confirm no conflict with modem/SD signals or boot
  straps.
- Identify/obtain the exact BMI270 breakout SKU/revision and confirm stock,
  orientation marks, connector/pin access and pull-up behavior. Availability
  was not treated as evidence of stock.
- Bench-test I²C/SPI bus timing, interrupt routing, FIFO timestamp behavior,
  vibration/noise, logging throughput, loss budget and calibration procedure.
- Choose the mounting enclosure/strain relief and complete powered vehicle
  noise/temperature checks before ride data collection.

The unresolved hardware keeps physical IMU qualification open. See also
[hardware bring-up](hardware.md) and [source/license notes](sources.md).
Estimator observability, GNSS limits and the finite independent reference plan
are documented in the [motion estimator feasibility plan](motion_estimator_plan.md).

The [experimental static estimator core](motion_estimator.md) converts qualified
raw records and computes externally referenced static roll/pitch. The opt-in
runtime integration below logs these outputs; dynamic lean and gravity-free
acceleration remain unsupported.


## Implemented static motion logging (V4)

The opt-in static core now runs in the serialized telemetry admission owner.
`LocalTelemetryConfig::motion_enabled` defaults false. Commissioning supplies
`motion_config` (qualified residual calibration and proper sensor-to-body
rotation) and `motion_snapshot_max_age_ms` in 1..60000. Requested but ineligible
motion reports Refused; original raw/GPS eligibility is preserved. Every requested
session selects MotionV4, including refused motion. Unrequested sessions retain
CameraV3. Neither shipped setup nor loop enables motion or manufactures calibration.
Dynamic lean and gravity-free acceleration are unsupported; #13 and physical
reference acceptance #31 remain open.

The caller-owned `StaticMotionReferenceSource` is an optional final constructor
argument, after the existing power argument on LocalTelemetryRuntime. It is
forwarded by both concrete ESP32 wrappers and their factories. First factory call
fixes configuration, resources and source. The source outlives `canRelease()` and
runs only on the owner: no I/O, allocation, wait, lock, throw or reentry. It returns
external stationarity evidence for the exact session, generation and batch with
strictly increasing nonzero declarations. Declarations survive output resets,
withdrawal and rejected updates; wrap/reuse cannot restore tilt. Null source allows
calibrated force/rate conversion but never static tilt. Unit-norm force and low
angular rate are vetoes, not stationarity detection.

Runtime chooses only the IMU inbox route. A direct IMU event permanently revokes
motion before preserving its raw admission. Standalone direct admission similarly
revokes on inbox input. At most two inbox/camera enqueue attempts occur per pass,
including failed attempts. Existing GPS reservation is unchanged. No queue, worker,
RecordKind, heap allocation or acquisition timestamp was added.

`status()` returns `motion_admission`, `motion_current`, `motion_estimate` and
`motion_source` (copied sample identity). Current requires successful raw admission,
valid conversion, known receipt, clear timing bits 0..2, valid session monotonic time,
and receipt age within both the configured limit and elapsed session time. Every
service pass clears current before processing. Status reads raw time again and
expires the snapshot without mutating SessionClock or calling the source.
`withdrawMotionReference()` clears live output while preserving declaration history.
Stop, terminal media, finished/failed IMU, safe-mode entry, wrong route, IMU supervision
bits or global fault permanently revoke before final inbox draining. Early stop or
revocation during allocation is retained. Camera-only refusal does not revoke motion.
Health/config/control records reset output and can permit a later fresh reference.

Historical queue records copy raw and motion evidence together. Later expiry,
withdrawal, source mutation or revocation cannot rewrite them. Conversion can be
historically valid despite unavailable/stale timing while current is false. Receipt
is a raw modulo-32-bit owner-domain observation, never frame acquisition time.
Service and source lifetime must be externally qualified to less than a full
32-bit clock wrap between observations; a complete unseen wrap is undetectable.

### CSV layout

The exact markers are:

```text
#ridesync_telemetry,4
#imu_layout,4,see_docs/motion_logging.md
#camera_layout,3,see_docs/log_format.md
```

GPS retains 36 columns. Camera retains its V3 schema. IMU/config/health/control
retain their 88-column raw prefix and append these 36 ordered fields (124 total):

| Suffix index (zero-based) | Fields in order |
|---|---|
| 0..9 | motion_state, motion_algorithm, motion_snapshot_max_age_ms, motion_convention, motion_mount_qualified, motion_residual_calibration_qualified, motion_mount_id, motion_calibration_id, motion_accel_compensation, motion_gyro_compensation |
| 10..18 | motion_r_bs_00, motion_r_bs_01, motion_r_bs_02, motion_r_bs_10, motion_r_bs_11, motion_r_bs_12, motion_r_bs_20, motion_r_bs_21, motion_r_bs_22 |
| 19..23 | motion_reference_stationary, motion_reference_session_id, motion_reference_generation, motion_reference_batch, motion_reference_declaration |
| 24..27 | motion_measurements_valid, motion_static_tilt_valid, motion_dynamic_lean_valid, motion_dynamic_acceleration_valid |
| 28..33 | motion_force_x_mps2, motion_force_y_mps2, motion_force_z_mps2, motion_rate_x_rad_s, motion_rate_y_rad_s, motion_rate_z_rad_s |
| 34..35 | motion_roll_rad, motion_pitch_rad |

State values: Disabled=0, Refused=1, Enabled=2, Revoked=3. Enabled uses algorithm=1,
convention=1 (residual counts offset then gain), qualified flags=1, nonzero mount and
calibration IDs and compensation=1 disabled or 2 enabled. Matrix is finite,
orthonormal, right-handed. Floats use finite `%.9g` output. Invalid measurement
vectors and invalid tilt angles are blank. Dynamic flags always zero. In inactive
states only state and four zero result flags are populated; all other suffix fields
are blank. Enabled non-sample rows carry configuration with zero reference and
result flags and blank result numerics. Readers must check validity and presence,
not interpret a blank value as zero. `test/telemetry_parser.py` independently checks
V4 numeric bounds, coefficients, configuration, context, flags and presence;
legacy V2/V3 layouts remain supported.

### Measured software resources (2026-10-09)

ABI probe baseline is pre-implementation main `a41a428`, using the same pinned
Xtensa 8.4 compiler, retained HERO12 flags and target headers. Native uses Apple
Clang host ABI. Private Active is measured by test-only probe access, not a sum.
Native concrete wrappers use the SDK-boundary harness with production class layouts.

| Object | Native before → after bytes | ESP32 before → after bytes |
|---|---:|---:|
| ImuEvidence | 240 → 240 | 240 → 240 |
| ImuBatch | 968 → 968 | 968 → 968 |
| ImuInbox | 3912 → 3912 | 3912 → 3912 |
| MotionEvidence | absent → 136 | absent → 136 |
| TelemetryRecord | 544 → 680 | 544 → 680 |
| Storage | 6768 → 8880 | 6752 → 8864 |
| MotionEstimator | 92 → 92 | 92 → 92 |
| TelemetryAdmission | 48 → 392 | 32 → 368 |
| CameraEventSession | 12120 → 14576 | 12064 → 14512 |
| LocalTelemetryRuntime::Active | 12816 → 15272 | 12728 → 15176 |
| LocalTelemetryStatus | 544 → 624 | 544 → 624 |
| LocalTelemetryRuntime | 13704 → 16304 | 13512 → 16104 |
| Esp32LocalTelemetry | 16392 → 19048 (SDK harness) | 16000 → 18656 |
| SupervisedEsp32Application | 19224 → 22024 (SDK harness) | 18616 → 21424 |

Eight slots add 8*136=1088 bytes and the row buffer adds 1024, totaling 2112 in
Storage. Admission adds 336 target bytes for immutable options, estimator, source,
state and current snapshot. Runtime adds 2592 target bytes, including its session,
configuration/status and source. Payload bounds remain MotionEvidence<=160 and
TelemetryRecord<=704; raw transport bounds remain unchanged. Storage retains eight
slots and 256-byte chunks. Conservative raw row 1848 + 36*21 suffix = 2604 bytes,
plus terminator, fits the 3072-byte buffer. Stress serialization checks maximum
finite float magnitudes and integer widths and enforces sink chunk limits.

Individual retained compiler `.su` frames: enqueueImu 752 (baseline 592),
admitImu 336, admission tick 192 (baseline 208), runtime service 592 (baseline 592),
runtime status 192, ESP32 telemetry service 32, supervised application service
672 plus 32-byte wrapper entry. These are individual frames, not a measured call
chain, task high-water or SDK interrupt reserve. The separate compiled configuration
stack audit passes. Physical stack usage, throughput, latency, heap and error bounds
remain unmeasured. Retained ELF contains the estimator/admission/concrete factories;
default ELF omits the opt-in motion/runtime entrypoints. Linking is software proof.

Reproduce:

```sh
pio test -e native -f test_motion_estimator -f test_telemetry -f test_storage -f test_local_telemetry -f test_camera_event_logging
python -m unittest discover -s test -p 'test_motion_disk.py' -v
python -m unittest discover -s test -p 'test_telemetry_disk.py' -v
python -m unittest discover -s test -p 'test_telemetry_stop.py' -v
python -m unittest discover -s test -p 'test_local_telemetry_esp32.py' -v
pio run -e lilygo_t_a7670e_r2 -e hero12_adapter_compile
pio run -e hero12_adapter_compile -t compiledb
python scripts/probe_motion_resources.py
python scripts/check_config_stack.py
```

The `motion-csv` concrete harness feeds repository-authored literal upright counts
through the real worker/runtime/admission/Storage path, closes the same sink and
passes its bytes to the independent parser. Low-level fixtures also cover copied
history after withdrawal/revocation, malformed fields, finite extremes, queue
refusal, raw-clock rollover, and tilted literal counts (roll 0.349066 ±0.001).
All are synthetic software evidence; no hardware motion accuracy is asserted.

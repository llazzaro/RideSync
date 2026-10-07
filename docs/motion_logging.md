# Motion logging hardware decision (#27)

Inspected 2026-10-07. This is a research decision only; no sensor is confirmed
in hand, purchased, wired, or tested on the RideSync unit.

## Decision

Provisional candidate: SparkFun 6DoF BMI270 Qwiic breakout, using the Bosch
BMI270 sensor API through SparkFun BMI270 Arduino Library v1.0.3. It combines
raw tri-axis acceleration and angular-rate measurements, programmable ranges,
a sensor FIFO and interrupt support. SparkFun's wrapper exposes FIFO setup,
watermark and read functions; the driver is MIT-licensed. This is enough to
select a candidate for later physical qualification, not to authorize purchase
or integration. The exact breakout (standard or Micro), board revision,
availability, breakout schematic fit and pin access are unresolved.

One comparison was retained: Adafruit LSM6DSOX breakout and Adafruit_LSM6DS
library. ST specifies 208 Hz batch rate, FIFO watermark/overrun interrupts and
programmable accel/gyro ranges; Adafruit's library is BSD-licensed. It is a
credible alternative, but its documented high-level API is centered on
individual sensor events and its driver path was not assessed for RideSync's
FIFO/raw-frame needs. No third candidate adds enough evidence to justify the
extra comparison.

| Candidate | Evidence relevant to RideSync | Decision |
|---|---|---|
| SparkFun 6DoF BMI270 Qwiic (standard or Micro) | BMI270: 16-bit accel and gyro; accel ±2/4/8/16 g, gyro ±125…2000 dps; ODR covers 200 Hz; 2 KiB FIFO with sensor-time support; data-ready and watermark interrupts. Breakout has I²C/SPI pins and interrupt pin(s), on-board I²C pull-ups, selectable address/SPI strap, 1.71–3.6 V sensor supply. SparkFun wrapper v1.0.3 exposes FIFO reads/configuration and reports converted g, dps and sensor time. | Preferred provisional module/driver pair. FIFO API and sensor time are a useful fit for buffered local logging. |
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
first bench acceptance budget: over a 10-minute 200 Hz run, account for all
120,000 expected paired samples, record zero FIFO overruns or unexplained
sequence gaps, and keep the 99th-percentile data-ready-to-buffered-record delay
below 10 ms. Measure actual inter-sample timing from sensor timestamps; report
sensor-time quantization and host scheduling jitter separately. These limits
are qualification targets to test, not demonstrated performance.

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

## Mounting, calibration and angle claims

Mount the board rigidly to the motorcycle frame near the vehicle centerline,
with its axes marked relative to the motorcycle: +X forward, +Y right, +Z up
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

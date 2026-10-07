# Hardware bring-up

Target: LILYGO TTGO T-A7670E R2, ESP32-WROVER-E and **A7670E with built-in GPS**.
The user selected this GNSS-equipped variant; acquisition will use modem AT
commands, with no external GPS receiver required.
The [LILYGO board guide](https://wiki.lilygo.cc/products/t-sim-series/t-a7670/)
and [vendor examples](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series)
are the hardware authority. The scaffold follows the vendor's generic esp32dev
PlatformIO setup with PSRAM flags. LILYGO currently specifies 4 MB flash and
8 MB PSRAM for T-A7670X R2; the factory diagnostic on this unit reported 4 MB
flash and 4 MB *usable* PSRAM. Neither the diagnostic nor the current guide
independently identifies the fitted module or physical PSRAM capacity.

The unit's physical PCB revision and its matching schematic have not been
verified. LILYGO's current R2 guide links a schematic titled T-A7670X V1.4;
that filename is not evidence that this unit is V1.4 or that the drawing matches
the PCB in hand. The following are **vendor-example candidates only**, taken
from the ESP32 T-A7670 definitions in the [LILYGO example source](https://github.com/Xinyuan-LILYGO/LilyGo-Modem-Series/blob/e8d8b82a23f324ef2ac1a24efe1c3b6cbabcca00/examples/S3_StandardSeries_External_GPS_Shield/utilities.h):

| Function | Candidate ESP32 GPIO | Qualification |
|---|---:|---|
| Modem UART TX/RX | 26 / 27 | Example's `MODEM_TX_PIN` / `MODEM_RX_PIN`; physical routing and baud not checked on this unit. |
| Modem DTR / RING | 25 / 33 | Example definitions; confirm peripheral direction and routing against the fitted revision. |
| Modem PWRKEY / RESET | 4 / 5 | Example definitions. GPIO5 is an ESP32 strapping pin; reset polarity/timing must match fitted board/modem. |
| Modem/peripheral power enable | 12 | Example's `BOARD_POWERON_PIN`; GPIO12 is a strapping pin, so boot level and attached circuitry matter. |
| SD SPI SCK / MISO / MOSI / CS | 14 / 2 / 15 / 13 | Example definitions. GPIO2 and GPIO15 are strapping pins and the SD socket shares these signals. |
| Default I2C SDA / SCL | 21 / 22 | Example defaults; verify no fitted-device or header conflict. |
| GNSS antenna | Not a GPIO | Confirm the fitted modem option, connector/antenna and RF path physically; no antenna connection or sky-view test is in evidence. |

This is a conflict checklist, not an approved wiring map. [Espressif identifies
GPIO0, GPIO2, GPIO5, GPIO12 and GPIO15 as ESP32 strapping
pins](https://docs.espressif.com/projects/esp-idf/en/v5.2.6/esp32/api-reference/peripherals/gpio.html);
peripheral pulls or reset states can change boot behavior. Several pins are
already allocated to the modem or SD.
Identify the board marking and compare the matching schematic to the unit,
then check continuity, boot straps, header labels and antenna connector before
assigning any accessory GPIO. Do not substitute T-Call or S3 board definitions.

Bring-up: attach USB, enumerate the serial port, build/upload, open 115200-baud
monitor and record the startup message. This unit's factory AT firmware restarted
on serial open; wait for its startup output to settle and require `AT` → `OK`
before issuing other commands. The baseline contains no elapsed-time measurement,
so a numeric command-ready deadline is still unqualified and must be measured
before firmware relies on one. Record board revision and modem firmware. Test on
a bench before mounting on a motorcycle.

The external momentary button will need a verified GPIO, pull resistor,
debounce and appropriate environmental protection. External RGB LED wiring and
power conditioning are tracked by #30; button and LED firmware are #8 and #23. Validate startup straps, peripheral
conflicts and modem supply current before wiring accessories.

## Planned motion logger hardware

Acceleration and motorcycle lean/pitch logging require an external IMU with an
accelerometer and gyroscope. The board/GNSS module alone does not provide these
motion measurements. Select the module during the IMU issue after checking
wiring, usable ranges, timestamping, driver license and rigid mounting. Preserve
raw samples; derived angles need a calibration and dynamic validation procedure.

### Provisional IMU selection (#27)

Research favors the SparkFun 6DoF BMI270 Qwiic breakout with SparkFun's
BMI270 Arduino Library v1.0.3 over the compared Adafruit LSM6DSOX breakout:
the BMI270 offers paired accel/gyro output, a 2 KiB FIFO with sensor-time
support, data-ready/watermark interrupts, and an MIT-licensed wrapper exposing
FIFO reads. Start later qualification at 200 Hz and ±16 g / ±2000 dps, with
raw counts, configured scales and converted units retained. This is provisional
only; no module is confirmed available or physically present. Its 3.3 V class
interface is not 5 V tolerant. The breakout revision, actual unit pin routing,
free interrupt/CS pins, bus electrical fit and mounting remain unverified.
See [motion logging decision and calibration gates](motion_logging.md). Raw
sensor counts may require direct register/FIFO parsing because the wrapper's
documented sample struct provides converted values; verify the exact API
before acquisition implementation.

## Observed bench evidence

See [factory bring-up](hardware-results/2026-10-07-bringup.md) for the A7670E-FASE
identity, GNSS power-on/READY and empty no-fix responses. This verifies only the
factory-firmware probe, not the planned RideSync drivers. #1 retains remaining
board/pin/antenna qualification work.

## Button software and qualification (#8)

`SourceConfig::button` supplies separately validated timing/action settings;
`button_gpio` is disabled by default with pin -1. Camera validation remains
independent. `ButtonManager::begin` rejects a missing callback, zero or >= 2^31 ms
intervals, long/double intervals <= debounce, and all invalid action enums.
Defaults are 20 ms debounce, 800 ms long, 300 ms double window; double detection
is disabled. Actions are intents only: short RecordingIntent, long WakeReconnect,
optional double Resync. Nothing connects these intents to cameras, wake, LEDs or
the application yet.

Poll from one scheduler context with a monotonic uint32_t millisecond clock,
with gaps less than 2^31 ms. State is bounded, with no allocations or waits.
Input and callback must return promptly. Calls are not thread/ISR safe; neither
input nor callback may reenter poll/begin or mutate the manager. The caller must
serialize access; this is a precondition, not a cross-context locking mechanism.
A continuously released sample must persist for debounce before startup arms;
a button held at startup never emits. Press/release durations use observed
**debounced** transitions, so polling cadence affects detection time. Long emits
at >= threshold, including a release at that boundary, once per hold. Without
double detection short emits immediately on debounced release (only debounce and
poll scheduling latency). With it enabled, short waits the double window after
release; a second debounced press strictly before expiry reserves the gesture,
then its short release emits double. At exact expiry the first short wins.
A long second press supersedes the reserved first short and emits only long.
Rollover uses unsigned elapsed subtraction.

`ArduinoButtonInput` is compiled only for Arduino ESP32. Its begin requires both
explicit enabled/board_qualified settings and a caller acknowledgement. Only
then does it configure/read the requested input; it never writes an output.
Silicon filtering rejects straps, flash GPIO6–11, WROVER PSRAM GPIO16/17,
unavailable pins, serial GPIO1/3 and the vendor candidate modem/SD allocations.
GPIO34/35/36/39 require an external pull because they have no internal pulls.
The remaining allowed inputs are **not approved accessory assignments**:
continuity, fitted-device conflicts (including I2C), voltage, pull resistance,
and the actual PCB/schematic still require physical qualification.

No external button wiring, pull resistor, GPIO reading or event trace has been
bench verified for RideSync. #8 remains open. Before enabling a pin, identify the
PCB revision, establish an electrically safe free input against its matching
schematic/continuity, document active polarity and measured pull, then capture
startup-held/release, bounce, short, long, double (if enabled) and repeat traces.
Include firmware revision, timing settings and polling cadence; confirm boot
behavior and that modem/SD/PSRAM continue working. Native fake-input tests and a
successful target build are software evidence only.

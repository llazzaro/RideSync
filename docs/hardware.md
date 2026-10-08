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
optional double Resync. The qualified opt-in [handlebar owner](handlebar_control.md)
connects these intents to the actual camera/group and LED route, under the
[supervised application startup](supervision.md). Physical button traces and
qualified board pins remain unverified.

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

## Status LED software and qualification (#23)

The software priority is **Error > Recovery/retry > Partial > Recording > Ready >
Off**. `selectLedState` consumes copied `RecordingStatus`, matching enabled-peer
lifecycle observations, and explicit application/device health. The application
owner must copy group status and lifecycle in the same serialized context as
camera/group service. Producer callbacks publish bounded copies promptly; they
must not call managers recursively. The LED neither owns recording intent nor
calls camera transports or manager commands.

Error means an explicit application fault, safe mode, required-worker stall, or
an enabled peer's terminal Failed lifecycle or copied terminal group-operation
failure (ordinary Cancelled is excluded). `RecordingPeerStatus::terminal_failure`
publishes the group's retired error stage independently of camera lifecycle: a
confirmation deadline or command admission failure is terminal even if the
connection remains Ready. Active camera retry remains Recovery, and an ordinary
pending confirmation remains Partial. Terminal group failure outranks recovery
on another peer or an explicitly published adapter recovery signal.
Connecting/Backoff or explicit adapter recovery selects Recovery, including
command retry. A pending shutter or query alone selects Partial. Transient peer
errors cannot mask recovery; copied terminal group-operation failure can.
A configured nonempty group with unavailable,
unknown, pending, mixed, or nonterminal-error peers is Partial. Recording and
Ready require every enabled peer ready, no pending or unknown observation, and
all authoritative observed counts Recording or Stopped respectively. Intent and
acknowledgement never prove recording. No composed group or enabled cameras
selects Off unless a higher priority condition is explicitly present. Cancellation
may invalidate camera observations and thus leave Partial until fresh evidence.

Device health stays separate from execution liveness. Only enabled, qualified,
current device observations participate. Their caller-selected `LedSeverity`
(Ignore, Partial, Error) makes severity policy explicit for Missing, NoFix,
Desynchronized, RetryExhausted, DisconnectStorm and IoError. Ok does not establish
camera readiness or worker liveness. Disabled, unqualified or stale observations
are ignored; a required-worker stall independently selects Error.

| State | Discrete RGB | Half-open on intervals and period | Mono |
|---|---|---|---|
| Ready | Green | Steady | Steady |
| Recording | Red | [0,500) ms every 1000 ms | Same timing |
| Partial | Red + green (amber) | [0,100), [200,300) ms every 2000 ms | Same timing |
| Recovery/retry | Blue | [0,100) ms every 200 ms | Same timing |
| Error | Red | [0,100), [200,300), [400,500) ms every 1000 ms | Same timing |
| Off | All channels off | Always off | Off |

`StatusLed::service` runs in one application scheduler with a forward uint32_t
millisecond clock. Each gap must be <2^31 ms; observed gaps >=2^31 restart phase
at that sample, and a completely unseen full wrap cannot be detected. Phase
accumulates elapsed time modulo the pattern period, retaining continuity across
multiple wraps when individual gaps meet the contract. A semantic state change
starts phase zero immediately, including equal-color transitions; unchanged
snapshots and generation updates preserve phase. Only a changed current frame
is written, with no missed-edge replay. A failed sink write invalidates the cache
and its error remains observable on subsequent attempts. Sink/SDK calls must
return promptly and must not reenter service; this is a caller precondition,
not a guarantee about arbitrary virtual callbacks. There are no delays, dynamic
allocations, timers or worker GPIO calls in the LED module.

`GpioLedSink` provides disabled, single-output and three separate GPIO discrete
RGB modes. `Esp32LedGpio` uses the pinned SDK's `gpio_config` and `gpio_set_level`
directly and preserves SDK errors; Arduino `digitalWrite` would drop them.
No addressable RGB, PWM, guessed `LED_BUILTIN`, modem status LED, or onboard
polarity is assumed. The source-only `LedWiring` defaults disabled, pins -1,
polarity Unspecified and unqualified. It is separate from saved configuration;
there is no new persistence schema. Startup currently composes this disabled
sink and loop consumes only already-published safe-mode/stall/watchdog fault
observations with no camera group. It cannot display fictitious camera Ready,
invoke watchdog policy, or activate a peripheral. Startup tests reject every
unexpected GPIO call.

Opt-in requires explicit board qualification and caller acknowledgement,
complete source-qualified reservations for **all** active board, button, SD,
IMU, interrupt and bus pins, and explicit ActiveHigh/ActiveLow per channel.
The enabled saved button GPIO is additionally passed to `begin` and checked for
conflict even if its own qualification is false; saved settings never qualify
an LED output. An unresolved reservation must leave `reservations_complete`
false. Validate the enabled button configuration separately before using it.
All validation completes before any GPIO operation. Reject duplicate/reserved
channels, unspecified/invalid polarity, malformed modes, and extra Mono channels.
The conservative output filter permits only GPIO18/19/21/22/23/32. It rejects
straps 0/2/5/12/15, flash 6–11, WROVER PSRAM 16/17, serial 1/3, input-only
34/35/36/39, unavailable pins and the candidate modem/SD allocations above.
**The filter does not establish free board pins.** GPIO21/22 may be I2C/IMU pins;
all allowed candidates still require matching schematic/header/continuity checks.

A disabled or refused configuration performs zero GPIO calls. Initialization is
startup-only: repeated begin returns its existing state without reconfiguration.
Changing hardware mode requires explicit owner recomposition, with fresh phase;
there is no live pin reassignment. Check the begin state, not only SDK error:
Refused has no SDK error because it performs no SDK operation. After qualification,
outputs are configured and initialized off. A configuration/output SDK fault
latches its first code and performs one bounded best-effort off pass for all
qualified channels. Later writes retain the code and perform no GPIO IO. Cleanup
failure cannot overwrite the original error. Electrical off after a fault and
glitch-free boot are not guaranteed by this software. Backend faults never alter
recording state, worker progress, recovery metadata or watchdog feed policy.

No onboard LED or external RGB module, pin, polarity, current, resistor value or
power source is physically qualified. #23 stays open for bench acceptance.
Before opt-in, record exact PCB revision/matching schematic, LED type and common
anode/cathode, each channel's series resistor, voltage/current and any required
driver, power source, routing and active polarity. Verify no modem/SD/IMU/button/
PSRAM/strap conflict. With firmware revision and polling cadence recorded,
measure startup/off behavior, all five distinguishable RGB and Mono signatures,
priority/transitions, current and boot/peripheral operation. Native fake-clock
and SDK tests plus target compilation are software evidence only. No firmware
has been flashed and no physical LED behavior is claimed here.

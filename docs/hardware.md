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

## Observed bench evidence

See [factory bring-up](hardware-results/2026-10-07-bringup.md) for the A7670E-FASE
identity, GNSS power-on/READY and empty no-fix responses. This verifies only the
factory-firmware probe, not the planned RideSync drivers. #1 retains remaining
board/pin/antenna qualification work.

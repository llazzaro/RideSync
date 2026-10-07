# Hardware bring-up

Target: LILYGO TTGO T-A7670E R2, ESP32-WROVER-E and **A7670E with built-in GPS**.
The user selected this GNSS-equipped variant; acquisition will use modem AT
commands, with no external GPS receiver required.
The [LILYGO board guide](https://wiki.lilygo.cc/products/t-sim-series/t-a7670/)
and [vendor examples](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series)
are the hardware authority. The scaffold follows the vendor's generic esp32dev
PlatformIO setup with PSRAM flags. Actual board flash/PSRAM must be verified.

Check the modem marking, board revision, GNSS antenna and exact schematic before selecting
UART, modem enable/reset, SD, LED or external-button GPIOs. No pin map is asserted
or used in the scaffold. Do not substitute T-Call or S3 board pin definitions.

Bring-up: attach USB, enumerate the serial port, build/upload, open 115200-baud
monitor and record the startup message. Next record board revision and modem
firmware. Test on a bench before mounting on a motorcycle.

The external momentary button will need a verified GPIO, pull resistor,
debounce and appropriate environmental protection. External RGB LED wiring and
power conditioning belong to milestone 7. Validate startup straps, peripheral
conflicts and modem supply current before wiring accessories.

## Planned motion logger hardware

Acceleration and motorcycle lean/pitch logging require an external IMU with an
accelerometer and gyroscope. The board/GNSS module alone does not provide these
motion measurements. Select the module during the IMU issue after checking
wiring, usable ranges, timestamping, driver license and rigid mounting. Preserve
raw samples; derived angles need a calibration and dynamic validation procedure.

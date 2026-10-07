# External sources and licensing

Inspected 2026-10-07. Commit links below freeze the research baseline. The three Insta360 MIT license files were read at the pinned revisions below.
Other license labels are metadata unless the audit notes explicitly say otherwise.
No third-party implementation or protocol documentation has been vendored.
RideSync's MIT license does not relicense any referenced project.

Official Insta360 GO 3S support pages were consulted on 2026-10-07. They describe
pairing and product behavior, but do not publish BLE services or control frames.
No official code or images are copied.

| Source | Inspected commit | License metadata | Use |
|---|---|---|---|
| [pchwalek/insta360_ble_esp32](https://github.com/pchwalek/insta360_ble_esp32/tree/83d4748b68d6ee5fd4414994a9e26b7d2f21364b) | `83d4748b68d6ee5fd4414994a9e26b7d2f21364b` | MIT | Reference only; no code copied |
| [marcelpallares/insta360-m5stick-remote](https://github.com/marcelpallares/insta360-m5stick-remote/tree/c76e140396de8b2404cdd36d17cf0d1a251a9dcc) | `c76e140396de8b2404cdd36d17cf0d1a251a9dcc` | MIT | Reference only; no code copied |
| [theserialhobbyist/insta360_m5StickC_remote](https://github.com/theserialhobbyist/insta360_m5StickC_remote/tree/6ea50ed8a2b276cb2461b06de97d8a801386a309) | `6ea50ed8a2b276cb2461b06de97d8a801386a309` | MIT | Reference only; no code copied |
| [xaionaro-go/insta360ctl](https://github.com/xaionaro-go/insta360ctl/tree/f94193ce03c5af0921a9992bfd1af6bd946150d0) | `f94193ce03c5af0921a9992bfd1af6bd946150d0` | No license detected (do not copy) | Reference only; no code copied |
| [TheAngryRaven/insta360-ble-gps-spec](https://github.com/TheAngryRaven/insta360-ble-gps-spec/tree/7964f1133e5d0f2c7eb73aaaaf5d6ebdd2127199) | `7964f1133e5d0f2c7eb73aaaaf5d6ebdd2127199` | No license detected (do not copy) | Reference only; no code copied |
| [Xinyuan-LilyGO/LilyGo-Modem-Series](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/tree/e8d8b82a23f324ef2ac1a24efe1c3b6cbabcca00) | `e8d8b82a23f324ef2ac1a24efe1c3b6cbabcca00` | MIT | Reference only; no code copied |
| [Insta360 GO Series: Action Pod Connection](https://onlinemanual.insta360.com/go3s/en-us/operating_tutorials/connect/actionpod) | Accessed 2026-10-07 | Official vendor support page; content reference only | GO 3S/Pod pairing prerequisites, control and stated Bluetooth range; no GATT protocol |
| [Insta360: Using GO 3S and Action Pod](https://onlinemanual.insta360.com/go3s/en-us/camera/basicuse/go3s_actionpod) | Accessed 2026-10-07 | Official vendor support page; content reference only | Describes button/control behavior when docked and remote Bluetooth control when separated |
| [Insta360 GO 3S Connection FAQ](https://onlinemanual.insta360.com/go3s/en-us/faq/operationtutorials/connection) | Accessed 2026-10-07 | Official vendor support page; content reference only | Connection indicator, wait/reset guidance and Action Pod wake behavior |
| [Insta360 GO 3S Firmware FAQ](https://onlinemanual.insta360.com/go3s/en-us/faq/operationtutorials/firmware) | Accessed 2026-10-07 | Official vendor support page; content reference only | Firmware update method; firmware versions remain unobserved |

The ESP32 example informs CE80 role/command research. The M5Stick fork and its
original project are multicamera/wake comparison points; their reported device
support does not prove RideSync support. insta360ctl compares remote emulation
and direct control. The GPS specification provides an X4 capture methodology
and telemetry investigation starting point. Vendor examples guide board setup.

[Vendor hardware guide](https://wiki.lilygo.cc/products/t-sim-series/t-a7670/)
is an additional documentation reference. Record exact modem AT manual revision
and license before adding GNSS implementation sources.

## IMU selection references (#27)

Inspected 2026-10-07 for a bounded module/driver comparison. No sensor code was
copied and no procurement or physical integration is implied.

| Source | Version/baseline | License | Use |
|---|---|---|---|
| [Bosch BMI270 product page and datasheet](https://www.bosch-sensortec.com/en/products/motion-sensors/imus/bmi270) / [datasheet PDF](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf) | Datasheet BST-BMI270-DS000-08, as identified by the linked PDF inspected 2026-10-07 | Manufacturer reference; no code reused | Ranges, output rates, FIFO, data-ready, sensor time and bare-chip supplies. |
| [SparkFun 6DoF BMI270 Qwiic hardware overview](https://docs.sparkfun.com/SparkFun_Qwiic_6DoF_BMI270/hardware_overview/) and [guide](https://docs.sparkfun.com/SparkFun_Qwiic_6DoF_BMI270/) | Guide accessed 2026-10-07; standard and Micro boards documented | SparkFun hardware design CC BY-SA 4.0 | Board supply/logic constraints, I²C/SPI/interrupt pins, pull-ups, address strap and dimensions. Stock and physical revision are unverified. |
| [SparkFun BMI270 Arduino Library](https://github.com/sparkfun/SparkFun_BMI270_Arduino_Library/tree/v1.0.3) | v1.0.3, release commit `21ea234` | MIT for SparkFun wrapper; bundled Bosch BMI270 API v2.86.1 files declare BSD-3-Clause | C++ driver evidence for I²C/SPI, FIFO, watermark, interrupt configuration and converted sample struct. Pin exact release before later integration. |
| [ST LSM6DSOX datasheet](https://www.st.com/resource/en/datasheet/lsm6dsox.pdf); [Adafruit LSM6DS breakout guide](https://learn.adafruit.com/st-9-dof-combo); [Adafruit driver](https://github.com/adafruit/Adafruit_LSM6DS) | DS12814 Rev 4; driver inspected 2026-10-07 | Adafruit driver BSD; board hardware license per product files | Bounded alternative comparison for 208 Hz ODR/FIFO, ranges, breakout logic conditioning and Arduino driver. |

The SparkFun library release repository contains a Bosch BMI270 API subtree;
the [v1.0.3 BMI270 header](https://github.com/sparkfun/SparkFun_BMI270_Arduino_Library/blob/v1.0.3/src/bmi270_api/bmi270.h)
declares Bosch BMI270 API v2.86.1 under BSD-3-Clause. MIT labeling for the
wrapper does not override those notices. Audit and pin the exact dependency
tree and licenses before adding it to RideSync.

The raw IMU software path now pins SparkFun v1.0.3 at
`21ea234de321da07c552f7a43cb36f7df4f73a27` in platformio.ini, preserving
SparkFun MIT (copyright 2020 SparkFun Electronics) and bundled Bosch 2.86.1
BSD-3-Clause notices in the unmodified dependency. No vendor implementation
is copied into repository sources. [raw_imu.md](raw_imu.md) documents the owned
Bosch API adapter and source-byte parser; physical activation stays disabled. The platform and resolved framework/toolchain/build
packages are pinned explicitly in platformio.ini. Consult each installed package's own license.

## GoPro planning references

The official [Open GoPro portal](https://gopro.github.io/OpenGoPro/),
[BLE setup](https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup/) and
[control API](https://gopro.github.io/OpenGoPro/docs/ble/control/) were consulted
on 2026-10-07 for compatibility, pairing and recording-control planning.
[GoPro's repository](https://github.com/gopro/OpenGoPro) is a reference only;
no code or SDK is included. GitHub's repository license metadata reports
NOASSERTION; inspect the applicable file/package license before any reuse.

## BLE decision evidence

Source/API inspection on 2026-10-07; no upstream code copied into RideSync.
The following immutable links support ADR-001:

- [ESP32 example](https://github.com/pchwalek/insta360_ble_esp32/blob/83d4748b68d6ee5fd4414994a9e26b7d2f21364b/Insta_BLE.ino):
  CE80 server and characteristics, secondary service, broadcast notifications,
  X3/RS 1-inch declaration. Its edited framework is reference only.
- [M5Stick fork routing and timer heuristic](https://github.com/marcelpallares/insta360-m5stick-remote/blob/c76e140396de8b2404cdd36d17cf0d1a251a9dcc/ble_handlers.h),
  [timeout](https://github.com/marcelpallares/insta360-m5stick-remote/blob/c76e140396de8b2404cdd36d17cf0d1a251a9dcc/insta360_m5StickC_remote-main.ino),
  [shutter event](https://github.com/marcelpallares/insta360-m5stick-remote/blob/c76e140396de8b2404cdd36d17cf0d1a251a9dcc/config.h),
  and [original event](https://github.com/theserialhobbyist/insta360_m5StickC_remote/blob/6ea50ed8a2b276cb2461b06de97d8a801386a309/config.h).
  MIT licenses read: ESP32 copyright pchwalek (2026); both M5 projects copyright
  Cameron Coward (2025). Preserve notices if code is reused in future.
- [NimBLE-Arduino 2.3.6 configuration](https://github.com/h2zero/NimBLE-Arduino/blob/dfb4ac561a06797081be9e752902a6582e7f029e/src/nimconfig.h):
  enabled roles, default 3 connections and controller-ceiling comment.
- [NimBLE send implementation](https://github.com/h2zero/NimBLE-Arduino/blob/dfb4ac561a06797081be9e752902a6582e7f029e/src/NimBLECharacteristic.cpp)
  and [callback API](https://github.com/h2zero/NimBLE-Arduino/blob/dfb4ac561a06797081be9e752902a6582e7f029e/src/NimBLECharacteristic.h):
  explicit connection-handle notifications, broadcast default and per-peer callbacks.
- [NimBLE central subscription API](https://github.com/h2zero/NimBLE-Arduino/blob/dfb4ac561a06797081be9e752902a6582e7f029e/src/NimBLERemoteCharacteristic.h),
  [client routing](https://github.com/h2zero/NimBLE-Arduino/blob/dfb4ac561a06797081be9e752902a6582e7f029e/src/NimBLERemoteCharacteristic.cpp),
  [device implementation](https://github.com/h2zero/NimBLE-Arduino/blob/dfb4ac561a06797081be9e752902a6582e7f029e/src/NimBLEDevice.cpp):
  client creation, host task and controller connection configuration.
  [Apache-2.0 LICENSE](https://github.com/h2zero/NimBLE-Arduino/blob/dfb4ac561a06797081be9e752902a6582e7f029e/LICENSE)
  read; individual source headers retain their own notices. Library is proposed,
  not yet a project dependency; redistribution must retain applicable notices.

Official Open GoPro documentation was rechecked on 2026-10-07:
[compatibility](https://gopro.github.io/OpenGoPro/docs/) (HERO12 minimum
v01.10.00), [setup](https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup/),
[control](https://gopro.github.io/OpenGoPro/docs/ble/control/) and
[statuses](https://gopro.github.io/OpenGoPro/docs/ble/statuses/).
These are live documentation URLs; record the checked date and recheck per
camera firmware before implementation. The portal describes the API as MIT;
this does not establish a blanket license for all SDK/dependency files.
The [repository LICENSE](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/LICENSE)
was read and points to component third-party dependency licenses rather than
providing a blanket grant. Repository inspected at
`0f963572611c4410a15678531e9681a6ff874edb`; reference only, no SDK code reused.
Unlicensed insta360ctl/GPS-spec sources remain research references only; no
implementation was copied or used by the stack probe.

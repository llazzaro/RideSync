# External sources and licensing

Inspected 2026-10-07. Commit links below freeze the research baseline. The three Insta360 MIT license files were read at the pinned revisions below.
Other license labels are metadata unless the audit notes explicitly say otherwise.
No third-party implementation or protocol documentation has been vendored.
RideSync's MIT license does not relicense any referenced project.

| Source | Inspected commit | License metadata | Use |
|---|---|---|---|
| [pchwalek/insta360_ble_esp32](https://github.com/pchwalek/insta360_ble_esp32/tree/83d4748b68d6ee5fd4414994a9e26b7d2f21364b) | `83d4748b68d6ee5fd4414994a9e26b7d2f21364b` | MIT | Reference only; no code copied |
| [marcelpallares/insta360-m5stick-remote](https://github.com/marcelpallares/insta360-m5stick-remote/tree/c76e140396de8b2404cdd36d17cf0d1a251a9dcc) | `c76e140396de8b2404cdd36d17cf0d1a251a9dcc` | MIT | Reference only; no code copied |
| [theserialhobbyist/insta360_m5StickC_remote](https://github.com/theserialhobbyist/insta360_m5StickC_remote/tree/6ea50ed8a2b276cb2461b06de97d8a801386a309) | `6ea50ed8a2b276cb2461b06de97d8a801386a309` | MIT | Reference only; no code copied |
| [xaionaro-go/insta360ctl](https://github.com/xaionaro-go/insta360ctl/tree/f94193ce03c5af0921a9992bfd1af6bd946150d0) | `f94193ce03c5af0921a9992bfd1af6bd946150d0` | No license detected (do not copy) | Reference only; no code copied |
| [TheAngryRaven/insta360-ble-gps-spec](https://github.com/TheAngryRaven/insta360-ble-gps-spec/tree/7964f1133e5d0f2c7eb73aaaaf5d6ebdd2127199) | `7964f1133e5d0f2c7eb73aaaaf5d6ebdd2127199` | No license detected (do not copy) | Reference only; no code copied |
| [Xinyuan-LilyGO/LilyGo-Modem-Series](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/tree/e8d8b82a23f324ef2ac1a24efe1c3b6cbabcca00) | `e8d8b82a23f324ef2ac1a24efe1c3b6cbabcca00` | MIT | Reference only; no code copied |

The ESP32 example informs CE80 role/command research. The M5Stick fork and its
original project are multicamera/wake comparison points; their reported device
support does not prove RideSync support. insta360ctl compares remote emulation
and direct control. The GPS specification provides an X4 capture methodology
and telemetry investigation starting point. Vendor examples guide board setup.

[Vendor hardware guide](https://wiki.lilygo.cc/products/t-sim-series/t-a7670/)
is an additional documentation reference. Record exact modem AT manual revision
and license before adding GNSS implementation sources.

Current firmware has no external library dependencies beyond the PlatformIO
Arduino framework/toolchain. The platform and resolved framework/toolchain/build
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

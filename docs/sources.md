# External sources and licensing

Inspected 2026-10-07. Commit links below freeze the research baseline. License
labels are GitHub metadata, not a substitute for reading the license before reuse.
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

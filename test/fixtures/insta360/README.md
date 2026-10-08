# CE80 shutter-event wire evidence

Inspected 2026-10-09. `ce80_shutter.h` independently records the complete literal
`FC EF FE 86 00 03 01 02 00` from
[config.h SHUTTER_CMD at c76e140](https://github.com/marcelpallares/insta360-m5stick-remote/blob/c76e140396de8b2404cdd36d17cf0d1a251a9dcc/config.h).
The same complete event is assigned byte by byte in
[Insta_BLE.ino shutterButton at 83d4748](https://github.com/pchwalek/insta360_ble_esp32/blob/83d4748b68d6ee5fd4414994a9e26b7d2f21364b/Insta_BLE.ino).
It travels from the emulated remote peripheral to the camera by CE82
notification; it is not a BE81 central write or an incoming state packet.

Both source LICENSE files were read at these exact pins:
[M5 MIT license, copyright 2025 Cameron Coward](https://github.com/marcelpallares/insta360-m5stick-remote/blob/c76e140396de8b2404cdd36d17cf0d1a251a9dcc/LICENSE)
and
[ESP32 MIT license, copyright 2026 pchwalek](https://github.com/pchwalek/insta360_ble_esp32/blob/83d4748b68d6ee5fd4414994a9e26b7d2f21364b/LICENSE).
Only wire facts are independently represented; no upstream implementation is
vendored. Source attribution is separate from RideSync's license.

Evidence class: **pinned licensed community source**, not a RideSync capture.
The ESP32 example declares X3/RS 1-inch; the M5 fork comments report X5 testing.
Neither provides our installed X5 firmware or a confirmed RideSync recording
result. The [separate one-shot bench trial](../../../docs/hardware-results/2026-10-09-x5-pairing.md#separate-one-shot-shutter-trial)
submitted this event once through addressed CE82 notification with SDK status 0.
The owner answered yes about an indicator/timer but also reported no X5 SD card
and inability to record. Recording remains unclassified; successful recording
and saved media were not established. The literal fixture is maintained
independently of encoder output, not derived from that trial.

The API returns exactly nine owned bytes and accepts no input buffer, frame,
direction or sequence parameter. The test checks every byte against this
literal; null/short-buffer and malformed-receive tests would have no applicable
surface. Do not infer a length field, sequence counter, ACK or state from its
opaque bytes. There is no receive parser and no supported idempotent Start/Stop.
Mode-dependent shutter/button toggling must never be automatically repeated
after ambiguous delivery or reset. The bench has a connected/subscribed X5
submission path; successful REC/STOP and authoritative state semantics remain
unverified. Issue #19 is not complete from this fixture or submission alone.

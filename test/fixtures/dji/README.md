# DJI R SDK codec evidence

Software/schema evidence only; no hardware captures or compatibility claim.
The codec is independently authored from DJI's public wire descriptions, pinned
at [dji-sdk/Osmo-GPS-Controller-Demo](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo) commit
`92fe23e5a749f189593f980a26a105c3bb66aa1c`:

- `docs/protocol.md`: R SDK framing, lengths, command/response and sequence bits.
- `docs/protocol_data_segment.md`: 0019 pairing, 1D03 explicit record control,
  1D05 subscription, 1D02 status, 1D04 mode switch, and model identifiers.
- `test/connect_cmd_frame_builder/connect_cmd_frame.txt`: first literal 0019
  vector (controller `0x12345678`, verification 8190, sequence 1).
- The literal Hyperlapse 1D04 vector is from `protocol_data_segment.md`.

No upstream implementation or CRC lookup table is vendored. The upstream files
carry MIT labels together with conflicting proprietary notices; the original
implementation uses only wire facts. CRC parameters are reflected CRC16/IBM
(poly `0xA001`) and CRC32/IEEE (`0xEDB88320`), seed `0x3AA3`, no final XOR.
Tests use independent bitwise polynomial division, plus the published literal
CRC results `0xEE57` / `0xD0E1FAF4`. Generated frames are synthetic/schema-derived.

## Deliberate limits and disagreements

- Exactly version 0, unencrypted frames, zero reserved command-type bits; header
  reserved bytes remain opaque. Incoming frames are bounded at 256 bytes;
  outgoing packets at 64 bytes. Receiver handles fragmented/coalesced frames one
  byte at a time and rejects invalid header CRC before waiting for the payload.
- Known payloads require exact lengths. Unknown valid commands remain opaque.
  No GPS, wake, key-button emulation or 1D06 display-string interpretation.
- The document specifies a five-byte record response, while the pinned demo's
  `record_control_response_frame_t` is one byte. Both exact forms are admitted;
  other sizes are invalid. Mode response is exactly the documented five bytes.
- Model IDs are the document's `0xFF33/44/55/66` encoded little endian. The
  published Hyperlapse example has `00 00 33 FF`, i.e. `0xFF330000`, despite
  the documented Action 4 ID `0xFF33`. The example validates framing and CRC;
  it does not override the model-ID table. The demo record sender separately
  hardcodes `0x33FF0000`, while the connection example uses `0x12345678`;
  these disagreeing sample IDs are not a model-authentication contract.
  Controller identity is separately
  commissioned by the adapter, never inferred from the target camera ID.
- ACKs never establish Recording/Stopped. Only a 38-byte status command can do
  that. Eligible observation modes are ordinary Video (`0x01`), Panoramic
  Video (`0x38`) and Boost Video (`0x41`). The remaining modes stay Unknown
  even with camera_status 3, including Timelapse (still subtype ambiguity),
  Selfie and Vortex (no explicit video label), slow motion, Hyperlapse and
  SuperNight (outside the initial supported observation scope). Photo, tracking
  (documented using photo semantics), livestream, unknown modes, pre-record,
  QS mode mismatch, unknown/sleep power, or high/unknown temperature remain
  Unknown. Normal or warning temperature with a matching current-mode flag
  admits camera_status 3 as Recording and 0/1/2 as Stopped.
- This codec preserves all 16-bit sequences, including camera sequence 0 and
  65535; uniqueness, pending command matching, model authentication and no-replay
  are adapter responsibilities. Authorization replies echo the passed incoming
  sequence and include the separately assigned camera index.

Focused host build (no PlatformIO shared-build mutation):

```
clang -I.pio/libdeps/native/Unity/src -c .pio/libdeps/native/Unity/src/unity.c -o /tmp/ridesync-dji-unity.o
clang++ -std=c++11 -Wall -Wextra -Werror -Iinclude -I.pio/libdeps/native/Unity/src test/test_dji_codec/test_main.cpp src/protocol/dji_codec.cpp /tmp/ridesync-dji-unity.o -o /tmp/ridesync-dji-codec-test
/tmp/ridesync-dji-codec-test
```

# Experimental DJI Osmo recording profile

Issue [#47](https://github.com/llazzaro/RideSync/issues/47) implements the initial
DJI recording adapter in the composed [bench application](bench_test_version.md).
All four profiles remain **Experimental / Not tested** on physical cameras.
The first installed model and firmware have not been supplied. Record them in
[#46](https://github.com/llazzaro/RideSync/issues/46) before bench execution;
[#22](https://github.com/llazzaro/RideSync/issues/22) owns compatibility acceptance.

| Camera | Config model | Wire target ID | Initial capabilities |
|---|---|---|---|
| Osmo Action 4 | `DJI_ACTION4` (5) | `0xFF33` | Pair, explicit REC/STOP, observed status/query |
| Osmo Action 5 Pro | `DJI_ACTION5_PRO` (6) | `0xFF44` | Same |
| Osmo Action 6 | `DJI_ACTION6` (7) | `0xFF55` | Same |
| Osmo 360 | `DJI_OSMO360` (8) | `0xFF66` | Same, model-specific video modes |

Osmo Nano and other DJI products are unsupported by this pinned route. GPS
forwarding, power/wake commands and automatic mode switching are unsupported in
this initial adapter. Source configuration requesting DJI GPS or wake is refused.
`WAKE` can request a normal reconnect, with no cold-power-on claim or shutter replay.

## Transport and protocol

The source baseline is DJI's official
[Osmo-GPS-Controller-Demo](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/tree/92fe23e5a749f189593f980a26a105c3bb66aa1c),
commit `92fe23e5a749f189593f980a26a105c3bb66aa1c`. The independently authored
C++11 codec uses AA framing, explicit lengths/version, sequence and command
matching, CRC16/CRC32, fixed buffers and bounded fragmented receive handling.
Official connection/mode examples and independently constructed expected frames
are synthetic source evidence, not hardware captures.

Service FFF0 exposes notification FFF4 and write FFF5. The documented demo uses
Write Without Response and application protocol authentication, without initiating
an SMP bond. DJI opts into those transport choices explicitly; existing profiles
retain their bonded transport and acknowledged-write defaults. Successful local
write completion proves SDK submission only. The host ownership barrier must
finish before the next operation, and camera protocol responses remain separate.

Pairing uses 00/19. A first pair requests camera-screen approval; a valid camera
allow response is required, and rejection or timeout retires the connection.
The documented ACK-then-grant and direct-grant flows are supported. Already-paired
mode must be declared from actual commissioning. Neither mode invents approval.
The adapter subscribes to periodic status using 1D/05 and resynchronizes with
single queries; recording observations come from 1D/02 camera status.

Explicit recording 1D/03 uses Start=0 and Stop=1. The status decoder reports video
recording only in known ordinary video mode, or the documented Osmo 360 video
modes, with normal power and valid recording status. Photo, burst, pre-recording,
QS, sleep, unsupported or undecodable modes remain Unknown. Commands require a
fresh eligible video observation. Set the camera to ordinary video before CONNECT;
automatic mode switching is withheld because the demo's hardcoded examples and
model-ID table disagree. ACKs never become recording observations. Stale status
expires after 1500 ms. Disconnect, cancellation and reconnect erase pending
intent; no Start/Stop is replayed.

## Private commissioning

Populate `BenchApplicationConfig::cameras.dji[slot]` for each configured DJI slot.
Use the same slot in `source.cameras` and telemetry peer mappings. The source
model must match `DjiQualification::model`, and the source address must match
its qualified identity. DJI does not require an X5 in the group; if an X5 is
present it retains slot zero. The mixed application still admits four active
camera slots, while source configuration retains its existing eight-slot format.

Supply actual values for every qualification field in `include/profiles/dji_camera.h`:

- `identity`: actual peer address, address type and commissioned identity facts.
- `model`, `firmware` and `firmware_size`: exact supported model and nonempty,
  bounded printable firmware version. No universal minimum firmware is published
  in the pinned source; do not substitute a guessed version.
- `source_qualified`: the selected documented protocol profile was reviewed.
- `controller_mac` and `controller_mac_verified`: the actual six-byte BLE
  controller address used by this board, confirmed separately.
- `controller_id`: explicitly commissioned nonzero controller identity. This is
  separate from the per-camera target ID table; do not copy conflicting sample
  endianness/constants as universal device facts.
- `connection_challenge`: declared four-digit pairing challenge (0–9999).
- `already_paired`: actual pairing state for this controller/camera.
- `unencrypted_protocol_reviewed`: explicit review of the demo's application
  authentication without a transport encryption guarantee.

Do not publish private addresses, challenge or store proofs. Supply the existing
board, SD namespace and local logger qualifications from the bench guide; adding
a camera does not commission unrelated hardware. Keep saved settings aligned
with the private profile: valid persisted settings take precedence over defaults.

Build `bench_application` for actual commissioning. `dji_adapter_compile` retains
the DJI implementation on the existing pinned ESP32/NimBLE host with activation
disabled; the private compile fixture always declines activation and supplies no
camera facts. For physical testing retain revision, binary hash, model/firmware,
private configuration and serial/SD evidence. In #46 record pairing allow/reject,
three observed REC/STOP cycles with saved-media playback, reconnect without
replay, and mixed-group/missing-peer progress while logging continues. Hardware
failure is a test result to fix, not evidence that undocumented support works.

## Provenance and reuse

The wire layout and model table come from
[protocol.md](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/92fe23e5a749f189593f980a26a105c3bb66aa1c/docs/protocol.md)
and [protocol_data_segment.md](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/92fe23e5a749f189593f980a26a105c3bb66aa1c/docs/protocol_data_segment.md).
Pairing and GATT decisions were checked against the pinned `logic/connect_logic.c`
and `ble/ble.c`. Record responses accept the documented five-byte form and the
one-byte form used by the demo. Fixture provenance is in `test/fixtures/dji/`.

The upstream [LICENSE](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/92fe23e5a749f189593f980a26a105c3bb66aa1c/LICENSE)
contains DJI EULA statements for protocol/demo and an MIT section; these are not
represented as a blanket MIT license. No upstream implementation is vendored or
translated into RideSync. Its bounded codec/adapter were authored independently
from published protocol facts. Any future copying of upstream code must resolve
applicable terms and retain its notices first.

## Build resource evidence

The retained DJI build uses 72416 bytes of static RAM and 890205 bytes of flash;
the private-provider compile branch uses 65520 and 900197 respectively. The
largest compiler-reported DJI function frame is 768 bytes. The mixed owner uses
a checked boot-lifetime internal-RAM allocation rather than static DRAM; its
callback addresses remain stable. Host tests verify inert terminal allocation
failure and one-time construction/attachment. These build numbers exclude dynamic
worker/SDK allocation and are not observed free-heap or stack headroom. Record
`STATUS` heap/stack measurements in the physical session before installation.

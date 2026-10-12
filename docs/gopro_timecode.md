# HERO12 Black BLE timecode investigation — #51

Investigated October 12, 2026. Decision: **Unresolved for native timecode
synchronization over BLE; do not enable a RideSync timecode operation.**
Documented BLE clock setting exists, but the inspected sources do not establish
subsecond phase control, a frame-alignment bound or native saved-footage accuracy.
This completes the source investigation boundary in
[acceptance_policy.md](acceptance_policy.md); conditional software implementation
and physical qualification are not delivered by this finding.

## Model, firmware and modes

Scope is **GoPro HERO12 Black**, not HERO9–11, HERO13 or MISSION 1.
The installed camera firmware, returned Open GoPro API version, Quik version,
recording preset and observed timecode behavior remain **Unknown / Not tested**.
No camera was connected or footage acquired in this investigation.

The [GoPro launch announcement](https://gopro.com/en/us/news/hero12-black-camera-max-lens-mod-2-launch)
documents native multi-camera timecode and editing support. The GoPro-authored
[HERO12 manual, pages 142–143](https://www.adorama.com/col/productManuals/GPHERO12HB.pdf)
(hosted by a retailer) describes adding the camera to Quik, selecting Sync
Timecode, and scanning its QR code using any mode or preset. The camera reports
success; after initial addition the QR screen is available without connecting
the camera. That mode statement concerns QR scanning, not BLE command eligibility
or every mode's resulting media/timecode semantics. TimeWarp, time lapse,
Hindsight, slow motion and chapter/editor/export behavior have no RideSync
qualification here.

The [current US stock update page](https://gopro.com/en/us/update/hero12-black)
listed **v2.42** on the investigation date. The pinned
[Labs firmware list](https://github.com/gopro/labs/blob/80d419a4bfed0d0e99631e20d79c5db09784cf74/README.md#L20)
listed HERO12 **v2.40.70**, November 4, 2025. These are documentary release
identifiers, not the installed firmware or proof of BLE timecode support.
The existing [GoPro plan](gopro_plan.md) records a documentary Open GoPro minimum
of v01.10.00; identity/API checks and feature qualification remain separate.

## What the BLE sources actually expose

Primary implementation inspected: GoPro's OpenGoPro repository pinned to
`0f963572611c4410a15678531e9681a6ff874edb`. The official
[command definitions](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/sdk_wireless_camera_control/open_gopro/api/ble_commands.py#L251),
[command IDs](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/sdk_wireless_camera_control/open_gopro/models/constants/constants.py#L29),
[serializer](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/sdk_wireless_camera_control/open_gopro/parsers/bytes.py#L152)
and [BLE control documentation](https://gopro.github.io/OpenGoPro/docs/ble/control/#set-date-time)
agree on ordinary date/time commands.

| Operation | ID | Value layout, big endian | Resolution represented |
|---|---|---|---|
| Set Date Time | `0x0D` | year uint16, month/day/hour/minute/second uint8; 7 bytes | Whole seconds; timezone ignored |
| Set Local Date Time | `0x0F` | Same 7 bytes, UTC offset int16 in minutes, DST uint8 (0/1); 10 bytes | Whole seconds plus timezone/DST |
| Get Date Time | `0x0E` | Read corresponding clock value | No subsecond readback established |
| Get Local Date Time | `0x10` | Read corresponding local clock value | No subsecond readback established |

The serializer explicitly uses `obj.second` and does not serialize microseconds.
There is no frame count, fractional-second field, DF/NDF flag, scheduled apply
time, phase measurement or round-trip correction field in these set payloads.

The [UUID definitions](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/sdk_wireless_camera_control/open_gopro/models/constants/uuids.py#L26)
use Command `GP-0072` and Command Response `GP-0073` under the control/query service.
The [data protocol](https://gopro.github.io/OpenGoPro/docs/ble/protocol/data_protocol/)
wraps commands as ID followed by parameter length/value pairs; responses include
ID and status (`0` success, `1` error, `2` invalid parameter; others reserved).
An ATT write success and a matching command success response are different
facts; neither contains an achieved timecode offset or drift measurement.
For example, the documented date value `2023-01-31 03:04:05` gives unpacketized
message `0D 07 07 E7 01 1F 03 04 05`. This is a schema-derived example, not an
observed camera capture and not an enabled RideSync command.

No inspected primary source establishes when a successful clock write resets
fractional clock phase, its processing delay/jitter, whether an existing native
timecode offset is overwritten, or what happens during encoding. Timing a
whole-second write to a host second boundary would depend on those unknowns.
It cannot justify frame accuracy merely because the host has GNSS UTC.

## GitHub solutions inspected

GitHub/web searches included `gopro timecode`, HERO12 BLE timecode, and the
OpenGoPro/Labs sources. This is a scoped search result, not a claim that no
private or undiscovered BLE route exists.

| Candidate and pinned evidence | Actual route | Outcome for #51 |
|---|---|---|
| [GoPro OpenGoPro BLE commands](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/sdk_wireless_camera_control/open_gopro/api/ble_commands.py#L251) and serializer above | Date/time over BLE, whole seconds | Real clock API; no evidenced precision timecode operation |
| [GoPro Labs precision QR implementation](https://github.com/gopro/labs/blob/80d419a4bfed0d0e99631e20d79c5db09784cf74/docs/control/precisiontime/README.md#L340) | Animated optical QR with millisecond date/time, DST, timezone | Working upstream generator; manual alternative, not BLE |
| [mikgangal/gopro-timecode code](https://github.com/mikgangal/gopro-timecode/blob/1579cad3012d62791b919bdb4b8ef8301136ecdb/gopro%20time%20sync/src/main.cpp) and [README](https://github.com/mikgangal/gopro-timecode/blob/1579cad3012d62791b919bdb4b8ef8301136ecdb/README.md) | ESP32/DS3231; BLE enables Wi-Fi, legacy HTTP writes whole-second date/time | README reports HERO9 tested and HERO10/11 expected; no HERO12 firmware/footage evidence. Requires additional Wi-Fi/RTC scope and does not prove native timecode precision |

No external code was copied or dependency installed.

## Manual alternatives and limitations

The [GoPro-owned HERO12 QR discussion](https://github.com/gopro/labs/discussions/640)
contains a collaborator's stock-HERO12 example
`oT231108082611.785oTD0oTZ-8`: precise time, DST and timezone are required by that
parser. This is an optical command, not bytes to send to a BLE characteristic.
The stock Quik workflow is a practical existing solution to try manually; it
requires no new RideSync display or firmware feature.

The pinned Labs generator above derives milliseconds from the host clock and
renders an animated QR. The [precision-time page](https://gopro.github.io/labs/control/precisiontime/)
includes HERO12 in Labs compatibility and recommends refreshing shortly before
capture because clocks drift. A [GoPro collaborator's power-state warning](https://github.com/gopro/labs/discussions/883)
(January 14, 2025) describes
approximately one-frame precision while powered and approximately one-second
precision after power-down. That is an upstream statement, not a measured
RideSync/HERO12 result or a bound for BLE clock writes.

[GoPro Labs LTC documentation](https://github.com/gopro/labs/blob/80d419a4bfed0d0e99631e20d79c5db09784cf74/docs/control/ltc/README.md)
and [HERO12 Deity/Tentacle discussion](https://github.com/gopro/labs/discussions/1350)
describe an audio-input/Labs route, with source-specific failures reported.
Do not add Labs firmware, an audio/timecode accessory, a display or Wi-Fi to
RideSync under this BLE ticket. QR/LTC documentation also does not establish
simultaneous shutter timing or sensor genlock.

## Exact missing evidence and next decision

Either of the following can reopen the BLE feasibility decision:

1. An official HERO12 SDK/schema/firmware statement specifying a precision
   clock or native timecode BLE operation: UUID, full framing, units, fractional
   resolution, acknowledgement and apply-time semantics, eligible modes/power
   states, firmware/API minimum, and an accuracy premise.
2. An annotated **HERO12** capture of a genuine Quik precision-sync operation,
   with exact stock/Labs firmware and Quik/phone versions. Record screen actions,
   whether QR was scanned, timestamped GATT UUIDs/directions/raw writes and
   notifications, and any Wi-Fi traffic. Compare one ordinary connection/date
   update with one Sync Timecode action, independently controlling the optical
   QR path. Include camera identity/API responses and matching saved native
   timecode/reference evidence. A BLE trace taken while scanning QR cannot by
   itself attribute clock synchronization to BLE.

If the only precision operation is optical and the ordinary BLE clock phase
cannot be bounded, mark the BLE goal infeasible for that tested firmware/scope;
keep the manual workflow available. Do not guess extra bytes or reinterpret
calendar seconds as frames.

If a usable BLE route is subsequently established, implementation must be
opt-in, use a valid fresh time source, admit only fresh pre-recording work, bound
its timeout, cancel on disconnect/reset/record start, and never replay an old
request on reconnect. Recording and local logging must progress independently.
Report separate states for requested, command acknowledged and native footage
alignment observed; do not derive Synced from a write/ACK. Independent protocol
fixtures, negative/error tests and the relevant pinned build/CI belong to that
conditional implementation, not this documentation-only finding.

## Finite physical handoff

[#46](https://github.com/llazzaro/RideSync/issues/46) executes ready camera rows;
[#22](https://github.com/llazzaro/RideSync/issues/22) owns compatibility acceptance.
The following checklist remains **Not run** and is conditional on an evidenced
route. A manual QR campaign may collect useful reference evidence but cannot
pass BLE implementation acceptance.

- Record model, installed firmware/API, route, Quik version if used, resolution,
  actual frame rate (including fractional rates), DF/NDF interpretation, power
  source and intended editor/export versions.
- Declare a target before testing: proposed initial native-timecode error of at
  most one frame against an independent timestamped reference. State reference
  uncertainty and the separate allowed drift over 30 minutes before the run;
  this proposal is not an achieved accuracy claim.
- Run three fresh pre-recording sync attempts; retain raw request/response logs
  and original saved files. Inspect native timecode and the independent optical
  or audio reference in saved footage rather than judging success from UI/ACK.
- Verify the intended editor/import/export preserves or deliberately maps native
  timecode. Record both original and exported values and treatment of chapters.
- Record one declared 30-minute clip and measure initial offset and drift with
  the same reference; report frames and elapsed seconds at the actual frame rate.
- Perform one reconnect and one declared sleep/power-cycle scenario; observe
  whether alignment survives or requires a fresh sync and confirm no stale
  request replay for any eventual RideSync implementation.

Documentation/source inspection is the only evidence produced here. Hardware
support remains **Experimental / Not tested**; no native timecode field, optional
operation, configuration flag or firmware behavior was added.

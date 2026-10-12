# Insta360 X5 timecode investigation (#50)

Reviewed October 12, 2026 for
[#50](https://github.com/llazzaro/RideSync/issues/50). Scope: X5 footage alignment
through optional BLE synchronization. Other cameras are separate work.

**Decision: unresolved BLE route; do not implement or transmit guessed clock or
timecode commands.** Native X5 timecode is documented. The sources below provide
clock-setting leads, but not a qualified command for RideSync's existing X5
connection or a measured timecode accuracy result. This is not a finding that
Bluetooth synchronization is impossible. Conditional implementation remains open.

## Camera feature and current evidence

The official [X-series timecode guide](https://onlinemanual.insta360.com/x5/en-us/operating_tutorials/step/tc)
documents X5 timecode stored in the media timecode track. Camera Settings >
Timecode offers system-time synchronization or manual alignment. The documented
app workflow connects the camera to the app and refreshes the timecode page.
An external generator can synchronize through the camera's Mic Adapter.

The guide lists Video, Active HDR, InstaFrame, PureVideo, Loop Recording, Road
Mode and Single-Lens modes. External generator and camera frame rates must match.
The guide does not give an X5 minimum firmware, BLE packet, application latency,
clock accuracy or persistence guarantee across sleep/power cycling. Record the
actual mode, frame rate and DF/NDF setting before measuring alignment.

Existing [X5 receive evidence](hardware-results/2026-10-10-x5-receive-capture.md)
reuses the owner's firmware report of **1.11.10**; it is not a fresh firmware-screen
observation or a timecode test. No local timecode-setting capture, saved timecode
track inspection, export check or drift measurement was obtained in this
investigation. Installed firmware and timecode behavior remain to be qualified.

## Source-backed leads

| Source | What it establishes | What remains unknown |
|---|---|---|
| Official Android [camera API](https://github.com/Insta360Develop/Insta360-Developer_Docs/blob/caa3700d0bf2a1b96317211f64431bcd0dfe11c0/docs/en/sdk/x-ace-go/android/camera-api.md) and [integration guide](https://github.com/Insta360Develop/Insta360-Developer_Docs/blob/caa3700d0bf2a1b96317211f64431bcd0dfe11c0/docs/en/sdk/x-ace-go/android/camera-integration.md) | `setLocalTime(localTime, callback)` exists; the SDK separately supports BLE connection | Whether this operation works over BLE on X5, its units/wire schema, precision, response semantics and effect on native timecode |
| Official desktop [camera API](https://github.com/Insta360Develop/Insta360-Developer_Docs/blob/caa3700d0bf2a1b96317211f64431bcd0dfe11c0/docs/en/sdk/x-ace-go/desktop/camera.md) | X5 is listed; `SyncLocalTimeToCamera(uint64_t utc_time, int32_t time_zone_offset_sec)` exists | The documented desktop transport is USB only; its method is not an ESP32 BLE protocol specification |
| RigacciOrg [Wi-Fi API implementation](https://github.com/RigacciOrg/insta360-wifi-api/blob/89320e06a2395140e9e506a735c59a7ff8d88446/insta360.py) and [README](https://github.com/RigacciOrg/insta360-wifi-api/blob/89320e06a2395140e9e506a735c59a7ff8d88446/README.md) | Source derived from ONE RS Wi-Fi traffic sends command 7 (`SET_OPTIONS`) with `LOCAL_TIME` and `TIME_ZONE`; default time is integer Unix seconds | X5 compatibility, BLE delivery, fractional precision and native timecode behavior are not established by that ONE RS Wi-Fi source |
| insta360ctl [BLE report](https://github.com/xaionaro-go/insta360ctl/blob/f94193ce03c5af0921a9992bfd1af6bd946150d0/doc/ble_protocol.md) and [stream schema](https://github.com/xaionaro-go/insta360ctl/blob/f94193ce03c5af0921a9992bfd1af6bd946150d0/pkg/protocol/protobuf/livestream.proto) | A BLE command table names `SET_OPTIONS`; a separate Wi-Fi stream schema names `SyncLocalTimeToCamera` with a millisecond field | A command-table entry plus a Wi-Fi schema is not an evidenced X5 BLE request/response; it must not be combined into an invented packet |

These are documentary or community-source findings, not camera observations.
The seconds-based ONE RS implementation and separate millisecond Wi-Fi schema
must not be treated as interchangeable. No upstream code/schema is copied into
RideSync, and no new firmware dependency is introduced.

Insta360 also advertises [Mic Pro Bluetooth camera connection and timecode](https://www.insta360.com/blog/insta360-announces-new-mic-pro.html).
That is an accessory investigation lead, not documentation of a BLE/GATT clock
command available to RideSync. Bluetooth audio and the current BLE remote profile
must not be assumed to share a protocol.

## RideSync transport boundary

The implemented X5 adapter uses the camera-initiated **CE80 remote peripheral**
route, with CE81 input and CE82 notifications. Its port exposes a bounded shutter
request, not arbitrary clock-setting operations. The alternative **BE80
camera-hosted central** route and phone/SDK connections are separate roles and
framing; see [protocol research](insta360_protocol.md#separate-ble-roles).

Neither existing CE80 recording-display packets nor the ONE RS GPS forwarding
encoder provide a qualified X5 clock-sync command. BLE throughput is not the
identified blocker for an occasional small clock update; the unknowns are the
request/response route, precision, timing and firmware behavior. A BLE ACK would
still not establish frame alignment or simultaneous shutter exposure.

## Finite next probe

Obtain one annotated official-app or official-SDK synchronization session on
the installed X5, before recording:

1. Record firmware from the camera, app/SDK version, selected recording mode,
   frame rate and DF/NDF setting. Record the timecode setting before and after.
2. Retain GATT discovery, direction, UUIDs, exact request/response bytes and
   monotonic timestamps. Record whether Wi-Fi is active; use a BLE-only session
   to establish BLE-only support. Capture connection authorization separately.
3. Compare a connection without explicit refresh against a timecode refresh to
   distinguish generic handshake/clock traffic from the requested operation.
4. Determine epoch, timezone treatment, fractional units, matching response,
   timeout/error behavior and whether the setting updates system time, timecode
   or both. Do not change timecode during recording in this first probe.
5. Retain raw capture privately and publish redacted protocol facts plus one
   independently constructed expected fixture. Unknown fields remain unknown.

The existing CE80 probe cannot observe phone-to-camera GATT writes on a different
connection. A suitable phone Bluetooth capture or SDK trace is required for that
route; no capture equipment or new firmware upload was used here.

If a BLE route is established, design its integration before modifying the
adapter. Use fresh valid UTC, synchronize only while stopped, and bound each
operation with cancellation/timeouts. Missing UTC or failed synchronization
must leave recording and local logging usable. Never replay an old request on
reconnect. Resynchronization and latency compensation depend on measured
behavior, not a guessed clock precision.

Physical execution belongs with [#46](https://github.com/llazzaro/RideSync/issues/46)
and compatibility acceptance with [#22](https://github.com/llazzaro/RideSync/issues/22).
Before the run, declare an alignment target and independent reference. Perform
three synchronization attempts, inspect original and exported media timecode,
measure initial offset and drift over one 30-minute recording, then check one
reconnect and one sleep/power-cycle scenario. Report measured frames/time and
pass/fail/blocked outcomes. No numeric accuracy target is claimed as achieved.

This investigation completes the initial source review and selects a concrete
next probe. #50 remains open for the missing BLE evidence and conditional
implementation; no placeholder is delivered as working timecode support.

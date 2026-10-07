# Insta360 BLE research

Research baseline: 2026-10-07. **No RideSync hardware observations exist.**
External reports are community reverse engineering, not confirmation for our
X5/GO 3S/ONE RS firmware versions. [Sources and licensing](sources.md).

## Separate BLE roles

Community [ESP32 example](https://github.com/pchwalek/insta360_ble_esp32)
implements an emulated GPS remote peripheral: CE80 service, CE81 camera writes,
CE82 remote notifications, and CE83 remote information. Cameras initiate the
connection. UUIDs use the Bluetooth base UUID; discover peer attributes rather
than hard-code ATT handles. Pairing, subscription and reconnect requirements
must be captured independently for each target model.

[insta360ctl](https://github.com/xaionaro-go/insta360ctl) also describes direct
camera control using camera-hosted BE80, BE81 writes and BE82 notifications.
Its framing is separate from CE80. We have not validated its compatibility claims.

## Commands and state

The ESP32 community example reports a shutter button frame:
`FC EF FE 86 00 03 01 02 00`. Treat it as a **toggle/button event**, not an
idempotent recording-start command. Mode selection and current recording state
matter. Do not transmit it repeatedly after a lost response: that can stop a
successful recording. Explicit start/stop and notification decoding need captures
before implementation. Unknown incoming messages should be logged with bounded
hex dumps; malformed lengths must be rejected by a future codec.

## Wake and identification

The [GPS specification](https://github.com/TheAngryRaven/insta360-ble-gps-spec)
reports iBeacon-like manufacturer advertisements containing a camera serial
identifier, followed by a camera connection to the remote. It reports X4
observations, not our target models. Wake identifier derivation, byte order,
advertisement timing, shutdown window and pairing prerequisites remain untested.
Do not assume a BLE MAC is interchangeable with a wake serial identifier.

Multi-camera wake is a hypothesis to test by rotating identifiers with deadlines
while preserving existing connections. Advertising support under multiple links
must be measured on the selected ESP32 stack. No wake implementation exists.

## Model differences and evidence labels

X5, GO 3S and ONE RS require independent profiles. GO 3S remote-service and wake
compatibility are unresolved; absence of evidence does not mean unsupported.
Use: Observed (captured locally), Community (reported externally), Hypothesis
(proposed), Confirmed by testing (repeatable result with model/firmware recorded).
At present this document contains Community and Hypothesis evidence only.

## Implementation prerequisites from pinned sources

The MIT ESP32 `Insta_BLE.ino` declares X3/RS 1-inch targets and creates CE80
(write CE81, notify CE82, read CE83), plus a secondary service. This does not
establish ONE RS generally, X5 or GO 3S support. Its `notify()` calls broadcast;
its edited Arduino BLE files are not a required dependency for the selected
NimBLE probe. Build the discovered service shape from captures rather than
assuming the secondary service can be omitted.

The MIT M5Stick original sends the same shutter event. The pinned fork adds
connection-ID routing via `esp_ble_gatts_send_indicate(..., false)` and an X5
name branch, but its recording detector accepts a sufficiently long packet
containing a colon, then assumes stopped after five seconds without a timer.
That is a heuristic, not an explicit start/stop codec or authoritative state.
Missing timer traffic must become unknown in RideSync. See pinned file links in
[sources](sources.md#ble-decision-evidence).

| Path | ESP32 role / direction | Current evidence | Gate before production |
|---|---|---|---|
| CE80 remote | Peripheral; camera writes CE81, ESP32 notifies CE82 | MIT community examples, shutter event | Per-model pairing/subscription capture, state semantics, addressed delivery |
| BE80 direct | Central; ESP32 writes BE81, camera notifies BE82 | Unlicensed research reference only | Independently captured GATT, authorization, framing, explicit REC/STOP and state |

For X5 first, record model/firmware, address type, GATT UUID/properties, MTU,
security/bonding, CCCD writes, CE83 reads and handshake order with timestamps.
Capture manual start, stop, mode change, disconnect during recording, repeated
connection and rejected requests; correlate packets with camera display and
saved media. Preserve raw evidence and annotations separately. Do not promote a
name match, accepted BLE write or timer heuristic into confirmed recording.
Repeat qualification for ONE RS and GO 3S; neither inherits X5 results. Wake
advertisements need their own serial/timing tests. No such captures exist yet.

Use the pinned NimBLE transport/routing and queue ownership in
[ADR-001](architecture.md#adr-001-ble-qualification-stack-and-roles-2026-10-07).
No code or packet implementation from the unlicensed direct-control/GPS sources
may be copied; their reports only identify questions for independent captures.

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

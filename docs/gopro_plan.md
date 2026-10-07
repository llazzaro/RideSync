# GoPro support plan

Status: **planned, not implemented or tested**. Added 2026-10-07.
The first target is **GoPro HERO12 Black**; record its installed firmware before
implementation. Additional GoPro models require separate qualification. Use the official
[Open GoPro compatibility table](https://gopro.github.io/OpenGoPro/) to qualify
each target rather than assuming every GoPro supports this API.

## Scope and approach

- Discover, pair/bond and reconnect to one qualified GoPro over BLE.
- Establish video mode; use explicit shutter on/off and verify encoding status.
- Handle responses, busy/not-ready state, notifications, timeouts and keep-alive
  per camera. A successful write or command response alone is not proof of video.
- Integrate with the same group recording intent and partial-success indication
  as Insta360; one missing GoPro must not block available cameras.
- Test sleep/reconnect/wake separately by model and power state. Do not assume
  full shutdown behaves like BLE-connectable sleep.
- Keep GPS and future IMU microSD logs independent. External telemetry injection
  into GoPro is outside the initial GoPro milestone; no API support is assumed.

The official [BLE setup](https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup/)
and [control API](https://gopro.github.io/OpenGoPro/docs/ble/control/) define the
starting point. Pairing prerequisites and available features must be verified
against the selected camera firmware. Use BLE for the initial control scope;
media transfer and preview are outside this milestone.

## Planned support matrix

| Feature | GoPro HERO12 Black |
|---|---|
| Pair/connect/reconnect | Not tested; planned |
| Video start/stop | Not tested; planned |
| Recording-state reporting | Not tested; planned |
| Sleep/wake | Not tested; model-dependent investigation |
| Mixed Insta360/GoPro operation | Not tested; planned |
| External GPS/IMU injection | Outside initial scope; capability unverified |

## Delivery order and acceptance

Single-camera GoPro issue: identify model/firmware and its documented API version; capture
pairing and responses; implement a separately testable adapter. Bench-test
repeated start/stop, busy state, camera power cycling, ESP32 reset during
recording, bond persistence and sleep recovery. Include golden packets and
malformed/fragmented notification cases in native codec tests.

Mixed-camera integration issue: run one GoPro together with the three target Insta360 cameras where
available. Measure ESP32 mixed BLE role support and connection capacity. Record
request and observed-state timestamps; group control does not promise frame
synchronization. With one camera absent, all other ready cameras must receive
REC/STOP and the user must see a partial result. Verify that local SD logging
continues through disconnects and retry activity.

Only promote per-model features after target-hardware results are recorded.

HERO12 Black is listed in the official compatibility table. This establishes an
API investigation path, not confirmation of RideSync hardware support.
The camera has no internal GPS receiver according to GoPro's
[GPMF field documentation](https://github.com/gopro/gpmf-parser/blob/main/docs/README.md).
RideSync will record GPS externally for later alignment with footage; this does
not imply GPS can be injected into HERO12 video metadata.

## Official baseline and adapter prerequisites (2026-10-07)

The official [compatibility table](https://gopro.github.io/OpenGoPro/docs/)
lists **HERO12 Black minimum firmware v01.10.00** and notes model-specific
features. Installed firmware is unknown; this is a documentation qualification,
not camera verification. GoPro documents one BLE client per camera, so remove
competing phone/remote connections during qualification.

[BLE setup](https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup/)
requires RideSync as central: pair once, discover services and re-subscribe on
every reconnect (subscription caching is unsupported). Discover FEA6; command/
response are GP-0072/0073, settings/response GP-0074/0075, query/response
GP-0076/0077, where GP expands to `b5f9XXXX-aa8d-11e3-9046-0002a5d5c51b`.
Poll Get Hardware Info with a deadline until BLE readiness succeeds. Retain
bond identity, characteristic ownership and fragmented response buffers per
camera, never globally.

[Control](https://gopro.github.io/OpenGoPro/docs/ble/control/) defines Set
Shutter 0/1 and recommends Keep Alive every three seconds. Establish video
mode before requesting shutter on. The adapter must distinguish transport
acceptance, command response and observed encoding. Use status 10 Encoding,
8 Busy and 82 Ready from [Statuses](https://gopro.github.io/OpenGoPro/docs/ble/statuses/),
with query/notification support checked on the actual firmware. Preserve unknown
state across disconnects; do not infer stop from absent notifications. Capture
packet fragmentation and supported commands before choosing reassembly limits.

Use NimBLE-Arduino 2.3.6 with one client for HERO12 as specified in
[ADR-001](architecture.md#adr-001-ble-qualification-stack-and-roles-2026-10-07).
Mixed CE80/GoPro role and four-link capacity are unmeasured. The official
API/firmware minimum does not certify this stack or RideSync hardware.

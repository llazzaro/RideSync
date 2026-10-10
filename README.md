# RideSync

An ESP32 motorcycle camera remote and ride logger for **Insta360 X5, GO 3S,
ONE RS, and GoPro HERO12 Black**, targeting the **LILYGO TTGO T-A7670E R2
with A7670E built-in GPS**. One handlebar button is intended to wake/reconnect
supported cameras and start recording, then stop recording on the next press.
RideSync will also save timestamped GPS, acceleration, and estimated lean/pitch
data to microSD. Motion measurements require an additional IMU; camera wake and
telemetry forwarding depend on each model's verified capabilities.

## Current status

**Software modules are implemented; hardware qualification is incomplete.**
Native tests cover camera/group state machines, button and LED behavior, session
time, A7670E GNSS parsing/transport, bounded GPS/raw-IMU storage, configuration
persistence, health supervision, the shared BLE central, the HERO12 adapter and
correlated camera-event logging, opt-in static MotionV4 telemetry and the pure
source-backed BE80 GPS encoder and pure recording-request codecs.
The retained ESP32 builds link the real BLE host and HERO12 composition. These
checks establish software behavior and build compatibility; they do not prove
camera or sensor operation on the motorcycle.

The default firmware provides serial diagnostics and health supervision, and
loads configuration when SDK admission permits. A bounded startup barrier fixes
worker policies before supervision and admits the commissioned optional runtime
only after the dedicated watchdog subscription succeeds. Camera control, GNSS,
SD, IMU and external controls remain disabled by default until wiring, identities,
firmware/API and required store evidence are qualified. The handlebar and local
telemetry workflows are composed and tested synthetically; physical bench
validation remains open. Qualified static force/rate and externally referenced
roll/pitch now have [runtime logging](docs/motion_logging.md#implemented-static-motion-logging-v4).
Dynamic linear acceleration/lean and Insta360 control/wake/GPS forwarding still need
their required protocol or reference evidence. Opt-in HERO12 recovery now has a
bounded discovery/connect/query/conditional-REC software path, with physical
sleep/wake behavior still untested. The retained camera/group logging route now
records admitted requests, validated wire ACKs and accepted state observations
as distinct facts in [CameraV3 logs](docs/log_format.md). Owner-receipt and storage
admission times are separate; radio receipt and camera acquisition remain unknown.
Configuration handoff, durable session IDs, local/control composition and
supervised startup have software implementations tracked in [#38](https://github.com/llazzaro/RideSync/issues/38)–[#42](https://github.com/llazzaro/RideSync/issues/42).

A factory-firmware bench probe confirmed board/modem startup and GNSS enable,
but acquired no position fix. Isolated RideSync diagnostics subsequently observed
[X5 CE80 pairing/subscription](docs/hardware-results/2026-10-09-x5-pairing.md)
and [one owner-confirmed wake](docs/hardware-results/2026-10-09-x5-wake.md#owner-requested-repeat-observed-wake).
After installing a camera card, the owner reported firmware **1.11.10** and
confirmed a playable clip after manual stop. A later probe session completed
[three camera-observed remote Start/Stop cycles and reconnect](docs/hardware-results/2026-10-09-x5-pairing.md#three-remote-recording-cycles--owner-confirmed).
The production X5 adapter and authoritative recording-state decoder remain
unimplemented. Remaining camera checks are consolidated in
[#46](https://github.com/llazzaro/RideSync/issues/46). See
[bring-up results](docs/hardware-results/2026-10-07-bringup.md).
This is not yet ride-ready firmware. The matrix below reports **hardware support**;
passing synthetic tests does not promote a camera to Confirmed.

| Feature | X5 | GO 3S | ONE RS | HERO12 Black |
|---|---|---|---|---|
| BLE connect | Observed (isolated CE80 probe) | Not tested | Not tested | Not tested |
| Start recording | Observed once (isolated probe, 1.11.10) | Not tested | Not tested | Not tested |
| Stop recording | Not tested | Not tested | Not tested | Not tested |
| Wake | Observed once (isolated probe) | Not tested | Not tested | Not tested |
| GPS telemetry | Not tested | Not tested | Not tested | Outside scope |

Observed probe results do not establish production support or firmware-qualified
compatibility. X5 recording/state decoding, the three-cycle smoke test and
production wake/reconnect scheduling remain open in #19, #3 and #9.

GoPro HERO12 Black is the first implemented GoPro profile. Its opt-in adapter
uses [Open GoPro](https://gopro.github.io/OpenGoPro/) for initial pairing/control,
identity/API qualification and explicit start/stop with observed Encoding
confirmation. Its tests use schema-derived synthetic host packets; physical
pairing, recording and wake remain untested. External GPS/IMU injection into GoPro
is not assumed; local microSD ride logging remains independent. See [GoPro plan](docs/gopro_plan.md).

The [pure GPS encoder](docs/gps_protocol.md#implemented-pure-encoder-29) has a
source-backed experimental 71-byte binary path with independent byte/error tests.
It does not transmit or enable GPS on any camera; profile forwarding and actual
stored metadata qualification remain open in #14/#22.

These statuses refer to RideSync, not claims made by upstream projects.
See [protocol research](docs/insta360_protocol.md) and
[limitations](docs/known_limitations.md).

## Build, flash, and monitor

Use Python 3.11 for the same host environment as CI. PlatformIO Core and the
ESP32 platform, Arduino framework, toolchain and build tools are pinned.
Firmware dependencies include commit-pinned NimBLE-Arduino and SparkFun BMI270
with its Bosch driver; see [sources and notices](docs/sources.md). A checked build
patch guards the pinned NimBLE empty-store restore case. Host tests use the pinned
PlatformIO native platform 1.2.1 and Unity 2.6.1 (the system C++ compiler is
provided by macOS developer tools or Ubuntu 24.04 in CI).

```sh
git clone https://github.com/llazzaro/RideSync.git
cd RideSync
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-dev.txt
pio run -e lilygo_t_a7670e_r2
pio device list
pio run -e lilygo_t_a7670e_r2 -t upload --upload-port /dev/your-port
pio device monitor --port /dev/your-port --baud 115200
```

Confirm the exact board variant before flashing; see [hardware](docs/hardware.md).
The default firmware does not enable the modem or drive external control pins.

## Configuration and controls

[examples/cameras.example.json](examples/cameras.example.json) records the planned
configuration shape for four cameras, including HERO12 Black. It is **not loaded
by firmware**. All example targets are disabled until their addresses and address
types are known. Source settings use the typed `SourceConfig` API in
[include/config.h](include/config.h); no JSON parser is implemented. Replace null
identifiers with measured BLE addresses, set public/random address type, then
enable only qualified profiles through their documented commissioning API.
[NVS persistence](docs/configuration.md) publishes owned validated settings to
the application startup barrier. A separately commissioned provider supplies
qualified resources and frozen opaque peer mappings; saved settings alone grant
no hardware admission. See [supervised startup](docs/supervision.md).
The configurable registry capacity is 1–8 cameras; the shared central currently
admits at most four peer slots and explicitly refuses a fifth. Neither bound is
a measured BLE connection limit. See
[camera contracts](docs/camera_contracts.md) for lifecycle and adapter rules.

The tested button state machine produces configurable short, long and optional
double-press events. The intended application mapping is group recording,
wake/reconnect and state resynchronization respectively. The tested status
renderer provides green ready, red recording, blinking amber partial availability,
blue recovery and fast red error. Physical GPIO qualification and complete
button-to-camera wiring remain open.

## Implementation plan

The [implementation plan](docs/superpowers/plans/2026-10-07-ridesync.md)
contains 39 focused work packages plus maintenance fixes, dependencies and
acceptance criteria, tracked in [roadmap #16](https://github.com/llazzaro/RideSync/issues/16). See the
[issue review](docs/issue_review.md) for scope splits:

1. Verify board/protocol evidence and build the shared camera abstraction.
2. Prove single-camera X5 and HERO12 Black recording control independently.
3. Add ONE RS, GO 3S and mixed-brand group control with partial-success status.
4. Implement debounced controls and verify per-model wake behavior.
5. Acquire A7670E GPS and log it independently to microSD.
6. Add an IMU, raw motion logging and validated acceleration/lean estimates.
7. Verify optional Insta360 GPS forwarding, then complete recovery and field tests.

The roadmap includes implemented software and outstanding protocol, integration
and hardware work. Each issue records its remaining acceptance; promote physical
support only with hardware evidence.

## Development

```sh
python -m unittest discover -s test -v
pio test -e native
python scripts/check_format.py
```

GitHub Actions builds firmware and runs repository checks, native source
configuration/lifecycle tests with fake clocks and transports, and C++ formatting.
Native suites exercise software contracts, schema-derived protocol packets and
SDK fault boundaries. CI also links the opt-in BLE central and HERO12 runtime
with activation disabled. None of those checks validates physical camera behavior.

See [CONTRIBUTING.md](CONTRIBUTING.md), [research sources](docs/sources.md),
[GPS protocol](docs/gps_protocol.md), and the [original brief](docs/project_brief.md).
RideSync-authored files are MIT licensed. Pinned dependencies and retained source
fixtures have component-specific licenses/notices; see
[sources](docs/sources.md) and [NimBLE fixture notices](test/fixtures/nimble/NOTICE-Apache-NimBLE).
Synthetic protocol fixtures are not physical camera captures.

The [single-X5 serial milestone](docs/x5_serial_milestone.md) provides a privately
commissioned production CE80 route with explicit CONNECT/REC/STOP/QUERY/STATUS/
DISCONNECT commands. Default activation remains off; observations, command
outcomes and transport acceptance are reported separately.

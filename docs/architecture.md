# Architecture direction

This document preserves the requested direction; firmware interfaces remain to
be designed before milestone 1. PlatformIO/Arduino is the initial build baseline,
following LILYGO's ESP32 setup. Reassess NimBLE versus ESP-IDF when connection
capacity and per-peer notification delivery have been measured.

| Module | Intended responsibility |
|---|---|
| ble_remote | BLE role, service discovery, pairing, transport and notifications |
| camera_profile | Camera family/model capabilities, protocol choice and verified codecs |
| camera_manager | Configured camera registry and per-camera lifecycle |
| recording_manager | Group intent and per-camera acknowledgement tracking |
| wake_manager | Bounded, camera-specific advertising schedule |
| modem_gnss | Modem power/UART setup, timed AT transactions and GNSS parsing |
| gps_manager | Normalized fix validity, age and distribution |
| button_manager | Debounced short/long/double press events |
| status_led | Aggregate status independent of GPIO/RGB backend |
| config | Source/file settings, later versioned NVS persistence |
| logging | Useful serial diagnostics |
| storage | Independent, bounded CSV/GPX writes and media error handling |

Each camera needs friendly name, model, configured BLE identifier and address
type, wake identifier, enabled/GPS flags, connection state, observed recording
state, last-seen time, capabilities and retry state. Avoid global state that
allows one camera failure to abort others. Do not confuse requested recording
with acknowledged recording; unavailable or ambiguous state remains unknown.

Use explicit deadlines and bounded retries per camera, rollover-safe clocks,
event-driven transport callbacks and a serialized command queue per peer.
The registry must allow at least three cameras; measure the BLE stack limit and
fail configuration clearly if capacity is exceeded. A transport write succeeding
is not proof a camera is recording.

The CE80 remote is a BLE peripheral with cameras acting as centrals; the BE80
approach makes ESP32 a central. These require different transport lifecycles.
Select milestone 1's approach after a live X5 service/handshake capture. A shutter
toggle must not be used as reliable start/stop without observed state.

GNSS pipeline: modem AT transport → parser → normalized fix → independent SD
logger and optional profile encoder → BLE transport. Never couple local GPS
logging progress to camera connectivity. Watchdog coverage, queue bounds and
reset recovery are release acceptance requirements, not current features.

## Planned GoPro camera family

Keep the group manager independent of vendor protocol. A camera profile selects
an Insta360 or GoPro adapter behind common connect, request-start, request-stop,
query-state and optional wake operations. Include camera family and exact model
in future configuration; reject incompatible profiles explicitly. The existing
three-Insta360 example remains illustrative; add HERO12 Black configuration
when the shared camera configuration contract is implemented.

The GoPro adapter will use the official Open GoPro BLE API where supported:
pairing/bonding, service/notification setup, command responses, recording status,
keep-alive scheduling and bounded recovery. Video mode must be established before
shutter-on is treated as a recording request. Feature support depends on model
and firmware; no blanket GoPro compatibility is claimed.

Mixed operation may require concurrent ESP32 central links for GoPro and
peripheral links for Insta360 CE80, or central-only operation if Insta360 BE80 is
validated. Measure this coexistence before promising mixed-camera capacity.
Do not reuse Insta360 wake advertisements or GPS encoding for GoPro. See the
[GoPro acceptance plan](gopro_plan.md).

## Ride logging and motion sensing

Local GPS/motion logging is a project goal independent of camera telemetry.
Add an external accelerometer/gyroscope IMU, with a sensor driver separate from
orientation estimation. Store raw acceleration and angular rate as well as
estimated linear acceleration, roll/lean and pitch, with validity and quality.
Never label raw accelerometer readings as gravity-free acceleration or infer
motorcycle lean directly from acceleration during a turn.

Use a shared monotonic timebase with UTC anchor records from valid GNSS time;
GNSS loss must not halt IMU logging. Log camera command/acknowledgement events
with the same timebase for post-production alignment, without promising frame
synchronization. Preserve sensor units, calibration, mounting orientation,
session identifiers and firmware version. Buffer SD writes with bounded memory;
report dropped records and media faults without delaying BLE control.

The first GoPro profile targets HERO12 Black. The issue-backed
[implementation plan](superpowers/plans/2026-10-07-ridesync.md) supersedes the
original brief's milestone ordering while preserving single-X5-first delivery.

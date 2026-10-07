# Architecture direction

This document preserves the requested direction; firmware interfaces remain to
be designed before milestone 1. PlatformIO/Arduino is the initial build baseline,
following LILYGO's ESP32 setup. Reassess NimBLE versus ESP-IDF when connection
capacity and per-peer notification delivery have been measured.

| Module | Intended responsibility |
|---|---|
| ble_remote | BLE role, service discovery, pairing, transport and notifications |
| camera_profile | Model capabilities, protocol choice and verified codecs |
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

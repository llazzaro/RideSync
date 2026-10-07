# RideSync

An ESP32 motorcycle remote for multiple Insta360 cameras, targeting the
**LILYGO TTGO T-A7670E R2 with A7670E built-in GPS**. The intended workflow is one handlebar button to
wake/reconnect and start recording, then a second press to stop. Optional
A7670E GNSS telemetry and microSD logging are later milestones.

## Current status

**Repository foundation only.** Firmware currently prints a serial bring-up
message and yields to the scheduler. BLE control, wake, buttons, LEDs, GNSS,
logging, watchdog configuration, and persistent settings are not implemented.
No board or camera has been tested. This is not yet ride-ready firmware.

| Feature | X5 | GO 3S | ONE RS |
|---|---|---|---|
| BLE connect | Not tested | Not tested | Not tested |
| Start recording | Not tested | Not tested | Not tested |
| Stop recording | Not tested | Not tested | Not tested |
| Wake | Not tested | Not tested | Not tested |
| GPS telemetry | Not tested | Not tested | Not tested |

These statuses refer to RideSync, not claims made by upstream projects.
See [protocol research](docs/insta360_protocol.md) and
[limitations](docs/known_limitations.md).

## Build, flash, and monitor

Use Python 3.11 for the same host environment as CI. PlatformIO Core and the
ESP32 platform, Arduino framework, toolchain and build tools are pinned.
No third-party firmware libraries are used yet.

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
The scaffold does not enable the modem or drive any external pins.

## Configuration and controls

[examples/cameras.example.json](examples/cameras.example.json) records the planned
configuration shape for three cameras. It is **not loaded by firmware**. Replace
null identifiers only after determining the correct address/serial mapping.
NVS persistence comes later. The design must support more than two cameras;
actual BLE connection capacity must be measured with the chosen stack.

Planned configurable controls: short press toggles the group recording intent;
long press wakes/reconnects all configured cameras; optional double press
resynchronizes state. Planned status: green ready, red recording, blinking amber
partial availability, blue reconnecting, fast red error. None is wired yet.

## Milestones

1. One X5: BLE integration, explicit start/stop semantics, serial status.
2. X5 and ONE RS together, independent failures and retry timeouts.
3. Add GO 3S using its own capability profile; verify three-camera operation.
4. Camera-specific wake identifiers and multiple wake advertisements.
5. A7670E GNSS acquisition and independent CSV logging to microSD.
6. Verified Insta360 GPS telemetry encoding and transport.
7. Waterproof button, LED backend, watchdog/recovery validation, enclosure.

Keep each milestone in reviewable commits. Details and acceptance tests are in
[architecture](docs/architecture.md) and [testing](docs/testing.md).

## Development

```sh
python -m unittest discover -s test -v
python scripts/check_format.py
```

GitHub Actions builds firmware and runs repository/configuration checks and C++
formatting. Protocol and state-machine unit suites will be added with their
implementations; current checks do not validate camera behavior.

See [CONTRIBUTING.md](CONTRIBUTING.md), [research sources](docs/sources.md),
[GPS protocol](docs/gps_protocol.md), and the [original brief](docs/project_brief.md).
RideSync-authored files are MIT licensed. Upstream sources are references only;
no third-party implementation has been copied.

# RideSync

An ESP32 motorcycle camera remote and ride logger for **Insta360 X5, GO 3S,
ONE RS, and GoPro HERO12 Black**, targeting the **LILYGO TTGO T-A7670E R2
with A7670E built-in GPS**. One handlebar button is intended to wake/reconnect
supported cameras and start recording, then stop recording on the next press.
RideSync will also save timestamped GPS, acceleration, and estimated lean/pitch
data to microSD. Motion measurements require an additional IMU; camera wake and
telemetry forwarding depend on each model's verified capabilities.

## Current status

**Repository foundation only.** Firmware currently prints a serial bring-up
message and yields to the scheduler. BLE control, wake, buttons, LEDs, GNSS,
logging, watchdog configuration, and persistent settings are not implemented.
A factory-firmware bench probe confirmed board/modem startup and GNSS enable,
but acquired no position fix. No RideSync firmware or camera behavior has been
verified on hardware. See [bring-up results](docs/hardware-results/2026-10-07-bringup.md).
This is not yet ride-ready firmware.

| Feature | X5 | GO 3S | ONE RS | HERO12 Black |
|---|---|---|---|---|
| BLE connect | Not tested | Not tested | Not tested | Not tested |
| Start recording | Not tested | Not tested | Not tested | Not tested |
| Stop recording | Not tested | Not tested | Not tested | Not tested |
| Wake | Not tested | Not tested | Not tested | Not tested |
| GPS telemetry | Not tested | Not tested | Not tested | Outside scope |

GoPro HERO12 Black is the planned first GoPro target.
BLE pairing/reconnect, start/stop and state reporting are planned using
[Open GoPro](https://gopro.github.io/OpenGoPro/). Wake is model-dependent and
untested. External GPS/IMU injection into GoPro is not assumed; local microSD
ride logging remains independent. See [GoPro plan](docs/gopro_plan.md).

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

## Implementation plan

The [implementation plan](docs/superpowers/plans/2026-10-07-ridesync.md)
contains 30 focused work packages, dependencies and acceptance criteria, tracked
in [roadmap #16](https://github.com/llazzaro/RideSync/issues/16). See the
[issue review](docs/issue_review.md) for scope splits:

1. Verify board/protocol evidence and build the shared camera abstraction.
2. Prove single-camera X5 and HERO12 Black recording control independently.
3. Add ONE RS, GO 3S and mixed-brand group control with partial-success status.
4. Implement debounced controls and verify per-model wake behavior.
5. Acquire A7670E GPS and log it independently to microSD.
6. Add an IMU, raw motion logging and validated acceleration/lean estimates.
7. Verify optional Insta360 GPS forwarding, then complete recovery and field tests.

These are planned capabilities, not current implementation results. Keep each
issue in reviewable commits and promote support only with hardware evidence.

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

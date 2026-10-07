Create a production-quality GitHub repository for an ESP32-based motorcycle remote that controls multiple Insta360 cameras over Bluetooth Low Energy and optionally forwards GPS telemetry.

## Hardware

Target board:

LILYGO TTGO T-A7670E R2
ESP32 + A7670E modem
Built-in GPS/GNSS
microSD available
BLE available from ESP32

The device will be mounted on a motorcycle and should eventually use an external waterproof physical button on the handlebar.

## Main goal

Build firmware that can emulate the relevant behavior of the Insta360 GPS Action Remote.

The target workflow is:

One physical button press
→ wake multiple Insta360 cameras if possible
→ connect/reconnect to them over BLE
→ start recording on all cameras

Second button press
→ stop recording on all cameras

Initial target cameras:

1. Insta360 X5
2. Insta360 GO 3S
3. Insta360 ONE RS

The architecture must not artificially limit the project to two cameras. Design it for at least 3 cameras and preferably an extensible number of cameras.

## Important existing research

Do not reverse engineer everything from zero.

Research and reuse knowledge from existing open-source work around:

- Insta360 GPS Action Remote BLE protocol
- Insta360 BLE GPS protocol
- insta360ctl
- insta360-m5stick-remote
- ESP32 Insta360 BLE remote implementations
- projects documenting Insta360 wake-up advertisements / iBeacon behavior
- BLE services around UUIDs such as CE80 / CE81 / CE82 and BE80 / BE81 where relevant

Search GitHub and public documentation for the current state of these projects.

Do not blindly copy code if licensing is incompatible. Document every external source and license used.

## Core firmware requirements

Implement the project in a modular way.

Suggested modules:

- ble_remote
- camera_manager
- camera_profile
- wake_manager
- recording_manager
- gps_manager
- modem_gnss
- button_manager
- status_led
- config
- logging
- storage

## Camera abstraction

Create a camera abstraction so each connected camera can have:

- name
- model
- BLE MAC/address or discovered identifier
- camera-specific wake identifier
- connection state
- recording state
- last seen timestamp
- supported capabilities
- retry state

Do not hardcode all logic directly around X5.

Support per-camera profiles because GO 3S may behave differently from X5 or ONE RS.

## BLE behavior

Investigate and implement:

- camera discovery
- bonding/pairing if required
- reconnect
- BLE service discovery
- remote shutter commands
- recording start
- recording stop
- camera state notifications if available
- multicamera operation
- connection retry
- timeout handling

A single button press should try to command every configured camera.

Do not abort the entire operation if one camera fails.

Example desired behavior:

X5 connected
GO 3S connected
ONE RS unavailable

Press REC

Expected:

X5 starts recording
GO 3S starts recording
ONE RS enters retry/error state

The user should receive a clear status indication that only 2 of 3 cameras are recording.

## Wake-up support

Research the wake-up protocol used by the Insta360 GPS Action Remote.

There is community research suggesting Insta360 cameras can be awakened using BLE advertising / iBeacon-like advertisements containing camera-specific identifiers.

Implement wake as a separate subsystem.

Support issuing wake requests for multiple cameras sequentially or concurrently where technically possible.

Do not assume the official remote's limitation of waking only one camera applies to a custom ESP32 implementation.

Test whether multiple wake advertisements can be emitted for:

- X5
- ONE RS
- GO 3S

Document limitations separately for each model.

## Physical controls

Initial hardware interface:

One external momentary waterproof button.

Suggested behavior:

Short press:
toggle recording on all available cameras

Long press:
wake + reconnect all configured cameras

Optional double press:
force state resynchronization

Make these actions configurable.

Implement proper button debounce.

## Status indication

Initially support:

- built-in LED if available
- optional external RGB LED

Suggested states:

Green:
all configured cameras connected and ready

Red:
all connected cameras recording

Blinking yellow/orange:
one or more cameras unavailable

Blue:
wake/reconnect in progress

Fast red blink:
error

Do not tightly couple UI state to a specific LED implementation.

## GPS / GNSS

The board uses A7670E with built-in GNSS.

Implement GPS acquisition through the modem using the appropriate AT commands.

Parse useful GNSS data:

- latitude
- longitude
- altitude if available
- speed
- heading
- UTC time
- fix quality / satellites

Research the reverse-engineered Insta360 GPS Action Remote telemetry protocol.

Implement a GPS output layer capable of sending location telemetry to compatible Insta360 cameras in the format expected from the official GPS remote.

Keep GNSS acquisition separate from Insta360 GPS BLE encoding.

Suggested architecture:

A7670E
→ GNSS parser
→ internal normalized GPS struct
→ Insta360 GPS encoder
→ BLE transport

## GPS logging

Also log GPS data locally to microSD.

Prefer a simple format such as:

CSV
or
GPX

Include timestamps.

Logging should continue independently of whether cameras are connected.

## Reliability

This device will be used while riding a motorcycle.

Prioritize reliability over complexity.

Requirements:

- watchdog
- non-blocking BLE operations
- robust reconnect
- no infinite waits
- state machine architecture
- explicit timeouts
- useful serial logs
- recover from one camera disappearing
- survive camera power cycling
- survive BLE disconnections
- clean startup after ESP32 reset

Avoid long blocking delays.

## Configuration

Initially support configuration through a file or source config.

Eventually support persistent configuration using NVS.

Camera config should include:

- friendly name
- model
- BLE identifier
- wake identifier if needed
- enabled/disabled
- GPS telemetry enabled/disabled

Example:

cameras:
  - name: Front X5
    model: X5
    enabled: true

  - name: Helmet GO3S
    model: GO3S
    enabled: true

  - name: Rear ONE RS
    model: ONE_RS
    enabled: true

## Development environment

Prefer PlatformIO unless there is a strong technical reason to use ESP-IDF directly.

If ESP-IDF is clearly better for simultaneous BLE connections, use ESP-IDF and explain why.

Repository should build reproducibly.

Include:

- platformio.ini or ESP-IDF configuration
- dependency versions
- build instructions
- flash instructions
- serial monitor instructions

## Repository structure

Create a clean structure similar to:

/
  README.md
  LICENSE
  CONTRIBUTING.md
  docs/
    architecture.md
    insta360_protocol.md
    gps_protocol.md
    hardware.md
    testing.md
    known_limitations.md
  src/
  include/
  test/
  scripts/
  examples/
  .github/
    workflows/

## Documentation

README must clearly explain:

- project goal
- supported hardware
- supported cameras
- current implementation status
- how to build
- how to flash
- how to configure cameras
- how button behavior works
- known limitations

Do not claim features are working unless they have actually been implemented or tested.

Use a support matrix like:

| Feature | X5 | GO 3S | ONE RS |
|---|---|---|---|
| BLE connect | status | status | status |
| Start recording | status | status | status |
| Stop recording | status | status | status |
| Wake | status | status | status |
| GPS telemetry | status | status | status |

Use labels such as:

Confirmed
Experimental
Not tested
Unsupported

## Protocol documentation

Create docs/insta360_protocol.md.

Document:

- relevant BLE services
- characteristics
- packet formats
- button events
- recording commands
- notifications
- wake advertisements
- camera identification
- known model differences

Clearly distinguish:

Observed behavior
Community reverse engineering
Hypothesis
Confirmed by testing

Never present assumptions as confirmed protocol behavior.

## Testing

Create unit-testable components where possible.

Especially test:

- button state machine
- camera state machine
- GPS parsing
- packet encoding
- retry logic
- protocol serialization

If hardware tests cannot be automated, document reproducible manual tests.

Example manual test:

1. Power X5 off.
2. Trigger wake.
3. Confirm X5 powers on.
4. Wait for BLE connection.
5. Trigger REC.
6. Verify camera recording state.
7. Trigger STOP.
8. Verify recording stopped.

Repeat separately for every camera model.

## GitHub Actions

Add CI that at minimum:

- builds firmware
- runs unit tests
- performs formatting/lint checks

## Initial milestone

Milestone 1 should be intentionally small:

LILYGO T-A7670E
→ ESP32 BLE
→ connect to one Insta360 X5
→ start/stop recording
→ serial logging

Milestone 2:

X5 + ONE RS simultaneously

Milestone 3:

X5 + ONE RS + GO 3S

Milestone 4:

multi-camera wake

Milestone 5:

A7670E GNSS acquisition

Milestone 6:

Insta360 GPS telemetry injection

Milestone 7:

external motorcycle button + LED + enclosure

Do not attempt to implement every milestone in one giant unstructured commit.

## Git history

Use meaningful commits.

Examples:

Initial PlatformIO project

Add Insta360 BLE transport

Add X5 camera profile

Add multicamera manager

Add wake advertisement support

Add A7670E GNSS parser

Add Insta360 GPS telemetry encoder

## Deliverable

Prepare the repository so another developer can clone it and immediately understand:

1. what is already known
2. what currently works
3. what remains experimental
4. how to build and flash it
5. how to add support for additional Insta360 cameras

Before implementing speculative BLE behavior, inspect the existing open-source projects and protocol research.

Most importantly: do not fake successful implementation. If something has not been verified against real hardware, mark it clearly as experimental or untested.
# Architecture direction

PlatformIO/Arduino remains the build baseline. ADR-001 below selects the BLE
stack for qualification; production adapters and hardware acceptance remain open.

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
ADR-001 selects CE80 for the first X5 qualification; keep it provisional until
a live X5 service/handshake capture. A shutter
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

## ADR-001: BLE qualification stack and roles (2026-10-07)

**Decision: provisional, source-backed; hardware qualification unresolved.**
Use `h2zero/NimBLE-Arduino@2.3.6`, upstream commit
`dfb4ac561a06797081be9e752902a6582e7f029e`, with the existing
`espressif32@6.12.0` / Arduino ESP32 2.0.17 package pins. Add the dependency only
when the probe/adapter work starts; this documentation decision does not modify
`platformio.ini`. Use one NimBLE host, with both central and peripheral roles.
CE80 peripheral emulation is the first X5 experiment; HERO12 uses a central
client. ONE RS and GO 3S remain separately qualified CE80 candidates. BE80 direct
control is a later, capture-gated alternative for each Insta360 model.

This choice avoids porting the ESP32 example's edited Bluedroid framework files.
It provides explicit peer routing and fits the current Arduino baseline. ESP-IDF
is a fallback if a reproducible NimBLE-Arduino defect or controller constraint
prevents mixed roles; changing frameworks alone is not proof of more capacity.
[Stack/source evidence](sources.md#ble-decision-evidence).

### APIs and source limits

At the pinned revision, `nimconfig.h` enables central and peripheral by default.
`NimBLEDevice::createServer()` and `createClient()` are available together;
that establishes API availability, not successful simultaneous radio operation.
`NimBLECharacteristic::notify(data, length, connHandle)` reaches
`ble_gattc_notify_custom(connHandle, ...)`. The default handle is
`BLE_HS_CONN_HANDLE_NONE`, which selects all peers: always pass the intended
peer handle for camera commands. A true return means the stack accepted the
operation, not that the camera acted. Track each peer's CCCD subscription via
`onSubscribe(..., NimBLEConnInfo&, subValue)` and reject unsubscribed sends.

Peripheral `onWrite` and `onConnect` receive `NimBLEConnInfo`; obtain
`getConnHandle()` there. Central `subscribe` callbacks receive the remote
characteristic; `getClient()->getConnHandle()` identifies its connection.
Maintain configured identity plus a connection generation, because handles can
be reused after disconnect. Revalidate before sending any queued command.

The library defaults to **3 total simultaneous connections**, insufficient for
three Insta360 cameras plus HERO12. The next probe should explicitly set
`-DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=4`. The upstream config comment gives an
ESP controller ceiling of 9, not a measured RideSync budget. Client object slots
and host/controller configuration use the connection macro; roles do not have
independent four-link budgets. No usable mixed-role connection count is yet
measured. No spare fifth connection is reserved for pairing/diagnostics.

### Concurrency and bounded memory decision

BLE host processing runs in a FreeRTOS task (`NimBLEDevice::host_task` calls
`nimble_port_run`). Treat callbacks as asynchronous to the application worker:
copy payload and peer/generation into a bounded event queue, return promptly,
and never perform SD/modem work, sleeps, discovery or blocking commands there.
One transport worker owns client/server lifecycle, subscriptions, advertising,
command submission and connection maps. One outstanding transaction per peer;
protocol replies update observed state separately from requested group intent.

Initial probe bounds: four peer slots, 32 event slots with at most 256 copied
bytes each (8 KiB payload storage plus metadata), four queued commands per peer,
and a maximum 1 KiB reassembly buffer per peer. These are proposed application
budgets, not measured camera packet maxima; oversize input is rejected and
counted, and validated captures must determine final codec limits. Queue
overflow invalidates affected observed state and increments counters. Keep BLE
allocations in usable internal RAM; board PSRAM flags do not prove spare BLE
memory. Measure free/minimum internal heap, largest free block, task stack high
water and dropped events at each added connection with SD/GNSS load. Do not use
upstream RAM-saving marketing as a RideSync memory result.

### Required bounded probe and fallback

Run a ten-minute test on the qualified board, with three subscribed CE80 camera
centrals and one HERO12 peripheral: uniquely tag notifications for each CE80
peer, prove only the selected peer receives each, exercise central responses,
advertise/reconnect while links remain active, and collect connection parameters,
heap/queue counters and failures. Repeat with a peer missing and after reset.
The first X5 run must retain GATT, handshake, firmware and command/state captures;
record installed HERO12 firmware against its official minimum v01.10.00.

A local compile-only probe on 2026-10-07 passed with the existing package pins,
NimBLE 2.3.6, both role flags asserted and connection macro set to 4. It compiled
server/client creation, addressed notifications, peripheral identity/subscription
callbacks and central notification subscription/client routing. Linker size was
35,812 bytes static RAM and 580,549 bytes flash; these omit runtime allocations
and production SD/GNSS work and do not establish memory headroom. Probe source,
command/log and hashes are retained in the issue #17 investigation report.

No live camera availability has been confirmed. Therefore X5 captures, HERO12
installed-firmware verification, over-air routing, role coexistence and memory/
connection budget are unresolved hardware gaps. A compile probe can prove only
API/build compatibility. Issue #17 remains partial until these measurements.
If mixed roles fail, keep single-X5 CE80 qualification and separate single-GoPro
qualification; gate group mode. Prefer central-only operation only after BE80
explicit control/state is independently validated for every participating model.
Do not silently toggle unknown cameras or promise four-camera support. Full
adapter/group acceptance belongs to #22, not this decision.

## Pure group recording policy (#7)

`RecordingManager` coordinates an already configured `CameraManager`. Its
`request(Recording/Stopped)`, `shortPress()`, `resync()`, `cancel()`, `tick()` and
`event()` API owns camera command admission; configure the camera registry before
using it, cancel before replacing configuration, and route all camera events and
ticks through this coordinator. Keep calls and optional status callbacks on the
same execution context as CameraManager. Callbacks receive an ephemeral semantic
snapshot and must not reenter the managers; copy any needed data into bounded UI
storage. No GPIO, BLE, wake advertisement, LED or logging driver is included.

The first short press derives Stop only when all enabled cameras are confirmed
recording. All-stopped and mixed confirmed groups converge to Start. Unknown
startup triggers bounded connect/query resynchronization in that same press;
when each peer has either a confirmed query result or an explicit error, the
usable confirmed subset selects the same rule and proceeds. Unavailable peers
retain their errors rather than block the usable peers or silently imply success.
If no peer yields usable state, the request ends with `UnknownState` and unknown
group intent. A query acknowledgement without an observation is not usable state.
This subset policy deliberately stops an all-recording reachable subset even if
another enabled camera is unavailable; the summary preserves that partial result.
Wake scheduling before connection is a future adapter/wake-manager concern.

Once group intent exists, the next short press reverses that intent, including
while commands remain pending. A short press during resynchronization is rejected
with `ResyncPending`; an explicit request or cancellation can supersede it.
Cancellation resets intent to unknown and retires operation tokens. New explicit
requests cancel superseded peer work instead of appending it to a FIFO. The
coordinator also retires each previous response token at the request boundary,
even when an already-confirmed-state shortcut sends no new command; this
preserves connection observations while rejecting prior command responses. Adapters
must implement verified explicit-state Start/Stop and echo tokens captured when
the transport accepted the operation. No vendor shutter toggle is blindly replayed.
Connection-scoped subscriptions remain authoritative observations across command
changes; operation-scoped responses from retired requests are rejected by the
CameraManager connection/operation generations.

Group intent, acknowledgement and observation are separate fields. Summaries
include enabled, ready, confirmed recording/stopped, unknown, pending and error
counts plus each peer's admission/asynchronous error and acknowledgement. Ready
counts include verified connected peers operating a command, but exclude failed,
connecting and retry-backoff peers. Recording counts reflect only explicit known
observations, never write acceptance or command completion. A confirmed opposite
state remains visible while a command is pending. A peer succeeds only after its
command completes and a fresh known observation matches intent (query accepts
either known state). Already-confirmed desired peers need no redundant command.

Connection/command delivery deadlines and retries are delegated to CameraManager;
after acknowledgement, a missing confirmation has a fixed 1000 ms deadline.
Expiry takes precedence over confirmation success at or after the deadline,
even without a preceding tick. Late physical observations remain visible but
cannot clear the terminal request error or launch an expired resync decision.
Confirmation waits use rollover-safe unsigned milliseconds and require service
at least once per 2^31 ms. No retry or indefinite wait occurs in the group layer.
Storage is a fixed `kMaxCameras` peer array and snapshots; command admission adds
no group queue or heap allocation. A missing peer cannot abort another peer's
transport or confirmation. Local GPS logging remains independent. Native fake
transports validate policy, not camera support, physical wake or mixed radio
capacity; these remain hardware acceptance gaps.

## Bounded asynchronous BLE central transport (#35)

The opt-in central backend now owns one pinned raw NimBLE host, fixed copied
callback routing and whole-link retirement. It accepts profile-supplied multiple
services/characteristics, verifies properties plus actual CCCD write/read ATT
completion, and integrates NVS/verified bond admission plus no-eviction refusal.
Default bring-up does not start it. Future peripheral profiles share this host
and store; camera setup/Ready/ACK/recording policy is separate. See
[ble_transport.md](ble_transport.md) for concrete opt-in usage, lifetime, restore
proof workflow, bounds, SDK latency limits and remaining physical gates.

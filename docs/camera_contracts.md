# Camera contracts and source settings

The Arduino bring-up entrypoint remains serial-only. The new portable C++11
contracts contain no Arduino, NimBLE, ESP-IDF or vendor codec dependencies.
They prepare a registry and lifecycle for future adapters, not camera support.

## Configuration

`SourceConfig` is a typed source configuration API, validated before a manager
accepts it. `examples/cameras.example.json` illustrates the equivalent shape;
there is no runtime JSON loader. JSON null identifiers correspond to empty
strings in the typed API. Disabled placeholders may have no address and unknown
address type. Enabling requires a six-octet hexadecimal BLE address and explicit
public/random type. An optional wake identifier uses the same address syntax;
an adapter must still verify its model-specific meaning before use.

Supported model labels are X5, GO3S, ONE_RS (Insta360) and HERO12_BLACK (GoPro).
These labels identify planned profiles, not verified capabilities. Unknown
models and mismatched families are rejected even for disabled entries. Names
must contain 1–64 bytes. Nonempty identifiers must be valid even when disabled;
duplicate BLE addresses are rejected case-insensitively across all entries.
Every error returns a code, zero-based camera index and actionable message;
capacity errors refer to the source config as a whole (index zero).

Capacity is configurable from 1 to `kMaxCameras` (8), and includes disabled
entries. Counts above capacity are rejected before any array access. This is a
bounded software registry, not a claim of eight simultaneous hardware links.
Configuration replacement is atomic with respect to validation: rejected input
leaves peers and active requests intact. Accepted replacement closes old links
and resets state. There is no NVS, JSON parser, pairing store or group policy.

## Lifecycle and result rules

`CameraManager` owns per-peer state and a four-entry FIFO behind one active
operation per peer. Operations are connect, request start, request stop, query
state and optional wake. Requests return admission errors; `CameraState.error`
records asynchronous timeout, transport or cancellation results. A successful
`begin()` means delivery was accepted, and `Completed` means the adapter
completed that operation; neither changes observed recording. Only a separate
`RecordingObserved` event does. Desired recording tracks the latest admitted
start/stop request, independently of observed recording. A rejected request does
not change intent. Retrying start/stop requires an adapter that implements an
explicit state request, not a shutter toggle.

Capabilities begin unknown and are supplied by a verified adapter on connect
completion. Start, stop, query and wake are admitted only when explicitly
supported; unknown and unsupported capabilities both reject admission. GPS is
represented as a capability/opt-in flag without telemetry implementation.
Timestamp validity booleans distinguish a valid time of zero from no observation.
Failures, disconnect, cancel and reset make recording unknown. Reset also clears
intent, capabilities and timestamp validity; reconnect does not infer recording.

Each peer has its own deadline, attempts and constant backoff. Default policy is
1000 ms timeout, 200 ms backoff, three total attempts. Policy allows 1–5 total
attempts and timeout/backoff intervals of 1–2^31−1 ms. Unsigned modular deadline
comparison handles clock rollover; the caller must service `tick()` at least
once per 2^31 ms. Events arriving at/after an active deadline cannot complete
it, even if `tick()` has not yet run. A failed operation exhausts only its peer,
clears its pending queue and closes its connection. Other peers keep progressing.
Cancellation flushes that peer's FIFO and invalidates callbacks; cancellation
of a command keeps a ready link, while cancelling connection setup closes it.
Wake is a connected optional adapter operation here; advertising-based wake
scheduling remains the responsibility of a later wake manager.

## Adapter ownership and generations

`Clock` supplies monotonic 32-bit milliseconds. `CameraTransport` accepts a peer
index, typed identity, operation and token, and provides operation cancellation
and idempotent connection closure (also for partial handshakes). Transport
callbacks must be queued to the manager's execution context after transport
calls return; the manager is not thread-safe and does not permit inline callback
reentrancy. No event queue or BLE resource ownership is implemented here.

Every attempt gets a new operation generation; connecting also advances the
connection generation. Reset/reconfiguration advance both without recycling
slots' counters. Adapters must echo both generations on **all** events,
including observations, and capture the token when work/notifications originate
rather than relabel delayed events with the latest token. The manager rejects
previous operation/connection events before mutating state. Observation delivery
for an older operation may be discarded even on the current connection; adapters
should query fresh state when necessary. Generation counters are 32-bit; they
must not wrap while callbacks from an earlier matching generation remain live.

## Native verification

Install `requirements-dev.txt` (PlatformIO Core 6.1.18, clang-format 18.1.8), then
run `pio test -e native`. `platformio.ini` pins native 1.2.1 and Unity 2.6.1;
CI uses Ubuntu 24.04 and Python 3.11. The host compiler is supplied by the OS,
while the ESP32 toolchain remains independently pinned. Tests compile production
sources except Arduino `main.cpp`, with fake clocks/transports and no hardware.
Run Python example checks and formatting, then `pio run -e lilygo_t_a7670e_r2`
to verify the same contracts compile in the Arduino baseline.

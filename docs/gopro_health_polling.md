# HERO12 health polling software boundary

Issue #68, October 13, 2026. Health polling is opt-in and remains **Experimental /
Not tested** on hardware. The status widths/units come from the pinned official
Open GoPro schema in [the research note](camera_health_alerts.md); host fixtures
are Synthetic, not camera captures.

Commissioning must provide the exact HERO12 Black identity, firmware, returned
classic API profile and `Hero12Qualification.health_enabled`. The serialized
owner must attach a `CameraHealthSampleSink`; neither qualification nor attachment
alone starts health IO. Remaining video time additionally requires
`ordinary_video_confirmed`. This is an explicit ordinary-video premise for the
connection, not a conclusion drawn from Encoding. Leave it false when external
mode changes can invalidate that premise. Mode changes are not decoded by v1.
Battery percentage requires a fresh battery-present sample; absent batteries and
unqualified video time produce Unavailable, never invented zero values.

After ordinary pairing/claim/identity/control subscriptions finish, one bounded
resource registration requests IDs 1/6/35/54/60/70. Encoding's existing subscription
is reused. Registration can supply the qualified minimum polling floor. If absent,
one bounded bootstrap query requests the documented fields; a known invalid floor
or a failed bootstrap leaves monitoring unavailable. A floor in 1..30000 ms enables
Encoding period `max(5000,floor)` and resource period `max(15000,floor)`. A changed
valid floor reschedules the next normal periods without an immediate catch-up.
The evaluator owns independent expiry at `2P+1000` and `2R+1000`.

At most one optional wire transaction per peer runs, with an absolute 1000 ms
camera/ATT budget. Fair rotation admits at most one optional request across all
four peers per 500 ms. Due fields batch together; the minimum-period synthetic
schedule stays within four requests per peer per 15 seconds, excluding the single
initial registration. No catch-up or retry queue exists. Required control and
keep-alive have precedence; a newly admitted command may wait only for the already
admitted optional wire transaction's bounded completion/deadline. Foreground
queries reset spacing before the next optional query, and do not wait behind the
periodic-health floor. A rejected optional field/response or camera-response timeout
does not retire functioning control; an actual missing ATT completion still retires
its transport lease safely.

Every background Get includes floor 60; Encoding refresh includes IDs 10/60.
Foreground single-field status transactions reject full health-shaped responses,
so a delayed complete background reply cannot advance a shutter transaction.
With health enabled, a bare/non-success Get result cannot be correlated to the
foreground read: it counts a failure while that read awaits its qualified field
or its finite deadline. An actual foreground rejection therefore becomes Timeout
when no qualified result follows; neither success nor recording is inferred.
Health-disabled legacy rejection behavior is preserved.
The classic protocol has no request/notification sequence: the adapter assigns a
monotonic per-connection sequence to distinct reassembled wire messages, preserves
callback receipt time (oldest fragment receipt for fragmented messages), and drops
matching-response duplicates once completed. It cannot prove that a separately
received identical unsolicited update is a camera retransmission, or correlate a
malformed/partial reply that omits its requested field shape; these remain source
premises rather than fabricated protocol identifiers. ACK/Busy/Ready/KeepAlive
never renew Encoding receipt.

Owner event storage coalesces connection-scoped Recording observations when health
is enabled. `healthCoalesced()` reports incomplete edge history;
`healthFailures()` counts optional failures, both saturating. Resource snapshots
are delivered directly to fixed evaluator storage, without another resource inbox.
Reset, disconnect and matching owner detach retire polling; detach does not replay
registration into a replacement owner on the same link. Fresh commissioning/link
setup creates a new polling generation.

Measured host layout: `HealthPoll` 36 bytes x4; compact pending-recording metadata
adds 12 bytes x8 (pending slot 56 versus 44 bytes); sink pointer 8 bytes and scheduler
counters/cursor 16 bytes. Incremental adapter storage is 264 bytes on arm64, bounded
independently of notification count. The pinned Xtensa compiler reports adapter
size 11904 bytes before polling and 12160 bytes afterward, with the same central
receipt dependency: **256 bytes actual incremental target storage** (tail padding
absorbs the difference from the field sum). Target pending slots are 52 versus
40 bytes; poll state remains 36 bytes. The isolated HERO12 build uses 72648 bytes
RAM and 915341 bytes flash. Its static stack reports: health scheduler 96 bytes,
health field publication 80 bytes, resource response 64 bytes, classic response
352 bytes, notification reassembly 336 bytes and owner drain 128 bytes. Overall
<=4096-byte accounting remains with final owner integration; no adapter-local heap,
worker or resource snapshot allocation is added.

Focused verification uses an isolated native build directory:

```sh
PLATFORMIO_BUILD_DIR=/tmp/ridesync-health-native .venv/bin/pio test -e native -f test_hero12_adapter
PLATFORMIO_BUILD_DIR=/tmp/ridesync-health-native .venv/bin/pio test -e native -f test_gopro_codec -f test_camera_health -f test_mixed_camera
```

These Synthetic checks cover qualification/owner opt-in, floor boundaries/change,
registration, optional rejection/timeouts, mode/battery absence, control preemption,
late full reply isolation, callback receipt age, coalescing loss, fair four-peer
rotation, request ceiling/batching, reset and no replay. Pinned firmware builds,
final pipeline/CI and graph refresh are serialized by the integration owner.
Physical resource/display comparison stays with #46/#22/#31.

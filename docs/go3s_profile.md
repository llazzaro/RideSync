# GO 3S recording profile (#21)

Implemented October 10, 2026 from pinned Community wire descriptions. Software
is Experimental; physical GO 3S support remains Not tested. Verification on the
fitted camera/Action Pod is deferred to [#46](https://github.com/llazzaro/RideSync/issues/46).
This supersedes the earlier CE80-first capture proposal and implementation blocker.

## References and route

The independently authored codec/adapter use **camera-hosted BE80**, BE81 Write
and BE82 Notify, with the GO-series FFFrame wrapper, 16-byte inner message and
CRC16/MODBUS. This differs from both CE82 shutter events and the Garmin Header16
profile even where UUIDs or command numbers coincide.

- [nicecx camera_remote.py](https://github.com/nicecx/insta360-ai-content-studio/blob/abf472566198aeefb493b742f3fe69d9132ff460/lib/camera_remote.py)
  and its result report describe GO 3S firmware **8.0.4.11**, BE80 remote
  Start/Stop and subsequent footage retrieval.
- [OpenGraphLabs GO 3S protocol](https://github.com/OpenGraphLabs/syncfield-python/blob/88a74d84e09602d73c0e8a77f0fe583beb33cb61/src/syncfield/adapters/insta360_go3s/ble/protocol.py)
  claims validation on three GO 3S cameras at that firmware. Its camera wrapper
  supplies sync, CheckAuth, normal-video options, StartCapture and StopCapture.

These are author-reported Community observations, not local captures. The
reviewed repositories contain no annotated raw GO 3S state fixture. Their
shared insta360ctl lineage is not independent confirmation. Source licenses and
provenance limits are in [sources](sources.md#go-3s-be80-implementation-references-21).
No upstream implementation, protobuf generator or camera binary is copied.

Official [GPS Action Remote compatibility](https://store.insta360.com/gb/product/gps-action-remote)
and [direct camera pairing guidance](https://onlinemanual.insta360.com/go3s/en-us/troubleshooting/connect/wake-camera)
identify the vendor-accessory target as the camera. They do not specify BE80.
The Pod supplies its UI/control link; it is not used as a third-party proxy.
Camera and Pod firmware must be recorded separately in the hardware session.

## Software contract

`go3s::encode` supports CheckAuth (0x27), normal Video options (0x02), explicit
StartCapture (0x04, mode 1) and StopCapture (0x05). Seven zero sync bytes have a
separate FFFrame subtype. Sequence numbers 1..254 are owned by each connection;
the adapter never wraps, reuses an uncertain number or automatically replays a
command. Exhaustion retires that link and requires an explicit new connection.

The pure decoder checks camera-to-app direction, exact outer/inner lengths,
CRC, message/protobuf header and a complete inner message. The 256-byte receive
cap is a local resource bound, not a camera maximum. Fragmented ATT delivery and
multiple complete frames in one notification are assembled with fixed storage;
inner protocol continuation/fragment offsets are refused. Bad framing or an
unfinished receive frame after 1 s retires the link. Unknown payload/status
fields remain opaque; response status is matched by sequence, not command ID.

`Go3sAdapter` implements `CameraTransport` over the existing `BleCentral`, with
four fixed peer slots, copied bounded notifications and host final-access
barriers. Each independently commissioned slot supplies a verified stable bond
identity, the source-qualified firmware string `8.0.4.11` and an explicit
printable authorization ID of 1..32 bytes. The authorization ID is not derived
from a MAC or example identifier. Other firmware needs a newly evidenced profile.
The shared host retains existing NVS refusal, encryption/bond/identity checks.
Actual camera security compatibility remains Not tested.

After verified subscription the adapter waits for a valid camera SYNC. If none
arrives within 2 s, it sends the source-backed single zero-byte prompt once and
waits 1 s after ATT completion. Missing SYNC retires the link. Once received, it
sends one sync response, then CheckAuth; a missing or rejected authorization ACK never becomes Ready. It does not copy
the references' best-effort auth continuation, substring sync detector, parser
CRC omissions or automatic connect/command retries. ATT write completion and a
validated sequence-correlated status 200 are both required for each message.
The local single-write profile requires MTU >= packet size + 3 (maximum 62 for
32-byte auth; normal-video packet requires 32). Smaller MTUs fail explicitly;
no unqualified outbound fragmentation is enabled.

Start waits for normal-video options success, then sends StartCapture once;
Stop sends the explicit stop request once. Per-step response deadline is 5 s,
whole control intent 10 s and connection setup 60 s, including host delays.
Cancellation, disconnect, queue/driver faults, malformed or late ACKs seal the
link; another peer can still finish. Manager policy permits one attempt only.
The keepalive is a source-backed CheckAuth every 3 s while idle; an arriving
control intent can wait for that in-flight heartbeat within its original
bounded deadline. Keepalive also consumes sequence space (about 12 minutes of
otherwise idle connection); this is not unattended endurance qualification.

## Command success and observed state

Start/Stop are Supported commands. A status 200 completes the command but
**does not publish Recording or Stopped**. Reset, disconnect and all command
responses keep observed state Unknown. Passive or queried GO 3S recording-state
fields are not independently qualified; Query, Wake and GPS are Unsupported.
No GO 3-only state mapping, X5 timer/settings vocabulary, filename heuristic or
silence-to-Stopped rule is transferred.

The group manager therefore cannot confirm GO 3S recording from this profile;
it reports an unconfirmed result/finite failure rather than successful observed
recording. This is an implemented command path, not a claim of full status or
mixed-camera compatibility. A later evidenced GO 3S state decoder can extend
capabilities without changing the honesty contract.

## ESP32 integration and verification

`go3sRuntime()` returns boot-lifetime adapter/manager/group objects on the
existing `Esp32BleHost`. Construction and the default application perform no
GO 3S radio or store operations. The serialized owner commissions slots,
configures the manager, explicitly calls `adapter.start(true, true)` and
requests Connect/Start/Stop; `ridesync_go3s_service()` advances that same owner.
Stop then service until `canDestroy()` before releasing a non-static instance.
Do not run another group/manager owner tick concurrently. This component does
not automatically replace the X5 or HERO12 application runtime.

`pio run -e go3s_adapter_compile` retains the actual shared SDK backend, adapter
and owner entry points with activation disabled. Native codec and adapter tests
use independent literal wire vectors and synthetic host events, including
malformed/partial frames, auth rejection, missing GATT/CCCD, MTU, stale/late ACK,
control during keepalive, sequence exhaustion, independent peers and reset/no
replay. [Fixture labels](../test/fixtures/go3s/README.md) preserve their origin.
The [finite hardware procedure](testing.md#go-3s-source-backed-profile-verification-21)
in #46 owns real pairing, observed recording, playable footage and reconnect.
No firmware was uploaded or camera command issued during implementation.

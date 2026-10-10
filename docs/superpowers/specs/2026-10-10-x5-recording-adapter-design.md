# Single-X5 production recording adapter (#3)

Date: October 10, 2026, Europe/Amsterdam.
Status: written spec and native execution approved by the owner on October 10, 2026.

## Intended outcome and closure

Deliver the existing first milestone: one ESP32 and one X5, serial REC/STOP,
and honest observed recording status. The owner approved the shared-host X5
adapter architecture on October 10 and asked for concrete ticket closure.
Work stays on main, with verified commits pushed directly to origin/main.

#19 is complete. #3 remains open until the actual peripheral backend, portable
adapter and runnable serial composition exist, focused tests and pinned builds
pass, and the finite final composition check is recorded. Existing three-cycle
and reconnect observations satisfy the unchanged component smoke criterion;
they do not establish that the new application composition runs on hardware.

Keep every remaining camera operation in #46. Reuse the two receive transcripts,
playback observations, three remote cycles and reconnect. Do not repeat generic
captures, collect narrower manual timestamps, or launch an alternate BE80
campaign. Once the implementation is ready, the only additional #3 physical
check is the existing #46 final serial REC/STOP and observed-status row. A wire
change or actual failure justifies rerunning its affected case only.

Excluded: other models, mixed groups, automatic wake/recovery, GPS forwarding,
GNSS/SD/IMU commissioning, handlebar wiring, statistical reliability and the
integrated release campaign. These retain their existing issue owners.

## Evidence and choice

Select the observed CE80 peripheral route, not the unqualified BE80 central
route. The X5 initiates the connection to the remote's GATT server. The retained
X5 firmware 1.11.10 observations include connection, CE82 subscription, MTU 251,
three owner-confirmed Start/Stop cycles and reconnect. The shutter primitive is
the existing nine-byte `encodeShutterEvent()` result; it is a mode-dependent
toggle, not separate wire Start and Stop commands.

The pure `X5CapturedDisplayV1` decoder validates complete CE81 writes and their
typed display bodies. All 305 retained writes replayed against it; exact elapsed
text yields Recording, recognized video/photo settings yield Stopped and mode,
and remaining time/count or other traffic yields Unknown. Its timer result has
mode Unknown and no ACK or sequence identity. Preserve those distinctions.

Protocol/source evidence is in `docs/insta360_protocol.md`, `docs/sources.md`,
`docs/hardware-results/2026-10-09-x5-pairing.md` and
`docs/hardware-results/2026-10-10-x5-receive-capture.md`. Preserve their source
pins, licensing and observation limits. No external unlicensed implementation,
private transcript, camera address, serial number or footage is added to Git.

The alternative of turning the isolated probe into the application would retain
a second SDK/store owner and bypass production lifecycle contracts. Instead,
add the peripheral route to the existing singleton `Esp32BleHost`, and put
protocol policy in a portable X5 adapter. Do not restructure the HERO12/local
telemetry application merely to complete the independent single-X5 milestone.

## Components and ownership

1. A portable `X5Adapter` implements `CameraTransport`, backed by a narrow
   peripheral port. It owns one camera slot, connection generation, passive
   display observations, finite operations and sticky command-consumption
   state. Configuration must explicitly select X5 and the captured display
   profile. Construction performs no device I/O.
2. A raw SDK peripheral backend uses the same boot-lifetime `Esp32BleHost` and
   existing host task/store guards as central and wake operations. It supplies
   service registration, finite advertising, addressed notification and copied
   inbound events. It never calls `NimBLEDevice::init`, creates another NimBLE
   host, changes the bond namespace, erases NVS or evicts another peer.
3. A boot-lifetime X5 runtime composes this adapter with its own `CameraManager`
   and `Clock`. Its manager policy has **max_attempts = 1**, including Connect.
   The manager's outer timeout is 15000 ms; the adapter enforces the shorter
   5000 ms command/query deadline and reports failure through the owner before
   that outer timeout. Thus Connect can use its full budget without extending
   a recording command. Adapter service runs before manager deadline service.
   A timeout or transport failure cannot cause a second shutter notification.
   HERO12's existing runtime and retry policy remain unchanged.
4. A selectable single-X5 serial milestone composition wires setup/loop to that
   runtime and exposes real bounded commands. It does not require local telemetry
   or instantiate the HERO12 owner. The default uncommissioned image remains
   inactive, but the milestone image must have a real commissioning route and
   service loop; link-only retention does not satisfy #3.

Use `include/profiles/insta360_x5.h`, `src/profiles/insta360_x5.cpp`, dedicated
ESP32 profile/runtime files, focused peripheral interfaces, and corresponding
native tests. Extend `ble_esp32` only for host ownership/routing and registration
hooks. Add a pinned `x5_adapter_compile` environment and a runnable
`x5_serial_milestone` environment. Wiring belongs in the existing startup/main
selection seam, with mutually exclusive runtime selection.

The camera owner serializes manager request/event/tick and adapter policy.
Callbacks copy events into a fixed queue; they never invoke manager methods,
decode arbitrary SDK-owned pointers, print serial output or perform NVS I/O.
Potentially blocking SDK submissions run on retained owner storage, not the
serial/control loop. Timeouts revoke admission without deleting a live task,
service definition, callback context or in-flight SDK request.

## Host and radio admission

Register services after raw NimBLE initialization and before host execution;
the pinned stack registers queued definitions before sync. Registration failure
is terminal for that boot. Register exactly once, and do not add services to an
already-running central-only host. Such a late incompatible startup is refused
explicitly rather than silently reinitializing the stack.

Keep the existing store-proof and restoration admission. Commissioning supplies
the exact current proof through the established host interface; an empty store
is acceptable only when actually observed and qualified. Missing proof, store
mismatch, safe mode, host fault or revoked configuration prevents control.
The milestone's documentation must give the real commissioning steps; neither
a hard-coded proof nor an invented camera identity is an acceptable fallback.

Only one explicitly qualified X5 peer is admitted. Match the incoming SDK peer
identity against privately supplied verified public/random-static identity;
an unresolved private address cannot be guessed from protocol text or a name.
Keep identity values out of normal serial/status output and public fixtures.
If the actual camera uses a resolvable private identity, accept it only through
the existing verified SDK resolution/proof path. Failure to resolve stays a
concrete commissioning error; do not widen admission to any connecting device.

The observed CE80 route does not establish a new security handshake. Do not
automatically request encryption, invent authorization writes or create bonds
to make control work. Incoming security/store events remain subject to existing
guarded ownership. An unqualified peer's connection or store request cannot
authorize recording or overwrite another peer's records.

Advertise only after an explicit CONNECT command and all admission checks.
Connect deadline: 15000 ms from accepted CONNECT, covering host readiness,
advertising and CE82 subscription. One incoming connection maximum. No automatic
advertising restart on disconnect, advertising completion, timeout or reset.
An explicit later CONNECT is a new generation and carries no old intent.

Peripheral advertising and wake/reset/central GAP work must respect a shared
exclusive admission reservation. Busy does not become a permanent host fault,
and waiting never extends an original deadline. Maintenance cannot mutate the
selected peer while its peripheral context or callback barriers remain live.

## Service and wire contract

Preserve the observed prototype profile without claiming every service is
necessary. Offer CE80 with CE81 Write, CE82 Notify and CE83 Read `01 02`.
Keep the additional D0FF service and static read/property definitions identical
to the observed probe, as documented in its README. Preserve normal advertising
UUIDs and scan-response name `Insta360 GPS Remote`. Do not add wake manufacturer
data, GPS packets, guessed handshake responses or mode-switch commands.

CE82's CCCD must have notify enabled for the current connection before Connect
completes or any shutter is admitted. Recheck that connection, subscription,
generation and live deadline immediately before SDK submission. Disconnect,
subscription loss, cancellation, reset or host/store fault revokes queued work.
An SDK-owned notification already admitted may finish; it is never replayed.

Only complete CE81 writes from the admitted connection are passed to the decoder
as CameraToRemote. Copy at most 256 bytes. Reject oversize, truncated or malformed
payloads; no prefix decode or inferred fragmentation. Other characteristics and
opaque CE81 types cannot establish recording. No ACK/query bytes are invented.

## Observations and control

Connection, subscription and SDK send success prove their respective transport
steps only. They do not prove camera recording. Serial reports link state,
command outcome and current observed recording independently.

Track receipt order and time in the current connection generation. The maximum
observation age is 5000 ms, an explicit application policy rather than a camera
guarantee. At expiry, queue overflow, truncation, disconnect or a relevant
unrecognized settings/elapsed display, invalidate control state to Unknown.
Opaque non-display traffic and remaining time/count do not refresh or overwrite
a still-fresh recording observation. Silence never means Stopped.

Track video qualification separately from the decoder's Recording result. A
recognized video settings display establishes Video in that connection; a photo
settings display clears it. Unsupported settings, event loss, reconnect or an
expired stopped-state observation clears it. During a continuous recording
epoch established from Video, valid elapsed observations maintain recording
freshness, but do not independently discover a mode. A timer received without
that Video qualification may be reported as Recording and still refuses toggle
control. This is an observed, experimental control contract, not race-free
knowledge of camera mode or immunity to unseen camera-side changes.

REC requires a current subscribed peer and fresh Stopped + Video evidence.
STOP requires fresh Recording and the same connection's qualified video epoch.
Already-Recording REC or already-Stopped STOP completes without a notification
only when the corresponding fresh evidence and video qualification exist.
Photo, stale or unknown prerequisites refuse the command with an explicit reason;
they never fall back to a blind toggle. No queue of delayed REC/STOP commands is
retained: one operation at a time, and Busy requests are discarded.

QUERY is passive: it reports fresh cached evidence or waits up to 5000 ms for
a qualified display. No wire query has been established. Its documentation must
say passive observation, not authoritative camera query. A connect or a query
can succeed with an honest Unknown status where applicable; it cannot promote
control readiness without the required evidence.

Each accepted toggle request has a nonwrapping command generation. Consume its
one send opportunity immediately before entering the SDK notification call,
including an ambiguous error return. A command expires after 5000 ms. Preparing
an mbuf, submitting it, timing out, canceling or receiving late traffic never
causes another send for that request. Neither reconnect nor reset restores it.
New commands require a new explicit user request and revalidated observations.

Retire pre-submission observation evidence at the send boundary. A later display
can establish observed state, but CE80 has no request ID or camera ACK: report
"desired state observed after submission", not "camera acknowledged command".
The first eligible post-submission display that matches the desired state may
finish the operation. An opposite/unknown display does not trigger a retry.
Delayed/stale generations and pre-submission queued displays cannot complete it.
No claim of causal correlation or exact camera action time is made.

## Bounds and lifecycle

Use a fixed 32-event receive queue with owned 256-byte payload storage, one
outbound request and one admitted peripheral connection. Keep service/attribute
definitions and SDK callback contexts alive for the boot. Queue loss counters
and a sticky invalidation latch cannot themselves be lost with the queue.
No heap allocation occurs in application policy or inbound callbacks; an
explicitly bounded SDK mbuf allocation may fail and consumes no automatic retry.

Connection and operation generations never wrap into reusable values. Refuse
new work on exhaustion. Handles are valid only with their captured generation.
Event receipt times and original deadlines use wrap-safe monotonic comparisons
with durations below half the clock range. Late callback release requires actual
terminal events and host-queue barriers; a timeout alone never permits reuse.

Loss of subscription invalidates state and pending admission. After disconnect,
seal that connection, drain its actual callback barrier and report Unknown.
Shutdown/configuration revocation blocks new work immediately; cleanup completion
is reported separately. Never spin waiting for SDK cleanup in the control loop.

## Serial milestone

Accept complete LF/CRLF lines: CONNECT, REC, STOP, QUERY, STATUS and DISCONNECT.
Use a fixed 32-byte command buffer, discard an overlong line through its delimiter,
and reject partial/unknown lines. No character within a malformed line may be
interpreted as another command. Service a bounded number of serial bytes/events
per loop so a noisy UART cannot stop deadline progress.

Boot/configuration does not advertise or issue recording commands. CONNECT is
explicit; REC and STOP are separately explicit. DISCONNECT revokes pending work
and requests cleanup, without sending an implicit Stop. STATUS has no camera
side effect. A boot-local monotonic operation ID appears in results; peer addresses,
raw payloads and identifiers do not. Output includes observed Recording/Stopped/
Unknown, observation age, link/subscription state and finite failure reason.

The commissioned milestone image must execute these commands against the real
backend; documentation must provide build, private commissioning and upload
steps. Preserve installed firmware backups, NVS and SD data. Do not upload or
operate the camera until software verification is complete and the consolidated
final hardware row is ready.

## Verification and acceptance evidence

Native tests use a fake peripheral port with independent literal captured
display fixtures and independently recorded expected shutter bytes. Cover:

- Disabled/wrong model/profile, missing host proof, registration failure,
  unqualified peer, missing CE80/CE82 setup, subscription failure and Connect
  deadline; no Recording inferred from connection or subscription.
- Fresh video Stopped -> one REC, fresh qualified Recording -> one STOP,
  already-desired no-op, photo/unknown/stale refusal and passive QUERY expiry.
- Exact freshness boundary, elapsed evidence without Video qualification,
  remaining display, unsupported settings, malformed/oversize writes and
  queue loss invalidation; receipt order and delayed prior-generation traffic.
- Successful SDK submission with no state response, ambiguous SDK return,
  canceled/expired preparation, allocation failure and every disconnect/
  subscription-loss point; exactly zero or one shutter, never an automatic retry.
- CameraManager integration with max_attempts=1, actual command expiry, no
  pre-submission state completion, no queued-intent replay after reconnect/reset.
- Handle reuse, generation exhaustion, clock rollover, Busy radio reservation,
  barrier retention and late cleanup; bounded serial parsing and loop progress.

Run focused tests first, strict compiler/sanitizers where portable, formatting,
the repository pipeline and pinned default/retained-X5/serial milestone builds.
Check retained SDK symbols and memory/stack reports for the actual linked backend,
not merely an unused class. Obtain one fresh read-only code review before delivery,
resolve findings and retain passing CI. Reuse passing unchanged checks rather
than rebuilding source after prose-only updates.

Publish finite commands/results, test coverage and review disposition in testing
documentation. Reconcile #3's outdated "unclassified receive" paragraph with
#19's completion when implementation evidence is available. Reconcile #46's
state-correlation row using the already completed decoder evidence; retain the
unestablished authoritative-query limitation. Do not claim a disabled placeholder
or a passing codec alone as completion of this adapter.

After the ready final composition check, update #3's remaining criteria, link
implementation/CI and reused hardware evidence, and close #3 if all are satisfied.
If that final check fails, fix the actual adapter defect and repeat only the
affected check. #22/#31 support and release claims remain separate.

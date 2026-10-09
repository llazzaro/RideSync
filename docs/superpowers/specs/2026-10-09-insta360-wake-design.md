# Insta360 finite wake software route (#9)

Date: 2026-10-09. Status: proposed design; implementation not started.
The owner selected #9's software work and requested execution. This document
makes that route reviewable before product implementation.

## Intended result and issue boundary

Implement real source-backed wake advertising with finite multi-peer scheduling,
then hand each peer to its qualified connect/observe/start route. A missing
camera adapter must produce an explicit Unsupported result, not synthetic
success. Software work can proceed without operating the physical camera.

Keep #9's complete acceptance criteria intact. This stage supplies a concrete
advertising backend and recovery seam; it cannot close #9 while required actual
Insta360 adapters and their recording observations are absent. #3/#5/#21 retain
those adapters; #22 retains per-model wake and coexistence measurements. No new
issue, firmware upload, NVS commissioning, model confirmation or hardware trial
is part of this design. Commit verified changes directly to main.

## Approach and alternatives

Use the existing Esp32BleHost singleton, a fixed-storage wake SDK worker, and a
portable scheduler. Reuse the current source-backed diagnostic wire layout.
This supplies actual advertising rather than a scheduler with a fake driver.

Do not create another NimBLE instance: host/store ownership must remain shared.
Do not run raw advertising SDK calls from the control service: preparation or
cleanup may stall. Do not promote the bench diagnostic wholesale: its separate
startup, RAM store and one-attempt-per-boot policy differ from production.

## Wire codec and qualification

Add include/insta360_wake_encoder.h and src/insta360_wake_encoder.cpp. Output is
owned fixed storage: a 31-byte advertisement and 25-byte scan response. The
advertisement is flags AD (02 01 06), followed by manufacturer AD (1B FF), then
26 manufacturer bytes:

4C 00 02 15 09 4F 52 42 49 54 09 FF 0F 00 <six identifier bytes> 00 00 00 00 E4 01

The scan response is 03 03 80 CE, then 14 09 and the 19 ASCII bytes
`Insta360 GPS Remote`. These AD layouts are RideSync's explicit legacy layout;
manufacturer constants and identifier order come from the pinned MIT sources.
Retain source/license provenance and independent full literal fixtures.

A named source wire profile is separate from a camera model capability. Default
profile is Disabled. Select M5WakeV1 explicitly and supply exactly six printable
ASCII bytes. A helper may derive them only from the exact source-qualified
advertised-name form `X5 ` followed by six printable bytes; all other names are
Unsupported. Never derive from a MAC, pad, truncate or infer from a model enum.
Explicit identifier admission still requires owner-supplied provenance.
Unknown profiles and malformed/null/oversized inputs fail with zeroed output.
Do not print identifiers or add them to public status/log records.

## Portable scheduler

Add include/wake_manager.h and src/wake_manager.cpp. Use four fixed peer slots,
matching the shared transport capacity, and at most one active advertising
lease. Reject duplicate slot requests and configuration beyond capacity.
Configuration is copied at admission and immutable for that operation.

A serialized owner calls service(now); callbacks copy publications and never
invoke a camera/group manager. service performs a bounded pass over four slots,
without waits, allocations or an extra CameraManager/RecordingManager tick.
Existing camera commands keep their existing service route.

Each request identifies peer, generation and strictly increasing nonzero
operation ID. Reject exhausted IDs instead of wrapping. Configurable total
request timeout defaults to 15000 ms and advertising slice to 3000 ms. Both
must be nonzero and less than 2^31 ms; the slice cannot exceed the total timeout.
Use rollover-safe subtraction. Queue time counts against the original total
request deadline. No retry resets that deadline.

Phases: Queued, Advertising, Releasing, Recovering, Ready, Unsupported, Failed,
Cancelled, Timeout. Publish phase, typed error, operation identity and release
status; no recording state is inferred from advertising completion.

Round-robin selection gives each queued peer one bounded advertising slice.
One advertising attempt per request, with no automatic repeat after uncertain
SDK delivery. Missing identifiers or unsupported wire profiles finish that peer
without aborting others. Host Busy defers admission within the original deadline
and rotates eligibility; it is not a permanent transport fault.

Cancel, STOP supersession, owner revocation and deadline seal the operation
immediately. A delayed worker completion cannot move a sealed request into
recovery. Terminal intent and physical release are separate: a Cancelled or
Timeout slot remains non-reusable until the backend reports actual release.
If cleanup cannot finish, quarantine that lease; other peer status still
advances and expires honestly, while no second advertising lease is admitted.

## Recovery and single REC authority

Use a typed WakeRecovery interface: begin(peer, operation, generation), poll,
cancel and released. The provider is a qualified camera adapter, not a fake
production implementation. No provider means Unsupported after wake release.

Default scheduler mode prepares a peer; it never dispatches Start. Group
RecordingManager remains the sole REC authority through its existing
RecordingPreparation contract. Recovery Ready requires a fresh adapter
observation tied to the admitted peer/generation/operation, not a link alone.
A provider may report Stopped or Recording from independently qualified state
facts. Unknown, stale, conflicting or lost state cannot become Ready.

Expose an adapter of the scheduler to RecordingPreparation for an explicitly
bound owner. Do not replace the current HERO12 preparation or install two
preparation owners. Until an actual Insta360 composition is supplied, retain
an opt-in standalone software route and test this seam with qualified synthetic
providers. Existing HERO12 recovery remains unchanged.

For Start intent: fresh Stopped permits the group to dispatch its explicit
qualified Start; fresh Recording lets the existing group avoid duplicate Start.
An unsupported Start or a shutter-only adapter cannot be promoted to explicit
Start. STOP seals pending wake/recovery first. Every later callback remains
ineligible to issue REC. Reset/reconfigure also retires the old generation.

## Concrete ESP32 advertising owner

Add include/insta360_wake_esp32.h and src/insta360_wake_esp32.cpp. Construction
has no SDK calls; activation is opt-in and requires an already Ready,
source/store-qualified Esp32BleHost. Do not call NimBLEDevice::init, alter store
callbacks, erase NVS, change bonds, or independently start/deinit the controller.

Extend Esp32BleHost with a narrowly scoped advertising reservation. Admission
must serialize with startup revocation, bond reset, scan and connection setup.
Established connections and their ATT traffic are preserved. Advertising does
not disconnect existing peers or stop discovery owned by another lease. Busy
causes finite scheduler deferral. Shared reset admission must account for queued,
running and quarantined advertising ownership, not merely SDK call counters.

One boot-lifetime SDK worker owns copied payloads, sticky cancellation, counters
and a single lease. It prepares raw advertisement and scan response through the
pinned GAP APIs and uses the diagnostic's connectable/general-discovery mode
and bounded duration. Recheck operation identity, cancellation and actual time
immediately before every new SDK submission, especially after preparation.
The final admission check is serialized with sealing and defines when an SDK
submission is accepted; no lock is held across the SDK call. Cancellation cannot
retract an already admitted call, whose effect remains uncertain. A sealed
operation admits no additional start or recovery submission.
Stop/cancel is attempted once, without an unbounded retry loop. A successful
start/stop SDK return does not itself establish host callback quiescence.

Advertising callbacks use stable boot-lifetime storage. They retain terminal
and callback/barrier lifetime facts before allowing payload/lease reuse. Follow
the existing pinned host queue-barrier implementation; do not introduce a
portMAX_DELAY wait on the control owner. No borrowed stack buffers reach SDK.

An incoming connection to wake-only advertising is not an admitted recording
peer. Retain its actual handle, refuse security/bonding, and retire only that
new connection through the SDK worker. Never terminate a pre-existing connection.
Wake lease release requires its incoming connection's actual disconnect and
callback quiescence. There is no CE80 handshake or recording profile invented
here. The subsequent qualified recovery provider owns its actual control route.
If incoming identity/ownership cannot be safely established, seal and quarantine
rather than routing it to an arbitrary peer.

Private advertising identifiers remain only in fixed RAM. Public status contains
counts and errors only. No SDK callback logs addresses, keys or packet bytes.
SDK calls can stall indefinitely: software intent deadlines remain bounded,
while release may remain quarantined. Do not claim bounded SDK execution time.

## Verification

Independent codec fixtures cover all AD bytes and lengths, forward identifier
order, strict name derivation, disabled/unknown profile, short/long/null/control
inputs and all-zero errors. Compare fixtures to pinned source facts, not output
of the encoder.

Scheduler tests use a scripted finite driver and recovery provider: four peers,
one unavailable peer, missing IDs, Busy deferral, queue deadline, rollover,
operation/generation exhaustion, cancellation before/during preparation,
completion after STOP/reset, never reconnecting, unknown/stale observation,
existing Recording observation and exactly one REC authority. Prove no sealed
request dispatches recovery or recording, and no lease is reused before release.

Worker/admission tests exercise SDK preparation stall, delayed start return,
start failure, stop failure, incoming wake-only connection, callback after
terminal intent, unreleased callback barrier and reset/scan/connect contention.
Preserve existing bond reset and BLE retirement regression coverage.

Run focused tests, full native, relevant Python regressions, formatting, pinned
default firmware and a retained opt-in wake backend build. CI must link actual
advertising entry points; default firmware activation remains disabled. Fresh
code review precedes completion of this software stage. Physical timing,
coexistence, actual wake and recording claims remain owned by #22/#3.

## Completion evidence and remaining scope

Record implementation commits, test commands/results, source provenance and
passing CI. Update #9 with implemented behavior and remaining adapter dependency,
without automatically checking or closing unmet end-to-end acceptance criteria.
No new physical test campaign or repetitive wake trial is required here.

# Experimental BE80 GPS forwarding (#14)

`Be80GpsForwarder` implements an opt-in `GarminBe80VideoV1` path through the real
#29 encoder and `BleCentral` ATT writes. ONE RS is an Experimental source-profile
selection based on the pinned Garmin compatibility claim; hardware/metadata is
Not tested. Default configuration remains off. This does not implement ONE RS
recording/state (#5), X5 CE82, GO 3S or GoPro injection.

## Owner binding

Use one serialized application/command owner. The existing BLE owner supplies
qualified host startup, private identity/bond admission and the actual BE80
connection. The forwarder never scans, pairs, reconnects or invents authorization.

1. Construct retained `Be80GpsForwarder(central)` and configure each enabled
   Insta360/ONE_RS peer with `gps_telemetry=true`, an enabled/source-qualified
   `GpsForwardingConfig` selecting `GarminBe80VideoV1`, and a retained
   `Be80CommandSequence`. All producers on that peer's BE80 command stream must
   share that sequence; different peers must not share one sequence object.
   Source qualification is wire-evidence review, not camera acceptance.
2. Connect through `profileSpec()` (BE80 service, BE81 Write endpoint0).
   Camera-observed additional setup belongs to the connection owner. Route real
   central results into `forwarder.result()` before policy. Only a fresh
   TransportReady generation on the exact discovered endpoint admits forwarding;
   wrong routes, duplicate readiness and old callbacks are rejected. No camera
   ACK/subscription/state semantics are invented from BE82.
3. Bind `LocalTelemetryConfig::gps_forwarding_consumer = &forwarder`, or pass
   `&forwarder` as the last `GpsManager` constructor argument. Both default to
   null. These implemented GNSS producer paths copy observations only; invalid
   fixes/cancellation defer any BLE teardown to forwarding service. They do not
   call the BLE SDK or wait for it. Local GNSS/SD work continues independently.
4. Service BLE callbacks/deadlines and determine current control intent. Call
   `forwarder.service(current_session_timestamp, control_mask)` BEFORE control
   submissions. Set the peer bit for pending/busy control or refused current
   camera admission, then advance control normally. Do not cache the mask,
   replay refusals or tick camera/local managers twice.
5. On shutdown, cancel the producer and keep forwarding service running until
   `canRelease()`, then remove consumer/result bindings before destruction.
   The central's own callback/barrier lifetime remains independently required.

The default/HERO12/X5 startup routes do not automatically establish a ONE RS
BE80 connection. Commissioned composition selects the public forwarding APIs
and producer binding above. This is completed forwarding component software,
not a claimed installed-camera or new recording adapter.

## Bounds and priority

Each peer retains one latest pending copied fix; replacements coalesce, with no
history/retry queue. The real encoder checks freshness/session, required fields
and numeric limits at offer, admission and each fragment. Retained observations
age even when GNSS stops publishing. Invalid/no-fix/stale data discards work.
Configuration requires age 1..60000 ms, interval 100..60000 ms and whole-packet
deadline 1..5000 ms. Interval starts at packet admission; time/session changes
cannot release old data or bypass a deadline.

Source-style delivery is 71 bytes in 20/20/20/11-byte chunks. At most one ATT write
across all peers is admitted per service call; the rotating cursor lets other
peers progress when one stalls. MTU must permit each chunk (normally 23 or larger
for 20 bytes). No truncation or alternate long-write convention is invented.
The existing transport's bounded deferred admission and callback barriers apply.

The forwarder reserves that peer's write stream before the first fragment.
Other producers' reads/writes refuse immediately until all four ATT completions
release the lease. Control cannot append bytes between telemetry fragments, even
with wrong owner ordering. Current control intent drops pending telemetry; if a
fragment is already admitted, the partial stream is abandoned and that link is
sealed before control submission. The affected peer is explicitly unavailable
for that command and requires fresh reconnection; other peer commands continue.
This conservative failure policy preserves framing rather than silently queueing
or replaying recording intent. It does not promise every same-peer command can
succeed during an interrupted packet.

Error, deadline, invalid evidence, session change and terminal cancellation drop
remaining chunks/snapshots without retry. Only a new explicit connection
generation and fresh observation can resume. Consumed sequence 1..254 is never
rolled back. Producer callbacks retain the lease until deferred teardown is
serviced, so control remains protected during publication/cancellation.

`admitted_packets` is software admission; `att_completed_packets` means all
transport writes completed. Neither is a camera ACK, recording state or proof
of stored metadata. `dropped` counts discard events, not lost camera samples.
SDK/radio latency needs existing installed measurements; no universal SDK
return-time or zero-contention promise is made.

## Evidence, units and physical verification

Use the pinned MPL-2.0 Garmin source and [#29 field audit](gps_protocol.md#implemented-pure-encoder-29):
UTC whole seconds since 1970 (encoder calendar 2000..2099), magnitude/hemisphere
degree coordinates, speed m/s, course-over-ground degrees and nonnegative MSL
altitude metres. Binary32 narrowing, coordinate/range validation, missing-field
and negative-altitude refusals are unchanged. Course is not stationary heading
or body yaw; opaque bytes are not availability flags.

ONE R 360-mod source testing is not local ONE RS evidence. Exact ONE RS
Core/lens/firmware, authorization and footage interpretation remain Not tested.
X5's production CE80 route, GO 3S and HERO12 stay disabled for this profile.

Host tests deliver independent literal packets through the real encoder/central,
and cover coalescing/rate/freshness/MTU, control interleaving, deadline, errors,
callback generations, sequence wrap and stalled-peer isolation. Actual
ModemGnss/GpsManager and composed local-runtime tests verify copied publication,
no-SDK producer callbacks, cancellation and continued local logging. Target
linkage retains the real forwarding service without invoking it.

The existing GPS row in [#46](https://github.com/llazzaro/RideSync/issues/46)
collects one annotated footage/metadata sample on the declared ONE RS combination,
rate/MTU and camera-continuity observations. Record pass/fail/blocked/not-run;
keep raw coordinates/identities private. #22 interprets stored metadata before
Confirmed support. No extra soak or all-model campaign is required to complete
the software scope.

Pinned Xtensa compile-only sizing: forwarding owner 1840 bytes; central 9416
bytes after the four per-peer stream-lease pointers. These are object sizes, not
measured peak stack/heap or physical latency.

# Experimental ONE RS command profile (#5)

Reviewed October 10, 2026. The owner requested use of the published GitHub
command information without waiting for a model-specific recording-state capture.
The selected candidate is a default-off **BE80 explicit StartVideo/Stop profile**.
It reports recording **Unknown**. Software verification and ticket acceptance are
separate from physical compatibility, which remains **Not tested**. The existing
ONE RS row in [#46](https://github.com/llazzaro/RideSync/issues/46) owns the finite
commissioning/smoke check; #22 owns compatibility decisions.

## Target declaration

The candidate targets one **ONE RS Core plus the ordinary 360 Lens**, with
explicitly declared firmware and normal-video settings. This is a source-backed
trial boundary, not evidence that this assembly has been fitted or tested. No
local ONE RS Core/lens/firmware observation is recorded. Other combinations stay
outside this candidate: a ONE R Core, 4K Boost Lens, 1-Inch Wide Angle Lens and
1-Inch 360 Lens must not inherit it. Do not infer an installed lens from an
edition name.

The official [product introduction](https://onlinemanual.insta360.com/oners/en-us/camera/firstuse/introduction)
distinguishes the modular assemblies. The [module compatibility FAQ](https://onlinemanual.insta360.com/oners/en-us/faq/compatibility/modules)
also permits some lens/Core combinations across ONE R and ONE RS. Record Core,
lens, battery assembly, firmware, mode and selected resolution/frame rate
separately. The reference does not provide a firmware version to use as a
source-qualified ONE RS baseline; a declaration is not a compatibility result.

## Sources and selected route

| Route | Primary evidence inspected | Source facts and limits |
|---|---|---|
| Selected BE80 camera peripheral | [Garmin README](https://github.com/arsfabula/Insta360-Remote-CIQ/blob/39c51b3aa7c453227831d811355899371bbb8b94/README.md) and [BLEBarrel.mc](https://github.com/arsfabula/Insta360-Remote-CIQ/blob/39c51b3aa7c453227831d811355899371bbb8b94/BLE%20Barrel/BLEBarrel.mc), MPL-2.0 | Author reports ONE R 360-mod testing and proposes ONE RS compatibility. Camera hosts BE80; controller writes distinct StartVideo/Stop requests on BE81 and subscribes to BE82. Actual ONE RS GATT/security/control access and authoritative state/error semantics remain unqualified. |
| Unselected CE80 remote peripheral | [ESP32 README](https://github.com/pchwalek/insta360_ble_esp32/blob/83d4748b68d6ee5fd4414994a9e26b7d2f21364b/README.md) and [Insta_BLE.ino](https://github.com/pchwalek/insta360_ble_esp32/blob/83d4748b68d6ee5fd4414994a9e26b7d2f21364b/Insta_BLE.ino), MIT | Author reports RS 1-inch control with an ambiguous lens description; source emulates CE80 with CE81 Write, CE82 Notify, CE83 Read and a shutter toggle. It provides no ONE RS state decoder or exact firmware baseline. |

The Garmin author's ONE R test cannot qualify a ONE RS assembly. RideSync uses
its existing pure `GarminBe80ControlV1` request codec and independently authored
bounded adapter/control policy; no upstream BLE/controller/state algorithm is
copied. The codec's MPL-2.0 notices and pinned license remain intact; see
[sources](sources.md#one-rs-profile-evidence-audit-5).

The official [ONE RS GPS remote instructions](https://onlinemanual.insta360.com/oners/en-us/camera/connect/gpsremote)
document vendor accessory behavior and camera-side remote selection, not BE80
UUIDs, third-party authorization or recording-state bytes. They do not authorize
erasing RideSync bonds or resetting the camera for this trial.

## Delivery and state contract

The experimental `OneRsAdapter` uses shared `BleCentral` as the controller.
Activation requires explicit opt-in, the declared candidate target and a
commissioned verified identity/bond. Connection admission requires discovery of
BE80, BE81 Write and BE82 Notify/CCCD, successful subscription, security premises
and an actual ATT MTU of at least 21 for the complete 18-byte write. Missing
prerequisites fail within the bounded connection attempt; no guessed SYNC,
CheckAuth or authorization bytes are added.

StartVideo and Stop use the [existing independent request literals](insta360_protocol.md#implemented-pure-be80-recording-requests-19-partial):

| Request | Complete bytes; SS is the allocated sequence |
|---|---|
| StartVideo | `12 00 00 00 04 00 00 04 00 02 SS 00 00 80 00 00 08 01` |
| Stop | `12 00 00 00 04 00 00 05 00 02 SS 00 00 80 00 00 10 01` |

Each independently requested operation sends at most one complete request.
Sequence values 1..254 are not reused within a link; exhaustion requires a new
connection. Explicit request bytes do not establish camera-level idempotence.
Timeout, cancellation, transport error and disconnect retire the operation and
prevent replay after reconnect. The camera manager uses one attempt.

**Completed means successful ATT write delivery, not a camera ACK or a recording
observation.** A successful connection/subscription or SDK submission alone does
not complete a command write. No `wireAck` or recording observation is synthesized
from an ATT completion, notification, timer string, silence or the desired state.
Start/Stop are experimental command capabilities; observed recording remains
Unknown after delivery, malformed/opaque notifications, reset and reconnect.
Query and Wake are Unsupported. Group camera-observed confirmation remains
incomplete while this profile cannot provide recording state.

The group status field `acknowledged` reflects generic manager operation
completion, which for this adapter is ATT delivery. It is not a protocol ACK;
CameraV3 `WireAck` and recording-observation rows remain absent.

The Garmin source's seven-byte standby prewrite starts its callback-driven
queue. It is omitted: the source does not establish it as a required camera
handshake. Its first-fragment/byte-17 state heuristic and error-to-stopped policy
are not adopted. The existing pure BE80 envelope decoder validates only an
envelope shape; it cannot supply an ACK or state schema.

## Opt-in ESP32 ownership

`oneRsRuntime()` / `ridesync_one_rs_runtime()` retain a boot-lifetime adapter,
camera manager and group coordinator on `Esp32BleHost::instance()`. Construction
does no radio/store I/O. A serialized owner configures the manager with its ONE RS
`CameraConfig`, supplies matching `OneRsQualification` records, and explicitly
starts the adapter. It services `ridesync_one_rs_service()` and issues separate
manager Connect/Start/Stop requests; the default application never calls these
entry points or enables the radio route. Keep the owner and contexts alive
through terminal callbacks and the host quiescence barrier during stop.

Use direct explicit manager operations for the finite manual trial. The group
policy requires fresh observed state and cannot confirm or automatically choose
a recording action from this profile's Unknown state. GPS forwarding is a
separate component; this adapter advertises no GPS capability or shared
forwarding integration.

## Finite commissioning and smoke check within #46

This replaces the earlier CE80-first capture prerequisite in the existing ONE RS
row. No duplicate campaign or all-lens matrix is required.

1. Declare the target above and ESP32 firmware revision. Preserve installed flash,
   NVS, bonds and camera/card data using the existing bench procedure. Keep the
   row Blocked until the ordinary-360 ONE RS assembly and safe preparation are
   available. Commission its private identity/bond explicitly; a source report
   or camera name is not proof of identity or authorization.
2. Record actual camera-hosted BE80, BE81 Write, BE82 Notify/CCCD, address type,
   negotiated MTU, security outcome and subscription. A refusal/missing service
   is an unresolved trial result, not proof that every ONE RS is unsupported.
   Stop at the bounded deadline; do not guess a handshake or erase bonds.
3. Select stopped normal video on the camera. Request StartVideo once and verify
   running recording on its display; request explicit Stop once and verify it
   stopped. Repeat for three independently requested cycles. Record each ATT
   outcome separately from the actual camera outcome and confirm playable saved
   footage. Serial/group observed recording must remain Unknown even when the
   owner observes successful recording on the camera.
4. Reconnect once and verify the connection alone sends no prior Start/Stop.
   Confirm no replay after a lost/cancelled/expired request. Reuse observations
   for this exact target/firmware/settings; rerun only affected checks. Mixed
   groups, wake and GPS remain their existing owners' scope.
5. If complete BE82 traffic can be retained during the same session, annotate it
   against stopped/running/mode/error observations and keep raw identifiers and
   captures private. This is optional evidence for a later state decoder, not a
   prerequisite to implementing or trying the explicit command path. Review and
   anonymize independent fixtures before publishing; partial data and timer
   heuristics cannot establish state.

The existing bounded [CE80 capture probe](../tools/bench/x5_peripheral/README.md)
remains an alternative investigation if the selected BE80 route fails and the
outcome warrants another route decision. It is not a mandatory second test.
Never send CE82 shutter bytes on BE81, configure ONE RS as X5 or enable
`X5CapturedDisplayV1`: X5 observations do not qualify ONE RS.

## Software acceptance

Focused checks must cover independent full request bytes, target/disabled
selection, missing GATT/security/subscription, MTU bounds, transport failures,
malformed/ambiguous notifications, deadlines, late callbacks, cancellation,
sequence exhaustion and disconnect/reconnect without replay. Every case retains
Unknown recording. Use the pinned ESP32 compile, formatting and CI required by
the [acceptance policy](acceptance_policy.md); record final results in the
[testing guide](testing.md#one-rs-source-backed-profile-verification-5).

This record does not itself close #5 or claim a hardware pass. No camera command,
firmware upload or ONE RS hardware qualification was performed during this
software work.

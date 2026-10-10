# ONE RS recording profile evidence (#5)

Reviewed October 10, 2026. **Implementation blocked on model-specific state
evidence; hardware support Not tested.** This is a protocol decision record,
not an enabled adapter or a negative compatibility finding. #5 remains open.
The existing ONE RS row in [#46](https://github.com/llazzaro/RideSync/issues/46)
owns the capture and subsequent smoke check; #22 owns compatibility decisions.

## Target declaration

The first target is one owner-fitted **ONE RS Core plus its explicitly named
lens/module**, firmware and video settings. That combination has not yet been
declared in repository evidence. Do not substitute a ONE R Core, treat the
ordinary 360 Lens as the 1-Inch 360 Lens, or infer an installed lens from an
edition name. Other combinations stay Not tested; this is not an all-lens
campaign.

The official [product introduction](https://onlinemanual.insta360.com/oners/en-us/camera/firstuse/introduction)
distinguishes the modular assemblies. The [module compatibility FAQ](https://onlinemanual.insta360.com/oners/en-us/faq/compatibility/modules)
also permits some lens/Core combinations across ONE R and ONE RS. Thus lens
identity alone does not identify the control target. Record Core, lens, battery
assembly, firmware, mode and selected resolution/frame rate separately.

## Candidate routes and evidence limits

| Route | Primary evidence inspected | What it establishes | What is missing for #5 |
|---|---|---|---|
| CE80 remote peripheral | [ESP32 README](https://github.com/pchwalek/insta360_ble_esp32/blob/83d4748b68d6ee5fd4414994a9e26b7d2f21364b/README.md) and [Insta_BLE.ino](https://github.com/pchwalek/insta360_ble_esp32/blob/83d4748b68d6ee5fd4414994a9e26b7d2f21364b/Insta_BLE.ino), MIT | Author reports RS 1-inch control; source offers CE81 Write, CE82 Notify and CE83 Read on an emulated remote and a shutter event | Precise reported module/firmware and our fitted combination; complete camera writes with independent stopped/recording/mode annotations; connection/security premises |
| BE80 camera peripheral | [Garmin README](https://github.com/arsfabula/Insta360-Remote-CIQ/blob/39c51b3aa7c453227831d811355899371bbb8b94/README.md) and [BLEBarrel.mc](https://github.com/arsfabula/Insta360-Remote-CIQ/blob/39c51b3aa7c453227831d811355899371bbb8b94/BLE%20Barrel/BLEBarrel.mc), MPL-2.0 | Author reports ONE R 360-mod testing and proposes ONE RS compatibility; source supplies distinct StartVideo/Stop requests on BE81 with BE82 receive traffic | Actual ONE RS GATT/security/control access; complete response framing and authoritative state/error semantics |

These are pinned community source facts, not local ONE RS observations. The
CE80 source's abbreviated module description does not establish whether it is
the 1-Inch Wide Angle or 1-Inch 360 Lens. The BE80 author's ONE R test cannot
qualify any ONE RS module. Existing pure shutter and BE80 request codecs can be
reused only for the route independently established on the declared target.

The official [ONE RS GPS remote instructions](https://onlinemanual.insta360.com/oners/en-us/camera/connect/gpsremote)
describe camera-side Bluetooth remote selection and a mode-dependent shutter
button that starts/stops video. They document vendor accessory behavior, not
CE80/BE80 UUIDs or third-party state bytes. Their remote-reset/unbind steps are
not instructions to erase RideSync bonds or reset the camera for this trial.

## Route decision

Investigate **CE80 first**, reusing the existing bounded
[peripheral capture probe](../tools/bench/x5_peripheral/README.md). This choice
uses the source's RS report and our existing receive recorder; it does not
transfer the X5 qualification to ONE RS. The probe remains diagnostic and
reports Unknown. Do not configure ONE RS as X5 or enable `X5CapturedDisplayV1`
for it: that decoder is based on X5 writes and a specific settings vocabulary.

No complete ONE RS receive fixture, firmware observation or installed module
declaration currently exists. The CE80 reference has no camera-write state
decoder; the BE80 reference's response handling is already unresolved in
[the control audit](insta360_protocol.md#licensed-be80-control-reference-19-5).
Therefore neither reference supplies enough evidence for an honest recording
adapter yet. Pairing, subscription, successful SDK submission, silence and a
timer-like string alone cannot establish state.

## Finite capture within #46

Refine the existing ONE RS row rather than add another campaign:

1. Declare the target above and ESP32/probe revision. Preserve installed flash,
   NVS and camera/card data using the existing bench procedure. Mark the row
   Blocked until the camera/module is available and the preparation is complete.
2. Use the isolated CE80 probe's default capture-only mode, `H` for private
   complete write capture, then `A` for one bounded attempt. On the camera,
   enter its Bluetooth remote search UI. Record camera-side pairing outcome,
   role, address type, MTU, subscriptions and any security refusal. An absent
   connection is unresolved; do not guess handshake bytes or erase bonds.
3. Timestamp stopped video, one camera-button Start, running video, camera-button
   Stop, and one mode change against the camera display. Confirm a playable new
   clip. Retain complete CE81 writes, event/connection sequence and drop/truncation
   counters privately; incomplete capture data is not a golden fixture. Use `X`
   or the existing 120-second deadline to end the attempt. Do not send `S`, wake
   or automatic commands during this receive-evidence pass.
4. Review/anonymize only the required display/state facts. Compare the full
   frames to the annotations and X5 layout without assuming they match. Identify
   any fresh authoritative state and its mode/error limits; otherwise retain
   Unknown and state the missing semantics. A mode change must not be mistaken
   for a recording transition. Publish independent fixtures only after review.
5. Implement/test the evidenced profile in #5, then run its existing three
   independently requested REC/STOP cycles and one reconnect in #46. Reuse
   qualifying observations from this same declared combination; rerun only
   affected checks. No new mixed-group, wake or GPS checks belong to #5.

If CE80 cannot provide a usable route, record the exact outcome before choosing
BE80. BE80 is conditional, not a mandatory second campaign: establish actual
GATT/security/subscription prerequisites and complete annotated exchanges
before enabling that route. Never send CE82 shutter bytes on BE81.

## Implementation acceptance after evidence

Model-specific capability selection must use only the qualified route/settings.
Test independent expected frames, malformed/ambiguous state, missing services
or subscription, freshness, deadlines and disconnect without recording replay.
Reset/lost receive evidence returns Unknown. A disabled model entry or an empty
adapter does not satisfy #5. Source-backed request bytes do not themselves
provide observed state or physical compatibility.

This audit changes documentation only; existing encoder, transport and X5
software evidence is reused under [the acceptance policy](acceptance_policy.md).
No camera operation, firmware upload or hardware qualification was performed.

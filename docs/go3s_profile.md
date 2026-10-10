# GO 3S recording profile decision (#21)

Reviewed October 10, 2026. **Recording implementation remains blocked; RideSync
support Not tested.** The official accessory target is now documented as the
camera. An actual third-party BLE route and receive-state evidence remain
missing. #21 stays open; this is a prerequisite decision, not an adapter.

## Official target and evidence limits

Insta360's [GPS Action Remote product page](https://store.insta360.com/gb/product/gps-action-remote)
lists GO 3S compatibility. Its
[GO Series accessory troubleshooting page](https://onlinemanual.insta360.com/go3s/en-us/troubleshooting/connect/wake-camera)
says remotes pair directly with the camera; the Pod has separate power control.
These Official facts make **the GO 3S camera the first candidate control peer**.
They supersede treating camera versus Pod ownership as wholly undocumented for
vendor accessory pairing. They do not establish an ESP32-accessible GATT route,
BLE central/peripheral roles, UUIDs, security policy, or recording-state schema.

The [camera/Pod connection instructions](https://onlinemanual.insta360.com/go3s/en-us/operating_tutorials/connect/actionpod)
use powered devices, docking and successful live view; firmware incompatibility
needs resolution. The Pod's UI/control role does not make it a third-party
protocol proxy. Record camera and Pod firmware separately, plus docking/power
state and the actual Bluetooth Remote UI offered by that firmware. No local
GO 3S baseline has been observed.

GPS Action Remote is distinct from GPS Preview Remote and its built-in-mic
variant. The broad troubleshooting page names several accessories across GO
models; that list cannot qualify each model/accessory combination. This decision
uses the Action Remote's explicit GO 3S compatibility entry. It neither selects
an accessory purchase nor imports accessory wake/GPS support into #21.

## Route decision

Investigate the camera's remote-search path first within the existing #46 row.
The bounded CE80 receive-only probe is a **Hypothesis** candidate when the fitted
UI permits remote search; it is not evidenced GO 3S protocol support. A camera
acting as central would not expose that remote service to a central scanner.
An absent camera GATT service therefore cannot establish incompatibility.

Do not configure GO 3S as X5, enable `X5CapturedDisplayV1`, transmit the CE82
shutter event, or send source-derived BE81 commands before target qualification.
Existing X5 and ONE R/RS reports do not provide GO 3S expected state fixtures.
If the bounded attempt fails, record UI, role, security and connection outcome
before choosing any alternate route. Do not automatically reset or erase bonds.

The [finite capture procedure](testing.md#go-3s-and-action-pod-evidence-protocol)
records one annotated manual Start/Stop and mode change, complete traffic and a
playable clip when a route exists. Three production REC/STOP cycles and one
reconnect follow only after implementation. Missing equipment/stages stay
Blocked; no all-accessory, app, wake, GPS or mixed-group campaign is added.

## Gate for an enabled adapter

| Required fact | Current evidence | Implementation consequence |
|---|---|---|
| Vendor accessory target | Official: camera | Investigate camera first; Pod UI is a separate role |
| Fitted camera/Pod firmware and pairing prerequisites | No local observation | Do not assume a compatible firmware or commissioned identity |
| ESP32 route, services, security and subscriptions | Unobserved | No enabled GO 3S transport/profile |
| Video mode, recording state and command/response mapping | No complete GO 3S fixtures | Keep Unknown; do not reuse X5 display vocabulary or timer heuristics |
| Deadline, reconnect and lost-response behavior | Pending an evidenced route | Test malformed/ambiguous frames, missing prerequisites, expiry and disconnect without replay |

Retain reviewed, licensed independent fixtures in
[`test/fixtures/go3s/`](../test/fixtures/go3s/README.md) after capture. An enabled
profile must expose only evidenced capabilities, tie observations to fresh
peer/generation/mode evidence, and return Unknown after reset or lost response.
Only then can #21 satisfy its recording implementation criteria; #22/#46 retain
physical smoke/compatibility decisions. A disabled entry is not implementation.

This change copies no vendor code, images or protocol packets. Source provenance
is recorded in [sources](sources.md#go-3s-recording-profile-source-reconciliation-21).
Documentation checks apply under the [acceptance policy](acceptance_policy.md);
unchanged firmware evidence is reused. No upload, camera command or hardware
qualification was performed.

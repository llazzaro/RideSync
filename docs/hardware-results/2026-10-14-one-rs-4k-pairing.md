# ONE RS 4K Boost pairing investigation — October 14, 2026

The owner declared an Insta360 ONE RS with the 4K Boost Lens and camera
firmware v2.0.11.4. This is an isolated CE80 probe observation, not qualification
of the production ONE RS BE80 adapter. The existing BE80 candidate is restricted
to the ordinary 360 Lens; no qualification flag was changed for this assembly.
Repository revision at preparation was `eb82150`. The connected ESP32 reported
the existing `X5_PROBE` banner. Its installed image/hash was not independently
read back, and no firmware upload or flash/bond maintenance was performed.

## Procedure

Serial was opened at 115200 baud with DTR/RTS deasserted. Each opening nevertheless
produced a fresh POWERON_RESET/idle-probe banner; the exact reset cause was not
investigated. A fresh idle report preceded one `A` per attempt, starting the
existing 120-second advertising/capture policy. No `S`, `W`, private hex option,
guessed handshake or recording command was issued. Length-only serial captures
are retained privately outside Git; these are not payload fixtures.

The owner used the camera's Bluetooth remote menu and reported that it listed
the remote and that they tried to pair. Individual camera action times were not
measured. Final camera-screen status for the second attempt is not yet confirmed.

## Observations

The first attempt successfully started advertising but received no connection
before the finite deadline. Final reports showed 15 events, zero dropped or
truncated events, no peer and completed SDK cleanup. The owner's subsequent
report establishes camera-side discovery, not completed pairing.

On the second attempt, advertising synchronization was reported at boot
3.023 seconds, followed by a successful connection at 4.104 seconds. An MTU
event reported 23. The camera sent five writes to the registered CE81 value
handle 16:

| Boot time (seconds) | Write length (bytes) |
|---|---:|
| 5.669 | 11 |
| 5.684 | 7 |
| 5.699 | 11 |
| 5.714 | 20 |
| 5.729 | 11 |

At 5.806 seconds, the disconnect event reported reason 531 (`0x213`): the
pinned NimBLE headers define HCI error base `0x200` and remote-user connection
termination `0x13`. This identifies peer termination, not its application-level
cause. The probe then stopped with its Disconnected reason. Reports showed 22
events, zero dropped or truncated events and all three SDK operations returned.
No subscription, security-change, passkey, repeat-pairing, shutter-request or
shutter-result event was reported. Each write was a complete bounded copy, but
its bytes were not emitted because private hex output was disabled.

## Decision and next evidence

Discovery and one brief BLE/GATT connection are observed for this owner-declared
assembly. Stable pairing and recording control remain unverified. No Start/Stop
cycle, playback, reconnect/no-replay, wake or GPS claim is established. Keep
production support Experimental / Not tested and recording Unknown.

The next diagnostic should confirm the camera's final pairing screen and retain
the complete incoming pairing exchange privately during one fresh bounded
attempt. Inspect that exchange before proposing a handshake or service change;
the current write lengths and disconnect code do not establish such a fix.
These observations belong to the existing #46/#22 camera investigation and do
not close either checklist or promote the 4K Boost assembly to the BE80 target.

## Third attempt: connected UI and shutter admission blocked

The owner requested another attempt. A fresh idle boot preceded `H` and `A`;
complete incoming bytes were retained privately and excluded from public output.
Advertising synchronization was reported at boot 3.046 seconds. The camera
connected at 27.544 seconds with MTU 23, and the owner reported "connected".
The owner later confirmed readiness after being asked to return to the main
video screen and leave the camera stopped. The connection remained active until
the probe's finite deadline, approximately 95 seconds after connection.

The camera sent 33 complete CE81 writes: 23 of length 11, two of length 7,
five of length 20, one of length 12 and two of length 14. All had the CE80-family
`FE EF FE` prefix. No subscription or security-change event was reported. A
single `S` submitted after the stopped-video readiness report produced a
ShutterRequest event at boot 103.414 seconds with `value=0` (refused). There was
no ShutterResult event or additional SDK submission: the current probe requires
an actual CE82 notification subscription before admitting a shutter operation.
No shutter notification was sent to the camera, so this is not a failed
camera-level recording command or a successful REC/STOP observation.

The probe reached its deadline at 122.799 seconds and reported actual disconnect
at 122.863 seconds with reason 534 (`0x216`, local connection termination),
consistent with its bounded cleanup. This differs from the second attempt's
early peer termination. At the last pre-deadline summary, all 49 events were
reported with zero dropped/truncated events and no pending SDK/shutter work;
the complete private transcript contains 52 events through disconnect.

Discovery, the owner's connected UI and a sustained finite BLE link are now
observed. Recording control remains blocked by missing notification admission;
the three camera-observed cycles/playback and reconnect/no-replay checks remain
unperformed. Retained traffic is opaque evidence, not a recording-state decoder.

The pinned MIT example initializes its BLE2902 descriptor using
`setNotifications(true)` and `setIndications(true)` before adding it; the current
NimBLE probe instead requires a client subscription event. This is a concrete
source difference to investigate, not proof that pre-enabling notifications is
required or sufficient on ONE RS 4K Boost. Do not infer a handshake or relax
production qualification from the camera's connected UI.

## Legacy image upload and first pairing window

After approving the source-backed legacy admission change, the owner replied
"sd card is now empty" to the request to disconnect power, remove any ESP32 SD
card and reconnect USB. This was treated as confirmation of the requested slot
preparation. The camera's own card was requested to remain installed.

The new isolated probe at `4b5e9b7` was uploaded over the existing USB port.
Its firmware SHA256 is
`34b78d58b865eb65318ece10a80ca455e4b6e15f9e38342d8d8bc8c6c14e8da5`.
The current full 4 MiB flash backup was verified against the device before
upload, and the partition table matched byte for byte. All four written regions
were independently verified afterward. The 20 KiB NVS readback at `0x9000`
matched the pre-upload baseline byte for byte. Private backups, hashes of
private flash/NVS and upload logs remain outside Git.

Opening serial produced a fresh idle banner containing the new L option. One
L/H/A sequence selected explicit ONE RS legacy mode and private capture, then
started the finite advertising window. The owner was asked to connect and
confirm stopped normal video; no readiness reply was received during this
window. Reports showed `one_rs_legacy=1 subscribed=0`. Synchronization occurred
at boot 3.038 seconds, and the window ended at 122.786 seconds without a camera
connection. No shutter request/result or camera write was recorded. All 15
events were reported with zero dropped/truncated events; final advertising,
in-flight SDK work and pending shutter were zero.

Post-trial NVS readback again matched the pre-upload baseline and reset the
board to idle. This is a verified installation and advertising observation,
not a failed camera-level shutter command or a compatibility verdict. Recording
and wake remain unperformed on the new image. Wait for explicit camera readiness
before opening the next pairing window; do not automatically repeat it while
the owner's camera-side preparation is unknown.

## Legacy shutter attempt: Start observed, Stop unresolved

On the same uploaded image, the owner replied "ready" after being asked for
stopped video. A fresh L/H/A session connected at boot 3.383 seconds with MTU 23
and without a subscription event. One S was admitted at 29.463 seconds; the
addressed CE82 SDK submission returned zero at 29.467 seconds. The owner then
answered "yes" when asked whether the camera displayed a running recording
timer. This establishes one camera-observed Start in the isolated legacy trial.

After that observation, a separate S was admitted at 91.235 seconds and its
SDK submission returned zero at 91.237 seconds. When asked to verify Stop and
playback, the owner replied "no it show and error. may be wrong command or
somethung else failed." Exact error wording, actual stopped/running state and
playback are unresolved. The link disconnected at 98.431 seconds with reason
520 (`0x208`, HCI connection supervision timeout), before the capture deadline.
Do not assign the camera error to the shutter bytes or SD without further
evidence. Neither successful SDK submission completes the Stop hardware check.

Incoming CE81 traffic stopped at boot 69.209 seconds, approximately 22 seconds
before the Stop request. All 69 incoming writes were complete bounded copies;
37 sixteen-byte messages appeared after Start. These are opaque observations,
not a decoder or independent confirmation of stopped/running state. Final
reports showed 90 events, zero drops/truncation, five admitted/returned SDK
operations and no pending shutter/SDK work. No automatic shutter retry occurred.

The owner requested a short Start/Stop retry. One session connected only at
119.226 seconds, reached its finite deadline at 122.743 seconds and disconnected
locally at 122.830 seconds. No shutter was sent; the owner's disconnected UI
report is consistent with intentional probe cleanup. Two further requested
advertising windows received no camera connection and expired without any
shutter request. These unperformed shutter trials do not constitute camera-level
command failures. Their repeated expiry motivated a separately tested ten-minute
L-mode session correction; the original normal/wake limits remain unchanged.

Start is Observed once on this assembly; Stop remains Not confirmed. Three full
cycles, playback, reconnect/no-replay after control and ONE RS wake remain open.

## Ten-minute image: successful Start/Stop and playback

The longer-session image at `24d9e13` was uploaded with firmware SHA256
`dac864e592ccf2af946dde6f35c4f497bac138cfc64955d2d8d95458b0a60da8`.
A fresh full-flash backup of the installed legacy image was required before
upload. Its first 460800-baud read failed with corrupt transfer data and was
retained as an incomplete read, not accepted as a backup. A new 230400-baud
read completed and verified against the device. NVS still matched the earlier
baseline. All four new written regions were independently verified, and
post-upload NVS remained byte-identical. No erase/recommissioning was used.

One fresh L/H/A session synchronized at boot 2.983 seconds. The Mac independently
observed `Insta360 GPS Remote` advertising CE80 while the camera was not yet
connected. Probe reports remained active/advertising beyond the old two-minute
boundary. The camera connected at 165.140 seconds with MTU 23 and no subscription
event; the owner reported "now is fixed" after the connected/stopped preparation
request. No shutter was sent automatically on connection.

One manual S was admitted at 225.652 seconds and its addressed CE82 SDK send
returned zero at 225.662 seconds. The owner answered "yes" to the running
recording-timer question. A separate S was admitted at 285.660 seconds and its
send returned zero at 285.664 seconds. The owner answered "yes" when asked
whether recording stopped without an error, then "yes" when asked whether the
new saved clip played correctly. This establishes one successful camera-observed
Start/Stop cycle with playable footage on the declared ONE RS 4K Boost firmware.
Owner reply times are not exact camera-action timestamps. Firmware recording
remained Unknown; no ACK/state decode was fabricated from the SDK results.

The peer disconnected at 391.048 seconds with reason 531 (remote user
termination), before the ten-minute deadline, and the probe retired normally.
The owner had been inspecting playback and Camera Info; exact physical action
timing/cause is unresolved. Final capture contained 134 events with zero
dropped/truncated events and two shutter requests/results. The owner-provided
wake identifier independently matched the camera's private incoming identifier;
its value and raw captures remain outside Git. The owner confirmed Bluetooth
Wakeup enabled; a powered-off wake observation is still pending.

The owner requested committing this working result and additional regression
tests. Preserve the earlier error/timeout rather than reclassifying it as a
pass. Three complete cycles and explicit reconnect/no-replay still remain
unchecked in #46/#22; this one cycle does not qualify production or a reliability
campaign. Software CI for `24d9e13` passed in
[run 38541557108](https://github.com/llazzaro/RideSync/actions/runs/38541557108).

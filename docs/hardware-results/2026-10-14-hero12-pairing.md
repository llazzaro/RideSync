# HERO12 ESP32 pairing investigation — October 14, 2026

The owner requested testing through the ESP32 and declared HERO12 firmware
version 2.32. A preliminary Mac BLE check independently returned model 62,
firmware `H23.01.02.32.00` and Open GoPro API 2.0. Mac queries returned
Encoding false, Busy false, Ready true. All eight documented control/management
endpoints were present with expected write/notify properties. These Mac results
are not ESP32 compatibility or production adapter qualification.

## Preparation and software evidence

The owner approved an isolated ESP32 probe, temporary pairing, three explicit
REC/STOP cycles and one reconnect. The probe reuses the pure GoPro codecs and
existing NVS refusal wrappers; it uses a fresh random static identity and RAM-only
bond store each boot. No modem, SD, IMU or handlebar peripheral is started and
no existing ESP32 bond is deleted. The camera may retain its side of a temporary
bond. No cold-restoration support is claimed.

The first full-flash read at 460800 baud failed with an invalid SLIP escape.
The subsequent 115200-baud full 4 MiB backup was verified against the device.
The compiled partition table matched the backup. After upload, all four regions
were independently verified and the 20 KiB NVS at `0x9000` matched the baseline
byte for byte. Backups and continuous serial transcripts remain private outside
Git. The documented flashing procedure now reuses a matching retained backup;
verified reconstruction from known written regions also avoids another full read.

The first uploaded firmware SHA256 was
`369ee312ccd21af6d2d196e608854cdde284965d20dde817475abb5623000742`.
Software checks passed: 97 existing native HERO12 adapter cases, the probe's
fresh/stale/status/expiry/reconnect/wrap admission checks, descriptor range checks,
normal and oversized burst refusal regressions, pinned probe build, formatting,
Python lint and the ELF audit of RAM-only storage and NVS/HAL/PHY refusal
boundaries. Review corrected descriptor discovery ranges, security revocation,
oldest-snapshot freshness/post-mode queries and complete burst rejection before
upload. This is build/test evidence, not recording evidence.

## First ESP32 attempt

The continuous serial owner observed idle status with `failed=0`, zero drops,
zero erase refusals, one core NVS initialization refusal and two PHY open
refusals. One explicit CONNECT selected one FEA6 candidate without ambiguity.
The actual connection callback succeeded. Security initiation and encryption
change returned zero; the connection descriptor reported encrypted and bonded.
All eight endpoints passed property checks; all four response CCCDs were enabled
and read back as `01 00`.

Management pairing finish returned protobuf result 1 and success. External
control claim returned protobuf result 0 and was refused. The probe retired the
connection, reporting local disconnect reason 534. All 19 submitted ATT/scan/
connect operations had terminal callbacks, with zero notification/log drops.
No Hardware Info/API/status/shutter request followed the refused claim in this
first sequence. No REC or STOP command was sent. The owner has not separately
confirmed the camera's pairing UI outcome.

The official [generic response schema](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/protobuf/response_generic.proto)
defines result 0 as Unknown and 1 as Success. The result must not be reinterpreted
as classic ACK zero. Its underlying cause is not established by this observation.

## Readiness-order test

The [official BLE setup](https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup/)
requires successful Hardware Info readiness before control. The diagnostic was
changed to verify Hardware Info/API before external control claim; this is a
single sequencing hypothesis, not a claim that Unknown is fixed. The production
adapter was not changed. The new image's four regions were independently
verified and its NVS readback again matched the original baseline.

The second image SHA256 was
`f65fa35345679cdcbed11d66579e124c7b49341affa835b2a9dca0490a6e6e4f`.
The next scan again selected one FEA6 candidate. Link establishment instead
reported disconnect reason 574 (`0x23e`, HCI connection failed to be established)
before a successful connection callback. The pending connect callback reached
the finite deadline and the diagnostic required reset. No security/setup,
identity/status or recording command followed. The readiness hypothesis remains
untested by that attempt. A further attempt requires the owner-confirmed pairing
screen and an explicit reset; no new flash upload is needed for that retry.

## Owner-confirmed pairing-screen retry

The owner subsequently confirmed the Quik App pairing screen and reported that
camera inactivity/power-off had occurred during preparation. Following an explicit
ESP32 reset (no firmware upload), the same readiness-order image connected,
reported encrypted/bonded security, checked all endpoints/CCCDs and completed
pairing finish. Hardware Info returned model 62 and exact matching firmware;
API returned 2.0. The subsequent control claim still returned Unknown and the
link was retired without recording. Waiting for Hardware Info did not resolve
this claim on the tested assembly. No alternate meaning for result zero was
accepted.

## Classic tutorial path and first observed recording cycle

The [official classic shutter tutorial](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/tutorial/tutorial_modules/tutorial_2_send_ble_commands/ble_command_set_shutter.py)
provides a path that does not depend on protobuf external-control claim. The
final isolated probe uses that path after encrypted/bonded connection, pairing
finish, exact identity/API match and fresh Encoding/Busy/Ready. It omits control
claim and explicitly labels it unqualified. The production adapter and its
refusal gate remain unchanged. The current full-flash image was reconstructed
from the original backup plus retained exact upload regions and verified before
this upload, avoiding another full read. Its firmware SHA256 is
`f1f5dbb14cbe883e4bdf54012dfcba9d5add853dfe63527e1959dff1edc757d5`.
All four new regions and the original
NVS comparison passed again.

An explicit CONNECT reached Ready with Encoding false, Busy false, Ready true.
One REC obtained fresh status, loaded video group 1000, obtained status again,
and sent one explicit shutter-on. Camera ACK was zero and a fresh query reported
Encoding true. The owner replied "it is recording".

One STOP obtained Encoding true and sent one explicit shutter-off. Camera ACK
was zero, but all five finite confirmation polls still reported Encoding true.
The probe recorded an unconfirmed transition and retired the link without
retrying Stop. The owner independently replied "it stopped". This establishes
one camera-observed Start/Stop cycle; the short API confirmation window did not
establish stopped state. Finalization latency or another explanation for the
API/display difference has not been measured. Playback is not yet confirmed.

A subsequent explicit CONNECT reused the same boot's RAM bond, rechecked
identity/API and reported Encoding false, Busy false, Ready true without sending
a shutter. This establishes a stopped observation after reconnect and no
recording replay in this probe. The owner then prioritized the wake test over
the remaining two recording cycles. An explicit DISCONNECT completed before
the owner was asked to power the camera off normally with its battery installed.

## Powered-off wake with the retained RAM bond

After actual disconnect, the owner replied "is off now" to the request to turn
the HERO12 off normally with its battery installed. The same ESP32 boot then
received one explicit CONNECT, without any recording command. One FEA6 candidate
was found; the connection succeeded, encrypted/bonded security was reported,
all endpoints/CCCDs passed, pairing finish succeeded and model/exact firmware/API
matched. Fresh queries reported Encoding false, Busy false, Ready true.

Asked whether the camera turned on and showed its normal screen while remaining
stopped, the owner replied "yes". This establishes one observed normal-powered-off
wake and post-wake recovery to the qualified classic diagnostic connection,
with no automatic recording replay. It does not establish wake without electrical
power, every power-down/USB/battery condition, repeated reliability or measured
camera power-on latency. No ESP32 reset or new bond was needed for this wake;
the same boot's RAM bond remained available. The host subsequently began the
remaining explicitly approved recording cycles as separate operations.

## Second cycle and regression delivery

After the owner-confirmed wake, the host explicitly requested REC for the second
approved cycle. Fresh status, video selection and fresh status preceded one
shutter-on; ACK zero and Encoding true were observed. The owner replied "is
recording". One STOP then returned ACK zero, while the five confirmation polls
again retained Encoding true. The probe retired without retry; the owner replied
"camera stop" and separately confirmed "yes". Two camera-observed cycles are
established. The owner then requested regression tests, so the third cycle and
saved-media playback remain pending rather than being counted as passes.

Final runtime status reported all 186 submitted scan/connect/ATT operations
returned, zero dropped notifications/logs and zero erase refusals. The post-session
20 KiB NVS readback matched the original baseline byte for byte. That deliberate
readback reset the ESP32; subsequent serial STATUS confirmed idle, disconnected
and no erase/recording replay. No host monitor was left owning the port.

Fourteen new native regression cases cover Unknown claim vs classic ACK domains,
owned observed firmware/API fields, adapter refusal/no retry, Stop ACK with
Encoding still true, wake-only recovery without video/shutter, and shared probe
admission/expiry/wrap/descriptor/burst cases. Responses remain explicitly
synthetic/schema-derived; no raw camera packet fixtures were captured. The
probe's standalone pre-upload runner shares the same nine cases as its new
native test, which the existing CI's `pio test -e native` discovers automatically.
All 119 focused GoPro/probe cases and the full 714-case native suite passed.
The final formatted-source probe build matched the uploaded SHA256 exactly;
ELF audit, focused formatting/lint and whitespace checks passed. Review found
no remaining actionable issue in the probe corrections or regressions. Tests
protect software contracts; they cannot replace real radio/camera observations.

## Acceptance disposition

ESP32 discovery, one encrypted temporary bond, endpoint/CCCD discovery and
pairing-finish success are observed. Actual ESP32 firmware/API match, two
camera-observed classic REC/STOP cycles, a stopped reconnect without replay,
and one owner-observed powered-off wake with post-wake classic recovery are
also observed. External-control admission, playback and the third recording
cycle remain pending. Production support remains Experimental / Not tested. This probe does not close #46/#22 or promote the production adapter.

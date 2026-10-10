# Initial test version: one X5 over serial

The first usable candidate is **`x5_serial_milestone`**, an Experimental production
CE80 path for one qualified X5. Start here to collect the composed-camera result
in [#46](https://github.com/llazzaro/RideSync/issues/46). Open compatibility,
installation, integrated acceptance and roadmap tickets do not block this first
bench test. They retain their own completion criteria and ride-ready claims.
See the [acceptance policy](acceptance_policy.md).

This candidate accepts explicit CONNECT/REC/STOP/QUERY/STATUS/DISCONNECT commands.
Board SD, GNSS, IMU, other cameras, GPS forwarding and wake qualification are not
prerequisites. A writable card in the **camera** is required. This is the first
serial bench milestone; the full handlebar and ride-logger workflow comes later.

## Build and commissioning gate

Declare the revision on `main` and retain the environment, build output and binary
SHA256 privately. The pinned host tools use Python 3.11. From the repository root:

```sh
python3.11 -m venv .venv
.venv/bin/python -m pip install -r requirements-dev.txt
git rev-parse HEAD
.venv/bin/pio run -e x5_serial_milestone -e x5_store_inspect
shasum -a 256 .pio/build/x5_serial_milestone/firmware.bin
.venv/bin/pio device list
```

The existing [CI workflow](../.github/workflows/ci.yml) builds these environments
and optional `x5_wake_milestone`, runs native/error tests, formatting and bench
tooling checks. Reuse passing evidence for unchanged code; documentation edits
do not require firmware rebuilds. Check the relevant revision's CI before the
physical session. A private provider changes the binary: rebuild and record its
hash separately. Public CI does not qualify that provider or the camera.

The public checkout intentionally has no private qualification. Without ignored
`include/x5_commissioning.local.h`, compilation succeeds but startup reports
`X5 commissioning refused: missing private provider, safe mode or NVS fault.`
CONNECT cannot start a camera session. Uploading this inert image is not a
recording test. Never copy synthetic identities/store values from tests.

Follow [private commissioning](x5_serial_milestone.md#private-commissioning) in
order. Declare actual board/revision, X5 firmware and video mode; captured-display
evidence uses owner-reported X5 **1.11.10**. Keep the verified factory backup and
make a verified full backup of currently installed board flash before upload.
Preserve NVS and partition layout. Unknown store/revocation history must be
resolved without erasing bonds or assuming an empty store.

1. Independently verify the camera's stable public or random-static identity;
   a name or unresolved private address is insufficient.
2. Upload `x5_store_inspect` on the backed-up board and privately capture its one
   `X5_STORE` line at 115200 baud. It starts no BLE and sends no camera commands.
   Require `complete=1 error=0`, retaining all seven `counts` and the exact
   64-hex-digit SHA256 `digest`.
3. Independently review the identity/store receipt and retain a private nonzero
   qualification record below UINT32_MAX and above the revocation floor. The
   inspector does not grant qualification.
4. Create the ignored header defining the following function, returning false
   whenever qualification remains incomplete:

   ```cpp
   bool ridesyncPrivateX5Qualification(ridesync::X5Qualification &qualification,
                                      ridesync::SourceConfig &source);
   ```

   Fill enabled/verified identity, exact observed `store.digest`/`store.counts`,
   reviewed `store.qualification_record`,
   `display.profile=ridesync::insta360::Ce80DisplayProfile::X5CapturedDisplayV1`,
   actual ASCII firmware/size and zero trailing firmware bytes. Supply exactly
   one enabled Insta360/X5 source camera, matching uppercase colon-separated
   identity/address type, empty wake identifier and `gps_telemetry=false`.
   SDK address bytes are least-significant octet first; textual identity reverses
   that order. Random-static identities require SDK byte 5's top two bits set.
   See [X5Qualification](../include/x5_peripheral.h),
   [BleStoreProof](../include/ble_pairing_reset.h) and
   [SourceConfig](../include/config.h).
5. Rebuild/hash/upload the milestone with that provider. The host also checks the
   real guarded store at startup; provider compilation alone is insufficient.

These are **operator steps for the declared backed-up session**, not preparation
commands to execute automatically. Replace the port placeholder with the
enumerated private port; run the inspector before creating the provider:

```sh
.venv/bin/pio run -e x5_store_inspect -t upload --upload-port <private-port>
# Capture/review the store; create the qualified ignored provider.
.venv/bin/pio run -e x5_serial_milestone
shasum -a 256 .pio/build/x5_serial_milestone/firmware.bin
.venv/bin/pio run -e x5_serial_milestone -t upload --upload-port <private-port>
.venv/bin/pio device monitor --port <private-port> --baud 115200
```

Use one continuous serial owner and record any reset when opening it. The
[tested macOS connection procedure](device_connection.md#avoid-restarting-the-modem-when-opening-serial)
documents the observed control-line workaround and limits. Startup qualification
has a 1000 ms owner deadline; late readiness or safe-mode/NVS failure requires
resolving the cause and a fresh boot.

## Bounded camera check and #46 results

Send one exact uppercase command per LF/CRLF line. Wait for its terminal outcome
before the next command; do not paste batches or automatically retry. The parser
retains one line and rejects extra complete lines. Boot is idle.

| Step | Command/action | Required evidence |
|---|---|---|
| Admission | `STATUS` | `configured=1 revoked=0`; no commissioning refusal |
| Connect | Camera remote-search mode, then `CONNECT` once | Within 15000 ms: `link=1 subscribed=1`; camera UI confirms connection |
| Ready | Confirm video mode/stopped on camera; `STATUS` | Fresh `observed=Stopped age_known=1 age_ms<5000`; typed Video admission also required |
| Record | `REC` once | Within 5000 ms: operation terminates, fresh Recording; owner sees recording/timer |
| Stop | After fresh Recording, `STOP` once | Within 5000 ms: operation terminates, fresh Stopped; owner sees camera stop |
| Media | Inspect saved clip | Owner confirms playable media independently of serial success |

`QUERY` is passive observation. `id` is the accepted operation;
`last_id`/`last_error`/`last_refusal` describe the latest request independently.
`active=0` alone is not success: retain final `error`/`failure`, observed state
and owner observation. Mode is not printed in this status line; confirm camera
video mode independently and honor WrongMode/UnknownState refusal. Busy, Stale,
LostInput, timeout, transport/qualification failures or missing camera confirmation
fail the affected row. Silence is Unknown, not Stopped. A recording timer alone
does not qualify Video or admit STOP. Disconnect does not send STOP: if recording
continues, stop it on the camera. An ambiguous result needs a fresh explicit
decision and fresh evidence, never an automatic second toggle.

For #46, add a dated report under `docs/hardware-results/` with revision,
environment/hash, actual board/camera firmware/card/video profile, commands,
outcomes, owner observations and playable-clip result. Keep identities, store
receipts, raw transcripts and binaries private; publish a redacted summary.
Link the [existing three-cycle/reconnect evidence](hardware-results/2026-10-09-x5-pairing.md#three-remote-recording-cycles--owner-confirmed)
when the wire profile matches, then record this new **composed path** observation
separately. Reuse the accepted reconnect evidence; repeat a reconnect only if a
changed wire/profile affects that qualification. The retained probe does not already pass the composed path. Mark
unavailable models/later capabilities Blocked or Not run; their open tickets do
not invalidate a passed single-X5 stage. Rerun failed/affected cases after fixes.
#22/#30/#31 retain their own physical checks and #46 retains its remaining rows.

Optional wake follows using
[x5_wake_milestone](x5_serial_milestone.md#optional-bounded-wakerecovery-9) and a
separately evidenced private wake identifier/provider. It is not required for
first REC/STOP. Record declared power state and wake → reconnect → fresh Video →
explicit REC/STOP in #46; isolated wake does not qualify the composed wake route.

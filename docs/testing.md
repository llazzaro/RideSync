# Validation

## Automated baseline

`pio run -e lilygo_t_a7670e_r2` compiles serial-only firmware.
`python -m unittest discover -s test -v` validates the sample configuration's
repository contract (including all three target models).
`python scripts/check_format.py` checks C++ formatting using pinned clang-format.
These do not test BLE, runtime configuration loading or physical peripherals.

Add native unit suites alongside each implemented subsystem: debounce and
press timing including rollover; independent camera transitions; retry budgets
and deadlines; valid/malformed GNSS input; protocol lengths, signed fields and
golden packet fixtures. Do not substitute implementation-shaped assertions for
observable behavior.

## Manual acceptance per model

Record board revision, ESP32 firmware commit, camera model/firmware, modem
firmware, configured identifiers, timings, serial logs and actual camera screen.
Run separately on X5, GO 3S and ONE RS:

1. Pair/discover services and capture subscriptions/handshake.
2. Power off camera; trigger wake; observe whether it powers on.
3. Wait up to a documented connection deadline; record failures.
4. Request REC; verify recording on the camera and compare notifications.
5. Request STOP; verify it stopped, then repeat without changing mode.
6. Disconnect or power-cycle the camera; confirm bounded recovery.
7. Reset ESP32 while camera is recording; ensure no accidental toggle.

Repeat with three cameras: two available, one absent. REC/STOP must reach the
two available peers and report partial success with the third unknown/error.
Measure command skew, concurrent connection capacity and retries independently.
Test rotation of all three wake identifiers; never infer wake compatibility from
one model. Test unavailable SD, full SD, GNSS no-fix/stale fixes, and reset.

Acceptance for the first X5 camera milestone: repeatable single-X5 start/stop with observed state,
bounded connect/command timeouts and honest serial status. It is not met yet.

## GO 3S and Action Pod evidence protocol

Status: **not run; target hardware unavailable**. Run this before adapter work
on each tested camera/Pod firmware pair. Record date, operator, camera model,
camera firmware, Pod firmware, app version, BLE controller/sniffer model and
software version, camera/Pod power state, activation/pairing history and
relevant settings. Keep exact identifiers only in a private raw capture; public
notes use stable labels such as `camera-A` and `pod-A`.

1. Start from the official connection prerequisites: camera and Pod powered on,
   first-use sticker removed, clean/aligned contacts, and successful live view
   on the Pod. If firmware incompatibility is shown, follow vendor update steps
   and record both resulting firmware versions. Allow the documented 10–15 s
   connection window. Do not factory-reset between repetitions unless reset is
   the variable under test.
2. Collect a passive BLE scan and over-air capture during: power-up, insertion
   into Pod, removal, camera-only button recording start and stop, Pod-button
   start and stop, app-triggered start and stop (if available), mode change,
   disconnect/reconnect, and a second full repetition. Mark each physical action
   with a timestamped annotation; record screen/indicator state and verify each
   result in saved media. Include idle controls as negative controls.
3. Separately attempt service/characteristic discovery with a BLE central only
   when it does not displace or alter the normal camera–Pod connection. Record
   advertisements, address type, services/characteristics/properties, security
   prompts, pairing/bonding outcome, notification subscriptions, MTU and
   discovery errors. If discovery requires disconnecting the Pod, label those
   runs as an altered condition. Do not infer GATT from a passive capture alone;
   encrypted traffic without keys is inconclusive.
4. Repeat each transition at least three times from known stopped and known
   recording states, then repeat after power-cycle and reconnect. Keep camera
   and Pod firmware constant within a repetition set. Compare packet direction,
   timing, payload and response/state to the annotations. Label results
   `Observed` only when locally captured; retain `Official` and `Community`
   reports as separate evidence classes and label explanations `Hypothesis`.
5. State the exact tested conditions and whether the controller can address the
   camera, the Pod, or neither. Only claim explicit start/stop when opposite
   state transitions are independently repeatable. If the same frame toggles,
   require reliable observed state and never replay after ambiguous timeout.
   Otherwise leave command/state unknown.

The current blocker is specific: we lack a GO 3S camera and Action Pod plus
their firmware/version record, repeated annotated recording transitions, BLE
advertisement/GATT/security evidence, and correlated command/response/state
captures. Therefore target ownership and start/stop/state feasibility remain
unresolved. A missing capture is not a negative compatibility result. Do not
start #21 from assumptions; resume this protocol when hardware is available.

## Planned GoPro and mixed-camera validation

Follow [GoPro milestone acceptance](gopro_plan.md) for each selected model and
firmware. Test pairing persistence, video-mode selection, explicit shutter
on/off, observed encoding state, busy responses, keep-alive, sleep versus full
shutdown, power cycling and ESP32 reset during recording. Codec tests must cover
notification fragmentation and malformed responses.

For mixed-brand groups, test concurrent BLE roles, per-peer response routing,
independent deadlines, one unavailable camera and measured command/recording
skew. Continue GPS/SD logging throughout. No GoPro hardware tests have run yet.

## Planned ride-logger validation

GNSS and raw IMU logs must continue through camera disconnects. Test invalid or
stale fixes, initial lack of UTC, clock corrections, missing/full/slow SD cards,
queue overflow, power loss and absent/saturated IMU. Record loss counters and
sensor quality. Validate linear acceleration and lean/pitch against an independent
reference; stationary tilt alone does not validate motorcycle cornering behavior.
See the [issue-backed plan](superpowers/plans/2026-10-07-ridesync.md).

# BLE timecode capture worksheet — #50, #51 and #52

Prepared October 13, 2026. This is a finite evidence handoff, not delivered
timecode support. The October 12 source investigations remain complete:
[X5](x5_timecode.md), [HERO12 Black](gopro_timecode.md) and
[DJI](dji_timecode.md). No camera capture or footage measurement was performed
while preparing this worksheet. All execution rows below start **Not run**.

[#46](https://github.com/llazzaro/RideSync/issues/46) coordinates camera execution;
[#22](https://github.com/llazzaro/RideSync/issues/22) owns compatibility acceptance.
Keep separate results for each model and installed firmware. Apply
[acceptance_policy.md](acceptance_policy.md): unavailable equipment is Blocked,
source evidence is not a physical pass, and unchanged passing evidence is reusable.

## Narrow source recheck

The recheck used current primary command/SDK evidence and GitHub API identity,
not a restarted broad repository search. No usable new BLE route was established.

| Source checked October 13 | Actual finding | Remaining evidence |
|---|---|---|
| [Insta360 Android camera API](https://github.com/Insta360Develop/Insta360-Developer_Docs/blob/eff53d7edd7de0cf3ed6f7fd634606c9ea1e0472/docs/en/sdk/x-ace-go/android/camera-api.md) and [exact update diff](https://github.com/Insta360Develop/Insta360-Developer_Docs/compare/caa3700d0bf2a1b96317211f64431bcd0dfe11c0...eff53d7edd7de0cf3ed6f7fd634606c9ea1e0472) | HEAD advanced to `eff53d7`; changes only English/Chinese iOS photo-result notifications. The reviewed Android/desktop clock documents are unchanged. | X5 BLE-only execution, qualified framing/units/responses and native timecode effect. |
| [Open GoPro BLE control](https://gopro.github.io/OpenGoPro/docs/ble/control/#set-date-time) and [SDK pin](https://github.com/gopro/OpenGoPro/tree/0f963572611c4410a15678531e9681a6ff874edb) | Repository HEAD remains `0f96357`. Public date/local-date setters still represent whole seconds. | HERO12 fractional phase/apply-time behavior or an evidenced precision BLE operation and native footage accuracy. |
| [DJI command catalog](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/92fe23e5a749f189593f980a26a105c3bb66aa1c/docs/protocol_data_segment.md) and [upstream clock/timecode request #13](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/issues/13) | HEAD remains `92fe23e`; request remains open with one May 11, 2026 follow-up, no vendor command answer. | Independent Action 4/5 Pro/6 BLE command evidence; Osmo 360 native capability first. |

These are **Source/documentary** findings only. They establish neither the absence
of all possible undocumented commands nor installed-camera support.

## Per-model readiness

Use one copy of the run record per available model. No success transfers between
models, firmware, connection roles or transports.

| Model / ticket | Documentary starting point | Exact first missing observation | Run state |
|---|---|---|---|
| Insta360 X5 / #50 | Native timecode documented; Android `setLocalTime` is a clock-setting lead. Historical owner report 1.11.10 is not a fresh firmware check. | Installed firmware, BLE-only official-app refresh or official-SDK clock session, and saved native timecode effect. | Not run |
| GoPro HERO12 Black / #51 | Native Quik QR sync; documented BLE calendar setters have whole-second fields. | Installed stock/Labs identity and API version; isolate BLE from QR, ordinary connection/date update and Wi-Fi. | Not run |
| DJI Osmo Action 4 / #52 | Native timecode documented; no qualified BLE setter in reviewed official protocol. | Installed firmware and distinct Mimo system-clock versus camera timecode-refresh events, plus BLE-only attribution. | Not run |
| DJI Osmo Action 5 Pro / #52 | Same evidence boundary; model-specific restrictions apply. | Independent model session and media evidence; do not reuse Action 4 results. | Not run |
| DJI Osmo Action 6 / #52 | Same evidence boundary; model-specific restrictions apply. | Independent model session and media evidence; do not reuse Action 5 Pro results. | Not run |
| DJI Osmo 360 / #52 | Native timecode capability remains unestablished in inspected sources. | Model-specific manual/API or observed menu **and** saved native timecode evidence before a synchronization probe. | Not run |

## Run record and capture prerequisites

Fill before execution; leave unavailable facts **Unknown**, never inferred from a
similar model. A run is Blocked if the required camera, capture visibility or
independent reference is absent. Stop that row and record the missing item.

| Field | Value to record |
|---|---|
| Run ID / operator / date | Unknown |
| Exact model / camera firmware screen evidence | Unknown |
| App or SDK version / phone model / OS / API version if exposed | Unknown |
| Mode / preset / resolution / actual recording rate / timecode rate / DF or NDF | Unknown |
| Pre-record, Hindsight or special mode / power source / starting idle state | Unknown |
| Time source / UTC validity and freshness / timezone / DST / clock uncertainty | Unknown |
| Reference method / independent clock / timestamp uncertainty | Unknown |
| Declared initial-offset target / separate 30-minute drift limit | Unknown |
| Capture method / timestamp resolution / clock mapping to screen actions | Unknown |
| GATT service and characteristic UUIDs / direction / pairing and authorization | Unknown |
| Wi-Fi, USB, QR, audio/LTC or accessory paths active and how excluded | Unknown |
| Original media IDs / hashes / raw capture location / intended export/editor versions | Unknown |

Use a phone Bluetooth capture or official-SDK trace that sees the actual
phone-to-camera request and camera response. RideSync's X5 CE80 receive probe
cannot observe another connection's GATT writes. Preserve discovery and connection
authorization separately from the timecode action. Retain raw ATT fragments and
reassembled messages with direction, handles/UUIDs, lengths and monotonic times;
if reassembly or clock mapping is uncertain, record that uncertainty explicitly.

Capture only official UI/SDK operations already offered for that exact camera.
Do not send guessed packets, transplant Wi-Fi/USB schemas into BLE, add fractional
bytes to GoPro calendar fields, reinterpret DJI GPS dates as clock setting, or
replay unknown captured writes. No firmware installation, reset, activation or
new accessory is a prerequisite imposed by this worksheet. Do not change
timecode while recording in the initial probe.

## Finite attribution sequence

For each qualified model, collect one baseline and one annotated action session
before evaluating conditional implementation. Declare a capture window before
starting (for example, 30 seconds around each event); a missing response at its
end remains Unknown, not permission to retry indefinitely.

1. **Baseline connection/background:** capture connection, authorization and
   idle traffic without explicit timecode refresh. Label any automatic ordinary
   clock/date update. Save a short reference clip with its native timecode.
2. **Explicit action:** record the exact screen action or SDK method and its
   monotonic event time. Retain both directions before/during/after it. Record
   camera system-clock and timecode displays before/after, then save a short
   reference clip. Keep unchanged mode/rate/reference settings for comparison.
3. **Compare:** identify traffic present only around the action versus repeated
   heartbeat, status, pairing or ordinary clock traffic. A difference is a
   candidate, not a proven command. Correlate transport, matched response and
   native media effect; preserve Unknown where attribution is incomplete.
4. **Stop and decide:** one review of this pair per available model yields
   evidenced candidate, unresolved, or infeasible for the tested firmware/scope.
   If capture fails, note the precise missing artifact and stop. New campaigns
   require a stated hypothesis; do not restart the completed source search.

Model-specific action controls:

- **X5:** compare ordinary connection without refreshing the timecode page with
  the official refresh. An official-SDK `setLocalTime` trace is an alternative
  clock probe, not assumed equivalent to native refresh. Prove whether Wi-Fi is
  used. Record SDK/camera-hosted role separately from RideSync's CE80 role;
  BE80/phone framing is not interchangeable with CE80.
- **HERO12:** compare ordinary connection/date update with Quik Sync Timecode
  while the camera cannot scan the QR. If available, record one normal QR sync
  as a separately labelled optical reference. Record QR exposure explicitly.
  A BLE trace coinciding with a scan cannot prove BLE caused precision sync.
  Do not require Labs installation; a QR-only result does not pass BLE acceptance.
- **Each DJI Action:** annotate Mimo connection/system-clock activity and the
  camera's timecode-refresh action separately. Observe whether camera-side
  refresh produces any BLE command at all. Begin with a documented lower rate
  such as 25 fps and pre-recording off; leave the inconsistent 60 fps boundary
  unresolved. Distinguish official `0xAA` BLE framing from other `0x55`/Wi-Fi
  protocols. No Action model inherits another model's packet or outcome.
- **Osmo 360:** stop at native capability verification if no model-specific
  timecode workflow/media track is established. Absence from Action help is not
  proof of universal infeasibility.

## Evidence packet and feasibility gate

| Required artifact / question | Result / evidence ID |
|---|---|
| Baseline versus explicit action timeline and raw capture | Unknown |
| BLE-only attribution, exact role, UUIDs and full framing | Unknown |
| Command ID/set, routing, payload boundaries and byte order | Unknown |
| Epoch, timezone, units, fractional precision and DF/NDF representation | Unknown |
| Matched command response, sequence, status/error meanings | Unknown |
| Difference between ATT write success, camera ACK and native media effect | Unknown |
| Effective apply time / latency and jitter / clock versus timecode reset | Unknown |
| Applicable exact firmware, modes/power states and vendor restrictions | Unknown |
| Original saved media native track and independent reference comparison | Unknown |
| Remaining unknowns and bounded follow-up hypothesis, if any | Unknown |

Label every assertion **Source/documentary**, **Synthetic/schema-derived**,
**Observed** or **Confirmed**. Keep raw captures privately; share redacted
protocol facts without credentials/identifiers. Redaction must preserve packet
lengths, byte offsets and event timing, or describe what was removed. An expected
fixture may follow an official schema; unknown fields must remain unknown rather
than become a guessed encoder.

A usable route needs the qualified per-model operation, its response and timing
premises, and native timecode semantics sufficient for a design. A clock ACK or
changed creation timestamp alone is insufficient. Report any newly evidenced
route before implementation. Conditional software then needs opt-in, fresh valid
time, stopped-camera admission, finite timeout/cancellation and no stale replay
on reconnect. Recording and local logging must continue through failed sync.
Invalid/stale time, disconnect, timeout and record-start behavior require focused
software tests; do not send malformed or undocumented packets to discover them.

## Conditional native-footage acceptance

Only an evidenced BLE route advances this table as BLE acceptance. Manual
QR/LTC/USB/Wi-Fi observations remain useful separately labelled references.
Declare the alignment target and independent reference before the run; no numeric
accuracy target is claimed as achieved by this worksheet.

| Finite check per model / firmware | Actual result and artifact IDs |
|---|---|
| Three fresh pre-recording sync attempts, short original clips and native-track inspection | Not run |
| Original versus intended export/editor import timecode, including chapters and rate interpretation | Not run |
| One declared 30-minute recording: initial offset, final offset and drift | Not run |
| One reconnect: persistence, background resync if any, fresh-request behavior | Not run |
| One declared sleep or power cycle: persistence and need for a fresh sync | Not run |

For media inspection, retain the tool/version, stream or native metadata location,
timecode value, timecode rate and DF/NDF interpretation. Compare original footage
to an independent visible/audio timestamp event and retain the frame indices.
An unsupported media parser is Blocked, not proof that no native track exists.
Creation date, filesystem timestamp, GPS date and recording timer are separate
metadata and cannot replace native timecode inspection.

Report signed offsets in frames and elapsed seconds at the actual rational frame
rate. Drift is final offset minus initial offset over the measured interval; DF
labels are not elapsed-second arithmetic. State reference uncertainty and whether
it can distinguish the declared target. Simultaneous shutter exposure/genlock
is not inferred from aligned timecode. Each result is Pass, Fail, Blocked or Not
run, with source, ACK and observed footage outcomes recorded separately.

The current outcome remains **unresolved BLE implementation for X5/HERO12/each
DJI Action; Osmo 360 native capability unresolved**. This worksheet does not
check off conditional software delivery, physical acceptance or ticket closure.

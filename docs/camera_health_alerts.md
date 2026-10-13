# Per-camera recording health investigation

Issue [#53](https://github.com/llazzaro/RideSync/issues/53), researched **2026-10-13**.
Decision: implement an opt-in, passive owner-context health evaluator for unexpected
stop and lost recording evidence, then add source-qualified **HERO12 Black**
battery, remaining video time/storage and overheating status. Reuse existing
LED/serial outputs. DJI resources are a documented follow-up, not a prerequisite.
GO 3S and ONE RS authoritative recording/resource status remain unresolved.

This note and the [finite implementation plan](superpowers/plans/2026-10-13-camera-health-alerts.md)
complete the investigation boundary in [acceptance policy](acceptance_policy.md).
No alert firmware is delivered here. No automatic shutter retry, new radio role,
hardware support promotion, or new issue is implied.

## Evidence and source audit

Repository baseline: `df73cc6eb6eefb4f15a1cc3659e2e80234e60685`.
Graphify supplied navigation; the following claims were checked against source.

| Existing contract | Reusable behavior | Gap for delivered alerts |
|---|---|---|
| [`camera_manager.h`](../include/camera_manager.h), [`camera_manager.cpp`](../src/camera_manager.cpp): `CameraState`, `Event`, `handleObservation()` | Separate desired/observed state, observation presence/time, connection and operation generations; serialized validation and audit | `last_seen_ms` is not recording freshness. `last_observed_ms` is owner admission time, including Unknown observations. No universal age expiry or resources; queued receive age must not be erased. |
| [`camera_profile.h`](../include/camera_profile.h): `Capabilities` | Unknown/Unsupported/Supported for start/stop/query/wake/GPS | No per-field health capability or validity. Query unsupported does not necessarily mean passive observations unsupported. |
| [`recording_manager.h`](../include/recording_manager.h), [`recording_manager.cpp`](../src/recording_manager.cpp) | Group intent/generation, enabled-peer state, pending/terminal failure and ACK separate from observation | Group completion/ACK cannot establish continuous recording. Need an independently armed monitoring episode and explicit Stop suppression. |
| [`gopro_codec.h`](../include/protocol/gopro_codec.h), [`gopro_codec.cpp`](../src/protocol/gopro_codec.cpp), [`gopro_hero12.cpp`](../src/profiles/gopro_hero12.cpp) | Bounded reassembly; Busy/Encoding/Ready query/register decode; model/firmware/API commissioning; separate encoding observations | No battery/storage/thermal requests or decode. KeepAlive runs every 3000 ms but does not sample Encoding or establish freshness. Resource register responses must not advance a shutter transaction. |
| [`dji_codec.h`](../include/protocol/dji_codec.h), [`dji_codec.cpp`](../src/protocol/dji_codec.cpp), [`dji_camera.cpp`](../src/profiles/dji_camera.cpp) | CRC/framing and exact 38-byte status; recording/mode/power; thermal byte decoded; forward status sequence validation; 1500 ms expiry and 500 ms poll schedule | `temperature` is a severity enum, not degrees. Adapter does not publish it as health. Battery/capacity/time fields are not decoded/published. Thermal >=2 makes recording Unknown, not an observed Stop. |
| [`insta360_x5.cpp`](../src/profiles/insta360_x5.cpp), [`insta360_protocol.md`](insta360_protocol.md#implemented-ce80-display-interpretation-19) | Qualified X5 CE80 display observations with 5000 ms freshness; elapsed Recording and exact captured settings Stopped | Remaining-runtime/count display is not a battery percentage or guaranteed storage estimate and never proves Stop. No battery/thermal alert contract. |
| [`handlebar_control.h`](../include/handlebar_control.h), [`handlebar_control.cpp`](../src/handlebar_control.cpp), [`status_led.cpp`](../src/status_led.cpp) | Coherent owner-copied control/group/local status; Error > Recovery > Partial > ordinary display; bounded LED service | `LedHealth.devices` describes local devices, not camera resources. All freshly Stopped peers can display Ready despite Recording intent; alerts need a separate camera-health overlay. No acknowledgement gesture or haptic backend. |
| [`camera_event_logger.h`](../include/camera_event_logger.h), [`telemetry_admission.h`](../include/telemetry_admission.h), [`log_format.md`](log_format.md#correlated-camera-events-opt-in-telemetry-version-3) | Opaque peer/session/intent/generation correlations; CameraInbox capacity 16; admission quota 2; loss accounting; no storage access from BLE callbacks | CameraV3 events lack health fields/alert transitions. Schema extension must preserve existing CameraV3 and MotionV4/V5 byte contracts and parser behavior. |

**Source/documentary** means primary API/schema facts or attributed research,
not proof that RideSync delivers alerts. **Synthetic** means independently expected
fixtures/software tests. **Observed** means recorded device behavior; **Confirmed**
means the declared physical criterion passed. Retained X5 probe/display evidence
supports that specific route only. There are no observed or confirmed RideSync
health-alert results for any model.

## Dated primary-source ledger

All external sources below were checked October 13, 2026. No upstream code or
captures are copied into this investigation.

- GoPro [compatibility table](https://gopro.github.io/OpenGoPro/docs/): HERO12
  Black API baseline v01.10.00; installed firmware still must be recorded.
  Model-specific features require separate qualification.
- GoPro [status IDs](https://gopro.github.io/OpenGoPro/docs/ble/statuses/) and
  [query/register operations](https://gopro.github.io/OpenGoPro/docs/ble/query/):
  targeted Get Status `0x13`, Register `0x53`, update `0x93`; an empty ID list
  selects all statuses, so use an explicit list. Newer 2-byte-ID variants are
  not silently adopted for the existing HERO12 profile.
- GoPro's pinned [official SDK status definitions](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/python/sdk_wireless_camera_control/open_gopro/api/ble_statuses.py)
  specify battery-present 1 (boolean), overheating 6 (boolean), encoding 10
  (boolean), remaining video time 35 (big-endian uint32 seconds, mode dependent),
  SD remaining 54 (big-endian uint64 kilobytes), minimum poll period 60
  (big-endian uint32 milliseconds), battery percentage 70 (uint8), and storage
  error fields 111/112. The cumulative error count 112 does not prove a current
  card fault; defer write-error alerts from v1. Pin width/range/enum fixtures
  before coding rather than relying on partially rendered documentation tables.
- DJI's pinned official [status data layout](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/92fe23e5a749f189593f980a26a105c3bb66aa1c/docs/protocol_data_segment.md)
  and [protocol envelope](https://github.com/dji-sdk/Osmo-GPS-Controller-Demo/blob/92fe23e5a749f189593f980a26a105c3bb66aa1c/docs/protocol.md):
  `1D/02` has remaining capacity at payload offset 15 (uint32 MB), remaining
  recording time at 23 (uint32 seconds), thermal severity at 30 (0 normal,
  1 warning, 2 recording blocked, 3 shutdown), battery at 37 (uint8 0–100).
  Unknown severities remain Unknown. Neither decimal/binary MB conversion nor
  missing-media sentinels are established here; retain named units, no guessed
  conversion. Model table and commissioning prerequisites are retained in
  [DJI profile research](dji_camera_profile.md); no universal firmware floor is
  published in that baseline.
- Insta360's official [GO 3S/Action Pod guide](https://onlinemanual.insta360.com/go3s/en-us/camera/basicuse/go3s_actionpod)
  and [ONE RS remote guide](https://onlinemanual.insta360.com/oners/en-us/camera/connect/gpsremote)
  describe product/remote use, not a RideSync health wire schema. Existing
  [GO 3S](go3s_profile.md), [ONE RS](one_rs_profile.md) and
  [X5 receive investigation](insta360_protocol.md#published-ce80-capture-compared-with-retained-x5-data-19)
  retain pinned community reports and local evidence; do not transfer X4/X5
  display meanings to another model, or camera/Pod battery values to each other.

## Per-model feasibility matrix

“Candidate” below means documented or retained observation path, requiring the
planned integration. “Unknown” means semantics/evidence missing. “Unavailable”
means a qualified field temporarily cannot be sampled. “Unsupported in v1” is a
software policy, not proof that a camera lacks the physical capability.

| Exact model | Unexpected stop / stale evidence | Battery | Remaining storage/time | Thermal | Prerequisites and delivery evidence |
|---|---|---|---|---|---|
| X5 | Candidate from qualified CE80 display; stale >5000 ms; no generic query heartbeat | Unknown; unsupported in v1 | Remaining display decoded, semantics insufficient for resource threshold; unsupported in v1 | Unknown; unsupported in v1 | `X5CapturedDisplayV1`, commissioned identity/CE80 route and fitted firmware; retained display/probe results include 1.11.10. Production alert operation not observed. |
| GO 3S | Unknown; existing ACK leaves Recording Unknown. Report monitoring unavailable, not an unexpected stop | Unknown camera vs Pod; unsupported in v1 | Unknown; unsupported in v1 | Unknown; unsupported in v1 | Commissioned camera/Pod and FFFrame profile; exact firmware required. Existing software is experimental, receive-state evidence unresolved. |
| ONE RS | Unknown; BE80 writes have no authoritative recording observation. Monitoring unavailable | Unknown; unsupported in v1 | Unknown; unsupported in v1 | Unknown; unsupported in v1 | Exact Core/lens/firmware and qualified BE80 route; query Unsupported. Do not inherit X5 decoder. |
| HERO12 Black | Candidate from Encoding in qualified video control; stale timer/refresh must be added | Candidate IDs 1/70 | Candidate IDs 35/54, valid mode/units required | Candidate ID 6, no temperature in degrees | >=v01.10.00 documentary baseline; commissioned exact firmware and returned API version, pairing/claim/subscriptions each link. Source/synthetic recording path; no physical alert qualification. Selected first resource model. |
| DJI Action 4 | Candidate from `1D/02`; stale >=1500 ms | Candidate offset 37 | Candidate offsets 15/23 | Candidate enum 30 | `DJI_ACTION4`, wire ID FF33; per-model firmware/identity/pair approval and ordinary-video profile; source/synthetic only. |
| DJI Action 5 Pro | Same candidate, independently qualify | Same candidate | Same candidate | Same candidate | `DJI_ACTION5_PRO`, FF44; no universal minimum firmware established; source/synthetic only. |
| DJI Action 6 | Same candidate, independently qualify | Same candidate | Same candidate | Same candidate | `DJI_ACTION6`, FF55; same declared commissioning, source/synthetic only. |
| DJI Osmo 360 | Candidate only in documented video modes; stale >=1500 ms | Same candidate | Same candidate | Same candidate | `DJI_OSMO360`, FF66; model-specific mode mapping, fitted firmware unknown; source/synthetic only. |

DJI Nano and other GoPro/DJI/Insta360 models are outside these profiles. Each
resource field stays Unknown until a valid model-qualified sample is accepted.
An unsupported request/result can mark that field Unsupported for the qualified
profile; a timeout/disconnect cannot do so. Keep other supported fields working.

## First version policy

Defaults below are proposed software choices, not measured detection guarantees.
An opt-in health policy monitors enabled peers only. No additional record command,
automatic reconnect, radio-role switch, wake or mode change is generated by alerts.
Existing user-triggered control/recovery contracts continue to own such actions.

### Intent, freshness and recovery

- Arm an episode only for an **admitted explicit Recording intent**, tied to its
  group generation and peer connection. ACK alone does not arm “was recording”.
  A fresh Recording sample arms unexpected-stop detection. A fresh Stopped
  sample after that, with no subsequently admitted Stop/cancel/reset/seal,
  immediately raises `UnexpectedStop`. A manual camera stop is unexpected to
  RideSync; the alert does not claim its cause.
- Before a first Recording observation, show pending within existing command
  deadlines, then `RecordingUnconfirmed` on terminal failure or expiry. This is
  distinct from a camera that was observed recording and later stopped. Peers
  with unsupported observation expose `MonitoringUnavailable` immediately rather
  than wait forever. Refused intents do not replace an existing episode.
- Admitted Stop disarms recording-loss detection immediately, even while queued;
  Stop failure remains an existing control error. Resource alerts remain active.
  Direct per-peer requests must participate too; otherwise a serial Stop could
  look like an unexpected stop. Correlate admitted audit intent IDs, never infer
  intent solely from the manager's desired state (set when a command starts).
- Use adapter-validated connection, sequence, receipt age and field validity.
  For X5 expire strictly after 5000 ms; DJI at 1500 ms, matching current profiles.
  For HERO12 propose Encoding refresh period `P=max(5000 ms, reported minimum
  poll period)` and stale at age >=`2P+1000 ms`. Require a known, valid bounded
  period before enabling polling; otherwise mark monitoring Unavailable. Reject
  a period >30000 ms as outside the declared v1 monitoring budget. KeepAlive,
  ACK, Busy, Ready, duplicate/out-of-order data and unrelated fields never refresh
  recording time. Notification silence alone is not Stop.
- During an armed episode, expiry/disconnect raises `RecordingEvidenceLost` and
  current Recording becomes Unknown. Retain the last known value as historical
  only. Reconnect requires a new generation/current sample; it never replays
  Recording intent. Fresh Recording clears active stop/lost/unconfirmed alerts;
  fresh Stopped clears lost evidence but leaves the stop episode unresolved until
  a user Stop/new intent. Reset retires all episodes, ACKs and polls without replay;
  a later explicit intent starts fresh. Log retirement separately from recovery.
- A field's freshness is independent of its numeric value. HERO12 resource
  refresh `R=max(15000 ms, minimum poll period)`; expiry >=`2R+1000 ms`.
  Absent/invalid/expired battery is Unknown, never 0%. Battery-present false makes
  percentage Unavailable, not “empty battery”. Unsupported mode makes remaining
  video time Unavailable; do not turn photo/timelapse capacity into video minutes.
  Stale previous faults remain unresolved history, with current validity Unknown;
  their current output becomes Partial, not a claim of current critical illness
  or healthy recovery. Only fresh evidence can assert healthy recovery.
  uint32 subtraction is valid
  only for declared service gaps <2^31 ms; longer discontinuity invalidates data.

### Thresholds and priority

| Alert | Enter | Clear with fresh evidence | Priority/output |
|---|---|---|---|
| UnexpectedStop / RecordingUnconfirmed | Conditions above | Recording, or explicit episode retirement | Critical / Error |
| RecordingEvidenceLost | Expiry/disconnect during episode | Current qualified observation; Stopped can reveal unresolved stop | Warning / Partial |
| BatteryLow | <=20%; critical <=5%, one valid sample | Warning >=25%; critical downgrades >=10%; two distinct samples >=5 s apart | Warning/critical |
| StorageLow | Video seconds <=300; critical <=60 in qualified ordinary video | Warning >=420; critical downgrades >=120; two samples >=5 s apart | Warning/critical |
| Overheating | HERO12 true, one valid sample | Two false samples >=5 s apart | Critical / Error |
| Resource/monitoring unavailable | Qualified field loses availability, or monitoring unsupported during Recording intent | Qualified valid field; unsupported stays explicit | Status; Partial for monitoring unavailable, serial-only for resource unavailable |

SD kilobytes are diagnostic status in v1; do not estimate video time from bitrate,
invent percent-full without total capacity, or claim zero means card failure.
When no valid remaining-time value exists, storage thresholding is Unavailable.
Hysteresis sample count resets on missing/stale data; duplicates cannot clear.
No degrees-based thermal threshold is proposed. A later DJI extension maps enum 1
to Warning and 2/3 to Critical independently of Recording becoming Unknown.

Application/supervisor faults and critical camera alerts select Error first;
existing Recovery follows; recording-evidence/resource warnings then select Partial;
only after those checks may ordinary group Ready/Recording display. Retain all
peer reasons in serial status; deterministic presentation tie-break is severity,
recording loss before resource status, then lowest peer slot. This priority is
an intentional extension of `selectLedState`, not current behavior.

V1 has **no rider acknowledgement control**: current Critical stays visible until
recovery or episode retirement; warning stays while unhealthy. There is no new
button mapping and no destructive “acknowledge by stopping” behavior. Serial
prints Enter/SeverityChanged/Recovered/Retired once per transition, with a summary
on explicit STATUS. Future acknowledgement must target peer+episode+alert, retain
active severity, reset on escalation/new episode, and never cause shutter IO.
Keep this distinct from camera protocol ACKs.

## Architecture, bounded IO and correlated evidence

Add a pure `CameraHealthEvaluator` with a fixed per-peer field snapshot and alert
state. The serialized application owner ingests validated observations after
adapter/manager service and evaluates once per pass; `HandlebarControl` and serial
copy the same snapshot. BLE callbacks never evaluate alerts, print, touch SD or
mutate manager state. Extend per-field capability/validity separately from
`Capabilities::query`; preserve source qualification and profile version.

HERO12 keeps existing Encoding notifications. Register only qualified new resource
IDs, once per connection, with bounded registration attempts. Refresh Encoding
with a targeted query; resource refresh batches only supported fields. Reuse
matching outstanding query responses; no duplicate request. Reserve one background
transaction per peer, no queue behind control. Explicit Start/Stop/Query and required
KeepAlive have precedence. If control starves health polling, show stale Unknown
rather than delaying commands or silently stretching freshness.

Budget for four live BLE peers (registry capacity remains eight): at most four
scheduled health requests per peer per 15 s at minimum periods, plus one bounded
initial resource registration per connection. Aggregate scheduler admits at most
one background request per 500 ms, fair rotation; no catch-up burst. One request
has <=1000 ms deadline and no retry until its next normal period. Failures count
and can mark data Unavailable; they do not retire a functioning control link merely
for a rejected optional field. Notification floods keep only newest validated
field values in fixed inbox slots; coalesced/dropped counts are visible. Edge
history can be lost under flood and must be labeled incomplete, not claimed
lossless. Preserve existing adapter/host notification caps and control progress.
All status queries to one HERO12 share its reported minimum-period spacing;
co-batch Encoding with resources when both are due. Required control queries take
precedence and restart that spacing, so health never adds an early extra query.

Incremental RAM target: <=256 bytes per registry peer x8 =2048; bounded health
transition ring 16x<=96 =1536; scheduler/scratch <=512; **total <=4096 bytes**,
no heap/task/stack growth dependent on notification count. These are design caps,
not measured sizes. Verify actual structure sizes, target map and stack before
activation. Existing transport buffers/storage queues are reused; any expansion
must fit this incremental budget or require an explicit revised scope.

Implementation resolution, October 13, 2026: retain the 4096-byte target cap and
all qualified receipt/order evidence, but deliver eight transition slots instead
of the illustrative sixteen-slot allocation above. Issue #67 permits up to sixteen.
The ring remains the sole health history queue, drains at most two per owner pass,
and reports earlier overflow explicitly; current peer alerts are independent of
history capacity. The [runtime delivery](camera_health_runtime.md) records measured
layout/stack accounting and the reproducible budget check. Health uses version 7
because the concurrent bookmark implementation occupies version 6.

Define a new opt-in health log layout/version without reusing occupied versions
1–5. Add typed health events rather than overloading a recording observation or
command ACK. Preserve GPS/camera/motion row contracts. Each transition carries
session/opaque peer/model, group and intent ID (or explicit none), connection and
operation generation (or none), health episode/sequence, reason/severity,
capability/validity, measured value+unit, sample receipt+age, and thresholds/profile.
Use owner receipt time unless actual radio/acquisition time exists; do not fabricate
it. Log recovery and reset/retirement with the same correlation. Bound admission
to two health transitions per pass and keep control/GPS quotas independent.
Coalesce repeated unchanged alerts, retain current status even if logging drops,
and expose transition/inbox/storage loss with saturating counters. No raw MAC,
camera serial or arbitrary notification body enters logs.

## Alternatives and dependencies

1. **Selected: passive evaluator + HERO12 resources, existing outputs.** Best
   documented firmware/API prerequisite and independent unit/type evidence;
   requires codec, owner freshness and versioned logger integration.
2. **DJI resources first.** Official packet already includes all resources and
   could reuse the 500 ms status path without new polls. Attractive follow-up,
   but fitted model/firmware is unknown, per-model qualification is required, and
   current thermal decode is not an alert feature. It does not resolve GO 3S/RS.
3. **All models plus haptics now.** Reject for v1: missing Insta360 receive
   evidence and new electrical/gesture qualification would widen the boundary.
   Future haptic output needs bounded sink, GPIO conflict/polarity/current checks
   in #30 and a rider notice test; LED/serial permit software delivery first.

Reuse existing control/no-replay, source-qualified adapters and bounded telemetry.
New schema/parser support, resource fixtures, optional-field failure isolation
and owner-visible current freshness are software dependencies. No hardware run
is required to close #53, and implementation remains future work.

## Finite verification and existing physical owners

Native tests must cover: ACK-only/no first recording; fresh Recording->Stopped;
admitted versus refused/queued Stop and direct serial requests; independent peer
expiry; age equality/rollover/discontinuity; stale/duplicate/out-of-order packets;
threshold entry/downgrade/clear and invalid battery/mode/thermal values; unsupported
field versus transient Unavailable; reset/seal/reconnect/no replay; missing peer
with others progressing; transaction interleaving, optional-query rejection and
notification floods with visible loss. Schema fixtures check old bytes unchanged,
new layout roundtrip, malformed/partial rows, opaque correlations and queue loss.
The linked plan gives module-specific tests and software completion boundaries.

- [#46](https://github.com/llazzaro/RideSync/issues/46): annotate the existing
  camera runs with exact firmware/profile, camera-screen recording/manual stop,
  receipt/alert timestamps and saved-media playback; correlate representative
  resource readings with camera display. Safe natural battery/storage cases or
  declared not-run/blocked rows suffice; do not manufacture thermal shutdown.
- [#22](https://github.com/llazzaro/RideSync/issues/22): per-model status semantics,
  observation cadence, resources during recording, missing-peer/radio load and
  notice latency within the existing staged matrix. No all-model prerequisite
  for the first software implementation; no generic detection-time certification.
- [#31](https://github.com/llazzaro/RideSync/issues/31): add alert/logging load,
  stale/recovery and reset/no-replay observations to the already declared integrated
  campaign when the feature is enabled. Reuse matching observations, no new soak.

Unavailable equipment or unsafe thermal scenarios remain Blocked/Not run. Synthetic
fault injection establishes software behavior only. Software delivery leaves new
health support Experimental / Not tested until these observations are recorded.

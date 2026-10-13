# Local highlight bookmark investigation

Issue [#55](https://github.com/llazzaro/RideSync/issues/55), researched **2026-10-13**.
Repository source baseline: `48177e0` (before this documentation change).

## Decision and scope

Recommend a local-first bookmark: an explicit uppercase `MARK` serial command
admits one fixed record through the running telemetry owner, independently of
camera connectivity, recording intent, GNSS fix and UTC availability. Add an
opt-in physical mapping only after that path works. Keep current button defaults.
Export a portable marker CSV offline; require an explicit alignment mapping before
producing editor positions. Optional HERO12 HiLight dispatch is a separate step
and cannot change local admission success. Other models remain explicitly
Unsupported by the proposed native-marker implementation until evidenced.

This research and the finite plan below close the investigation boundary under
[acceptance policy](acceptance_policy.md). They implement no firmware, exporter,
editor import or native camera marker. No new issue or physical qualification is
required for this investigation. The rider's reaction delay, input processing and
camera pipeline prevent a claim of frame-accurate alignment.

## Source audit and ownership

Graphify query supplied navigation; these contracts were checked against source.
All paths below describe existing behavior, not delivered bookmark behavior.

| Source | Existing contract | Required change / constraint |
|---|---|---|
| [`button_manager.h`](../include/button_manager.h), [`button_manager.cpp`](../src/button_manager.cpp) | 20 ms debounce, 800 ms long press, 300 ms double window; short RecordingIntent, long WakeReconnect, optional double Resync (disabled by default). Release emits short; long fires once and suppresses short; held-at-boot input needs release to arm. Expiry wins at equality. | Add validated Bookmark action without changing thresholds or defaults. A double-to-bookmark mapping displaces resync and delays single REC/STOP; it must be an explicit configuration choice. |
| [`bench_console.h`](../include/bench_console.h), [`bench_console.cpp`](../src/bench_console.cpp), [`bench_console_esp32.cpp`](../src/bench_console_esp32.cpp) | 16-byte line buffer; exact uppercase commands; one pending command; a second complete line loses admission. Service drains serial in slices of 64 bytes before dispatch. Camera commands require a commissioned running camera owner. | Append MARK without renumbering existing enums. Handle it before the camera-only admission gate; retain burst/no-replay refusal. No labels or parameters in firmware v1. |
| [`handlebar_control.cpp`](../src/handlebar_control.cpp) | `submit()` and `beforeAdvance()` require camera admission and enabled group; bounded action FIFO; reset/revoke clears pending actions; all non-recording actions use group resync. | Merely adding an enum would accidentally resync or refuse local-only markers. Route Bookmark independently to telemetry; allow button polling with local telemetry Running and camera unavailable. Keep old camera action checks and recovery behavior. |
| [`session_clock.h`](../include/session_clock.h), [`log_format.md`](log_format.md) | Nonzero caller-unique session ID; uint64 session milliseconds; copied RecordTimestamp/UTC anchor; raw 32-bit clock wrap extension; 365-day duration limit; same serialized owner. Missing/expired UTC is explicit. | Reuse a single copied snapshot at marker admission, never restamp at SD write. Do not mutate clock from an ISR, BLE callback or worker. |
| [`telemetry_record.h`](../include/telemetry_record.h), [`storage.h`](../include/storage.h), [`storage.cpp`](../src/storage.cpp) | Fixed trivially-copyable record, current envelope <=792 bytes; storage SPSC queue 8 records, row <=3072 bytes, chunks 256; per-kind saturating counters. `publish(reserve=true)` refuses at occupancy 6, leaving two slots; false permits all 8. | Add explicit kind/format and bounded payload; review all kind arrays/format switches and RAM assertions. Use reserve=true for markers: no priority eviction, blocking or borrowing the protected two slots. |
| [`telemetry_admission.h`](../include/telemetry_admission.h), [`telemetry_admission.cpp`](../src/telemetry_admission.cpp), [`camera_event_session.cpp`](../src/camera_event_session.cpp) | Sole producer/context for clock and storage admission; CameraInbox 16 copied events, quota 2 per tick; worker alone touches sink. Camera-free activation finishes CameraInbox and its service branch does not call the control action. Existing session chooses CameraV3/MotionV4/MotionV5. | Local markers use a direct bounded owner method, not CameraInbox. Expose that method through local runtime/session composition with opt-in schema selection. A finished camera inbox must not disable bookmarks; move local input servicing outside the camera-only action branch. |
| [`camera_event_logger.h`](../include/camera_event_logger.h), [`camera_manager.h`](../include/camera_manager.h), [`camera_profile.h`](../include/camera_profile.h) | Camera audit correlates session, peer, intent and generations; request/attempt/ACK/observation/completion separate. Operations are Connect/Start/Stop/Query/Wake; capabilities lack marker support. | Native marker requires a separate capability and transaction correlation. Do not reuse Start/Stop or interpret RecordingObserved as marker confirmation. |

## Identity, timestamp and receipt contract (proposed)

The key is `(session_id, bookmark_id)`, with nonzero uint32 bookmark IDs allocated
in owner order starting at 1. Consume an ID for each valid owner attempt even if
storage is full; never reuse it. Refuse on exhaustion until a new uniquely
allocated session. Parser errors and inactive/invalid-session requests allocate
no bookmark ID. Reset retires all pending inputs and native attempts; the next
session restarts numbering. Persistent session allocation remains the existing
storage namespace/counter owner's responsibility.

Timestamp means **owner admission attempt**, immediately before enqueue. Serial
receipt means execution of a complete valid command, not the first received byte;
physical receipt means callback execution after the gesture is recognized, not
raw switch closure. Return the same timestamp on acceptance or refusal, and store
it only on success. V1 has no earlier edge/ISR timestamp. This makes the latency
meaning clear without guessing a historical UTC anchor.

Copy the full RecordTimestamp: monotonic quality, UTC estimate presence, anchor
quality/sequence/receipt/UTC, age and known uncertainty. Missing or expired UTC
still permits a valid monotonic bookmark. Invalid monotonic time, zero session,
duration exhaustion, wrong session, shutdown, disabled format and terminal storage
refuse. Later GNSS corrections do not rewrite previous bookmarks. Existing clock
sampling premises (<2^32 ms between samples, new session on reboot) still apply.

### Timing limitations

Let P be the maximum interval between button polls and A the delay from callback
to telemetry admission, **only if those maxima are measured for the enabled
configuration**. With no double detection, stable electrical release-to-recognition
is approximately 20 ms to 20+2P ms, plus A to record admission. Time since press
also includes the person's hold duration. With double detection enabled, a lone
short press adds the 300 ms expiry window and up to a further P. A double marker
is timed at the second debounced release; long press recognition is at least
800 ms after debounced press and up to another P. These are conditional scheduler
estimates, not measured deadlines. Bounce/reversal can extend recognition.

At 115200 baud, 8N1, `MARK\n` takes about 0.434 ms on the wire; USB/UART buffering,
serial draining, scheduling and human reaction are additional and unbounded by
this API. SD write/flush delay never changes the record timestamp and has no
universal upper bound. BLE dispatch/encoding latency and each camera's footage
start offset remain unknown. Millisecond units alone do not imply millisecond
accuracy. Do not subtract an assumed debounce, reaction or camera delay.

## Input, bounded admission and feedback

| Choice | Behavior and trade-off |
|---|---|
| Recommended first version: MARK | One line, one attempt, no camera action, no auto REC/wake/resync. Repeat separate commands intentionally creates distinct IDs, even in the same millisecond. Pasted multi-line bursts keep existing LostInput refusal; no deferred replay. |
| Opt-in double Bookmark on existing button | Preserve short REC/STOP and long wake; explicitly replace double Resync. Enable double detection, accept delayed single action, document displaced resync (retain serial QUERY/recovery controls). Refuse installation configuration that requires all four actions from three gestures. |
| Separate qualified marker button | Can preserve every existing gesture and use immediate short-release Bookmark, but adds pin/wiring qualification and another input manager. Defer unless the rider needs simultaneous physical resync; no new default pin. |
| Reject automatic long/triple overload | Long is already wake; triple would need a new ambiguous state machine. No automatic gesture selection or hidden REC/STOP behavior. |

Both inputs call one `BookmarkAdmission` owner API. Keep at most one marker attempt
per input per owner pass; no separate queue or heap allocation. The serial pending
slot and gesture state are the only input buffering. Do not place markers in the
camera-action FIFO. A held button emits no repeats. Legitimate repeated recognized
gestures get distinct IDs; electrical bounce is handled by existing debounce.

At occupancy >=6, drop the new marker, report Overflow and increment its per-kind
loss counter; existing rows stay intact. Distinguish Inactive, DisabledFormat,
InvalidTime, Exhausted, Overflow and StorageUnavailable in the owner result.
Do not retry on space recovery, camera reconnect, clear, reset or restart.
Loss counters saturate and a gap in IDs is not a complete durable loss ledger:
a failed final marker or power cut can leave no later row exposing the gap.

Immediate serial feedback reports result, session/key and monotonic time:
`admitted` means copied into the RAM queue. STATUS exposes Bookmark accepted,
dropped/rejected, written/flushed/lost counters, not a per-marker durability ACK.
Worker failure after admission can lose markers. Reuse existing flush policy;
never flush synchronously to provide feedback. Optional LED acknowledgement is a
150 ms nonblocking overlay for local admission, below Error/Recovery/Partial
priority; dropped markers get serial refusal/counters, not a success pulse.
A hidden pulse under higher priority is not a failure to record. No haptic device
or camera-success implication is added.

## Record and offline sidecar contract (proposed)

Choose a new explicit **BookmarkV6** CSV envelope, opt-in only. It retains all
MotionV5 column meanings and adds a Bookmark row payload; old GpsV1/MixedV2/
CameraV3/MotionV4/MotionV5 byte contracts remain unchanged. This is a proposed
version name, not an available storage enum. Schema coordination with the
[ride export investigation](ride_log_export.md) is required before implementation;
do not silently change its five-format parser contract.

Payload: uint32 `bookmark_id`, uint8 `source` (1 serial, 2 physical), uint8
`gesture` (0 serial, 1 short, 2 long, 3 double), plus reserved zeros. Target a
<=16-byte fixed C++ payload; serialize named numeric fields, never struct memory.
No strings, filenames, target-peer list or clip timestamp in firmware v1.
Keep the existing envelope ceiling unless a measured size review justifies an
explicit replacement bound. Adding a separate field could increase each of eight
queue slots by up to 16 bytes (128 bytes total), plus any scratch envelopes; a
union refactor is not a prerequisite and would need its own review. Row bound
stays <=3072 with a longest-row fixture. GPS/IMU/camera rows have empty bookmark
columns; Bookmark rows carry common timestamp plus payload, with unrelated
payload columns empty. Bookmarks before REC or after camera STOP remain valid
within the running local session. SHUTDOWN refuses new admission.

Offline output is `ridesync-markers-v1.csv` with a versioned fixed header:

```text
schema,session_id,bookmark_id,monotonic_ms,source,gesture,anchor_quality,anchor_sequence,anchor_receipt_ms,anchor_utc_ms,anchor_age_ms,uncertainty_known,uncertainty_ms,utc_estimate_ms
```

Each row has schema `ridesync-markers-v1`; IDs and times are decimal integers,
not floating point. UTC estimate is empty unless present; absent anchor fields
are empty, uncertainty_known is 0/1 and uncertainty_ms is empty when unknown.
UTC quality uses Missing/Fresh/Expired. Monotonic quality must be Valid to export.
ASCII, comma delimiter, LF newlines, no free text; one session per output. Reject
duplicate keys, zero IDs, invalid fields, session mismatch or malformed middle
rows. Allow only an explicitly reported unterminated final power-cut row to be
excluded. Preserve equal-time markers in ID order; do not deduplicate by time.
Stream within fixed row/memory limits; preserve source hash, format/version,
input/excluded counts and available loss counters in a report. Lost rows cannot
be reconstructed. Create outputs exclusively, never truncate inputs/destinations.

### Footage mapping and an actual editor workflow assessment

An additional alignment file identifies session, clip (user-supplied path plus
hash), clip duration, session milliseconds corresponding to clip zero, project
placement seconds, and alignment method/uncertainty. Unknown mapping stays absent;
UTC, shutter ACK and REC intent cannot fill it automatically. For normal-speed
continuous footage:

```text
clip_seconds = (marker.monotonic_ms - clip_zero_session_ms) / 1000
project_seconds = clip_project_start_seconds + clip_seconds
```

Keep out-of-clip markers in the portable sidecar and report their exclusion from
editor placement; never clamp to clip start. Separate camera clips/chapters need
separate mappings. Speed ramps, dropped footage, pauses and drift need multiple
anchors or a qualified time transform and are outside v1. Example (synthetic
arithmetic, not an import test): marker 12500 ms, clip zero 10000 ms, project start
30 s gives clip 2.5 s / project 32.5 s; no mapping yields no editor position.

Primary editor sources consulted on 2026-10-13:

- [REAPER User Guide](https://www.reaper.fm/userguide.php) and the
  [v7.36 guide](https://www.reaper.fm/userguide/ReaperUserGuide736c.pdf),
  Region/Marker Manager: CSV/TXT marker import with merge/replace workflows.
- [REAPER API](https://www.reaper.fm/sdk/reascript/reascripthelp.html#AddProjectMarker2):
  explicit project marker position/name and error result; suitable for a bounded
  optional importer once alignment is supplied.

Assessed workflow: put a clip on a project timeline, establish its offset using
an observable sync event, then merge markers through Region/Marker Manager or a
small ReaScript. This supports the editing need but generic CSV is not necessarily
REAPER's CSV dialect. Therefore choose the neutral contract above first; derive
an optional REAPER output from an exported template from one pinned installed
version, or use its documented API. Keep marker names generated from the key,
preview placement and avoid overwriting existing markers. Actual import/position
checks are implementation step 4. No editor was run in this investigation and
no compatibility with Resolve, Premiere, Final Cut or Insta360 Studio is claimed.

## Native camera highlights: dated primary-source findings

Sources consulted **2026-10-13**; model/API/firmware applicability must be frozen
when adding a route. Native support is separate from camera-independent markers.

| Model | Documentary finding | Proposed support disposition |
|---|---|---|
| HERO12 Black | [Open GoPro Hilights](https://gopro.github.io/OpenGoPro/docs/ble/hilights/) defines Hilight Moment only during encoding. [Official Kotlin implementation](https://github.com/gopro/OpenGoPro/blob/0f963572611c4410a15678531e9681a6ff874edb/demos/kotlin/kmp_sdk/wsdk/src/commonMain/kotlin/com/gopro/open_gopro/operation/commands/HilightMoment.kt) uses the dedicated TLV command/response. [SDK changelog](https://gopro.github.io/OpenGoPro/python_sdk/changelog.html) includes HERO12 support. | Documentary candidate for optional extension; RideSync has no marker operation/codec/confirmation. Do not advertise working support before fixtures and the bounded physical row. |
| X5 | [Official Ring Remote guide](https://res.insta360.com/static/da29b83ad8828633ce1136cf76b35923/Insta360%20Ring%20Remote%20QSG-V1.3.pdf) limits its Mark Video feature to Ace/GO series, even though the accessory also discusses X5. Accessory behavior is not a public BLE marker contract. | Unsupported in RideSync; exact X5 native marker opcode, response and saved-clip evidence unresolved. Do not copy another model/accessory's opcode. |
| ONE RS | [Official GPS remote tutorial](https://onlinemanual.insta360.com/oners/en-us/camera/connect/gpsremote) documents remote connection/video operation, not an evidenced marker transaction. Existing BE80 route proves no marker semantics. | Unsupported; native protocol unresolved. |
| GO 3S | Ring Remote's GO-series statement does not specify RideSync FFFrame encoding or ACK/marker evidence for GO 3S. | Unsupported; accessory marketing is insufficient to enable a protocol. |
| DJI Action 4/5 Pro/6, Osmo 360 | Existing [`dji_codec.h`](../include/protocol/dji_codec.h) and [`dji_camera.cpp`](../src/profiles/dji_camera.cpp) supply no marker command. This audit established no per-model primary marker wire contract. | Unsupported in proposed v1. This is an evidence gap, not proof that the camera lacks a native feature. |

For an optional HERO12 route, take at most one pending request per peer, keyed to
the local bookmark, connection and operation generation. Dispatch only after local
admission and with fresh observed Encoding plus a ready qualified connection;
Stop/reset/shutdown retires pending attempts. No wake/start/reconnect/replay or
retry after an ambiguous timeout (a retry could duplicate a saved tag). Never let
marker traffic delay STOP or keepalive. Busy/refused/Unsupported is reported per
peer without erasing the local marker. Codec bytes must come from the pinned
schema/SDK in step 5; this note does not invent framing.

Keep distinct evidence states: local RAM admission; optional wire attempt;
matching successful command response (WireAck); camera-side marker observation;
and saved-clip confirmation. [Open GoPro statuses](https://gopro.github.io/OpenGoPro/docs/ble/statuses/)
list 58 Active Hilights and 59 Time Since Last Hilight, but neither contains the
RideSync bookmark key. A count/time change could be another controller's marker;
record it as uncorrelated observation unless correlation is independently
established. Saved media inspection is needed for hardware-confirmed inclusion.
A local timestamp cannot be sent as a historical encoding timestamp with Hilight
Moment, so delayed dispatch marks the camera's current moment, not the old input.

## Dependencies and finite implementation plan

Steps 1–4 are local bookmark delivery. Step 5 is optional per-model work, not a
prerequisite. Existing telemetry/session allocation, commissioned sink/isolated
worker, opt-in button wiring and old schema evidence are reused. The future GPX
exporter is not required; share validated row parsing if it exists, otherwise
implement only the bounded parsing needed here. Coordinate version allocation
with other schema plans before coding. No firmware changes are made by this note.

| Step | Behavior and affected modules | Verification | Completion boundary |
|---|---|---|---|
| 1. Bounded local contract | Add Bookmark kind/payload, explicit BookmarkV6 opt-in, per-kind counters, owner admission/result and source/gesture validation in telemetry_record, storage, telemetry_admission, camera_event_session/local_telemetry_runtime. Update log-format documentation. | Independent expected CSV fixtures; missing/expired UTC; copied anchors after correction; same-ms IDs; wrap/duration/exhaustion; reset/wrong session; invalid source; occupancy 5 admits/6 refuses; stopped/terminal sink; no allocations; measured sizeof/longest row; old-format fixtures unchanged. | One owner can accept/refuse and serialize camera-free bookmarks within reviewed memory bounds, without altering old logs or storage ownership. |
| 2. Serial path | Append MARK in bench_console and dispatch via bench_console_esp32 before camera gate; status exposes marker counters/result. | Native parser tests for exact MARK/LF/CRLF, lowercase/overlong/invalid, pasted burst LostInput, no replay; local Running with no camera/no UTC; inactive/shutdown refuses; existing REC/STOP/WAKE/QUERY remain unchanged. Compile only newly affected pinned bench application targets. | Usable opt-in serial local marker with truthful admission feedback; no new physical claim. |
| 3. Opt-in physical mapping | Extend button validation, handlebar_control owner routing/polling, camera_event_session/local runtime camera-free input servicing and qualification/configuration; optional bounded LED overlay in status_led. Keep settings defaults and camera FIFO policy. | Debounce/hold-at-boot/bounce, single/double equality, long suppresses short, repeated gestures, camera loss, camera-free polling, reset pending-short retirement, old default actions, priority and overflow/refusal feedback. Build affected targets and check format. | One explicitly selected mapping works in synthetic tests, defaults stay unchanged, incompatible mappings refused. Installed gesture timing remains with #31; wiring qualification with existing #30 owner. |
| 4. Portable export and one editor check | Add streaming offline marker parser/CSV/report, explicit clip mapping, optional REAPER adapter and fixtures in tools/scripts; usage documentation. | Integer boundaries/duplicate key/malformed middle/truncated tail, missing UTC, equal times, negative/out-of-clip positions, unknown offsets, session changes, pre-existing outputs unchanged; large-input bounded memory. In one pinned REAPER version, export a template, merge three synthetic aligned markers at known seconds, inspect positions/names and existing-marker preservation; record version/hash and expected/observed results. | Deterministic portable sidecar and one recorded editor import, or explicit editor blocker; no generalized editor certification. |
| 5. Optional HERO12 native extension | Separate capability, token/correlation, pinned Hilight codec, adapter/service dispatch and camera audit event schema in camera_profile, gopro_codec, gopro_hero12, camera_manager/runtime and logger. Reconcile any further schema change before delivery. | Official expected request/response fixture, wrong/stale ACK, Busy/non-Encoding/Unknown refusal, timeout without retry, STOP priority, reset/session/connection cancellation, unsupported peers and local marker retention during camera loss. Build affected pinned targets. | Experimental software path only after focused checks; native marker confirmation requires affected #46 row and no promotion of other models. |

### Only newly affected physical checks

- **#31:** after steps 1–3, one declared installed-input/SD run measures poll and
  gesture-to-owner delay under the enabled load, marks three intentional events
  (including repeated presses), checks saved IDs/times against serial receipt,
  and verifies REC/STOP plus wake/resync for the selected mapping. One camera-loss
  interval proves local marking continues; one reset proves no input replay.
  Reuse existing SD fault/power-cut cases, inspect marker loss at their tested
  boundaries rather than launching a new durability campaign. None run here.
- **#46:** only if step 5 is enabled, one qualified HERO12 recording: three native
  marker attempts, annotated request/response evidence and saved-clip marker
  inspection; one stopped-camera refusal and one disconnect with no reconnect
  replay. Measure input-to-tag discrepancy where observable, retain Unknown
  alignment otherwise. Other models need a resolved protocol first; no blanket
  camera matrix rerun is required by local-only marking. None run here.

## Investigation acceptance and unresolved capabilities

**Documented/source-audited:** ownership/gesture constraints; bounded local
recommendation; explicit schema/export/mapping contracts; Open GoPro candidate
and one real editor's documented workflow. **Synthetic:** the arithmetic example
only; planned firmware/export fixtures have not been executed. **Observed:** no
RideSync bookmark recording or editor import performed. **Hardware-confirmed:**
none added; prior evidence retains its original scope.

Unresolved: installed timing maxima, footage start offsets/drift, editor dialect
until a pinned import check, per-model native commands beyond the documentary
HERO12 candidate, and unambiguous online marker-to-command correlation. Arbitrary
frame-accurate placement and universal power-loss durability are infeasible claims
from the current host clock/input/storage contracts. These limitations do not
block the local-first decision or close any implementation/physical work.

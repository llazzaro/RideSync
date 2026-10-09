# Insta360 BLE research

Research baseline: 2026-10-07; bench observations updated 2026-10-09. Limited
X5-associated BE80 discovery, CE80 pairing/subscription and one owner-confirmed
wake have been observed with isolated diagnostics. A card-ready retry on
owner-reported X5 firmware 1.11.10 established one remote recording start, a
playable clip after manual stop. A later probe session completed three
camera-observed remote Start/Stop cycles and reconnect. The production X5
adapter and authoritative state decoding remain unimplemented; camera-dependent
follow-up checks are consolidated in [#46](https://github.com/llazzaro/RideSync/issues/46).
External reports are community reverse engineering, not confirmation for our
X5/GO 3S/ONE RS firmware versions. [Sources and licensing](sources.md).

## GO 3S and Action Pod feasibility (#6)

**Finding: unresolved; no RideSync GO 3S or Action Pod capture has been obtained.**
This is not evidence of incompatibility. No firmware versions, pairing state,
advertisement, GATT database, command, notification or recording-state packet
has been observed locally. No fixture is available; see
[`test/fixtures/go3s/README.md`](../test/fixtures/go3s/README.md).

Official Insta360 support material establishes product behavior, not the BLE
service protocol. It says a connected Action Pod supplies live footage and
remote shooting control; with GO 3S removed, the Pod can control and preview
the camera over Bluetooth up to 5 m. When seated in the Pod, camera buttons are
disabled and control is through the Pod buttons or app. Pairing guidance says
power on both, remove the first-use rear sticker, insert the camera, and confirm
the Pod displays live footage; incompatible firmware prompts an app update.
The support material describes waiting 10–15 seconds for connection, cleaning
the contacts, resetting both devices with a seven-second button hold after
failure. The firmware FAQ describes updating the camera; this investigation
requires recording camera and Pod firmware independently, though no Pod version
was available to inspect here.
See [Action Pod connection](https://onlinemanual.insta360.com/go3s/en-us/operating_tutorials/connect/actionpod),
[using GO 3S and Action Pod](https://onlinemanual.insta360.com/go3s/en-us/camera/basicuse/go3s_actionpod),
[connection FAQ](https://onlinemanual.insta360.com/go3s/en-us/faq/operationtutorials/connection),
and [firmware guidance](https://onlinemanual.insta360.com/go3s/en-us/faq/operationtutorials/firmware).

These sources do **not** identify which unit advertises or accepts a third-party
BLE connection, publish GATT UUIDs, pairing/bonding requirements for such a
controller, or map recording transitions to bytes. A Bluetooth control/preview
link between camera and Pod is documented; whether an ESP32 can control the
camera as a BLE peer, whether the Pod is the control target, and whether either
unit exposes CE80/BE80 are **unknown**. Camera/Pod owner, address identity and
firmware-specific behavior must be captured. Nothing in the X5 community
CE80/BE80 reports can be transferred to GO 3S.

**Operation evidence:** no GO 3S start, stop, toggle, mode-selection or
authoritative state operation is currently evidenced for RideSync. The
community shutter frame elsewhere in this document remains a reported toggle
for another target, not a GO 3S command. A UI change, BLE write accepted by a
stack, elapsed-time heuristic or file appearing later is insufficient alone to
claim a command/state mapping. Require repeatable packet correlation with the
camera/Pod recording indicator and saved media; classify each operation and
firmware combination independently. The recording procedure and exact gaps are
in [validation](testing.md#go-3s-and-action-pod-evidence-protocol).

## Separate BLE roles

Community [ESP32 example](https://github.com/pchwalek/insta360_ble_esp32)
implements an emulated GPS remote peripheral: CE80 service, CE81 camera writes,
CE82 remote notifications, and CE83 remote information. Cameras initiate the
connection. UUIDs use the Bluetooth base UUID; discover peer attributes rather
than hard-code ATT handles. Pairing, subscription and reconnect requirements
must be captured independently for each target model.

[insta360ctl](https://github.com/xaionaro-go/insta360ctl) also describes direct
camera control using camera-hosted BE80, BE81 writes and BE82 notifications.
Its framing is separate from CE80. We have not validated its compatibility claims.

## Commands and state

The ESP32 community example reports a shutter button frame:
`FC EF FE 86 00 03 01 02 00`. Treat it as a **toggle/button event**, not an
idempotent recording-start command. Mode selection and current recording state
matter. Do not transmit it repeatedly after a lost response: that can stop a
successful recording. `insta360::encodeShutterEvent()` now returns this exact
nine-byte event as an owned fixed-size array. The independent literal fixture
and pinned MIT source/license provenance are in
[`test/fixtures/insta360/README.md`](../test/fixtures/insta360/README.md).
This pure primitive has no BLE delivery or automatic repeat and is not an
idempotent Start/Stop API. No receive parser, ACK, sequence field or recording
observation is invented from these opaque bytes. CE80 shutter delivery still has no idempotent Start/Stop API. The implemented
CE80 display parser below now interprets known captured recording/settings
fields; its state cannot come from shutter submission alone. The later card-ready trial established a
camera-observed recording start, without decoding the incoming state packets. Other incoming data stays Unknown; retained captures support the typed parser
below, not a colon-only or missing-timer inference.

The [October 9 name-aligned trial](hardware-results/2026-10-09-x5-pairing.md)
established a real CE80 connection, CE82 subscription and CE81 writes; the owner
confirmed the remote connected on the X5 display. Compared with the preceding
zero-connection trial, only the name changed to the pinned MIT examples'
`Insta360 GPS Remote`. This supports using that name on this bench, without
establishing a universal name-filter requirement. That pairing trial sent no
shutter event. The [separate one-shot trial](hardware-results/2026-10-09-x5-pairing.md#separate-one-shot-shutter-trial)
submitted exactly one explicitly requested, addressed CE82 shutter event and
returned SDK status 0, without replay. SDK status 0 does not prove camera delivery.
The owner answered yes about an indicator/timer but also reported that the X5
had no SD card and could not record. Recording
remains unclassified; no completed recording or saved media was established.
That no-card observation was superseded by the [card-ready retry](hardware-results/2026-10-09-x5-pairing.md#card-ready-recording-retry--firmware-11110):
the owner reported firmware 1.11.10, confirmed video mode/stopped before one
shutter submission, then a running timer and a playable clip after manual stop.
A subsequent fresh-boot attempt reconnected/subscribed without a shutter
submission. The later [three-cycle session](hardware-results/2026-10-09-x5-pairing.md#three-remote-recording-cycles--owner-confirmed)
completed three camera-observed remote Start/Stop cycles, with the last Stop
submitted after reconnect. An expired-window request was refused locally; no
automatic shutter replay occurred. Incoming display framing is now classified by the published-capture comparison below;
production adapter/state integration remains unimplemented. Keep NVS refusal guards;
no event from this encoder should be sent on the unrelated BE80 path.

## Licensed BE80 control reference (#19, #5)

The October 9 audit found another licensed direct-control reference:
[Garmin BLEBarrel](https://github.com/arsfabula/Insta360-Remote-CIQ/blob/39c51b3aa7c453227831d811355899371bbb8b94/BLE%20Barrel/BLEBarrel.mc),
pinned with its MPL-2.0 licenses in [sources](sources.md). It declares distinct
18-byte video-start and stop packets, changes a one-byte sequence at offset 10,
and writes BE81 with notifications on BE82. This supplies source-level command
expectations separately from the CE82 shutter toggle. The pure source-derived request encoder and fixtures now retain MPL-2.0; no
RideSync camera profile enables this route.

Its receive callback reads a command byte and byte 17 from the first fragment,
ignores later fragments, and can label stopped after command/service errors.
Those decisions do not establish fresh authoritative recording state for
RideSync. Full framing, response-versus-observation identity and error semantics
still need independent validation. The source's ONE R 360-mod testing report
does not qualify ONE RS modules, X5 or GO 3S; no actual camera firmware is
recorded in this audit. Keep reset/disconnect/errors Unknown and do not
automatically replay an ambiguous command. #19/#5 remain open.

### Implemented pure BE80 recording requests (#19, partial)

`encodeRecordingCommand(const Be80ControlConfig&, Be80RecordingCommand, uint8_t)`
in `protocol/insta360_be80_codec.h` is a stateless, request-only codec, separate
from the existing CE82 shutter toggle. `Be80ControlConfig::profile` defaults
Disabled; `GarminBe80ControlV1` explicitly selects the pinned source's default
`cmdStartRec`/`cmdStopRec` wire literals. This is a wire-format opt-in, not a
camera-model capability or recording-state qualification.

| Command | Complete source-derived bytes (SS is caller sequence) |
|---|---|
| StartVideo | `12 00 00 00 04 00 00 04 00 02 SS 00 00 80 00 00 08 01` |
| Stop | `12 00 00 00 04 00 00 05 00 02 SS 00 00 80 00 00 10 01` |

Byte10 uses caller sequence1..254, matching the pinned `sendCMD` sequence range.
The codec does not increment, queue, transmit, retry or reset that sequence.
Other fixed bytes are retained without inventing header/framing meanings.
Success is None/size18 with fixed `std::array<uint8_t,18>` storage. Every error
has size0 and all-zero bytes. Validation precedence is Disabled profile,
UnsupportedProfile, InvalidCommand, then InvalidSequence (0/255). Unknown enum
values fail; no partial packet is exposed. No heap, driver, clock or IO is used.

The independent literal fixtures are source-derived rather than camera captures.
Tests preserve the existing shutter primitive and verify both full requests,
every valid sequence, all unknown uint8 profile/command values, invalid sequence
endpoints, zeroed errors, precedence and repeatable independent result copies.
The new header/source/fixture use MPL-2.0 with the existing pinned license text;
[license coverage](sources.md#pure-be80-recording-request-file-licensing-19).

An explicit request is distinct from a shutter toggle, but camera-level
idempotence/ACK/state semantics have not been established. Submission never
becomes an observation, and ambiguous delivery does not justify automatic replay.
No receive decoder or camera profile is enabled by this extension; reset,
disconnect and unknown responses retain existing Unknown semantics. Source ONE R
reporting does not qualify X5, fitted ONE RS modules or GO 3S. #19 remains open
for its usable target/recording-state and finite receive/malformed-frame corpus;
#3/#22 retain real camera checks. This codec is concrete progress on known fields,
not a substitute for those remaining requirements.

### Additional CE80 reference audit (#19)

The MIT-licensed [Multicam protocol source](https://github.com/dstrat28/action-multicam-remote/blob/b89cd0022e5a50b357a7fdbab154b9a5ff037e21/ActionCamRemote/Bluetooth/Insta360RemoteProtocol.swift)
was inspected at `b89cd0022e5a50b357a7fdbab154b9a5ff037e21`, together with
its root license, service and regression tests. Its detector accepts any packet
of at least 18 bytes containing a colon; the service reports Stopped after five
seconds without such a packet. A synthetic regression timer even has a different
magic prefix. These are heuristic tests, not camera-state evidence. RideSync
does not adopt this detector or infer Stopped from absent traffic.

The source also checks a six-byte ASCII identifier behind a CE81 handshake
prefix. That is a candidate peer-association fact for the annotated #46 capture,
not a qualified X5 identity parser, a BLE MAC or a recording observation. This
audit supplies no new authoritative state mapping or installed-firmware proof.
No upstream implementation or asset was copied; #19 remains open.

### Implemented BE80 receive envelope (#19, partial)

`decodeBe80Envelope(config, direction, data, size)` in the existing
`protocol/insta360_be80_codec.h` now validates one complete, explicitly opted-in
`GarminBe80ControlV1` envelope. The pinned Garmin `onCharacteristicChanged`
source documents little-endian total length in bytes0..1, signature
`00 00 04 00 00` in bytes2..6, and examines this family only at size18 or larger.
These facts qualify the envelope shape, not a camera ACK or state schema.
No upstream receive implementation is copied.

The local storage limit is 256 bytes; it is not an observed MTU, camera limit or
fragmentation rule. The decoder requires size18..256 and exact declared total
length. Short keepalives, other signatures, partial frames and concatenated
frames are rejected without exposing a prefix. It does not assemble fragments;
the future transport must separately qualify complete-frame boundaries and
fragment retirement. There is no retained decoder state, allocation, I/O or retry.

Validation precedence is Disabled, UnsupportedProfile, WrongDirection,
InvalidSize, InvalidBuffer, LengthMismatch, UnsupportedHeader. Only
`Be80Direction::CameraToRemote` is admitted; unknown enum values fail.
The direction argument must come from the caller's actual GATT route: it cannot
authenticate origin or distinguish a wrongly routed request with identical
header bytes. Size is checked before buffer access; non-null input must reference
at least size readable bytes. Every failure returns size0, zeroed fixed storage
and recording Unknown. Success returns an owned copy with a zeroed unused tail.
All remaining bytes, including command and sequence positions, stay opaque;
every result still has recording Unknown. Success proves only envelope validity,
never camera delivery, freshness, ACK, video mode or observed recording state.

The independently specified `test/fixtures/insta360/be80_envelope.h` is a
**synthetic source-specified envelope**, not captured camera data. Its opaque
bytes deliberately have no assigned semantics. Seven added behavior tests cover
admission/error precedence, owned copies, full-capacity/little-endian length,
altered signatures, all command-byte values with zero/nonzero status-like bytes,
and all accepted sizes with every proper prefix rejected. The existing five
request/shutter tests remain unchanged. The twelve codec tests passed natively
and under AddressSanitizer/UndefinedBehaviorSanitizer. No camera was operated for
this change. #19 stays open for the usable target/receive-state path; #46 owns
the annotated capture and final camera checks.

Software verification: the full repository pipeline passed (55 Python tests,
408 native cases, existing bench harnesses and C++ formatting), and the pinned
default ESP32 build passed. Fresh read-only review found no defects; its
independent warning-clean sanitizer build also passed the twelve codec tests.
Review declined to qualify physical/model behavior, reassembly/MTU, opaque
message semantics, ACK/freshness/authentication, recovery/I/O, and invalid or
concurrently mutated caller buffers; those remain outside this pure complete-frame
contract, not silently enabled capabilities. Repository and firmware acceptance
were verified separately from that focused review.

## Published CE80 capture compared with retained X5 data (#19)

On October 10, the owner requested online capture research rather than more
repeated camera runs. [TheAngryRaven's X4 capture report](https://github.com/TheAngryRaven/insta360-ble-gps-spec/blob/7964f1133e5d0f2c7eb73aaaaf5d6ebdd2127199/README.md#61-camera-state-including-recording)
describes passive-sniffer observations, six-byte framing, display message type
`0x10`, four display-control bytes and an elapsed-time text format. The report
warns that type `0x02` is not a reliable recording flag. Its repository contains
only the report, not a downloadable pcap or license; no source code, prose,
external capture or packet fixture is copied into RideSync.

Independent comparison against our two private X5 transcripts found all 305
writes have the documented prefix and exact `6 + byte[5]` frame length. All 99
`0x10` messages have four control bytes followed by text at byte 10. Strict
elapsed text `.HH:MM:SS` accounts for 69 messages (42 in the first run, elapsed
0–41 s; 27 in the follow-up, elapsed 0–11 s, including duplicates). Both elapsed
sequences are nondecreasing. Four messages contain video settings; one contains
photo settings, consistent with the completed manual sequence. Eighteen contain
remaining-runtime text; seven contain a numeric remaining count. Remaining
runtime/count is not promoted into a stopped observation.

This explains the formerly opaque length groups: eleven-byte writes are mainly
type `0x02`; fifteen-byte writes are display runtime; nineteen-byte writes include
both elapsed timers and photo settings. A length/colon search discards these
field distinctions. We now have externally documented framing and independently
matched X5 display evidence, sufficient for the implemented strict typed display decoder below, without
another generic capture run. Its model qualification,
connection freshness, command correlation and mode-safe control still require
explicit implementation; silence/disconnect remains Unknown. A display parser
is distinct from a dedicated recording-state query.

The separate [insta360ctl protocol report](https://github.com/xaionaro-go/insta360ctl/blob/f94193ce03c5af0921a9992bfd1af6bd946150d0/doc/protocol.md)
identifies BE80 capture-status command candidates but documents model-specific
GO 3 deviations and a different transport. No license is declared in the
inspected tree. It does not establish a CE80 query or X5 response mapping, and
its code/protobuf definitions are not copied. The selected next step stays on
the already evidenced CE80 route. See the [local capture record](hardware-results/2026-10-10-x5-receive-capture.md).

## Wake and identification

The [GPS specification](https://github.com/TheAngryRaven/insta360-ble-gps-spec)
reports iBeacon-like manufacturer advertisements containing a camera serial
identifier, followed by a camera connection to the remote. It reports X4
observations, not our target models. Wake identifier derivation, byte order,
advertisement timing, shutdown window and pairing prerequisites remain
unqualified across target models; the limited X5 wake observation is below.
Do not assume a BLE MAC is interchangeable with a wake serial identifier.

The [pinned MIT M5 fork's `camera.h`](https://github.com/marcelpallares/insta360-m5stick-remote/blob/c76e140396de8b2404cdd36d17cf0d1a251a9dcc/camera.h)
uses the final six characters of the advertised camera name as six bytes in
forward order, storing the BLE address separately. Its scanner accepts `X5 `;
that source-side filter does not prove X5 wake behavior or a canonical serial.
[Its wake builder](https://github.com/marcelpallares/insta360-m5stick-remote/blob/c76e140396de8b2404cdd36d17cf0d1a251a9dcc/ble_handlers.h)
specifies 26 manufacturer-value bytes: `4C 00 02 15 09 4F 52 42 49 54 09 FF 0F 00`,
then those six name-suffix bytes, then `00 00 00 00 E4 01`. Keep unknown constants
opaque. The independent MIT ESP32 example agrees on the fixed regions but
provides camera-specific suffix examples only for X3 and RS 1-inch. The M5
original and fork are one lineage, not independent camera confirmation.

These licensed source facts can specify software expectations without requiring
raw captures for every byte. Manufacturer contents are distinct from the full
serialized advertisement, which must account for AD lengths, flags, services,
name placement and the legacy payload limit in the pinned SDK. The
[October 9 wake-only bench trial](hardware-results/2026-10-09-x5-wake.md)
transmitted a manufacturer value derived from the observed X5 name; a passive
Mac scan matched the exact expected value. On an explicitly requested repeat
with the same settings, the owner confirmed the X5 woke. This is an observed
bench result with firmware still unrecorded, not a general compatibility claim.
#9 still needs its actual bounded shared advertising/reconnect route; #22 owns
physical per-model wake confirmation. Do not add wake data to normal pairing
or infer recording state from a wake or connection response.

Multi-camera wake is a hypothesis to test by rotating identifiers with deadlines
while preserving existing connections. Advertising support under multiple links
must be measured on the selected ESP32 stack. The isolated wake-only diagnostic
is available. The opt-in bounded production wake encoder, scheduler and shared
host worker are implemented below; the actual Insta360 reconnect/state/control
provider is still absent.

## Model differences and evidence labels

X5, GO 3S and ONE RS require independent profiles. GO 3S remote-service and wake
compatibility are unresolved; absence of evidence does not mean unsupported.
Use: Official (vendor documentation of product behavior), Community (reported
externally), Hypothesis (proposed), Observed (captured locally), and Confirmed
by testing (repeatable result with model/firmware recorded). This document
now includes a limited RideSync Observed result: [X5-associated BE80 discovery
and unclassified notification capture](hardware-results/2026-10-08-x5-discovery.md).
That BE80 observation does not establish remote pairing or recording control.
The later [CE80 pairing trial](hardware-results/2026-10-09-x5-pairing.md) includes
subscription and owner-confirmed connection. The separate one-shot trial records
one shutter submission with SDK status 0 and an unresolved camera recording result.
The later card-ready and three-cycle trials record owner-reported X5 firmware
1.11.10 and confirmed camera-observed shutter control through the isolated
probe. This does not qualify the unimplemented production adapter or state
decoder. GO 3S target/protocol remain unresolved.

## Implementation prerequisites from pinned sources

The MIT ESP32 `Insta_BLE.ino` declares X3/RS 1-inch targets and creates CE80
(write CE81, notify CE82, read CE83), plus a secondary service. This does not
establish ONE RS generally, X5 or GO 3S support. Its `notify()` calls broadcast;
its edited Arduino BLE files are not a required dependency for the selected
NimBLE probe. Build the discovered service shape from captures rather than
assuming the secondary service can be omitted.

The MIT M5Stick original sends the same shutter event. The pinned fork adds
connection-ID routing via `esp_ble_gatts_send_indicate(..., false)` and an X5
name branch, but its recording detector accepts a sufficiently long packet
containing a colon, then assumes stopped after five seconds without a timer.
That is a heuristic, not an explicit start/stop codec or authoritative state.
Missing timer traffic must become unknown in RideSync. See pinned file links in
[sources](sources.md#ble-decision-evidence).

| Path | ESP32 role / direction | Current evidence | Gate before production |
|---|---|---|---|
| CE80 remote | Peripheral; camera writes CE81, ESP32 notifies CE82 | MIT community examples, shutter event | Per-model pairing/subscription capture, state semantics, addressed delivery |
| BE80 direct | Central; ESP32 writes BE81, camera notifies BE82 | MPL-2.0 Garmin ONE R source plus unlicensed research references | Target applicability, GATT/authorization, complete framing, REC/STOP responses and authoritative state |

For X5 first, record model/firmware, address type, GATT UUID/properties, MTU,
security/bonding, CCCD writes, CE83 reads and handshake order with timestamps.
Capture manual start, stop, mode change, disconnect during recording, repeated
connection and rejected requests; correlate packets with camera display and
saved media. Preserve raw evidence and annotations separately. Do not promote a
name match, accepted BLE write or timer heuristic into confirmed recording.
Repeat qualification for ONE RS and GO 3S; neither inherits X5 results. Wake
advertisements need their own serial/timing tests. The first X5-associated BE80
discovery/notification observation and subsequent CE80 pairing are linked above;
annotated manual recording/mode captures are now retained, with explicit timing
uncertainty. Use the published-capture comparison above before requesting another
physical run; repeat only a named unresolved check after software integration.

Use the pinned NimBLE transport/routing and queue ownership in
[ADR-001](architecture.md#adr-001-ble-qualification-stack-and-roles-2026-10-07).
The newly audited licensed Garmin source is a separate reference, not a
replacement for the selected target's qualification. No code or packet
implementation from the unlicensed direct-control/GPS sources
may be copied; their reports only identify questions for independent captures.

## Finite wake preparation (#9 software stage)

`insta360_wake_encoder` supplies a fixed source-derived `M5WakeV1` advertisement
and scan response. Its six-byte identifier must be printable ASCII; the optional
name helper accepts only exactly `X5 ` followed by six printable bytes. It never
converts a MAC address, pads or truncates. Disabled and unknown profiles fail
closed. These are pinned-source fixtures, not model compatibility claims.

`WakeManager` holds four copied peer configurations and one advertising lease.
The default total deadline is 15 seconds and each advertising slice is at most
3 seconds; queue time counts toward the original deadline. Busy defers finitely.
No uncertain accepted submission is retried. Cancellation, expiry and generation
invalidation seal intent independently of physical release. A blocked SDK worker,
missing disconnect or full host queue retains ownership, while every other peer
can still time out. Operation IDs increase for the boot and never wrap or reuse.

The opt-in `Esp32Insta360Wake` uses the already-qualified `Esp32BleHost`, one
boot-lifetime mailbox and one fixed 4096-byte RTOS worker created at activation.
It prepares raw AD/scan-response bytes and admits bounded connectable advertising
only after rechecking current time and cancellation. Advertising excludes new
scan/connect and targeted bond reset; established unrelated ATT remains usable.
It never initializes another host, changes global bonding, or erases bonds.
Cleanup attempts stop/terminate once; late incoming handles remain eligible for
retirement until final release is claimed. A failed CONNECT status is not proof
of no live handle. Errors quarantine rather than manufacture
release. Callback references, host barriers and the worker's last access must
all finish before storage reuse. The incoming peripheral handle is wake-only:
refuse its security/store records, retire only that new handle, and require its
actual disconnect. Public store refusal starts before the delayed CONNECT notification. Exact-pin
SDK hooks also refuse incoming SMP dispatch/initiation, early RAM privacy-record
mutations, and private NVS read/write/delete paths that bypass public callbacks.
Foreign established handles are never terminated. Known
foreign stable identities remain usable; unknown private aliases, wildcard keys
and unknown private-key schemas are conservatively refused during that lease.

Successful advertising means submitted bytes, not an awake/recording camera.
An independently supplied `WakeRecovery` must reconnect and publish a fresh
Stopped or Recording observation tied to the same peer/generation/operation.
Unknown/stale observations fail. No provider means Unsupported after release.
The provider releases its procedure storage while retaining its qualified live
control link; it must also update the actual CameraManager from qualified facts.

`WakePreparation` attaches explicitly to the existing RecordingManager. It
never sends Start/Stop/Query or advances the camera/group owner. Call its service
once per owner pass before the existing camera/group advancement. Fresh Stopped
allows the group to issue its supported explicit Start; fresh Recording avoids
a duplicate. STOP seals pending work. A live link qualified before retirement
may still admit STOP; late Ready cannot acquire this permission. Replacement
prepare and invalidate revoke it. Only one preparation owner may attach, so this
bridge is not automatically added to HERO12, HandlebarControl or startup.

The compile-only `insta360_wake_compile` environment retains the actual worker
and GAP paths while application activation remains disabled. A real Insta360
control/state adapter and model/radio qualification remain necessary for the
end-to-end #9 recording path (#3/#5/#21/#22).

## Implemented CE80 display interpretation (#19)

`decodeCe80Display()` extends the existing pure shutter codec. Default profile
is Disabled. The explicit `X5CapturedDisplayV1` profile and independently known
CameraToRemote route are required. The caller supplies one complete stable
readable frame; the decoder checks size6..256 (local cap), pointer, magic and
exact `6 + payload length` before body access. Unknown message types expose no
raw text or state. Display type0x10 requires observed flag80/81, control byte6=1,
and the supported text-layout fields at bytes8/9; byte7 remains opaque layout.

Typed elapsed `.HH:MM:SS` (minutes/seconds0..59) yields Recording and owned
elapsed seconds, with mode Unknown. Exact captured video `5.7K|30` and photo
`72MP|MEGA` settings yield Stopped with distinct mode. They are supported display
observations under this explicit profile, not a dedicated state query. Other
settings, layouts and malformed text return UnsupportedDisplay/Unknown.
Remaining-runtime and padded numeric-count displays yield Remaining while
recording and mode stay Unknown. Every error has Unknown kind/mode/state and
seconds0; no input pointers, arbitrary bodies or identifiers escape.

This stateless parser cannot establish freshness, peer identity, replay safety,
command correlation or delivery. #3 must attach actual generation/time and
mode-safe command admission. Silence, disconnect and runtime/count displays
never become Stop. No adapter or camera capability is enabled by this change.
BE80 receive semantics remain opaque; no guessed ACK or capture-status fields
are adopted. The [owned display fixtures](../test/fixtures/insta360/README.md#ce80-display-fixtures-october-10-2026)
and [published-capture comparison](#published-ce80-capture-compared-with-retained-x5-data-19)
provide the independent evidence for the known supported fields.

# GoPro support plan

Status: **opt-in HERO12 software adapter implemented; no hardware tests**.
Added 2026-10-07.
The first target is **GoPro HERO12 Black**; record its installed firmware before
activation and bench verification. Additional GoPro models require separate qualification. Use the official
[Open GoPro compatibility table](https://gopro.github.io/OpenGoPro/) to qualify
each target rather than assuming every GoPro supports this API.

## Scope and approach

- Discover, pair/bond and reconnect to one qualified GoPro over BLE.
- Establish video mode; use explicit shutter on/off and verify encoding status.
- Handle responses, busy/not-ready state, notifications, timeouts and keep-alive
  per camera. A successful write or command response alone is not proof of video.
- Integrate with the same group recording intent and partial-success indication
  as Insta360; one missing GoPro must not block available cameras.
- Test sleep/reconnect/wake separately by model and power state. Do not assume
  full shutdown behaves like BLE-connectable sleep.
- Keep GPS and future IMU microSD logs independent. External telemetry injection
  into GoPro is outside the initial GoPro milestone; no API support is assumed.

The official [BLE setup](https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup/)
and [control API](https://gopro.github.io/OpenGoPro/docs/ble/control/) define the
starting point. Pairing prerequisites and available features must be verified
against the selected camera firmware. Use BLE for the initial control scope;
media transfer and preview are outside this milestone.

## Planned support matrix

| Feature | GoPro HERO12 Black |
|---|---|
| Pair/connect/reconnect | Not tested; planned |
| Video start/stop | Not tested; planned |
| Recording-state reporting | Not tested; planned |
| Sleep/wake | Documentary BLE-connectable recovery path implemented; physical states not tested |
| Mixed Insta360/GoPro operation | Not tested; planned |
| External GPS/IMU injection | Outside initial scope; capability unverified |

## Delivery order and acceptance

Single-camera GoPro issue: identify model/firmware and its returned API version; capture
pairing and responses; bench-test the separately testable adapter with
repeated start/stop, busy state, camera power cycling, ESP32 reset during
recording, bond persistence and sleep recovery. Include golden packets and
malformed/fragmented notification cases in native codec tests.

Mixed-camera integration issue: run one GoPro together with the three target Insta360 cameras where
available. Measure ESP32 mixed BLE role support and connection capacity. Record
request and observed-state timestamps; group control does not promise frame
synchronization. With one camera absent, all other ready cameras must receive
REC/STOP and the user must see a partial result. Verify that local SD logging
continues through disconnects and retry activity.

Only promote per-model features after target-hardware results are recorded.

HERO12 Black is listed in the official compatibility table. This establishes an
API investigation path, not confirmation of RideSync hardware support.
The camera has no internal GPS receiver according to GoPro's
[GPMF field documentation](https://github.com/gopro/gpmf-parser/blob/main/docs/README.md).
RideSync will record GPS externally for later alignment with footage; this does
not imply GPS can be injected into HERO12 video metadata.

## Official baseline and adapter prerequisites (2026-10-07)

The official [compatibility table](https://gopro.github.io/OpenGoPro/docs/)
lists **HERO12 Black minimum firmware v01.10.00** and notes model-specific
features. Installed firmware is unknown; this is a documentation qualification,
not camera verification. The official [multi-camera FAQ](https://gopro.github.io/OpenGoPro/docs/faq/#multi-camera-setups),
checked 2026-10-08, says up to four simultaneous generic BLE connections; it
does not qualify HERO12 physical coexistence or control ownership. Remove
competing phone/remote control during qualification. RideSync still plans one
central peer per camera.

[BLE setup](https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup/)
requires RideSync as central: pair once, discover services and re-subscribe on
every reconnect (subscription caching is unsupported). Discover FEA6; command/
response are GP-0072/0073, settings/response GP-0074/0075, query/response
GP-0076/0077, where GP expands to `b5f9XXXX-aa8d-11e3-9046-0002a5d5c51b`.
Poll Get Hardware Info with a deadline until BLE readiness succeeds. Retain
bond identity, characteristic ownership and fragmented response buffers per
camera, never globally.
Initial pairing also needs Camera Management GP-0090, write GP-0091 and response
GP-0092, with subscribed response before pairing-finish. Bonding alone does not
clear the pairing screen. Setup then claims external control on Command; neither
acknowledgment observes recording state.
The native fake host also tests a resource-unavailable external-control claim:
setup stops before identity/status discovery, keeps capabilities and recording state
unknown, retires the link and does not replay intent. This synthetic result
does not prove the cause of a real camera refusal; qualify competing-client
ownership with physical capture.

[Control](https://gopro.github.io/OpenGoPro/docs/ble/control/) defines Set
Shutter 0/1 and recommends Keep Alive every three seconds. Establish video
mode before requesting shutter on. The adapter must distinguish transport
acceptance, command response and observed encoding. Use status 10 Encoding,
8 Busy and 82 Ready from [Statuses](https://gopro.github.io/OpenGoPro/docs/ble/statuses/),
with query/notification support checked on the actual firmware. Preserve unknown
state across disconnects; do not infer stop from absent notifications. Capture
packet fragmentation and supported commands before choosing reassembly limits.

Use NimBLE-Arduino 2.3.6 with one client for HERO12 as specified in
[ADR-001](architecture.md#adr-001-ble-qualification-stack-and-roles-2026-10-07).
Mixed CE80/GoPro role and four-link capacity are unmeasured. The official
API/firmware minimum does not certify this stack or RideSync hardware.

## Pure HERO12 setup codec (2026-10-08)

`gopro_setup_codec` encodes a synthetic, schema-derived pairing-finish request
`03 01 08 00 12 08 52 69 64 65 53 79 6E 63` with required state zero and the
fixed eight-byte name `RideSync`; the bounded packet is 15 bytes compact or 16
bytes with the extended-13 header. It separately encodes external-control claim
`F1 69 08 02` (enum 2, rather than camera-only enum 1). These byte strings are
author-created vectors, not camera captures. Responses require their logical
Management/Command route and exact `03/81` or `F1/E9` pair before parsing.

The setup response has an explicit GenericProtobuf result domain: required
result 1 is success; 0 is unknown, 2–6 are known rejections, and other numeric
results remain diagnostics without setup success. This differs from classic TLV
ACK result 0. The bounded reader validates a complete message and skips
well-formed unknown wire 0/1/2/5 fields. It rejects groups, malformed tags,
wrong known-field wire type and overflow. Duplicate singular result fields use
last-value semantics; any unknown enum occurrence keeps the response from
becoming success even if a later duplicate is known. Raw response bytes remain
owned and bounded to 256 bytes. A setup ACK never publishes Encoding.

Classic Hardware Info `3C` and API Version `51` responses now have a separate
owned identity extractor. Seven length-prefixed hardware fields and two nonempty
API fields must validate fully before any model/API value is published. Numeric
fields are big-endian unsigned up to eight bytes; wider fields are rejected.
Firmware/name remain opaque slices into owned raw data. Hardware reserved tail
must be absent or exactly eleven bytes, preserving the existing classic policy.
HERO12 model 62 and firmware minimum v01.10.00 are documentary eligibility facts,
not values observed from an installed camera; firmware is not an API version.

## Opt-in HERO12 adapter (2026-10-08)

`Hero12Adapter` composes with the shared `BleCentral` and boot-lifetime
`Esp32BleHost`; `hero12Runtime()` exposes an ESP32 composition with the real
backend and a `CameraManager`. Its construction has no BLE I/O. The normal
firmware still does not call `adapter.start(true, true)`, configure a runtime
camera source, or choose board pins. A commissioning caller must provide a
verified `BondIdentity`, independently retained bond/store proof, and an exact
expected installed firmware byte string and numeric API major/minor in
`Hero12Qualification`. `source_qualified` and `classic_profile_confirmed` are
explicit caller assertions backed by commissioning evidence, never inferred
from a configured MAC, the published minimum firmware, or an advertisement.
The adapter compares actual owned Hardware Info model 62 and firmware field,
and actual API Version fields, against that qualification before publishing
start/stop/query capabilities. A mismatch or absent evidence retires the link.
The shared host refuses unverified restoration, full bond admission and absent
store refusal; the adapter cannot bypass those checks.

On each connection the shared central discovers FEA6 and GP-0090, checks eight
endpoint properties and reads back all four response CCCDs after subscribing.
The adapter then sends Management pairing-finish, Command external-control
claim, bounded Hardware Info readiness polling/API, three status registrations
and fresh Busy, Encoding, Ready queries. Each successful Register reply must
contain the requested status ID, one-byte length and boolean value; `53 00`
alone cannot establish registration. ATT completion, setup protobuf success 1, classic ACK 0,
camera Ready and observed Encoding remain separate facts. Setup ACKs never
become recording observations. A Start queries fresh Encoding, Busy and Ready;
if stopped/available it loads video mode, sends explicit shutter on, then
requires a fresh Encoding query showing recording. Stop queries Encoding and,
when recording, sends explicit shutter off and confirms stopped. It never loads
mode while encoding. A same-state fresh query can complete without shutter.
The connection-scoped Encoding notification is an observation, not proof that
an outstanding shutter ACK arrived.

Application composition uses `Hero12Adapter::managerPolicy()`: 120-second
manager operation deadline, one attempt and a 200 ms backoff value that is
unused at one attempt. Each camera transaction has a 2-second absolute
response deadline; fragments retain the codec's 1-second absolute deadline.
These are bounded software budgets, not measured camera/SDK latency.
Initial setup has ten serialized camera transactions plus BLE discovery;
the 120-second policy leaves bounded room for the shared central's 5-second
per-ATT phase deadlines. If discovery or camera delivery becomes ambiguous,
the adapter seals the peer and whole BLE link and delivers the captured
connection-scoped disconnect before the manager ticks, clearing any queued
Start/Stop intent. A canceled operation follows the same rule; no shutter
write is blindly retried. Physical response times and actual firmware/API
compatibility remain to be measured.
The Hardware Info poll is a RideSync cap of twenty attempts separated by
500 ms after explicit nonzero readiness replies. Lost, malformed or ambiguous
responses retire the link; polling does not retry a shutter or another
uncertain delivery. BLE encryption and bonding with verified identity are
required. NimBLE's optional `authenticated` bit denotes MITM protection and
is not treated as proof of ordinary pairing; the official setup page does not
specify a HERO12 MITM requirement.

Once connected, one Settings keep-alive `5B 01 42` is due every 3 seconds.
It shares each peer's serialized camera transaction slot with queries and
commands; a due keep-alive runs at the next safe step and its next deadline
starts from its actual acknowledged response, without catch-up bursts. A
blocked or lost response retires the link. Four fixed BLE peer slots are
available; CameraManager's eight-entry software registry does not expand
that hardware capacity. No Mission capability path, two-byte IDs, Wi-Fi,
GPS injection, sleep/wake or other camera profile is enabled here.

The shared reassembler retains 64 bytes per GATT chunk, 256 bytes per message,
four global streams, 32 packets and a 1000 ms absolute deadline. Management
uses the same capacity and does not establish four-camera operation. Missing or
late fragments have no transaction identity; the adapter retires
ambiguous connections and correlates serialized operations. Camera pairing,
installed firmware/API, observed status and recording, twenty cycles and
power-reset/no-replay gates in #4/#20/#17/#24 remain open.

## HERO12 recovery policy (2026-10-08)

`Hero12Adapter::requestRecovery(peer, ensure_recording, condition)` is an
opt-in, owner-loop API on the retained real adapter/manager/ESP32 host
composition. `hero12Runtime()` exposes those same objects and
`ridesync_hero12_service()` advances the coordinator together with central and
manager processing. The ordinary firmware does not activate this runtime. A
commissioning caller must first meet the qualification and bond/store proof
requirements above. The common `CameraManager::Wake` operation remains
unavailable for a disconnected HERO12; this callable coordinator submits the
existing `Connect`, `Query`, and `Start` operations in sequence.

For a disconnected peer it conducts at most two 3-second scans, rotating the
shared scan lease among pending peers. It considers only a well-formed FEA6
16-bit service advertisement whose observed public or random-static address
exactly matches the separately verified `BondIdentity`. An unresolved private
address is not matched without a verified stack identity mapping. An observed
match cancels only the scan, waits for its terminal callback and host barrier,
then requests ordinary connection. Central security, fresh subscriptions,
pairing-finish, external-control claim, Hardware Info readiness, exact
model/firmware/API checks, and fresh Busy/Encoding/Ready queries remain the
existing adapter contract. A retired connection lease must close before another
recovery scan for that peer. The coordinator has a 140-second absolute deadline;
the underlying manager connection limit is 120 seconds with one attempt and
never retries ambiguous camera delivery. These are software limits, not measured
camera latency guarantees.

For an existing Ready RideSync link, recovery issues a fresh Encoding query
without rescanning or disconnecting. With `ensure_recording=false`, fresh Ready
and observed Encoding complete a wake-only request. With
`ensure_recording=true`, observed Recording completes without another shutter
write; observed Stopped permits one ordinary `Start`, whose completion requires
fresh observed Encoding. Unknown observation, claim refusal, connection loss,
timeout, and cancellation never replay REC. A missing advertisement returns
`Unavailable`, meaning the cause is unknown: no inference of long shutdown,
another client's ownership, disabled wireless, or full power removal follows.
An explicit caller-reported `PowerRemoved` condition returns `Unsupported`,
because no BLE path exists without electrical power. No GoPro wake beacon,
undocumented power command, or Insta360 packet is sent.

The official [BLE setup](https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup/)
describes advertising for the first eight hours after sleep and connecting to
a sleeping camera as the wake mechanism. That documentary statement has not
been measured on this installed HERO12 or mapped to every Auto Power Down,
manual shutdown, USB, and battery condition. The generic Open GoPro FAQ's
multi-link ceiling does not prove simultaneous HERO12 control; claim refusal
is a failed recovery, never proof that a specific competing client caused it.

Physical acceptance for #24 remains **OPEN / Not tested**. Record the actual
model, installed firmware/API, pairing and verified identity, wireless and Auto
Power Down settings, power source, battery level, and competing phone/remote
state. For awake, deliberate Sleep, idle auto-sleep before and after the
documented eight-hour window, UI shutdown, and full battery/USB removal and
restoration, timestamp advertising, connection/security, subscriptions,
Hardware Info readiness, claims, UI/LED state, fresh Encoding, shutter response,
and recording result. Repeat cancellation, absence, existing connection and
already-recording trials; measure the inactivity and physical-button-required
boundary. Public evidence must omit MAC, serial, bond secrets, and private
location. Synthetic native host tests validate the software sequence only.

## Documentary pure codec (2026-10-07)

`include/protocol/gopro_codec.h` and `src/protocol/gopro_codec.cpp` implement a
portable C++11 codec without BLE dependencies or dynamic allocation. This
completes the documentary software portion of #20; **#20 hardware acceptance
remains unmet**. The overall adapter/lifecycle support above is still planned.

The explicit documentary profile is classic one-byte status IDs and
Get `0x13`/Register `0x53`/Notify `0x93`, Busy 8/Encoding 10/Ready 82. Enable a future adapter only
after recording the actual model, installed firmware, returned API version,
capabilities and successful request/notification captures. Current documentation
also has capability-selected two-byte IDs. This codec neither invents nor enables
those alternative profiles; discovery and qualification must precede extensions.

`encode(Request, Packet&, extended=true)` produces only required short requests
and exposes `Packet.channel` and `Packet.id`. Command routes cover video group 1000
(uint16), explicit shutter on/off, hardware-info and API-version queries;
settings routes carry keep alive; query routes carry Get/Register status. The
channel is logical: adapters map Command writes/responses to GP-0072/0073,
Settings to GP-0074/0075 and Query to GP-0076/0077. Both compact and extended-13
requests are supported. No outbound extended-16 or transmit fragmenter is needed.

`Reassembler::feed(peer, channel, packet, size, now_ms, message)` returns an
explicit outcome and a complete payload only on Complete. IDs are caller-supplied
stable peer identities and logical response channels; they must remain distinct
through reconnects. Use `resetPeer` on disconnect and `reset` for channel failure.
The four active stream slots each hold one incomplete payload, maximum 256 bytes,
maximum 64 bytes/GATT packet, maximum 32 packets and an absolute 1000 ms deadline
from first packet. Slots are reclaimed when a later feed observes expiry.
Callers need their own receive timers to expire silent streams promptly; an
expired stream's next packet returns Timeout and is discarded. Time is unsigned
monotonic 32 milliseconds; subtraction tolerates rollover, provided calls do not
span an entire clock period. Buffer/packet limits are application choices to
qualify against real MTU/response sizes, not camera maxima. Oversized messages
reject rather than truncate. All arrays and copies are bounded.

Receive accepts general 5-bit, extended-13 and receive-only extended-16 headers.
Undefined header selectors, nonzero reserved bits, zero-length messages,
truncated headers, overrun, empty fragments and invalid continuations reject and
clear the affected stream. Documented continuation counters `80`..`8F` are accepted
without strict progression until firmware behavior is qualified. Interleaved
peers and characteristics remain separate. A second start on an unfinished same
stream returns Interrupted and discards both starts: the caller must restart.
Continuations have no transaction identity, so undetectable same-stream fragment
mixing cannot be reconstructed. The adapter must serialize same-stream messages
or reset on suspected interleaving; this receiver cannot promise correlation.

`decode(channel, Message)` retains bounded raw data plus response ID and numeric
camera result. Nonzero result is a complete camera rejection, including unknown
numeric errors. Outcome::Complete describes framing/schema, not command success;
check result separately. Unknown response/element IDs return Unknown with raw
bytes. Recognized boolean TLVs are fully validated before publishing Busy,
Encoding or Ready; malformed messages never publish partial observed state.
Command acknowledgment leaves encoding unknown. Hardware/API fields are
bounds-checked length-prefixed data retained raw for future qualification;
no model/firmware/API width or value is fabricated. Truncated partial hardware
reserved tails reject; absent or complete 11-byte reserved tails are allowed.

[Fixture provenance and license audit](../test/fixtures/gopro/README.md) separates
pinned official documentary byte examples, schema-derived cases, and synthetic
adversarial fragmentation tests. It records the conflicting Kotlin uint32 video
encoder and fragment transmitter, the chosen documented wire contract, and
component licenses. No SDK implementation is reused. Native tests exercise
packets, result/state semantics and bounded independent reassembly. ESP32 compile
checks portability, not on-camera behavior.

Before claiming HERO12 support or #20 acceptance, capture the installed firmware
and API version; qualify video group 1000, shutter, hardware info, all three status
Get/Register/Notify operations, keep-alive settings responses, errors and actual
fragment counter behavior on that version. No hardware captures exist here.
Pair/connect/readiness lifecycle, keep-alive scheduling, wake/sleep and Wi-Fi/media
remain excluded from this codec change.

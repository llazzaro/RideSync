# Session time and record contract

`SessionClock` is a C++11 clock/record contract for local GPS, raw IMU, derived
motion and camera event producers. It has no camera connection dependency;
producers can continue timestamping during camera loss. The clock itself supplies no
SD, UART, BLE, sensor driver, camera clock setting or footage importer.

A record stores its complete `RecordTimestamp` by value when acquired. Raw sensor
values and derived estimates belong in separate payload fields with their own
validity/quality; a UTC estimate does not validate a fix or motion estimate.
The GPS storage/serialization contract is specified below. Serialize the named fields, not
C++ struct bytes (padding and enum representation are not a file format).

## Session identity and monotonic time

- `session_id`: caller-supplied nonzero unsigned 64-bit ID. The caller must allocate
  unique IDs across resets/boots and persisted logs, for example a provisioned
  device namespace plus persistent boot counter. This module neither generates
  random IDs nor establishes hardware uniqueness. Zero produces `InvalidSession`
  timestamps and rejects UTC anchors.
- `monotonic_ms`: unsigned 64-bit elapsed milliseconds since construction or
  successful `reset(new_id)`, independent of UTC. Reset rejects zero and the
  current ID without changing state. It cannot detect reuse of older IDs; that
  remains the caller's responsibility. Reset removes the current UTC anchor.
- `monotonic_quality`: `Valid`, `InvalidSession` or `DurationExceeded`.
  A session is supported through exactly 31,536,000,000 ms (365 days). The next
  increment latches `DurationExceeded`, saturates elapsed time at that bound and
  disables UTC estimates/anchor acceptance until a successful reset. Saturated
  timestamps cannot order subsequent events and must be treated as invalid.
  Start a new uniquely identified session before the limit.

The clock composes the existing `Clock::now()` unsigned 32-bit millisecond source
without changing camera-manager timing semantics. Every `snapshot()` and
`anchor()` call samples it using unsigned modulo subtraction. **Calls must occur
less than 2^32 ms (about 49.7 days) apart**, even if no records or valid UTC arrive.
There can be multiple wraps across a session, but fewer than one full wrap period
between samples. A raw-clock reset/reboot needs a new session. No 32-bit-only
algorithm can detect or guess missed complete wraps or distinguish raw clock
reset from rollover. Violating this input contract is unobservable; the caller
must prevent it or retire the session as invalid rather than trust those times.
Use one serialized execution context; this class provides no locking.

## UTC anchors and receipt quality

`anchor(calendar, uncertainty_known, uncertainty_ms)` samples the monotonic clock
at receipt and returns whether the update was accepted. Calendar fields accept
Gregorian years 2000–2099 inclusive, valid month/day combinations (including
February 29 in leap years), hours 0–23, minutes/seconds 0–59 and milliseconds
0–999. Leap-second values are rejected; no leap-second time scale is implemented.
Accepted values convert to signed 64-bit POSIX milliseconds from
1970-01-01T00:00:00Z, ranging from 946,684,800,000 through 4,102,444,799,999 for
anchor inputs. Extrapolated estimates may extend beyond the calendar input range.
The calendar range bounds validation work; it is not a GNSS hardware promise.

Each accepted anchor copies into subsequent snapshots:

| Field | Meaning |
| --- | --- |
| `anchor.sequence` | Session-local uint32 sequence, starting at 1; zero means absent |
| `anchor.receipt_ms` | Session monotonic time of accepted receipt |
| `anchor.utc_ms` | Accepted calendar value in POSIX milliseconds |
| `anchor.uncertainty_known` | Whether caller supplied a known uncertainty bound |
| `anchor.uncertainty_ms` | Supplied uint32 bound; zero when unknown, not a zero-error claim |
| `anchor_age_ms` | Snapshot monotonic time minus anchor receipt time |
| `anchor_quality` | `Missing`, `Fresh` or `Expired` according to receipt age |
| `has_utc_estimate` | True only with a fresh anchor and valid monotonic time |
| `utc_estimate_ms` | Anchor UTC plus age when available; otherwise zero, not valid UTC |

`anchor_max_age_ms` is the caller's uint32 freshness policy, inclusive at the
boundary. Zero permits an estimate only at receipt. Expired anchors remain in
snapshots for offline analysis but supply no current UTC estimate. Calendar
errors preserve the previous anchor and its sequence; rejection still advances
the sampled monotonic clock. Sequence exhaustion rejects further anchors rather
than reusing IDs; start a new session. Storage is constant: only the current
anchor is retained by the clock, with no heap allocation or unbounded queue.
Producers retain the complete anchor in each emitted timestamp, so an expired or
replaced anchor can still be understood without a separate unbounded history.

Forward/backward UTC corrections create new anchor associations without changing
monotonic elapsed time. Previously emitted timestamp copies keep their original
anchor, age, quality and estimate through later corrections and resets. Writers
must preserve those fields; offline tooling must not apply the newest anchor
retroactively to old records. The snapshot copy is the immutable record contract,
not a reference into the clock's mutable state.

GNSS UTC often refers to a solution generated before its UART/message receipt.
This module knows only receipt time. Passing solution UTC directly gives a
receipt-associated estimate with unknown transport/receiver/parser latency unless
independently characterized. A caller claiming known uncertainty must include
that offset, GNSS uncertainty and other measurement delays in the supplied bound.
The bound is retained metadata, not a measured guarantee or a propagated drift
model: estimate uncertainty can grow with oscillator drift and age. Fresh means
receipt-age freshness, not verified GNSS fix quality, camera-time agreement or
frame synchronization. Actual timing accuracy requires hardware measurements;
none are claimed here.

## GPS CSV version 1 and storage ownership

`Storage` writes `/gps-<16 lowercase hex session ID>.csv`. The caller supplies a
unique nonzero session ID and immutable `RecordTimestamp` and `ModemSnapshot`
from that same session and sampling instant (for example, `now = clock.snapshot()`
then `modem.snapshot(now)`). A `Valid` monotonic timestamp is required; mismatched
session IDs, contradictory anchor/age associations and malformed available
values are rejected. Each accepted payload is copied deeply into eight fixed
SPSC slots. One producer calls `enqueue()`/`requestStop()`; one isolated worker
calls `workerStep()`. Do not reset a clock into a new session and keep using the
old storage instance. No camera, IMU or camera-event dependency is present.

Files begin with `#ridesync_gps,1`, session/firmware/provenance metadata and a
flush/queue/chunk/retry policy line, then a named CSV header. Firmware and
provenance are copied at construction: 1–48 ASCII letters, digits, underscore,
period or hyphen; unsafe CSV/newline strings are rejected rather than escaped.
All fields are ASCII with comma delimiters, decimal numbers and LF lines. The
[sample CSV](example_gps.csv) is **synthetic**, not a receiver/card/ride capture.
The default firmware leaves the logger and GNSS hardware inactive. An explicitly
qualified [commissioning provider](supervision.md) composes them through the real
firmware entry point. This software path supplies no actual valid fix evidence.

The header names define the serialization; no native struct bytes are persisted:

| Fields | Units and validity |
| --- | --- |
| `session_id`, `monotonic_ms`, `monotonic_quality` | Decimal session ID, elapsed milliseconds, quality enum (0 Valid, 1 InvalidSession, 2 DurationExceeded); only Valid accepted |
| `anchor_quality` | 0 Missing, 1 Fresh, 2 Expired |
| `anchor_sequence`, `anchor_receipt_ms`, `anchor_utc_ms` | Copied association, local receipt milliseconds and signed POSIX UTC milliseconds; blank if missing |
| `uncertainty_known`, `uncertainty_ms`, `anchor_age_ms` | Boolean, bound in milliseconds (blank when unknown), age in milliseconds (blank without anchor) |
| `has_utc_estimate`, `utc_estimate_ms` | Boolean and extrapolated clock UTC milliseconds; estimate blank unless available; expired anchors stay in their own fields |
| `modem_state` | 0 Disabled, 1 Startup, 2 Power, 3 WaitReady, 4 Poll, 5 Desynchronized, 6 Failed |
| `uart_health` | 0 Disabled, 1 Healthy, 2 Timeout, 3 Overflow, 4 ProtocolError, 5 Exhausted, 6 InvalidClock |
| `gnss_power`, `receiver_ready` | Boolean software observations, not independent physical measurements |
| `fix_validity`, `fix_age_ms`, `fix_valid` | 0 Missing, 1 Valid, 2 NoFix, 3 Invalid, 4 Stale; receipt age blank when unavailable; `fix_valid` describes coordinate validity of the stored payload |
| `latitude_deg`, `longitude_deg` | Signed degrees; blank when `fix_valid=0`; finite ranges ±90/±180 |
| `altitude_msl_m`, `speed_m_s`, `course_deg` | Optional metres, metres/second, degrees; blank when unavailable. Admission bounds: altitude ±100000, speed 0–100000, course 0–360; these are defensive serialization bounds, not receiver accuracy claims |
| `source_utc_date`, `source_utc_time` | Optional GNSS solution calendar, `YYYY-MM-DD` and `HH:MM:SS.cc`; blank when unavailable. Years 2000–2099, valid Gregorian dates and ordinary seconds 0–59 |
| `fix_receipt_ms`, `satellites`, `fix_quality` | Local payload receipt milliseconds (blank without evidence), optional satellite count and documentary receiver quality code |
| `accepted`, `dropped`, `rejected`, `lost`, `written`, `flushed` | Observational health counters sampled when formatting this row, before writing it |

A retained stale payload can have `fix_valid=1` and coordinates; those coordinates
are historical, not a current fix. A stale no-fix payload can have `fix_valid=0`.
Only `fix_validity=Valid` and `fix_valid=1` identify a current valid coordinate
solution. Source GNSS UTC is separately flagged calendar data generated before
receipt; it is neither `fix_receipt_ms` nor `utc_estimate_ms`. None of these alone
establishes measured latency, UTC precision or camera synchronization. Missing
coordinates/optional fields are blank rather than manufactured zero readings.

### Queue, counters and filesystem failures

Producer work is bounded validation and copying. It does not allocate, format
CSV, call a sink, acquire a mutex, or wait for the worker. uint32 and boolean
atomics are compile-time required to be lock-free. Queue ownership uses release/
acquire publication; the worker copies a slot before freeing it and holds no
producer/control mutex across I/O. `health()` uses atomics and is suitable as the
#15 health/progress hook: a hung sink leaves `progress` unchanged. Counters
saturate at uint32 max and are not a coherent transaction across fields.

- `accepted`: admitted into queue, not a persistence acknowledgment.
- `dropped`: well-formed records refused because queue full, terminal or stopping.
- `rejected`: invalid session/sample/value/metadata contract, before admission.
- `written`: entire CSV record accepted by sink; bytes can still be cached.
- `flushed`: entire records covered by a completed successful sink flush call.
- `lost`: terminal failure's accepted records not covered by flush, including
  queued, in-flight/partial and cached records; conservatively uncertain, not
  necessarily all physically absent. A racing final producer publication is
  included by `accepted - flushed` in terminal health. Exact accounting holds
  until counter saturation; stop the session well before that point.

Runtime health is authoritative for final losses: a failed card cannot reliably
persist its own final failure/counter row. Row counters are snapshots, not a
footer or guarantees about later writes. `terminal` disables admission after
media error; `stopped` means the worker's close completed. A sink that hangs can
leave either incomplete. `requestStop()` stops new admission, drains queued
records, flushes cached records and closes in the worker. Quiesce the producer
before releasing a storage object. Graceful closure rechecks the queue after
acquiring the producer stop signal, so a final accepted enqueue cannot be skipped
by an earlier empty-queue observation. The Arduino adapter additionally requires
`workerFinished()` before releasing it, its sink or SPI bus.

The worker has a 2048-byte serialization buffer (at most 2047 bytes plus NUL),
256-byte write chunks, eight by-value records, and one temporary record. No core
heap allocations. Each step does at most one mount/open/write/flush/close call;
mount attempts are capped at 1–3 (default 2), with one attempt per step. Open,
write and flush have no retries. **Any short/zero/oversized write result is
terminal**, including a partial header: do not replay uncertain bytes or append
more data to that stream. Remaining records become loss/uncertainty; close runs
only in the worker. A new session/object and fresh unique filename are needed
for recovery. No automatic format, deletion, truncation recovery or old-file reuse.

### Commissioned session identities and pre-session SD ownership

Unattended SD sessions reserve `id = (uint64_t(namespace32) << 32) | counter32`.
The opaque namespace is nonzero and assigned once by an external commissioning
authority; counters are `1..UINT32_MAX`, with exhaustion terminal and no wrap.
Unique namespace assignment, exclusive ownership and retention of acknowledged
ledger commits are explicit premises. No hardware identifier, camera peer ID,
settings epoch, random sample, RTC count or GNSS/UTC time supplies uniqueness.
Camera configuration/NVS refusal and safe mode do not prevent an independently
qualified local-telemetry allocation. Local eligibility alone does not start SD.

`QualifiedSdConfig` defaults disabled and additionally requires
`namespace_commissioned` and independently supplied `commissioned_namespace`.
The worker mounts its one private SDFS, then automatically recovers and commits
one counter. `allocation()` publishes copied `{status, id, error}` with
release/acquire ordering only after exact write, successful `fsync`, successful
close, and a separate exact readback/EOF/close verification. Repeated reservation
on that owner returns the same result; no retry/reset after uncertainty. An ID
committed before cancellation, publication, binding or CSV creation is burned.
Each new owner reserves a new ID, even if all CSV files have been deleted.

The adapter retains the same mount while waiting for control to construct its
`SessionClock` and session-specific `Storage` (including camera-free sessions).
Use `ArduinoSdStorage(spi, qualified_config)`, check `start()` succeeded, then poll `allocation()`,
and only on `Committed` construct `SessionClock` and `Storage` with the copied
ID and `owner.sink()`, then `owner.bind(storage)`. Bind accepts exactly once,
checks configuration, matching session ID and actual sink ownership, and rejects
stopped/terminal Storage. The same worker services that Storage; its mount is
idempotent. Future camera composition must give `CameraEventSession` this sink
and bind its one Storage; do not create another logger or filesystem owner.
`cancel()` also terminates an owner waiting for bind or allocation, and a bound
cancel drains Storage through its existing stop contract. One serialized control
context owns polling/bind/cancel and producer coordination. All owner, sink,
SPI, Storage and session lifetimes extend through `workerFinished()`; no worker
access to them follows its final publication. Unbound/refusal paths close and
unmount too. Producers must be quiescent before destruction. There is no automatic
retry of a terminal owner and no in-place Storage reset.

A false first `start()` return means qualification or task creation refused:
no worker was created, no SD IO occurs, allocation remains `Pending`, and no
`workerFinished()` publication follows. Handle that returned refusal directly;
never wait for a nonexistent worker or construct a session. Destruction is safe
immediately after that first-call refusal. A second `start()` on an active owner
also returns false but does not stop or replace its existing worker.

The persistent files at SD root are `.session-id-a` and `.session-id-b`, distinct
from CSV paths and reserved from cleanup. Each is exactly 40 bytes, independent
of compiler ABI, with ten little-endian 32-bit words:

| Byte offset | Value |
| --- | --- |
| 0 | Magic `0x44495352` (`RSID`) |
| 4 | Version `1` |
| 8 | Commissioned namespace |
| 12 | Committed high-water counter |
| 16 | Sequence, exactly equal to counter |
| 20, 24, 28 | Bitwise complements of namespace, counter, sequence |
| 32 | Reserved, zero |
| 36 | IEEE CRC32 of bytes 0..35 |

Both slots must be present, exact length, valid and match the independently
expected namespace. Normal recovery accepts both `0/0` commissioning baselines,
or a coherent pair with adjacent positive high-water values (including `1/0`).
Equal positive, nonadjacent, invalid checksum/version/complement, or mismatched
namespace slots refuse. Never fall back to a lone valid slot: it might be the
older copy of a lost acknowledged allocation. Commit overwrites only the older
slot, without creation/truncation; ordinary startup never initializes a blank
card. The copied statuses distinguish `IdentityUnavailable`, `LedgerCorrupt`,
`Exhausted`, `MediaError` and `CommitUncertain`, with supported backend errno.
A later CSV `ioStatus()==PathCollision` (`EEXIST`) is separately terminal;
`O_EXCL` is a secondary safeguard and cannot prove identity uniqueness.

First-use commissioning is an explicit offline operation. With firmware and all
other volume users stopped, the authority assigns a **never-used** namespace and
records exclusive ownership outside the card. Run
`python3 scripts/commission_session_ledger.py <host-mounted-volume-root> --namespace <value> --authority-confirms-never-used`, then safely unmount before firmware ownership.
This creates both checked baseline slots with exclusive creation, sync, close
and readback; it never replaces an existing slot. The portable explicit
`SessionIdentityAllocator::commission` offers the same baseline protocol to an
exclusive provisioning backend; boot never calls it. Partial commissioning is
terminal and must not be retried as a reset. Missing/replaced/corrupt media needs
explicit recovery: a newly assigned never-used namespace, or an independently
verified high-water transfer that retires all old ownership. Neither a card CID,
label nor an on-card marker establishes uniqueness or prevents clones.

The software fault model assumes every acknowledged commit survives reboot.
Tests cover torn writes with both retained and discarded unacknowledged bytes,
all open/read/write/sync/close/readback boundaries, burn before bind/CSV, overflow,
removed/replaced/corrupt media, NVS refusal and final worker barriers. A wholly
valid older snapshot of **both** slots is indistinguishable from a legitimate
earlier state without an independent monotonic anchor. The test explicitly
shows the repeated candidate in that out-of-model rollback case, including after
CSV removal; no anti-rollback guarantee is claimed. Detectable lone-slot damage
and nonadjacent conflicts still refuse.

Pinned [IDF 4.4.7 FAT VFS](https://github.com/espressif/esp-idf/blob/v4.4.7/components/fatfs/vfs/vfs_fat.c)
reports `fsync` through `f_sync`; [FatFs](https://github.com/espressif/esp-idf/blob/v4.4.7/components/fatfs/src/ff.c)
synchronizes file/directory state through `CTRL_SYNC`.
Pinned [Arduino 2.0.17 SPI SD](https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/SD/src/sd_diskio.cpp)
implements `CTRL_SYNC` by selecting/deselecting the card. This source proves
software ordering/error propagation, not physical card/controller persistence.
Physical abrupt-power-cut tests must establish acknowledged-commit survival,
torn-write/media-removal handling, rollback behavior, latency, scheduler and
stack high-water limits on each qualified board/card. These criteria remain open;
compilation, mocks and host `fsync` do not close them. Default activation and the
original mixed-brand/IMU/ride qualification gates remain unchanged.

### Opt-in Arduino SD worker and loss bounds

`ArduinoSdStorage` uses the SD library bundled with the already pinned Arduino
ESP32 framework `3.20017.241212+sha.dcc1105b`; no additional dependency. Its
configuration defaults disabled and requires explicit opt-in, qualified wiring/
card/filesystem, exclusive volume ownership, commissioned namespace, output-capable CS and frequency.
Caller supplies a dedicated already configured `SPIClass`; the adapter assigns no
board pins. Pinned `SDFS::begin()` calls `SPIClass::begin()` internally; its early
return on an already initialized bus preserves the caller's configuration. This
is why preconfiguration is required. `start()` only creates an 8192-byte-stack,
priority-1 unpinned FreeRTOS task. **Private SDFS.begin (format disabled), POSIX
open/write/fsync/close and SDFS.end all execute in that worker.** The serial
firmware does not start it. The adapter constructs a private SDFS/VFS object,
which uses SDK allocation outside the producer enqueue path.

Exclusive creation uses `open(O_WRONLY | O_CREAT | O_EXCL, 0600)` against the
private `/ridesync` VFS mount. There is no existence probe, truncating flag,
`fopen` mode inference, fallback or creation retry. `EEXIST` and every allocation,
path or I/O error fail closed without opening/truncating a previous log. The
pinned ESP-IDF 4.4.7 [FAT VFS source](https://github.com/espressif/esp-idf/blob/v4.4.7/components/fatfs/vfs/vfs_fat.c)
maps `O_CREAT | O_EXCL` to FatFs `FA_CREATE_NEW`; `fsync` calls `f_sync` and reports
failure. This mapping was also checked in the pinned `libfatfs.a` disassembly.
`ioError()` provides the latest reported errno (zero initially), preserving
collision (`EEXIST`) versus allocation (`ENOMEM`) and I/O errors. SDK mount failure
reports generic `EIO`; a busy adapter reservation reports `EBUSY`. Close errors
are reported there too; they do not retroactively change flushed row counters.

`/ridesync` is reserved for this adapter: no external user may mount or register
that namespace. A lock-free atomic reservation serializes adapter ownership;
other instances refuse their pre-session mount rather than adopting/releasing the
first instance's mount. The private SDFS starts unmounted and never reuses or
ends the application's global `SD` instance. Successful mounts are ended in the
worker after descriptor close, before `workerFinished()` publishes completion.
Failed SDFS initialization cleans up its own partial mount in the pinned SDK;
the adapter releases only its own reservation. A caller can then retire the old
SPI instance and construct a new logger on a newly qualified bus. Other clients
must not access the same physical volume/bus concurrently. No repair, format,
deletion or old-file reuse occurs.

Mount, write, fsync, close and unmount can block indefinitely: chunking bounds
bytes and calls, not their wall time. Dedicated SPI/volume ownership avoids shared
control-task locks, but native tests cannot establish ESP32 scheduler fairness,
SDK interrupt behavior or watchdog safety; measure them on the actual board.
Successful `fsync` detects reported FatFs sync failures but never establishes
physical durability or detects every silent media failure.

Flush occurs after configurable 1–8 complete records (default 4), on idle after
any cached records, and during graceful stop. With worker progress and truthful
sink flush behavior, the software bound on admitted records since the last flush
is **queue capacity + flush threshold = 12 default, 16 maximum**, including the
one in-flight row. Written-but-unflushed records are at most the threshold; a
partially emitted row may remain. There is **no time bound** during blocked I/O.
Drops/rejections are additional explicit pre-admission loss. Power loss may lose
queued records, partial rows, cached bytes, metadata and even previously flushed
sectors, or corrupt filesystem structures. No physical durability/corruption
bound has been established. Parsers must reject incomplete trailing rows and
unknown versions; do not silently promote partial or stale data to valid fixes.

GPS v1 includes GPS only. Opt-in [mixed telemetry v2](mixed_telemetry.md) adds
bounded raw IMU/configuration/health/control rows through the same storage worker
and one admission owner; no camera rows or physical sensor qualification.

Raw selected BMI270 acquisition and evidence semantics: [raw_imu.md](raw_imu.md).
The mixed v2 schema remains unchanged; acquisition timestamps stay unknown.

## Correlated camera events: opt-in telemetry version 3

`StorageFormat::CameraV3` creates `/telemetry-<session>.csv` with
`#ridesync_telemetry,3` and a `#camera_layout,3` marker. It accepts the same GPS
and raw IMU/configuration/health/control rows through the **same** storage worker
and admission owner, plus `camera` rows. GPS v1 and mixed v2 stay separate format
selections; their headers and row bytes are unchanged. The parser rejects a
camera row in v2, an unknown version, a missing v3 layout marker, malformed field
counts or an incomplete trailing row. V3 checks session duration, anchor
presence/association, boolean flags and exact transport attempt error/admission
pairs. V3 is never inferred from an old file.

A camera row begins with `camera,` and the 12 common session/anchor columns above.
The remaining columns are, in order:

`peer_slot,peer_id,model,group_generation,intent_id,connection_generation,operation_generation,event_kind,operation,error,recording,ack_domain,ack_action,delivery_admitted,time_domain,event_receipt_known,event_receipt_ms,event_receipt_age_ms,radio_receipt_known,radio_receipt_ms,acquisition_known,acquisition_ms,accepted,dropped,rejected,lost,written,flushed`.

`peer_slot` is the configured registry index (0–7). `peer_id` is a
caller-provisioned nonzero opaque numeric ID, stable for the
session. It must not be derived from or replaced with a MAC, serial, credential,
camera name or hash of private identity. A missing or mismatched identity prevents
logger activation or is counted as rejected evidence. `model` uses the
`CameraModel` values (1 X5, 2 GO3S, 3 ONE_RS, 4 HERO12_BLACK); model names here
identify configuration, not verified physical camera identity. The numeric
`operation` values are 0 Connect, 1 Start, 2 Stop, 3 Query, 4 Wake. A refused
request has a `CameraError` code and no intent or transport generation; an
accepted or queued request gets a distinct nonzero per-peer intent ID. A queued
request has **no** operation token. The `attempt` row later assigns its actual
connection and operation generations and records `delivery_admitted`; false
means the transport rejected that attempt. These fields never imply a camera
command executed. Intent IDs refuse exhaustion rather than wrap. Connection and
operation generations reserve 64 increments before exhaustion; new requests
are refused there, and retirement increments saturate instead of wrapping.

`event_kind` values are 0 RequestAccepted, 1 RequestQueued, 2 RequestRefused,
3 Attempt, 4 WireAck, 5 RecordingObserved, 6 ManagerCompleted, 7 Failed,
8 Cancelled, 9 Disconnected. `recording` values are 0 Unknown, 1 Stopped and
2 Recording, and are populated only for accepted observation rows. Connection
notifications have no intent or operation generation; command observations are
scoped to the active operation. Repeated fresh unsolicited same-value
notifications remain separate observations. A duplicate matching transaction
reply cannot create a second wire ACK. `ManagerCompleted` is internal operation
completion and must never be read as a camera ACK or recording observation. A
timely command observation may confirm after manager completion; the group
retires that operation-scoped token when its Confirm deadline is reached. The
manager also bounds that completed token to 1,000 ms from `Completed`, independent
of the original attempt deadline; a new attempt or cancellation retires it.
Fresh connection-scoped unsolicited observations remain admissible afterward.

`ack_domain` is 0 None, 1 classic HERO response, 2 Generic Protobuf setup
result. The corresponding `ack_action` values are 0 None, 1 Pair, 2 Claim,
3 Hardware, 4 Api, 5 RegisterBusy, 6 RegisterEncoding, 7 RegisterReady,
8 GetBusy, 9 GetEncoding, 10 GetReady, 11 Video, 12 ShutterOn, 13 ShutterOff,
14 ConfirmEncoding, 15 QueryEncoding. Setup actions require domain 2; all other
actions require domain 1. Only a validated expected route, ID, result and
required status element for the current active attempt can generate a wire ACK.
The profile and manager reject responses at or after their own deadlines,
including when a queued callback is delivered before the normal timeout pass.
An ATT write-complete callback or a successful shutter ACK without a fresh
Encoding observation does not establish recording.

The common `monotonic_ms` is sampled by the sole telemetry owner when it admits
the copied camera evidence. The manager audit hook also samples the shared raw
host clock when publishing an accepted event or request into the camera inbox.
The owner converts that by-value sample to `event_receipt_ms` only when the
modulo-32-bit age is at most 60 seconds and fits within this session's elapsed
time; `event_receipt_age_ms` is the delay to admission. Unknown or ambiguous
samples have `event_receipt_known=0` and blank values. This is an owner audit
hook time, not an SDK callback or radio packet receipt; it excludes any time
spent before the validated manager event. The checked modulo translation assumes
the queue is serviced before a full 32-bit clock cycle elapses; a 49-day stalled
consumer cannot be distinguished from a recent event with this clock API.
`time_domain` is always `owner_admission`;
`radio_receipt_known=0` with blank `radio_receipt_ms`, and
`acquisition_known=0` with blank `acquisition_ms`. BLE SDK callbacks in this
source path do not provide a qualified radio-receipt timestamp. Queue delay
between callback, camera owner validation and storage admission is unmeasured.
The session clock's UTC anchor is a receipt-associated estimate of this **owner
admission** time, not camera time, radio receipt, frame acquisition or frame sync.
No camera row contains GPS coordinates.

The camera owner copies events into a fixed 16-entry camera inbox. The telemetry
owner drains camera and IMU fairly with a two-record per-pass quota and keeps
GPS admission independent. Storage retains its eight by-value slots and reserves
two for GPS when admitting camera or inbox IMU rows. Inbox overflow, malformed
events, storage rejection/drop and terminal unflushed loss appear in per-kind
health counters. A refused log admission never cancels camera control. At stop,
the camera owner finishes after its last callback and publication; the telemetry
owner waits for both camera and IMU producers to finish, drains both inboxes,
then requests the worker's final flush/close. Sink failure leaves final loss
authoritative in runtime health, since a failed card cannot reliably log itself.
If activation never succeeded, the session drains and closes only its telemetry;
it does not service, stop or wait for an independently owned adapter.


## MotionV4 extension

Requested static motion sessions use [MotionV4](motion_logging.md#csv-layout):
GPS remains 36 columns, cameras retain V3, and each raw IMU/config/health/control
row appends 36 motion fields to the existing 88-field prefix. Invalid numerics
are blank and dynamic result flags remain zero. Current runtime validity expires
independently of immutable historical log records. CameraV3 remains the default
when motion is not requested.

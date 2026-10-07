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
The current firmware entry point does not instantiate the logger or a qualified
GNSS hardware profile and supplies no actual valid fix evidence.

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

### Opt-in Arduino SD worker and loss bounds

`ArduinoSdStorage` uses the SD library bundled with the already pinned Arduino
ESP32 framework `3.20017.241212+sha.dcc1105b`; no additional dependency. Its
configuration defaults disabled and requires explicit opt-in, qualified wiring/
card/filesystem, exclusive volume ownership, output-capable CS and frequency.
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
other instances fail bounded mount attempts rather than adopting/releasing the
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

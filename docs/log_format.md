# Session time and record contract

`SessionClock` is a C++11 clock/record contract for local GPS, raw IMU, derived
motion and camera event producers. It has no camera connection dependency;
producers can continue timestamping during camera loss. This change supplies no
SD, UART, BLE, sensor driver, camera clock setting or footage importer.

A record stores its complete `RecordTimestamp` by value when acquired. Raw sensor
values and derived estimates belong in separate payload fields with their own
validity/quality; a UTC estimate does not validate a fix or motion estimate.
Storage/serialization is a later implementation. Serialize the named fields, not
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

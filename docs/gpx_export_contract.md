# Frozen offline GPX stream contract — #70

Python 3.11 standard library; API definitions live in
[`tools/ride_export/contracts.py`](../tools/ride_export/contracts.py). These
immutable, slotted dataclasses carry validated evidence, without parsing in
constructors. The normative scalar/time/position policy remains
[`ride_log_export.md`](ride_log_export.md). Fixtures are synthetic software
examples, never a ride, hardware observation or video synchronization claim.

Reader entry point:

```python
read_events(source: BinaryIO, *, salvage: bool = False) -> Iterator[StreamEvent]
```

Writer entry point:

```python
write_gpx(events: Iterable[StreamEvent], output: TextIO, *,
          time_source: str = "solution", max_gap_ms: int = 10000,
          salvage: bool = False,
          diagnostic_sink: Callable[[Diagnostic], None] | None = None
          ) -> ExportSummary
```

The source and output are caller-owned open streams. Reader consumes binary
ASCII/LF input with at most 4096 bytes per physical line including LF. Writer
streams Unicode XML to the supplied text stream. Neither closes caller streams,
accumulates events, XML trees or diagnostics, or publishes outputs. Time modes
are `solution` and `receipt-estimate`. The CLI passes the same salvage option to
both stages; same-receipt conflicting GPS payload is a writer-detected malformed
row, fatal in strict mode and discarded with a break in salvage mode.

`Metadata` is emitted exactly once, first, only after the complete schema,
session, policy, named GPS header and required layout markers validate. Its
`policy` is a tuple of string key/value pairs in source order, bounded by the
six known firmware policy keys. `schema` is one of `GpsV1`, `MixedV2`, `CameraV3`,
`MotionV4`, `MotionV5`; `version` is 1–5. Metadata line identifies the schema
line (1). Row events carry physical source line numbers, never record indexes.

Each valid data row emits exactly one `GpsEvent` or `IgnoredEvent` in file
order. Both preserve a `Timestamp` (the twelve named common fields) and the six
named `Counters`. GPS carries each named field after the common timestamp and
before the counters, using `Decimal` for scalar coordinate/altitude/speed/course
values, strings for validated source calendar fragments, booleans for flags and
integers for enums/receipts/satellites/quality. `None` always means absent;
`Decimal('0')`, integer 0 and a known zero uncertainty are present evidence.
Decimal scale is retained; XML uses plain decimal formatting, never binary
float conversion. GPS source numbers use plain decimal grammar; coordinates
have at most eight fractional digits, matching firmware serialization. Extra
precision is rejected before projection can round into an invalid boundary.
Unknown uncertainty is `uncertainty_known=False` with
`uncertainty_ms=None`. Expired copied associations remain present but cannot
supply an emitted receipt estimate. The writer never substitutes a new anchor.

A salvaged malformed complete data row or unterminated tail emits a
`Diagnostic` followed immediately by `Discontinuity` at the same line, and no
row event. The next admitted point starts a new segment. Reasons are stable
machine-readable tokens; field/reason/remedy identify the error without echoing
raw source values or coordinates. Reader emits no synthetic rows for discarded
bytes. A tail is discarded even when the fields appear complete. Reader normally
emits discontinuities only for salvage; writer determines fix exclusion, dedupe,
clock/counter continuity, gap and time-basis changes from valid row events.
Multiple causes at a point may be recorded but never create empty segments.

Strict failures raise `FormatError` containing a `Diagnostic` and category
`header`, `version`, `session`, `row`, `encoding`, `line_length`, `tail`, or
`no_points`. Header/version/session errors, session mismatch, overlong lines and
invalid encoding are fatal even in salvage. Malformed complete data rows and
unterminated final rows are salvageable. I/O failures propagate as `OSError`;
CLI maps them to exit 4. A fatal failure can leave partial temporary XML; it must
never be published. Reader owns scalar ranges, calendar validity, exact row
layout, flags/presence, session ownership and clock/age equations. Writer owns
point/time admission, duplicate/conflict handling and segmentation, reporting
zero points as `FormatError(category='no_points')`.

Counter domains follow the source: GPS `health()` snapshots are **global**,
whereas non-GPS `kindHealth()` snapshots are **per kind** (`storage.cpp`, GPS
formatting and `formatImu`/`formatCamera`). A lower counter on an interleaved
kind is not itself corruption. Compare successive snapshots only within each
of the six bounded domains: `global`, `imu`, `config`, `health`, `control`,
`camera`. Loss increases in any domain schedule a break. Never sum snapshots or
infer an exact number of missing positions. `CounterObservation` stores scope,
maxima, initial nonzero names, increase/decrease observation counts and saturated
names. `ExportSummary.counter_domains` contains only observed fixed domains;
legacy `counter_*` fields represent the global GPS domain only.

Writer returns `ExportSummary`: metadata, fixed numerical counts, bounded
ignored-kind/reason tuples and counter observations. Reasons and kinds must come
from a fixed implementation vocabulary, never arbitrary source strings. The
callback receives reader diagnostics and writer diagnostics once, in event
order; the CLI streams them to disk. It owns input size/hash/basename provenance,
summary JSON, exclusive temporary-file publication and exit status (0 success,
2 CLI/format/no-point error, 3 successful salvage discarding rows, 4 I/O or
publication error). Expected invalid/stale fixes, duplicates and untimed points
alone do not make an export status 3. Input bytes must remain unchanged.

Fixture field counts are independently frozen: GPS 35 in v1, GPS 36 with literal
`gps` prefix in v2–v5, IMU/config/health/control 88 in v2/v3, 124 in v4, 147 in
v5, camera 41 in v3–v5. Non-GPS counters are positions 82–87 in the raw prefix;
camera counters are the last six. Recognized ignored rows validate exact count,
common timestamp/session and counters, without certifying sensor/camera payloads.

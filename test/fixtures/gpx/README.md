# Independent synthetic GPX input evidence

Author: RideSync contributors, October 13, 2026. License: repository MIT
([LICENSE](../../../LICENSE)). All new CSV and JSON files here are independently
authored synthetic examples. Locations are public mathematical coordinates
(equator, poles, meridians and invented whole-degree points); session 42 and
firmware `gpx-fixture-v1` are fictional. No device capture, private identifier,
ride, receiver quality mapping, acquisition accuracy or physical certification
is represented. Satellite/quality values exercise optional serialization only.

The literals were reviewed against `src/storage.cpp` `formatName`,
`formatHeader`, `formatTimestamp`, `formatGpsFix`, `formatImu` and `formatCamera`,
and the published layout tables. They were authored with standard Python string
operations independently of production storage formatting, telemetry parsers or
GPX code. `expectations.json` contains handwritten values/counts/reasons, not
computed parser/exporter output. Keep these files frozen except for a documented
source-contract correction. No production parser may generate expectations.

The five schema-named examples each admit two points in one segment. Mixed
schemas legally interleave all permitted kinds; unqualified sensor payloads and
camera fields do not establish measurements or camera state. Counts are GPS
35/36, raw 88/124/147 and camera 41. The contract test independently compares the
named header to firmware string literals and checks each layout and count.

| Focused file | Independent expected behavior |
| --- | --- |
| coordinates | Five points: all hemispheres, zero, poles, ±180 and optional altitude ±100000/zero/absent; +180 normalizes to -180; optional sat/quality max, zero and absence differ |
| continuity | Eight GPS snapshots become six points/five segments; repeated receipt suppressed; stale retained fix excluded; gap10000 connects,10001 splits; loss and reset break |
| clock_variants | Five points; solution mode two untimed and two segments; receipt mode three untimed, four segments, one unknown uncertainty; partial calendar stays partial, copied correction never retimes solution |
| counter_saturation | Initial nonzero dropped/saturated bounds remain observations; later decrease breaks rather than inventing a reset session |
| interleaved_loss | Global GPS accepted20 and per-kind IMU accepted1 are independent scopes; control loss schedules a GPS break |
| fix_exclusions | Missing, NoFix and Invalid rows excluded; next valid coordinate starts fresh segment |
| solution_utc_correction | Backward/equal source UTC each break between distinct receipt solutions |
| conflicting_solution | Same receipt, different coordinates is malformed; writer owns strict/salvage decision |
| remaining malformed files | Explicit strict/salvage verdicts, known field/category and prefix point counts in JSON |
| tail_delimiter, tail_digit, tail_complete | Missing final LF is discarded in salvage regardless of plausible fields; strict refuses it |
| non_ascii, overlong | Fatal byte-level errors even in salvage; overlong physical row4098 bytes |

Reason lists use semantic cause names; implementation diagnostics may use a
more specific fixed token vocabulary. They are not text snapshots of stderr.
Repeated causes describe causal observations, not necessarily distinct segment
starts: simultaneous reasons create one boundary. Counts are normative. Optional
normalized-point JSON fields are explicit when their absence/zero/boundary is
under test; omitted expectation keys do not imply an omitted GPX extension.

The existing immutable
[`../telemetry/gps_v1_timestamp_variants.csv`](../telemetry/gps_v1_timestamp_variants.csv)
is reused through a pinned SHA-256 assertion. It is independent clock
serialization evidence with no valid positions, **not a ride**. Its existing
provenance/license remains unchanged. No fixture can count unknown silent
power-cut loss or detect every plausible newline-terminated tear.

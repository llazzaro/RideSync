# Portable ride-log export investigation — #54

Investigated October 13, 2026 against source revision
`df73cc6` and the primary sources linked below (accessed on that date).
Decision: **Implement an offline, dependency-free Python GPX 1.1 exporter next;
defer FIT.** Accept GPS records from all five current storage schemas. Export
only evidenced current positions, preserve session identity, split discontinuous
spans, and omit unavailable absolute time. This is a design and finite delivery
plan as recorded for #54, **not an implementation or observed import result at
that investigation boundary**. The later implementation status and usage below
are a separate October 13, 2026 software update.

This finding completes the investigation boundary of
[#54](https://github.com/llazzaro/RideSync/issues/54) under
[acceptance_policy.md](acceptance_policy.md). No firmware changes, new issues,
camera qualification or ride-readiness claims follow from it. Existing physical
owners in #22/#31/#46 retain their checks.

## Implemented CLI usage — October 13, 2026

`scripts/export_gpx.py` and `tools/ride_export/` now implement the GPX-first
reader, projection, report and publication path described here. #54 remains a
completed design investigation. #70–#73 are implemented and verified at
[`cc992fa`](https://github.com/llazzaro/RideSync/commit/cc992fafe37d0ed7443c3597331881dbeaf6f500)
with [passing CI](https://github.com/llazzaro/RideSync/actions/runs/38377168267);
their software boundary is independent of #74.
The [dated evidence](software-results/2026-10-13-gpx-export/README.md) records an
actual Python 3.11 CLI export and schema validation. **#74 remains open: QGIS
is unavailable, and its required observed import is Blocked / Not run.**

From the repository root, with Python 3.11 (standard library runtime only):

```sh
python3.11 scripts/export_gpx.py --input ride.csv --output ride.gpx --report ride-report.json
python3.11 scripts/export_gpx.py --help
```

One invocation reads one session in GpsV1, MixedV2, CameraV3, MotionV4 or
MotionV5, using the exact schema/header/markers in the table below. It writes
GPX 1.1 and JSON report version 1. All three paths must be distinct, including
symlink/hardlink aliases; existing destinations are refused. Use fresh output
and report names for every run. The report includes input basename/size/SHA-256,
metadata, selected options, streamed diagnostics, point/segment counts and
counter observations. Treat source and outputs as location-bearing private
artifacts unless explicitly declared synthetic/public.

Default `--time-source solution` emits only complete validated solution UTC.
`--time-source receipt-estimate` instead uses eligible copied row associations,
with explicit uncertainty and unmeasured receipt latency/drift. Neither fills
missing time or establishes camera alignment. `--max-gap-ms 10000` is the
default; a GPS receipt gap strictly greater than the positive threshold splits
the track. Changes in emitted-time availability/basis also split it.
Untimed GPX remains useful for mapping; it is not promised video-overlay input.

Strict mode rejects malformed data and unterminated tails before publication.
`--salvage` discards malformed complete data rows/final unterminated tails with
diagnostics and segment breaks. Header/version/session errors, invalid encoding
and overlong lines remain fatal. No points is an error. Exit codes are:

| Status | Meaning |
| --- | --- |
| `0` | Published GPX/report, no malformed rows discarded; excluded fixes, duplicates and untimed points may still exist |
| `2` | CLI, format or no-point error; no completed GPX is published |
| `3` | Published salvage result with discarded malformed rows/tail |
| `4` | I/O or publication failure; stderr identifies an already-published GPX if report publication fails |

Input bytes are preserved and a detected concurrent input change aborts the
export. Complete staged files are published exclusively. The two output files
are **not an atomic transaction**; a report publication failure can leave a
complete GPX, which is retained. This provides no physical power-loss durability
promise. Global GPS and per-kind counters are separate scopes, can overlap and
must not be summed into exact missing positions. Silent losses and plausible
newline-terminated tears can remain undetectable.

Memory checks compare 100 and 20,000 discarded-row diagnostic streams with
`tracemalloc`, using a declared 32 MiB incremental and total Python-allocation
budget; measured results and their limits are in the dated evidence. This is a
host software check, not a firmware memory or whole-process RSS guarantee.
Development schema checks use pinned lxml and an ignored official XSD cache
prepared by `scripts/prepare_gpx_schema.py`; the exporter never downloads files
or accesses the network at runtime. FIT stays deferred; generated-file
Insta360/VIRB acceptance, extension support and footage alignment remain
unresolved. No hardware-confirmed evidence is added.

## Audited inputs and existing tools

The contract is in [log_format.md](log_format.md),
[telemetry_record.h](../include/telemetry_record.h) (`StorageFormat`),
[storage.cpp](../src/storage.cpp) (`formatName`, `formatHeader`, `formatTimestamp`,
`formatGpsFix`, `Storage::format`) and
[storage.h](../include/storage.h) (`kMaxRowBytes=3072`). The GPS header has 35
named columns in every format. Mixed files have heterogeneous rows: their GPS
rows add a `gps,` prefix although the named header itself has no kind column.

| Admit in first exporter | Exact first line | Required layout markers after header | GPS row |
| --- | --- | --- | --- |
| GpsV1 | `#ridesync_gps,1` | None | 35 columns |
| MixedV2 | `#ridesync_telemetry,2` | `#imu_layout,2,see_docs/mixed_telemetry.md` | `gps` + 35 columns |
| CameraV3 | `#ridesync_telemetry,3` | MixedV2 IMU marker; `#camera_layout,3,see_docs/log_format.md` | Same as v2 |
| MotionV4 | `#ridesync_telemetry,4` | `#imu_layout,4,see_docs/motion_logging.md`; camera marker | Same as v2 |
| MotionV5 | `#ridesync_telemetry,5` | MotionV4 markers; `#dynamic_layout,1,see_docs/dynamic_motion_estimator.md` | Same as v2 |

Require one exact named GPS header, one nonzero decimal uint64 `#session` ID,
firmware/provenance metadata and policy line; compare every row's session to the
metadata. Unknown versions, missing/duplicate/conflicting markers or embedded
second file headers are fatal. Filename identity is advisory: preserve it in
provenance, but use metadata and row IDs, not filename guesses. One invocation
accepts one file/session; independent sessions produce separate files. Duplicate
session IDs across independently supplied logs cannot be resolved automatically.

[mixed_telemetry.md](mixed_telemetry.md), [motion_logging.md](motion_logging.md)
and [dynamic_motion_estimator.md](dynamic_motion_estimator.md) define non-GPS
layouts. Recognized non-GPS kinds (`imu`, `config`, `health`, `control`, `camera`
where permitted) are counted and skipped after version-specific column-count,
common timestamp/session and counter checks. The exporter does not certify
their sensor/camera payloads. Unknown kinds fail; non-GPS rows never become
positions or determine GPS cadence.

The [test parser](../test/telemetry_parser.py) is an independent oracle, not a
production import API: `parse` materializes the whole file, rejects any partial
tail, excludes GpsV1, and `_parse_record` retains only the common timestamp fields
of a GPS row. Do not reuse it as the exporter or silently expand its acceptance.
[prepare_dynamic_motion_replay.py](../scripts/prepare_dynamic_motion_replay.py)
requires strict MotionV4 and an explicitly modelled IMU cadence;
[replay_dynamic_motion.py](../scripts/replay_dynamic_motion.py) runs the
experimental estimator against normalized inputs. Neither produces portable
tracks, measured acquisition timestamps or trustworthy motion overlays. Reuse
their explicit evidence labels and refusal to overwrite existing output.

## Proposed mapping and time policy

The [GPX 1.1 specification](https://www.topografix.com/gpx/1/1/) supplies track
segments, optional elevation/time/satellite fields and foreign-namespace
extensions. Use the `http://www.topografix.com/GPX/1/1` namespace, version `1.1`,
creator `RideSync offline exporter`, one named track `RideSync session <ID>` and
ordered, nonempty `trkseg` elements. The [official XSD](https://www.topografix.com/GPX/1/1/gpx.xsd)
is the implementation validation authority; pin its bytes/hash before tests.
All choices below are **proposed exporter policy**, not new firmware guarantees.

| Input | Output and admission |
| --- | --- |
| `fix_validity`, `fix_valid` | Point only for `1` (Valid) and `1`; Missing/NoFix/Invalid/Stale break continuity, even if retained coordinates are present |
| `latitude_deg`, `longitude_deg` | Finite signed decimal degrees; retain eight decimal places. Latitude within ±90; longitude within ±180 at input. Canonicalize exactly +180 to -180 for the GPX exclusive upper bound; report normalization. Zero is a valid coordinate, never a missing-value substitute |
| `altitude_msl_m` | Optional `ele` in metres; preserve serialized value as a plain decimal without adding precision; validate firmware bounds ±100000. No terrain lookup, geoid adjustment or interpolation |
| `source_utc_date`, `source_utc_time` | Preferred optional `time`: validated Gregorian 2000–2099 date plus `HH:MM:SS.cc`, serialized with `Z`. Both required; use GNSS solution time once per solution, never file mtime or host timezone |
| `satellites` | Optional `sat`, integer 0–65535; blank means omit, not zero satellites |
| `fix_quality` | Preserve optional raw 0–255 code in RideSync extensions; omit GPX `fix`. No evidenced receiver-code-to-GPX-enum mapping exists here |
| `speed_m_s`, `course_deg` | Preserve optional original values/units in extensions, with firmware admission bounds. GPX 1.1 has no standard speed/course point children; do not derive motion, distance or editor-specific fields |
| Common clock/anchor fields | Preserve copied association, monotonic time and available uncertainty in extensions; never re-anchor old rows |

Use an exporter namespace `urn:ridesync:gpx:export:1`. Point extensions contain
session ID, input line, monotonic and fix receipt/age, fix validity/quality,
time basis (`solution`, `receipt_estimate` or `absent`), original source calendar
and available copied anchor fields. Missing values stay omitted and unknown
uncertainty stays explicitly unknown. Extensions follow standard point children
in XSD order and use XML escaping, with a small project XSD checked separately
because GPX validates foreign extensions laxly. Importers may ignore them.

Default time mode is `solution`: valid coordinates without complete solution UTC
remain untimed points. A half-present but otherwise valid calendar remains
partial evidence: omit `time` and diagnose it. A malformed present calendar
field is a malformed row. Never use an expired anchor, latest anchor,
monotonic elapsed time or a different row to fill missing UTC.

An explicit `--time-source receipt-estimate` mode uses only that row's
`utc_estimate_ms` when `has_utc_estimate=1`, anchor quality Fresh, monotonic
quality Valid and all copied association equations hold. It does not mix in
solution time. Serialize integer POSIX milliseconds as UTC with three fractional
digits, reject unrepresentable dates, and preserve uncertainty-known/bound/age.
It may emit time with unknown uncertainty, but diagnoses it as an unmeasured
receipt estimate. Freshness is not accuracy: UART latency and oscillator drift
are unmeasured. Neither mode establishes video synchronization. A known zero
uncertainty bound is distinct from unknown and is not hardware validation.

Validate the common timestamp contract in [log_format.md](log_format.md),
including session duration, enum/boolean values, presence rules, receipt ≤
monotonic time, age equations and available UTC = anchor UTC + age. Reject
contradictory fields and nonfinite numbers; do not accept a merely parseable CSV
row as valid evidence. Available GPS age likewise equals monotonic time minus
fix receipt; a valid solution must have its recorded receipt for continuity.
The source currently leaves satellites/quality optional: [gnss_parser.cpp](../src/gnss_parser.cpp)
does not populate those fields for the selected CGPSINFO schema. Fixtures may
exercise them synthetically without asserting receiver support or 2D/3D/DGPS.
WGS84 follows the documented GNSS/GPX route; actual receiver datum/accuracy stays
a physical qualification premise, with no offline conversion guessed.

## Segmentation, duplicates and loss

Keep file order. Never sort, interpolate, smooth, join sessions, filter points by
an invented motorcycle speed bound, or bridge a known gap.

- Break before the next admitted point after a bad/stale/missing fix, rejected
  row, salvaged tail, increased `dropped`/`rejected`/`lost` counter, backward
  monotonic time, or change in emitted-time availability/basis. A backward clock
  with the same session ID is diagnosed as reset/identity misuse, not a new
  legitimate session. Row-session mismatch is fatal.
- Default `--max-gap-ms 10000` splits when consecutive unique GPS fix receipt
  times differ by more than ten seconds; equality stays connected. A backward
  fix receipt also breaks continuity and is diagnosed. This is a
  configurable conservative display policy, not the receiver's firmware stale
  threshold. Compare GPS solutions, not interleaved high-rate IMU rows.
- Split for equal/backward emitted UTC between distinct solutions. In receipt
  mode also split at an anchor sequence change, even if the correction happens
  to remain increasing. In solution mode an anchor change alone cannot retime
  the GNSS solution. Do not repair UTC by offsets.
- Suppress identical repeated snapshots of a solution using the last GPS
  `fix_receipt_ms` plus the solution payload (coordinates, optional fields and
  source UTC), retaining the first point. The same receipt with different
  payload is malformed; a later receipt with the same location is a valid
  stationary sample. Across a break, allow a fresh segment to begin at that
  location. No unbounded deduplication set is needed.
- Require six observational counters to be bounded uint32 values. GPS rows use
  global `Storage::health()` counters; other kinds use their own
  `Storage::kindHealth(kind)` counters. Compare snapshots only within the same
  scope (global, imu, config, health, control or camera), in file order. Decreases
  within a scope break continuity and diagnose possible corruption without
  guessing a reset identity; differences between scopes do not. Report maxima,
  initial nonzero loss, later increases/decreases and saturation per scope.
  Saturation is a lower bound. Counters are sampled before writes; do not sum
  snapshots or overlapping global/per-kind counts, claim exact missing GPS
  points, or treat `flushed` as physical durability. An increase seen on a skipped
  non-GPS row still schedules a break before the next point.

Unknown silent power-cut loss cannot be counted from the file. Report observed
row exclusions separately from firmware counters and explicitly state that
neither measures all lost data. No empty segments; a single-point segment is
allowed and reported (it may not draw a line in a mapping tool).

## CLI, bounded parsing and alternatives

Proposed invocation, **not available yet**:

```sh
python scripts/export_gpx.py --input ride.csv --output ride.gpx \
  --report ride.export.json --time-source solution --max-gap-ms 10000
```

Require distinct input/output/report paths, reject aliases including symlinks
and hardlinks, and refuse existing outputs. Preserve input bytes. Publish GPX
only after a complete successful parse, using a temporary file and exclusive
publication; publish a report likewise. Two-file publication is not universally
atomic: if report publication fails after GPX publication, exit nonzero and name
the already-created GPX; never overwrite or remove preexisting data.

Use Python 3.11 standard library (`argparse`, `csv`, `decimal`, `datetime`,
`hashlib`, XML escaping). Read at most 4096 bytes per physical line (including
newline), enough for the current 3072-byte firmware buffer; strict ASCII/LF,
unquoted single-line CSV with exact versioned field counts. GPS numbers must
be plain decimals; coordinates have at most eight fractional digits, matching
the firmware's serialization. Reject unsupported exponent/extra-precision forms
instead of guessing or rounding positions across the GPX longitude boundary.
Overlong lines and
invalid encoding fail without reading the remainder into memory. Stream GPX,
SHA-256 provenance and JSON diagnostics to temporary files; hold one row and the
previous solution/clock/counters, not the complete log, XML tree or list of errors.
The fixed report summary plus streamed per-line diagnostics can grow on disk
but memory remains bounded. No automatic downloads or network access at runtime.

Default strict mode fails on malformed complete rows or any non-LF final row;
publishes no GPX. Explicit `--salvage` skips malformed complete data rows, breaks
continuity, and discards a final unterminated row even if its fields look complete.
It cannot salvage an invalid header/version/session or overlong line. Keep and
report the valid prefix; never repair partial digits or splice torn rows. A
newline-terminated tear with plausible values can be undetectable: report this
limit. Zero admitted points is an error and publishes no track.

Report exporter version, input basename/size/SHA-256, schema/session,
firmware/provenance/policy, selected options, input and emitted counts, ignored
kinds, duplicate count, untimed/unknown-uncertainty points, normalization,
segment reasons, skipped/truncated rows and counter observations. Diagnostics
identify line, field, reason and remedy without reproducing private coordinates.
Exit `0` for an export without discarded malformed rows (expected bad fixes,
untimed points and duplicates remain visible in summary), `2` for CLI/format/no
points errors, `3` for published salvage with discarded rows, `4` for I/O or
publication failure. Each status has an actionable stderr summary; JSON is the
stable machine-readable contract.

| Alternative | Dependency/licensing and trade-off | Decision |
| --- | --- | --- |
| Standard-library streaming writer | Project-authored MIT code; Python's runtime carries its own license. Small fixed GPX subset; explicit schema/order tests required | Select |
| [gpxpy](https://github.com/tkrajina/gpxpy) | Apache-2.0, extra pinned package and notices; convenient GPX object API, but cannot replace RideSync validation and a full track object conflicts with the memory goal | Defer unless manipulation needs justify it |
| General CSV/GPS conversion tool | Still needs a RideSync adapter for heterogeneous schemas, validity, time and loss; adds dependency without resolving admission | Do not select for v1 |

XSD validation is a test dependency, not exporter runtime: pin a validator such
as lxml in development requirements and retain schema provenance/redistribution
notices after checking the downloaded XSD's terms. No library or schema is
vendored by this investigation.

## FIT and downstream compatibility

**FIT: defer**, not infeasible. Garmin's
[SDK overview](https://developer.garmin.com/fit/overview/) identifies profile and
file-type guidance; its [official Python SDK](https://github.com/garmin/fit-python-sdk)
now documents an Encoder as well as Decoder. A later activity exporter must pin
an SDK/profile, specify file ID, records, timer events, lap/session/activity
summaries and valid timestamps, map positions to profile semicircles and scaled
altitude/speed, handle invalid sentinels, and verify header/length/CRC by independent
decode. RideSync's heterogeneous CSV is not FitCSVTool input. Missing UTC,
summary semantics, resets and discontinuities need explicit policy; no fabricated
activity duration, distance, manufacturer identity or trusted IMU fields.

The [SDK license](https://raw.githubusercontent.com/garmin/fit-python-sdk/main/LICENSE.txt)
is a Garmin FIT Protocol agreement, not MIT/Apache. Review the selected release's
distribution/modification terms before incorporating it. SDK availability and a
valid binary do not prove editor import. FIT adds profile, dependency and
qualification work without improving the initial portable map-track outcome.
Reconsider only for a declared consumer requiring fields GPX cannot carry, with
one sample import and a resolved licensing/dependency choice.

| Consumer | Dated primary-source evidence | RideSync status / consequence |
| --- | --- | --- |
| [QGIS GPX loading](https://docs.qgis.org/3.40/en/docs/user_manual/working_with_gps/plugins_gps.html#loading-a-gpx-file) | Documents GPX layers and WGS84 coordinates. 3.40 documentation is a stable reference, not a claim it is the newest supported release | Documented; select QGIS for the finite map import below. RideSync import Not run |
| [Insta360 Studio Stats](https://onlinemanual.insta360.com/studio/en-us/operation-guide/edit-function/dashboard-function) | Manual third-party GPX/FIT from v5.7.6; mode restrictions, flat-video Stats export, and no Studio Stats time correction documented | Documented capability; generated RideSync-file acceptance, extension use and footage alignment Unresolved / Not tested |
| [Insta360 app dashboard](https://onlinemanual.insta360.com/acepro2/en-us/camera/features/dashboard) | Local FIT/GPX import; warns that GPX without timestamps is incompatible. This page is Ace Pro 2 documentation | Documentary lead only, not X5/ONE RS qualification or proof arbitrary files work. Untimed GPX is a map product, not promised overlay input |
| [Garmin VIRB Edit](https://www.garmin.com.sg/products/apps/virb_edit/) | Describes G-Metrix overlays from VIRB/compatible Garmin data; reviewed page does not specify arbitrary RideSync GPX/FIT acceptance | Unresolved; neither GPX nor FIT exporter promises VIRB compatibility |

No editor or mapping tool was exercised here. GPS receipt UTC precision,
camera clock alignment, IMU lean/acceleration overlays, installed-app availability
and per-editor extension support remain unresolved. Do not infer them from XML
validity or promotional lists of available gauges.

## Finite implementation plan

The table preserves the original #54 implementation plan. Steps 1–4 are now
implemented and verified in #70–#73, with passing focused tests and full CI.
Step 5 has reproducible synthetic export evidence, but its required QGIS import
is Blocked / Not run, so #74 is unfulfilled. Each boundary remains unchanged.

| Step | Behavior and affected modules | Verification | Completion boundary |
| --- | --- | --- | --- |
| 1. Freeze input fixtures | New `test/fixtures/gpx/` and fixture README; author independent GPS payloads for GpsV1/MixedV2/CameraV3/MotionV4/MotionV5 plus legal interleaved kinds/markers. Reuse existing immutable GPS timestamp fixture as clock evidence, not a real ride | Handwritten expected field/point counts; compare schema/header against `storage.cpp`; label every fixture synthetic and retain licenses/provenance | Five admitted schemas and rejection cases have independent expected results; no production parser reuse |
| 2. Bounded reader | New `tools/ride_export/` reader and `test/test_gpx_export.py`; exact metadata/layout/row checks, strict and salvage modes, counters and session ownership | Unknown version/kind, duplicate header, absent markers, session mismatch/zero/uint64 overflow, illegal booleans/anchor equations, NaN/Inf, empty optional values, extra/missing columns, overlong/non-ASCII line, malformed middle row and power-cut tails at delimiter/digit/newline boundaries | Every input either yields validated GPS evidence/explicit exclusions or a deterministic error within line and memory bounds |
| 3. GPX projection | New writer in `tools/ride_export/`, extension XSD and fixtures; implement mapping, time modes, duplicate suppression and segment policy | All four hemispheres, zero/±90/±180 and out-of-range coordinates; negative/zero/±100000 altitude and missing altitude; sat 0/65535 and raw quality boundaries; invalid/stale retained fixes; missing/expired/unknown-uncertainty UTC, leap day, rejected leap second, UTC correction, duplicate/conflicting solutions, reset, loss/saturation and exact gap boundary. Assert literal expected XML values, point/segment counts; validate official pinned GPX XSD and project extension XSD | Schema-valid deterministic output with no invented coordinates/time, correct omission and finite state; GPS-only projection makes no IMU claims |
| 4. CLI and publication | New `scripts/export_gpx.py`; report streaming/provenance, exclusive publication and documented exit statuses; update dev requirements/CI only for exporter checks | Subprocess tests for every exit code, no-point input, path aliases, existing destinations, unreadable input, disk/write/report publication failures and unchanged input hashes. Process a generated large log and compare small/large peak memory under a declared ≤32 MiB incremental budget | Usable offline CLI, actionable reports, bounded memory and preservation of original logs and existing files |
| 5. One map smoke and delivery | Update this note, `log_format.md` and README with actual usage/results; use one installed QGIS build and record exact version/OS | Export a single synthetic two-segment track with known WGS84 coordinates, optional negative elevation and a missing-time span; add GPX Tracks and Track points layers, inspect point count/coordinates/elevation and segment separation. Record screenshot plus expected/observed values and file hash; a consumer joining segments is a failure to resolve, not a pass | One recorded observed map import (or explicit blocker if QGIS unavailable), focused unit/schema/CLI/lint checks and documentation checks; no video-editor compatibility or hardware certification |

The QGIS run is a bounded software import check using public synthetic locations;
do not schedule rides or expand to a matrix of editors. If it fails, diagnose the
specific exporter/consumer discrepancy and record it before claiming map delivery.
Passing unaffected firmware evidence is reused under the acceptance policy.

## Investigation acceptance evidence — historical #54 boundary

- **Documented/source-audited:** five storage formats and common GPS payload,
  immutable time contract, stale-fix distinction, replay limitations, GPX schema,
  library/SDK alternatives and downstream documentary restrictions.
- **Synthetic:** existing timestamp fixture demonstrates clock serialization
  cases; planned exporter fixtures and XML checks have not been executed.
- **Observed:** no generated RideSync GPX/FIT, QGIS import or editor import in this
  ticket. Those are future implementation evidence, not closure prerequisites
  for this investigation.
- **Hardware-confirmed:** none added. Existing camera/GNSS/card results retain
  their original scope and owners.

Closure decision is GPX-first implementation with the explicit contract and
five-step boundary above; FIT is deferred and arbitrary video-overlay import is
source-backed unresolved. Documentation delivery does not complete that plan.

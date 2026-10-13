# Offline bookmark export

`scripts/export_bookmarks.py` reads one complete BookmarkV6 telemetry log
(`#ridesync_telemetry,6`) or combined CameraHealthV7 log
(`#ridesync_telemetry,7`) and writes a portable marker sidecar. It requires the
version's exact named header and layout markers and validates every complete
GPS, IMU/config/health/control, camera, camera-health, bookmark and marker-audit
row, including its
session and copied timestamp. V7 requires the camera-health layout marker;
V6 has no camera-health rows. Camera marker audit rows are
evidence about attempted camera traffic; they do not create portable bookmarks.
The input log remains the source of record.

Run from the repository root with Python 3.11 or newer:

```sh
python3 scripts/export_bookmarks.py /path/to/telemetry.csv /path/to/empty-output-directory
```

The output directory must already exist. The command creates
`ridesync-markers-v1.csv` and `ridesync-markers-report-v1.json` exclusively. If
either output already exists, or parsing/publication fails, it leaves existing
files untouched. It never truncates the source or an existing output. Each
output covers one session. A source log with no bookmark rows yields a header-only
sidecar and an accepted count of zero.

The sidecar header is fixed:

```text
schema,session_id,bookmark_id,monotonic_ms,source,gesture,anchor_quality,anchor_sequence,anchor_receipt_ms,anchor_utc_ms,anchor_age_ms,uncertainty_known,uncertainty_ms,utc_estimate_ms
```

`schema` is `ridesync-markers-v1`. Integer values are decimal; absent fields are
empty. `anchor_quality` is `Missing`, `Fresh` or `Expired`. A fresh anchor has a
UTC estimate. Missing or expired anchors do not. Markers sharing one monotonic
millisecond remain separate and ordered by increasing ID; duplicate, zero or
out-of-order IDs fail the export. Serial source/gesture is `(1,0)`, and physical
source/gesture is `(2,1..3)`. The exporter refuses a malformed complete row,
wrong session or invalid timestamp. It may exclude only one unterminated final
power-cut row and records that exclusion. It does not reconstruct lost markers.
This also applies when the first data row is cut after otherwise complete
metadata: the sidecar is header-only and the report includes the excluded row
and source hash. A truncated or incomplete metadata block still fails.

The report contains the input SHA-256, source and sidecar schemas, session ID,
complete input row count, accepted marker count, excluded final-row count, and
the maximum available bookmark kind counters (`accepted`, `dropped`, `rejected`,
`lost`, `written`, `flushed`) seen in complete bookmark rows. These counters are
only available when a bookmark row persists; `loss_evidence_available` distinguishes
that case from a header-only log. A gap in IDs and the final counters
are not a complete loss ledger. A failed last marker or a power cut can leave no
later row reporting that loss.

## Explicit footage alignment

Supply an alignment file only after establishing clip zero from an observable
sync event. For example, if session millisecond 10000 is clip zero, the clip is
five seconds long and begins at project second 30:

```json
{
  "schema": "ridesync-alignment-v1",
  "session_id": 7,
  "clips": [
    {
      "path": "/absolute/path/to/clip.mp4",
      "sha256": "<64 lowercase hex digits of the clip's SHA-256>",
      "duration_seconds": "5",
      "clip_zero_session_ms": 10000,
      "project_start_seconds": "30",
      "method": "visible_sync",
      "uncertainty_ms": 100
    }
  ]
}
```

The path must name a readable file whose hash matches. `duration_seconds` and
`project_start_seconds` are nonnegative decimal strings; duration must be
positive. `clip_zero_session_ms` and `uncertainty_ms` are nonnegative integers.
`method` is an explicit identifier describing how the offset was established.
Use a separate mapping for each clip or chapter. Up to 16 clips and a 64 KiB
alignment file are accepted. No automatic alignment is inferred from UTC,
recording intent or a camera command response.

```sh
python3 scripts/export_bookmarks.py /path/to/telemetry.csv /path/to/empty-output-directory \
  --alignment /path/to/alignment.json
```

For continuous normal-speed footage, `clip_seconds =
(monotonic_ms - clip_zero_session_ms) / 1000`, and `project_seconds =
project_start_seconds + clip_seconds`. A marker at 12500 ms maps to clip 2.5 s
and project 32.5 s in the example. The portable sidecar retains every valid
marker. Markers before clip zero or at/after clip duration are counted as
`outside_clip` for that clip and omitted from its editor placement; no position
is clamped. The
report includes placement counts, clip identities, alignment method and
uncertainty, plus the first ten placement previews. With no alignment file,
the report says `unknown` and emits no editor positions. Speed ramps, pauses,
dropped footage and drift transforms are outside this version's mapping.

With an alignment file, the command also writes `ridesync-reaper-v1.lua`. Review
its comment previews and the report before running it in the intended REAPER
project. The script calls REAPER's documented
[`AddProjectMarker2`](https://www.reaper.fm/sdk/reascript/reascripthelp.html#AddProjectMarker2)
API for in-clip markers,
using names such as `RideSync 7:2 [clip 1]`. It only adds markers; it does not
delete or replace existing project markers. Re-running the same script skips an
existing marker with the same name and position. It does not import the neutral
CSV as an assumed REAPER dialect.

## Verification and editor status

The [three-marker REAPER acceptance bundle](software-results/2026-10-13-bookmark-editor/README.md)
provides hashed synthetic inputs, generated outputs and exact expected positions
for the remaining import check. It does not establish an observed REAPER import.

Synthetic tests cover exact sidecar fields, session/ID/anchor validation,
malformed middle and final rows, exclusive publication, camera audit skipping,
explicit alignment, independent chapters, exact-duration/out-of-clip exclusion,
available loss-counter maxima, CLI/row-limit errors and a 10,000-marker bounded-memory
run. The parser limits each source line to 3072 bytes and reads the alignment
file within 64 KiB. The Python peak allocation in the synthetic large-input
test is asserted below 8 MiB; clip hashing and source parsing use 64 KiB and
fixed-size reads respectively.

On October 13, 2026, on macOS 27.0 arm64, no REAPER application was found in `/Applications`,
`~/Applications`, the Homebrew Caskroom or `PATH` during the #63 implementation.
The generated script has not been run in REAPER. Therefore the three-marker
expected/observed import check, installed version/hash and preservation of
existing project markers remain **unobserved**. The 32.5-second example is
synthetic arithmetic, not an editor observation. There is no general editor or
physical alignment certification from these tests.

For [#63](https://github.com/llazzaro/RideSync/issues/63), the portable schema,
strict parsing, bounded exclusive publication, explicit mapping and normal-speed
mapping criteria have synthetic software evidence. The pinned REAPER installation
criterion remains **Blocked / Not observed**. Complete it with one installation:
record version/OS and hashes of the source log, alignment, clip and generated
script; preview three synthetic marker positions/names; import alongside an
existing marker; record all three expected and observed seconds/names and confirm
the existing marker is preserved. No source/API review substitutes for that run.

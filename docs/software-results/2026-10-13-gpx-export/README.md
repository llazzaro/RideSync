# GPX exporter software evidence — October 13, 2026

Scope: #70–#74 offline host software. **Synthetic/schema-validated**, not a
ride, receiver observation, hardware certification or editor import. #54 is the
completed design investigation. #70–#73 have software implementations and
passing focused checks; final delivery still awaits the exact candidate commit
and CI. **#74 remains open because QGIS import is Blocked / Not run.**

## Public synthetic specimen and reproduction

Author: RideSync contributors, October 13, 2026; repository MIT license
([LICENSE](../../../LICENSE)). Input header/layout follows GpsV1. Session 42,
firmware `gpx-map-evidence-v1`, clocks and payloads are invented. Four independently
chosen WGS84 coordinate literals lie in public Atlantic Ocean locations near the
equator; elevation is a synthetic scalar, not bathymetry or a receiver reading.
No private route, camera capture, sensor measurement or real person is represented.
The source was authored with literal rows, without using the production reader
or writer to generate expected values. The CLI generated the output/report.

Files: [input CSV](synthetic-two-segments.csv), [GPX](synthetic-two-segments.gpx),
[JSON report](synthetic-two-segments-report.json). From the repository root:

```sh
python3.11 scripts/export_gpx.py --input docs/software-results/2026-10-13-gpx-export/synthetic-two-segments.csv --output docs/software-results/2026-10-13-gpx-export/synthetic-two-segments.gpx --report docs/software-results/2026-10-13-gpx-export/synthetic-two-segments-report.json --time-source solution --max-gap-ms 10000
```

This exact generation command ran successfully with Python 3.11.14 and exporter
1.0.0, status `0`, no diagnostics. For reproduction after checkout, change both
output/report names to fresh paths: the checked-in files are intentionally
refused as existing destinations. The original input SHA-256 remained unchanged
and agrees with report provenance (1,016 bytes).

| Artifact | SHA-256 |
| --- | --- |
| `synthetic-two-segments.csv` | `500090588422221fd667801fbdde7bb78ce39eecf3a6032d941d5fbba1b1075f` |
| `synthetic-two-segments.gpx` | `6d4b281889c804d0b99128dfb5b53c38246942141243d4f9975c5addcefa2fa3` |
| `synthetic-two-segments-report.json` | `83fa779d8d6fc47c5c11f9c9fcb79b998af1441fbe88e9d44debadae307a1edc` |

| Expected property, independently declared | Actual XML/report software observation |
| --- | --- |
| One track, 4 points, 2 nonempty segments of 2 points each | Matches |
| Segment 1 positions `(lat, lon)` = `(0,-1)`, `(0.001,-1)` | `0.00000000,-1.00000000`; `0.00100000,-1.00000000` |
| Segment 1 elevation `-2.5`, `0` metres | Exactly `-2.5`, `0` |
| Segment 1 UTC `2026-10-13T12:00:00.00Z`, `2026-10-13T12:00:01.00Z` | Exactly matches |
| Segment 2 positions `(0,-2)`, `(0.001,-2)` | `0.00000000,-2.00000000`; `0.00100000,-2.00000000` |
| Segment 2 elevation and time absent | Both omitted; 2 untimed points |
| Time availability change breaks before point 3 | `time_change: 1`; 2 segments |
| Exclusions, duplicates, discarded rows and longitude normalizations = 0 | All 0 |

Schema validation used development Python 3.14.7 with lxml 6.1.3; runtime export
used Python 3.11 standard library only. The official GPX XSD was prepared by:

```sh
.venv/bin/python scripts/prepare_gpx_schema.py
```

Official cached GPX 1.1 XSD SHA-256:
`9e4d1988b862edbe556305b130f8f6f1b29864fefd0dc02d5dab04ccdd1f34d6`.
Both full GPX and each RideSync extension element validated successfully:

```python
from lxml import etree
xml = etree.parse("docs/software-results/2026-10-13-gpx-export/synthetic-two-segments.gpx")
gpx = etree.XMLSchema(etree.parse("test/fixtures/gpx_schema/.cache/gpx-1.1.xsd"))
gpx.assertValid(xml)
project = etree.XMLSchema(etree.parse("test/fixtures/gpx_schema/ridesync-export-1.xsd"))
for element in xml.findall(".//{http://www.topografix.com/GPX/1/1}extensions/*"):
    project.assertValid(element)
```

## Finite QGIS check — Blocked / Not run

Host: macOS 27.0, build 26A428 (`sw_vers`). No QGIS build/version is installed
or exposed through the available app/tool inventory. Checks of `/Applications`,
`command -v qgis`, Spotlight `mdfind 'kMDItemFSName == "QGIS*.app"c'` and Python
QGIS module availability found none. The command and Spotlight checks were
independently repeated during evidence preparation. No large GUI application
was installed and no substitute mapping-tool campaign was performed.

| Required observed check | Status |
| --- | --- |
| Exact installed QGIS version | Blocked: unavailable |
| Load GPX Tracks and Track points layers | Not run |
| Inspect coordinates, elevations and point count in QGIS | Not run |
| Confirm disconnected spans remain separate in QGIS | Not run |
| Save screenshot of successful import | Not run; no screenshot exists |

XML validation and the table above do not establish consumer rendering or
segment preservation. Once QGIS is available, use this hashed specimen for the
single finite import, record the exact version/OS, inspect both layers against
the expected table, and save the actual screenshot. Joining spans is a
consumer/exporter discrepancy to resolve, not a pass. This missing observation
keeps #74 unfulfilled even when upstream component checks pass.

## Focused verification and limits

Final local focused candidate run reported by the coordinating implementer:
Python 3.11, **64 passing tests** (contract 11, reader 20, writer 14, CLI 19),
57.784 seconds. Pinned Ruff 0.16.10 passed. Independent core and delivery review
approved the fixes. Final candidate commit identity and CI result are pending;
these local checks do not claim CI success. No firmware rebuild is required
for these host/documentation changes under [acceptance policy](../../acceptance_policy.md).

The CLI memory test profiles fresh Python 3.11 processes after CLI import,
streaming 100 versus 20,000 malformed same-session rows through salvage/report
publication. `tracemalloc` allocation peaks were **153,404 bytes** and
**477,460 bytes**, delta **324,056 bytes**: both total and incremental peaks pass
the declared 32 MiB budget. This covers Python allocations during that synthetic
diagnostic workload, not whole-process RSS or device memory.

Source CSV remains the evidence record. GPX/report publication refuses existing
files and aliases and detects input change; both staged files complete before
exclusive publication. The pair is not an atomic transaction: report publication
failure after GPX publication returns status 4 and retains the GPX. Runtime
operates offline with no external dependencies/downloads. Status 2 covers
CLI/format/no-point errors; status 3 publishes salvage with discarded malformed
rows/tail; status 0 may still have expected exclusions, duplicates or untimed
points. Salvage never repairs partial values and cannot recover invalid metadata,
session, encoding or overlong input.

Global GPS and per-kind counter domains are separate overlapping observations,
never a sum or exact lost-position total. Silent power-cut loss and plausible
newline-terminated tears remain undetectable. Receipt estimates have unmeasured
latency/drift; no exported time proves footage synchronization. Untimed GPX is
useful for mapping, with no promised overlay input. FIT remains deferred;
generated-file Insta360/VIRB acceptance, extension support and footage alignment
remain unresolved. Hardware checks stay with #22/#31/#46.

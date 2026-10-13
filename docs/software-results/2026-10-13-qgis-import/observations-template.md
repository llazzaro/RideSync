# QGIS observed import record — template

**Not run.** Copy to a dated result file and replace placeholders only with
actual observations. This template does not satisfy #74.

| Run identity | Recorded value |
| --- | --- |
| Date/time and operator | Not run |
| Repository revision | Not run |
| OS version/build | Not run |
| QGIS exact version/build | Not run |
| GDAL version | Not run |
| GPX SHA-256 before / after | Not run |
| Tracks provider and source URI | Not run |
| Track points provider and source URI | Not run |
| Layer CRS / project CRS | Not run |
| Tracks feature count / geometry type | Not run |
| Track points feature count | Not run |
| Tracks geometry parts / vertices per part | Not run |
| Actual coordinate/elevation/time field names or inspection method | Not run |

| Expected point `(lat, lon)` | Expected elevation | Expected UTC | Observed values / evidence |
| --- | --- | --- | --- |
| `(0, -1)` | -2.5 m | 2026-10-13T12:00:00.00Z | Not run |
| `(0.001, -1)` | 0 m | 2026-10-13T12:00:01.00Z | Not run |
| `(0, -2)` | absent/null | absent/null | Not run |
| `(0.001, -2)` | absent/null | absent/null | Not run |

| Required observation | Outcome / evidence |
| --- | --- |
| Four points with expected WGS84 geometry | Not run |
| Elevation and UTC values, including absent spans | Not run |
| Two disconnected rendered spans, no bridge | Not run |
| Two geometry parts, two vertices each | Not run |
| Screenshot path(s) and SHA-256 | Not run |
| Saved project path and SHA-256, if retained | Not run |
| Discrepancies or fields unavailable in consumer | Not run |
| Final disposition: pass / fail / blocked | Blocked pending QGIS |

Link the actual screenshot(s) and supporting geometry/table observations here.
Do not turn source XML expectations into consumer observations. If any required
value or segment separation is unverified, keep that row explicit and #74 open.

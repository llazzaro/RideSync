# QGIS observed GPX import — #74

Observed October 13, 2026 at 16:59 UTC by Codex using the native QGIS GUI and
its Python console. **Pass for the finite 2D map import**, with the Tracks Z
representation limit below. This is software consumer evidence for the declared
public synthetic specimen, not a ride, receiver measurement, video overlay or
hardware qualification. The earlier unavailable-app check is retained below.

## Run and installation identity

Repository candidate: `5dc3d2909d5efb0a932066e48161eeebf3bce2fb`.
Host: macOS 27.0, build 26A428, arm64. Installed QGIS **3.44.15-Solothurn**,
GDAL **3.13.3 "Iowa City", released 2026/08/13**. The executable contains arm64
and x86_64 architectures. `codesign --verify --deep --strict` passed and
`spctl --assess --type execute -v` accepted Notarized Developer ID. No sudo,
security bypass, app overwrite or quarantine removal was used.

The [official QGIS download](https://www.qgis.org/download/) supplied LTR DMG
`qgis_ltr_final-3_44_15.dmg` (1,551,615,153 bytes). Its SHA-256 matched the
[official metalink](https://dl1.qgis.org/downloads/macos/ltr/qgis_ltr_final-3_44_15.dmg.meta4):
`302547ebee16d4d38ddfd509469cc86e337b23c545307cdbe13c2c2d233ab7cb`.
Mounted read-only and copied the signed `QGIS.app` to `~/Applications/QGIS.app`,
following the [official installation route](https://www.qgis.org/resources/installation-guide/#binary-packages-installers).

## Exact specimen and loader

Used the existing [GPX](../2026-10-13-gpx-export/synthetic-two-segments.gpx)
directly, without regeneration or conversion. The
[export evidence](../2026-10-13-gpx-export/README.md) contains its independently
chosen expected values and CLI/schema evidence. SHA-256 before and after import:
`6d4b281889c804d0b99128dfb5b53c38246942141243d4f9975c5addcefa2fa3`.

In a new blank QGIS project, Data Source Manager → Vector → File loaded the GPX
with default loader options; only `tracks` and `track_points` sublayers were
selected. Both layer CRS and project CRS were EPSG:4326 (WGS84).
The actual provider was `ogr`, with source URI prefix
`/Users/leonardo/workspace/RideSync/docs/software-results/2026-10-13-gpx-export/synthetic-two-segments.gpx`
and suffixes `|layername=tracks` and `|layername=track_points` respectively.
No basemap or location upload was used.

## Expected and observed values

The point attribute table showed four rows and the console inspected the actual
loaded layer features, not source XML. `x` is longitude and `y` latitude.
The [raw JSON observation](observed-import.json) preserves consumer values;
[console accessibility capture](qgis-console-observation.txt) records commands,
versions, provider URI, fields, WKT and output.

| Point (lat, lon), expected | Observed geometry (x, y) | Expected/observed `ele` | Expected/observed `time` UTC |
| --- | --- | --- | --- |
| (0, -1) | (-1, 0) | -2.5 m / -2.5 | 12:00:00 / 2026-10-13T12:00:00.000Z |
| (0.001, -1) | (-1, 0.001) | 0 m / 0.0 | 12:00:01 / 2026-10-13T12:00:01.000Z |
| (0, -2) | (-2, 0) | absent / NULL | absent / NULL |
| (0.001, -2) | (-2, 0.001) | absent / NULL | absent / NULL |

The attribute table displayed the timed rows as `13/10/2026 12:00:00 (UTC)` and
`13/10/2026 12:00:01 (UTC)` (localized decimal `-2,5` for elevation).
Console conversion with `toUTC().toString(Qt.ISODateWithMs)` confirmed the same
UTC instants. Segment IDs were `0,0,1,1`, point IDs `0,1,0,1`, track FID all `0`.

Tracks contained one feature named `RideSync session 42`, declared
MultiLineStringZ. Actual XY parts were exactly
`[(-1,0),(-1,0.001)]` and `[(-2,0),(-2,0.001)]`: **two parts of two vertices**.
Full extent showed two separated locations with no bridge; the closeups showed
each north–south span, and console geometry records both endpoints. Full extent alone compresses
each 111-metre span to nearly a dot across the approximately 111-km gap; the
closeups and actual imported geometry substantiate segment preservation.

**Consumer preservation limit:** GDAL's Tracks MultiLineStringZ representation
zero-filled the absent second-part Z coordinates:
`MultiLineString Z ((-1 0 -2.5, -1 0.001 0),(-2 0 0, -2 0.001 0))`.
Those zero Z values are consumer geometry padding, not measured or exported
elevations. Track points `ele` attributes remain NULL and their missing-elevation
features have 2D WKT (`Point (-2 0)`, `Point (-2 0.001)`). Use the GPX source and
Track points `ele` attributes as elevation evidence, not Tracks Z. This observed
representation difference is resolved as a documented 2D mapping limit; it does
not join spans or invent point attribute/time values. No exporter change follows.

## Captured evidence

- [Full extent, both loaded layers](qgis-full-extent.png)
- [First span closeup with console identifying its coordinates](qgis-segment-1.png)
- [Second span closeup with console identifying its coordinates](qgis-segment-2.png)
- [Point attribute table](qgis-point-table.png) and [accessibility values](qgis-point-table-observation.txt)
- [Console geometry/attribute output screenshot](qgis-console.png)
- [Consumer observation JSON](observed-import.json)

No saved QGIS project was needed. Screenshots are native app captures; console
commands inspect loaded features or change only map viewport. No screenshot was
constructed from XML. Artifact SHA-256 values:

| Artifact | SHA-256 |
| --- | --- |
| `observed-import.json` | `c83564bf935a2bcc636279d3d33282334aef2e94708e6b9a16ddd4aa022eead0` |
| `qgis-console-observation.txt` | `755701389f62ef696ccb312bb049080dace8e5c191b3b8497cf5b38e96f4c7ac` |
| `qgis-console.png` | `3acd82c12bb9e9db8341ab32122a668d83411e043b4a9503ee19fa073e2fb9ce` |
| `qgis-full-extent.png` | `c0b15c9c6381154ccf4a1afacd5167127e8f5fe0ae2adfc4c09d00769ee5c9af` |
| `qgis-point-table-observation.txt` | `b7fe03e800ccf676e81fabdb71c162dc16f7a2963382e33022129c62d0d3649c` |
| `qgis-point-table.png` | `1a59cae6e4feaadd0914253b1c93a56b11cdef642fb2ad51f9eeac3a162ff859` |
| `qgis-segment-1.png` | `c9105326cf9ed9cbf8ab8e5233e26a264b669e15441e566674d76e6ec78d39fd` |
| `qgis-segment-2.png` | `07de1ed736cfd5674354c39c0c00e8ad5b966e57cbe1083e8e2c71df74209f2a` |

## Earlier availability check — historical handoff

Earlier on October 13, before this installation, QGIS was unavailable on this
same host: no `qgis` executable, Python module, QGIS-named app/path entries or
Spotlight match. The earlier app inventory also had no QGIS. That was an honest
Blocked / Not run handoff, superseded by the actual observed import above.
The [unused observation template](observations-template.md) remains explicitly
Not run; it is a template, not this result.

## Delivery verification and disposition

All four points, point elevation/time including NULL, CRS, feature counts,
two geometry parts and rendered separation matched the independently declared
map expectations. #74's finite observed 2D map check is satisfied with the
prominent Tracks Z limitation. Existing exporter tests and passing CI are reused
for unchanged code under the [acceptance policy](../../acceptance_policy.md);
this update requires documentation checks only. Untimed GPX is useful for mapping;
no video-overlay, FIT, extension support, footage alignment or hardware claim is
added. Physical owners remain #22/#31/#46.

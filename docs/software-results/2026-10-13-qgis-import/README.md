# QGIS import handoff — #74

Prepared October 13, 2026. **Blocked / Not run**: this is an execution handoff,
not an observed QGIS result. The existing [export evidence](../2026-10-13-gpx-export/README.md)
already records the synthetic specimen, schema validation and passing software
checks. Reuse it under the [acceptance policy](../../acceptance_policy.md).

Availability was checked again on macOS 27.0, build 26A428. `qgis` was absent
from the command path, Python had no `qgis` module, and there were no QGIS-named
entries in `/Applications`, `~/Applications`, `/opt/homebrew/opt` or
`/usr/local/opt`. Spotlight's `mdfind 'kMDItemFSName == "QGIS*.app"c'` returned
no match. The computer-use app inventory also contained no QGIS. No QGIS
installation, layer import, rendered observation or screenshot was made.

## Specimen and independent expectations

Use the existing [GPX](../2026-10-13-gpx-export/synthetic-two-segments.gpx)
directly; do not regenerate it or import the CSV instead. These are invented
public Atlantic locations, not a ride or GNSS measurement. From repository root:

```sh
git rev-parse HEAD
shasum -a 256 docs/software-results/2026-10-13-gpx-export/synthetic-two-segments.gpx
sw_vers
```

Expected GPX SHA-256:
`6d4b281889c804d0b99128dfb5b53c38246942141243d4f9975c5addcefa2fa3`.
On another OS record its exact version/build using the native system information.
Capture QGIS's About/version information, including its GDAL version, and the
actual layer provider/source URI: importer choice is part of the result.

| GPX order | Segment | Latitude (degrees) | Longitude (degrees) | Elevation (metres) | UTC |
| --- | --- | --- | --- | --- | --- |
| 1 | 1 | 0 | -1 | -2.5 | 2026-10-13T12:00:00.00Z |
| 2 | 1 | 0.001 | -1 | 0 | 2026-10-13T12:00:01.00Z |
| 3 | 2 | 0 | -2 | absent | absent |
| 4 | 2 | 0.001 | -2 | absent | absent |

There is one GPX track, `RideSync session 42`, with four points and two nonempty
segments, each containing two points. Absent values must remain null/unavailable,
not acquire zero elevation or an invented time. Coordinate geometry uses
`x = longitude`, `y = latitude` in WGS84 (EPSG:4326).

## One finite import

1. Open one installed QGIS build and a new project. Record its exact version/OS
   before importing. Use a blank map; a basemap download is unnecessary.
2. Open Data Source Manager's **Vector** file loader, select the hashed GPX, and
   select its `tracks` and `track_points` sublayers. Record the provider and
   source URI from each layer's properties. The
   [QGIS vector loading documentation](https://docs.qgis.org/3.40/en/docs/user_manual/managing_data_source/opening_data.html#loading-a-layer-from-a-file)
   covers file loading; [GDAL's GPX driver](https://gdal.org/en/stable/drivers/vector/gpx.html)
   documents these sublayers. These are procedural references, not observed
   RideSync compatibility. If the installed loader does not expose both layers,
   record that discrepancy rather than substituting a converted file.
3. Inspect the Track points attribute table: four rows, all four coordinate
   geometries, both elevation values, both UTC instants and the two null spans
   must agree with the table above. Use Identify Features/geometry inspection
   for coordinates if they are not table columns. Record the actual displayed
   field names and time representation; an equivalent UTC instant is acceptable.
   If a field is not exposed, record it as unverified rather than inferring it
   from XML.
4. Zoom to Tracks and inspect the geometry and rendering. Expect two separate
   north–south spans, at longitudes -1 and -2, with no connecting line. The
   imported track feature count alone does not prove segment preservation:
   inspect its geometry parts/vertices and confirm two parts of two vertices.
   Record actual counts and any grouping identifiers exposed on Track points.
   If the view compresses the short lines, zoom into each span to confirm it.
5. Save an actual screenshot showing both layers and disconnected spans, plus
   the point table/geometry evidence needed to substantiate the values. Save a
   QGIS project with relative source paths if useful for review, then hash the
   original GPX again to confirm preservation. Store captured artifacts alongside
   the completed copy of [the observation template](observations-template.md).

Stop after this single check. A joined span, missing point or altered/null-filled
value is a discrepancy to diagnose, not a pass. A successful render alone does
not verify attributes. Keep #74 open until the required observations and actual
screenshot exist. This check makes no FIT, video alignment, extension-support or
hardware claim.

# GPX schemas and independent evidence

All RideSync expected coordinates and XML assertions are synthetic, handwritten
software evidence, not a receiver capture or observed consumer import.

Run `python scripts/prepare_gpx_schema.py` once during explicit development/CI
setup before writer tests. It retrieves the authoritative bytes from
https://www.topografix.com/GPX/1/1/gpx.xsd into ignored `.cache/gpx-1.1.xsd`, with
SHA-256 `9e4d1988b862edbe556305b130f8f6f1b29864fefd0dc02d5dab04ccdd1f34d6`.
The hash pins the original CRLF bytes fetched October 13, 2026. Tests fail if
setup is absent or bytes change. Exporter runtime does not fetch or use schemas.

Provenance: TopoGrafix GPX 1.1, released August 9, 2004. Official documentation:
https://www.topografix.com/gpx/1/1/ and https://www.topografix.com/gpx.asp.
The downloaded schema contains its documentation but no copyright/license
notice. The main GPX and developer webpages say their webpage text is public
domain; that scope does not expressly license XSD redistribution. Therefore
third-party schema bytes are cached locally and are **not redistributed**.
No broader license claim is made.

`ridesync-export-1.xsd` is project-authored MIT code under the repository license.
It validates each `rs:point` separately because GPX validates foreign extensions
laxly. Missing values are omitted; `uncertainty_known=0` explicitly preserves
unknown uncertainty. Numeric speed/course retain their documented m/s and degree
units in their field names. Standard children appear as elevation, time,
satellites, then extensions. No GPX fix enum is guessed from raw fix quality.

Development-only validator: `lxml==6.1.3`, BSD-3-Clause, Python >=3.8 with
Python 3.11 and 3.14 classifiers, pinned from the primary project release metadata
https://pypi.org/project/lxml/6.1.3/ (accessed October 13, 2026).

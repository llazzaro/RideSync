# Synthetic telemetry timestamp fixture

`gps_v1_timestamp_variants.csv` is author-created synthetic software output under
repository MIT. No physical GPS/IMU/card/ride capture or private identifiers,
credentials or precise ride locations. Generated from the public
`TIMESTAMP_SOURCE` in `test/test_telemetry_disk.py` with Storage source at
`b13d27b3b346dbafd50a598443f1ae983625e2b6`, before the common timestamp formatter
refactor. It freezes complete GPS v1 bytes, including header/counters.

Six cases: missing anchor, fresh unknown uncertainty, fresh known zero uncertainty
and zero age, fresh nonzero uncertainty, exact freshness boundary, and expired
anchor copied before a later UTC correction. The independent Python test also
asserts handwritten expected timestamp fields from actual mixed GPS/IMU rows;
the parser imports no production formatter or timestamp helper.

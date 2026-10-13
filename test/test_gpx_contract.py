"""Independent tests of the frozen export API and handwritten fixture evidence."""
import dataclasses
import unittest
from decimal import Decimal
from tools.ride_export.contracts import Counters, Timestamp, GpsEvent, Diagnostic, FormatError


class ContractTests(unittest.TestCase):
    def test_evidence_is_immutable_and_exact(self):
        stamp = Timestamp(42, 10, 0, 0, None, None, None, False, None, None, False, None)
        counters = Counters(1, 0, 0, 0, 0, 0)
        event = GpsEvent(5, stamp, counters, 0, 0, False, False, 1, 0, True,
                         Decimal('0.00000000'), Decimal('180.00000000'),
                         Decimal('0'), None, None, None, None, 10, 0, 0)
        self.assertEqual(format(event.latitude_deg, 'f'), '0.00000000')
        self.assertIsNone(event.speed_m_s)
        self.assertEqual(event.satellites, 0)
        with self.assertRaises(dataclasses.FrozenInstanceError):
            event.line = 7
        with self.assertRaises(dataclasses.FrozenInstanceError):
            event.timestamp.monotonic_ms = 11

    def test_counter_domains_and_stream_records_remain_immutable(self):
        from tools.ride_export.contracts import CounterObservation, IgnoredEvent, Metadata, ExportSummary
        counters = Counters(20, 0, 0, 0, 0, 0)
        global_observation = CounterObservation('global', counters)
        imu_observation = CounterObservation('imu', Counters(1, 0, 0, 0, 0, 0))
        metadata = Metadata('MixedV2', 2, 42, 'synthetic', 'synthetic',
                            (('queue_records', '8'),))
        summary = ExportSummary(metadata, counter_domains=(global_observation, imu_observation))
        self.assertEqual(summary.counter_domains[0].maxima.accepted, 20)
        self.assertEqual(summary.counter_domains[1].maxima.accepted, 1)
        stamp = Timestamp(42, 10, 0, 0, None, None, None, False, None, None, False, None)
        ignored = IgnoredEvent(6, 'imu', stamp, imu_observation.maxima)
        for event, field, value in ((metadata, 'session_id', 43),
                                    (global_observation, 'scope', 'imu'),
                                    (summary, 'input_rows', 2),
                                    (ignored, 'kind', 'gps')):
            with self.assertRaises(dataclasses.FrozenInstanceError):
                setattr(event, field, value)

    def test_error_retains_private_value_free_diagnostic(self):
        diagnostic = Diagnostic(5, 'latitude_deg', 'out_of_range',
                                'Use an unmodified valid source log.', 'error')
        error = FormatError(diagnostic, category='row')
        self.assertIs(error.diagnostic, diagnostic)
        self.assertEqual(error.category, 'row')
        self.assertIn('latitude_deg', str(error))


class FixtureTests(unittest.TestCase):
    """Tests intentionally use csv/json only, never production parser helpers."""

    def setUp(self):
        import json
        from pathlib import Path
        self.root = Path(__file__).parent / 'fixtures' / 'gpx'
        self.expected = json.loads((self.root / 'expectations.json').read_text())

    def test_five_named_headers_match_firmware_literals(self):
        import re
        from pathlib import Path
        source = (Path(__file__).resolve().parents[1] / 'src' / 'storage.cpp').read_text()
        fragments = source.split('const char *header =', 1)[1].split(';', 1)[0]
        source_header = ''.join(re.findall(r'"([^\"]*)"', fragments)).replace('\\n', '')
        for schema in ('gpsv1', 'mixedv2', 'camerav3', 'motionv4', 'motionv5'):
            lines = (self.root / (schema + '.csv')).read_text().splitlines()
            self.assertEqual(lines[3], source_header)
            self.assertEqual(len(lines[3].split(',')), 35)
            self.assertEqual(lines[1], '#session,42,firmware,gpx-fixture-v1,provenance,synthetic')

    def test_handwritten_schema_expectations_and_heterogeneous_counts(self):
        import csv
        for version, schema in enumerate(('gpsv1', 'mixedv2', 'camerav3', 'motionv4', 'motionv5'), 1):
            name = schema + '.csv'
            lines = (self.root / name).read_text().splitlines()
            expected = self.expected[name]
            rows = [next(csv.reader([line])) for line in lines[4:] if not line.startswith('#')]
            gps = [row if version == 1 else row[1:] for row in rows
                   if version == 1 or row[0] == 'gps']
            self.assertEqual(len(gps), 2)
            self.assertEqual(expected['admitted_points'], 2)
            self.assertEqual(expected['segments'], 1)
            for idx, row in enumerate(gps):
                self.assertEqual(len(row), 35)
                point = expected['normalized_points'][idx]
                self.assertEqual(row[19], point['latitude'])
                self.assertEqual(row[20], point['longitude'])
                self.assertEqual(row[21], point['elevation'])
                self.assertEqual(row[24]+'T'+row[25]+'Z', point['time'])
            ignored = [row for row in rows if version > 1 and row[0] != 'gps']
            self.assertEqual([row[0] for row in ignored], expected['ignored_kinds'])
            for row in ignored:
                count = 41 if row[0] == 'camera' else {2: 88, 3: 88, 4: 124, 5: 147}[version]
                self.assertEqual(len(row), count)
                self.assertEqual(row[1], '42')
                counters = row[-6:] if row[0] == 'camera' else row[82:88]
                self.assertEqual(counters, ['1','0','0','0','0','0'])
            markers = lines[4:4+{1:0,2:1,3:2,4:2,5:3}[version]]
            if version > 1:
                self.assertEqual(markers[0], '#imu_layout,2,see_docs/mixed_telemetry.md'
                                 if version < 4 else '#imu_layout,4,see_docs/motion_logging.md')
            if version >= 3:
                self.assertEqual(markers[1], '#camera_layout,3,see_docs/log_format.md')
            if version == 5:
                self.assertEqual(markers[2], '#dynamic_layout,1,see_docs/dynamic_motion_estimator.md')

    def test_boundaries_are_literal_and_missing_is_not_zero(self):
        rows = [line.split(',') for line in (self.root / 'coordinates.csv').read_text().splitlines()[4:]]
        self.assertEqual([(row[19],row[20]) for row in rows], [
            ('90.00000000','180.00000000'),('-90.00000000','-180.00000000'),
            ('0.00000000','0.00000000'),('1.00000000','-1.00000000'),
            ('-1.00000000','1.00000000')])
        self.assertEqual([row[21] for row in rows], ['100000','-100000','0','','-2.5'])
        self.assertEqual(rows[0][27:29], ['65535','255'])
        self.assertEqual(rows[2][27:29], ['0','0'])
        self.assertEqual(rows[3][27:29], ['',''])
        point = self.expected['coordinates.csv']['normalized_points'][3]
        self.assertIsNone(point['satellites'])
        self.assertEqual(self.expected['coordinates.csv']['normalized_points'][0]['longitude'], '-180.00000000')

    def test_expected_continuity_is_independent_of_high_rate_snapshots(self):
        expected = self.expected['continuity.csv']
        self.assertEqual((expected['gps_rows'], expected['admitted_points'],
                          expected['excluded_points'], expected['duplicate_points'],
                          expected['segments']), (8,6,1,1,5))
        rows = [line.split(',') for line in (self.root / 'continuity.csv').read_text().splitlines()[4:]]
        self.assertEqual(rows[0][26], rows[1][26])
        self.assertEqual(rows[2][16], '4')
        self.assertEqual(int(rows[4][26])-int(rows[3][26]), 10000)
        self.assertEqual(int(rows[5][26])-int(rows[4][26]), 10001)
        self.assertGreater(int(rows[6][1]), int(rows[7][1]))

    def test_partial_calendar_unknown_and_known_zero_uncertainty(self):
        rows = [line.split(',') for line in (self.root / 'clock_variants.csv').read_text().splitlines()[4:]]
        self.assertEqual(rows[0][24:26], ['', ''])
        self.assertEqual(rows[1][24:26], ['2024-02-29', ''])
        self.assertEqual(rows[2][7:9], ['0',''])
        self.assertEqual(rows[3][7:9], ['1','0'])
        self.assertEqual(rows[4][3], '2')
        self.assertEqual(rows[4][10:12], ['0',''])
        self.assertEqual(self.expected['clock_variants.csv']['solution']['untimed_points'], 2)

    def test_interleaved_counter_domains_are_independent(self):
        rows = [line.split(',') for line in (self.root / 'interleaved_loss.csv').read_text().splitlines()[5:]]
        self.assertEqual([row[0] for row in rows], ['gps', 'imu', 'gps', 'control', 'gps'])
        self.assertEqual(rows[0][30], '20')
        self.assertEqual(rows[1][82], '1')
        self.assertEqual(rows[3][83], '1')
        expected = self.expected['interleaved_loss.csv']
        self.assertEqual(expected['gps_rows'], 3)
        self.assertEqual(expected['segments'], 2)

    def test_all_fixture_files_have_explicit_expectations(self):
        self.assertEqual({path.name for path in self.root.glob('*.csv')}, set(self.expected))
        for name in ('tail_delimiter.csv','tail_digit.csv','tail_complete.csv'):
            self.assertFalse((self.root/name).read_bytes().endswith(b'\n'))
            self.assertEqual(self.expected[name]['truncated_rows'], 1)
        for name in ('duplicate_header.csv','missing_marker.csv','session_mismatch.csv'):
            self.assertEqual(self.expected[name]['salvage'], 'fatal')

    def test_immutable_timestamp_evidence_is_reused_without_edit(self):
        import hashlib
        path = self.root.parent / 'telemetry' / 'gps_v1_timestamp_variants.csv'
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                         'f460be324282a822a25f3eb988f7fc0002596e57b422d22b65dcd1aa4833d766')


if __name__ == '__main__':
    unittest.main()

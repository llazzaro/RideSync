"""Synthetic event oracle; schema validation uses development-only lxml."""
import io
import hashlib
import json
import sys
import unittest
from dataclasses import replace
from decimal import Decimal
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lxml import etree
from tools.ride_export.contracts import (
    Counters, Discontinuity, FormatError, GpsEvent, IgnoredEvent, Metadata, Timestamp,
)
from tools.ride_export.gpx import write_gpx

SCHEMA = Path(__file__).parent / 'fixtures/gpx_schema'
NS = {'g': 'http://www.topografix.com/GPX/1/1', 'r': 'urn:ridesync:gpx:export:1'}
META = Metadata('GpsV1', 1, 7, 'synthetic', 'handwritten', ())
ZERO = Counters(0, 0, 0, 0, 0, 0)


def point(ms=1000, **changes):
    ts = Timestamp(7, ms, 0, 0, None, None, None, False, None, None, False, None)
    base = GpsEvent(10, ts, ZERO, 0, 0, True, True, 1, 0, True,
                    Decimal('0'), Decimal('0'), None, None, None,
                    '2024-02-29', '12:00:00.00', ms, None, None)
    return replace(base, **changes)


class WriterTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (SCHEMA / '.cache/gpx-1.1.xsd').exists():
            raise RuntimeError('Run python scripts/prepare_gpx_schema.py before writer schema tests.')

    def export(self, *events, **options):
        out = io.StringIO()
        diagnostics = []
        summary = write_gpx(iter((META, *events)), out,
                            diagnostic_sink=diagnostics.append, **options)
        tree = etree.fromstring(out.getvalue().encode())
        etree.XMLSchema(etree.parse(str(SCHEMA / '.cache/gpx-1.1.xsd'))).assertValid(tree)
        extension = etree.XMLSchema(etree.parse(str(SCHEMA / 'ridesync-export-1.xsd')))
        for node in tree.findall('.//r:point', NS):
            extension.assertValid(node)
        return summary, tree, diagnostics

    def test_boundaries_and_optional_values(self):
        s, tree, _ = self.export(point(latitude_deg=Decimal('-90'), longitude_deg=Decimal('180'),
                                        altitude_msl_m=Decimal('-100000.00'), satellites=0,
                                        fix_quality=255, speed_m_s=Decimal('0.00'), course_deg=Decimal('359.99')))
        node = tree.find('.//g:trkpt', NS)
        self.assertEqual(node.attrib, {'lat': '-90.00000000', 'lon': '-180.00000000'})
        self.assertEqual(node.findtext('g:ele', namespaces=NS), '-100000.00')
        self.assertEqual(node.findtext('g:sat', namespaces=NS), '0')
        self.assertEqual(node.findtext('g:time', namespaces=NS), '2024-02-29T12:00:00.00Z')
        self.assertIsNone(node.find('g:fix', NS))
        self.assertEqual(s.normalized_longitudes, 1)
        self.assertEqual(s.single_point_segments, 1)

    def test_duplicates_conflict_and_restart(self):
        p = point()
        s, _, _ = self.export(p, p, Discontinuity(11, 'skipped_row'), p)
        self.assertEqual((s.emitted_points, s.duplicate_points, s.segments), (2, 1, 2))
        with self.assertRaises(FormatError):
            self.export(p, replace(p, latitude_deg=Decimal('1')))
        s, _, _ = self.export(p, replace(p, latitude_deg=Decimal('1')), point(2000), salvage=True)
        self.assertEqual((s.skipped_rows, s.segments), (1, 2))

    def test_gap_boundary_and_bad_fix(self):
        s, _, _ = self.export(point(source_utc_time=None), point(11000, source_utc_time=None),
                              point(21001, source_utc_time=None), point(22000, fix_validity=4),
                              point(23000, source_utc_time=None))
        self.assertEqual((s.emitted_points, s.segments, s.excluded_points), (4, 3, 1))

    def test_time_policy_and_anchor_changes(self):
        p = point(source_utc_date=None, source_utc_time=None)
        ts = replace(p.timestamp, anchor_quality=1, anchor_sequence=1,
                     anchor_receipt_ms=1000, anchor_utc_ms=1700000000000, anchor_age_ms=0,
                     has_utc_estimate=True, utc_estimate_ms=1700000000000)
        p = replace(p, timestamp=ts)
        s, tree, _ = self.export(p, time_source='receipt-estimate')
        self.assertEqual(s.unknown_uncertainty_points, 1)
        self.assertEqual(tree.findtext('.//g:time', namespaces=NS), '2023-11-14T22:13:20.000Z')
        q = point(2000, timestamp=replace(ts, monotonic_ms=2000, anchor_age_ms=1000,
                                         utc_estimate_ms=1700000001000, anchor_sequence=2))
        s, _, _ = self.export(p, q, time_source='receipt-estimate')
        self.assertEqual(s.segments, 2)
        s, tree, _ = self.export(p)
        self.assertEqual(s.untimed_points, 1)
        self.assertIsNone(tree.find('.//g:time', NS))

    def test_counter_domains_and_ignored_loss(self):
        p = point(source_utc_time=None, counters=replace(ZERO, accepted=100))
        ignored = IgnoredEvent(11, 'imu', p.timestamp, replace(ZERO, accepted=1, lost=1))
        s, _, diagnostics = self.export(p, ignored, point(2000, source_utc_time=None,
                                                        counters=replace(ZERO, accepted=101)))
        self.assertEqual(s.segments, 2)
        self.assertFalse(any(d.reason == 'counter_decrease' for d in diagnostics))

    def test_equal_utc_monotonic_backwards_availability(self):
        s, _, _ = self.export(point(), point(2000), point(500, source_utc_time=None),
                              point(600, source_utc_time=None))
        self.assertEqual(s.segments, 3)

    def test_pinned_schema_bytes(self):
        self.assertEqual(hashlib.sha256((SCHEMA / '.cache/gpx-1.1.xsd').read_bytes()).hexdigest(),
                         '9e4d1988b862edbe556305b130f8f6f1b29864fefd0dc02d5dab04ccdd1f34d6')

    def test_reader_fixture_integration(self):
        from tools.ride_export.reader import read_events
        fixture_dir = SCHEMA.parent / 'gpx'
        expected = json.loads((fixture_dir / 'expectations.json').read_text())
        for name, oracle in expected.items():
            with self.subTest(name=name):
                for salvage in (False, True):
                    with (fixture_dir / name).open('rb') as source:
                        output = io.StringIO()
                        if oracle.get('strict') == 'fatal' and (not salvage or oracle.get('salvage') != 'discard' and name != 'conflicting_solution.csv'):
                            with self.assertRaises(FormatError):
                                write_gpx(read_events(source, salvage=salvage), output, salvage=salvage)
                            continue
                        summary = write_gpx(read_events(source, salvage=salvage), output, salvage=salvage)
                        tree = etree.fromstring(output.getvalue().encode())
                        etree.XMLSchema(etree.parse(str(SCHEMA / '.cache/gpx-1.1.xsd'))).assertValid(tree)
                        ext = etree.XMLSchema(etree.parse(str(SCHEMA / 'ridesync-export-1.xsd')))
                        for node in tree.findall('.//r:point', NS):
                            ext.assertValid(node)
                        if 'admitted_points' in oracle:
                            self.assertEqual(summary.emitted_points, oracle['admitted_points'])
                            self.assertEqual(summary.segments, oracle.get('segments', oracle.get('solution', {}).get('segments')))
                        elif salvage and name != 'conflicting_solution.csv':
                            self.assertEqual(summary.emitted_points, oracle['salvage_admitted_points'])
                            self.assertEqual(summary.segments, oracle['salvage_segments'])
                            self.assertEqual(summary.truncated_rows, oracle.get('truncated_rows', 0))
                        for node, literal in zip(tree.findall('.//g:trkpt', NS), oracle.get('normalized_points', [])):
                            for key, attribute in (('latitude', 'lat'), ('longitude', 'lon')):
                                self.assertEqual(node.get(attribute), literal[key])
                            for key, path in (('elevation', 'g:ele'), ('time', 'g:time'), ('satellites', 'g:sat'),
                                              ('speed', 'g:extensions/r:point/r:speed_m_s'),
                                              ('course', 'g:extensions/r:point/r:course_deg'),
                                              ('quality', 'g:extensions/r:point/r:fix_quality')):
                                if key in literal:
                                    self.assertEqual(node.findtext(path, namespaces=NS),
                                                     None if literal[key] is None else str(literal[key]))
        for mode, key in (('solution', 'solution'), ('receipt-estimate', 'receipt_estimate')):
            with (fixture_dir / 'clock_variants.csv').open('rb') as source:
                output = io.StringIO()
                summary = write_gpx(read_events(source), output, time_source=mode)
                tree = etree.fromstring(output.getvalue().encode())
                oracle = expected['clock_variants.csv'][key]
                self.assertEqual(summary.segments, oracle['segments'])
                self.assertEqual(summary.untimed_points, oracle['untimed_points'])
                self.assertEqual([node.findtext('g:time', namespaces=NS) for node in tree.findall('.//g:trkpt', NS)], oracle['times'])

    def test_literal_xml_and_streaming(self):
        output = io.StringIO()
        def events():
            yield META
            yield point()
            self.assertIn('</trkpt>', output.getvalue())
        write_gpx(events(), output)
        self.assertEqual(output.getvalue(), (SCHEMA / 'expected-zero-point.gpx').read_text())
        expected = etree.parse(str(SCHEMA / 'expected-zero-point.gpx'))
        etree.XMLSchema(etree.parse(str(SCHEMA / '.cache/gpx-1.1.xsd'))).assertValid(expected)
        etree.XMLSchema(etree.parse(str(SCHEMA / 'ridesync-export-1.xsd'))).assertValid(expected.find('.//r:point', NS))

    def test_counter_decrease_saturation_and_domains(self):
        p = point(source_utc_time=None, counters=replace(ZERO, dropped=4294967295))
        q = point(2000, source_utc_time=None, counters=replace(ZERO, dropped=1))
        s, _, diagnostics = self.export(p, q)
        self.assertEqual(s.segments, 2)
        self.assertEqual(s.saturated_counters, ('dropped',))
        self.assertEqual(s.counter_initial_nonzero, ('dropped',))
        self.assertEqual(s.counter_decreases, (('dropped', 1),))
        self.assertEqual(s.counter_domains[0].scope, 'global')
        self.assertIn('counter_decrease', [d.reason for d in diagnostics])

    def test_known_zero_uncertainty_expired_anchors_and_time_mode(self):
        p = point()
        ts = replace(p.timestamp, anchor_quality=1, anchor_sequence=1,
                     anchor_receipt_ms=1000, anchor_utc_ms=1700000000000, anchor_age_ms=0,
                     has_utc_estimate=True, utc_estimate_ms=1700000000000,
                     uncertainty_known=True, uncertainty_ms=0)
        s, tree, _ = self.export(replace(p, timestamp=ts), time_source='receipt-estimate')
        self.assertEqual(s.unknown_uncertainty_points, 0)
        self.assertEqual(tree.findtext('.//r:uncertainty_ms', namespaces=NS), '0')
        s, tree, _ = self.export(replace(p, timestamp=replace(ts, anchor_quality=2,
                                has_utc_estimate=False, utc_estimate_ms=None)), time_source='receipt-estimate')
        self.assertEqual(s.untimed_points, 1)
        self.assertIsNone(tree.find('.//g:time', NS))

    def test_unrepresentable_estimate_is_strict_or_salvaged(self):
        p = point()
        ts = replace(p.timestamp, anchor_quality=1, has_utc_estimate=True,
                     utc_estimate_ms=9223372036854775807)
        invalid = replace(p, timestamp=replace(ts, monotonic_ms=1100), fix_receipt_ms=1100)
        with self.assertRaises(FormatError):
            self.export(invalid, time_source='receipt-estimate')
        s, _, _ = self.export(p, invalid, point(2000), time_source='receipt-estimate', salvage=True)
        self.assertEqual((s.skipped_rows, s.segments), (1, 2))

    def test_solution_anchor_change_preserves_time(self):
        p = point()
        q = point(2000, timestamp=replace(p.timestamp, monotonic_ms=2000, anchor_sequence=9),
                  source_utc_time='12:00:01.00')
        s, _, _ = self.export(p, q)
        self.assertEqual(s.segments, 1)

    def test_no_points(self):
        with self.assertRaises(FormatError):
            self.export(point(fix_valid=False, fix_validity=2))


if __name__ == '__main__':
    unittest.main()

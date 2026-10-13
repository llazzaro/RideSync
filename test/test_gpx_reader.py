"""Independent synthetic inputs for bounded reader validation."""
import io
import json
from pathlib import Path
import unittest
from decimal import Decimal
from tools.ride_export.contracts import (GPS_COLUMNS, Diagnostic, Discontinuity,
    FormatError, GpsEvent, IgnoredEvent, Metadata)
from tools.ride_export import reader

HEADER = ','.join(GPS_COLUMNS)
COMMON = ['7', '1000', '0', '0', '', '', '', '0', '', '', '0', '']
GPS = COMMON + ['4', '1', '1', '1', '1', '0', '1', '52.00000000',
    '4.00000000', '-2.5', '0', '360', '2024-02-29', '12:34:56.78',
    '1000', '0', '0', '1', '0', '0', '0', '0', '0']


def log(version=1, rows=None):
    lines = ['#ridesync_gps,1' if version == 1 else f'#ridesync_telemetry,{version}',
        '#session,7,firmware,synthetic,provenance,unit-test',
        '#policy,queue_records,8,flush_records,8,idle_flush,1,chunk_bytes,256,mount_attempts,2,write_retries,0', HEADER]
    if version >= 2:
        lines += ['#imu_layout,2,see_docs/mixed_telemetry.md' if version < 4 else
                  '#imu_layout,4,see_docs/motion_logging.md']
    if version >= 3:
        lines += ['#camera_layout,3,see_docs/log_format.md']
    if version >= 5:
        lines += ['#dynamic_layout,1,see_docs/dynamic_motion_estimator.md']
    lines += rows if rows is not None else [','.join(([] if version == 1 else ['gps']) + GPS)]
    return ('\n'.join(lines) + '\n').encode('ascii')


def changed(**fields):
    values = GPS.copy()
    for key, value in fields.items():
        values[GPS_COLUMNS.index(key)] = value
    return ','.join(values)


class ReaderTests(unittest.TestCase):
    def events(self, data, salvage=False):
        return list(reader.read_events(io.BytesIO(data), salvage=salvage))

    def test_five_schemas_and_exact_optional_zero_evidence(self):
        for version in range(1, 6):
            with self.subTest(version=version):
                events = self.events(log(version))
                self.assertIsInstance(events[0], Metadata)
                self.assertEqual(events[0].version, version)
                point = next(e for e in events if isinstance(e, GpsEvent))
                self.assertEqual(point.latitude_deg, Decimal('52.00000000'))
                self.assertEqual(point.altitude_msl_m, Decimal('-2.5'))
                self.assertEqual(point.satellites, 0)
                self.assertIsNone(point.timestamp.utc_estimate_ms)

    def test_invalid_gps_and_clock_scalars_are_rejected(self):
        cases = {'monotonic_ms': '31536000001', 'monotonic_quality': '1',
            'gnss_power': '2', 'fix_validity': '5', 'latitude_deg': 'NaN',
            'longitude_deg': '181', 'altitude_msl_m': '100001',
            'speed_m_s': '-1', 'course_deg': '361', 'satellites': '65536',
            'fix_quality': '256', 'accepted': '4294967296',
            'source_utc_date': '2023-02-29', 'source_utc_time': '12:00:60.00',
            'fix_receipt_ms': '1001', 'fix_age_ms': '1'}
        for field, value in cases.items():
            with self.subTest(field=field):
                with self.assertRaises(FormatError):
                    self.events(log(rows=[changed(**{field: value})]))

    def test_session_errors_and_header_errors_are_fatal_in_salvage(self):
        cases = [log(rows=[changed(session_id='8')]),
            log(rows=[changed(session_id='0')]),
            log(rows=[changed(session_id='8') + ',extra']), log(rows=[changed(session_id=str(2**64))]),
            log().replace(b'#ridesync_gps,1', b'#ridesync_gps,2'),
            log().replace(HEADER.encode(), b'bad_header'),
            log(5).replace(b'#dynamic_layout,1,see_docs/dynamic_motion_estimator.md\n', b''),
            log(rows=[HEADER, ','.join(GPS)]), log(rows=['#ridesync_gps,1'])]
        for data in cases:
            with self.subTest(data=data[:25]):
                with self.assertRaises(FormatError):
                    self.events(data, True)

    def test_salvage_malformed_middle_and_any_torn_tail(self):
        data = log(rows=[','.join(GPS), changed(latitude_deg='nan'), ','.join(GPS)])
        with self.assertRaises(FormatError):
            self.events(data)
        events = self.events(data, True)
        self.assertEqual(sum(isinstance(e, GpsEvent) for e in events), 2)
        self.assertEqual(sum(isinstance(e, Diagnostic) for e in events), 1)
        self.assertEqual(sum(isinstance(e, Discontinuity) for e in events), 1)
        for tail in [b'7,', b'7,100', ','.join(GPS).encode()]:
            with self.subTest(tail=tail[:5]):
                with self.assertRaises(FormatError):
                    self.events(log() + tail)
                result = self.events(log() + tail, True)
                self.assertEqual(sum(isinstance(e, GpsEvent) for e in result), 1)
                self.assertTrue(any(isinstance(e, Diagnostic) and e.field == 'tail' for e in result))

    def test_encoding_and_overlong_remainder_are_fatal_without_read_ahead(self):
        class Bounded(io.BytesIO):
            def readline(self, size=-1):
                self.assert_size = size
                if not 0 < size <= 4096:
                    raise AssertionError('unbounded readline')
                return super().readline(size)
        source = Bounded(log() + b'x' * 100000)
        with self.assertRaises(FormatError) as caught:
            list(reader.read_events(source, salvage=True))
        self.assertEqual(caught.exception.category, 'line_length')
        self.assertLess(source.tell(), len(log()) + 4097)
        for suffix in [b'\xff\n', b'7\r\n']:
            with self.assertRaises(FormatError):
                self.events(log() + suffix, True)

    def test_ignored_kinds_validate_common_counters_not_sensor_payload(self):
        for version in range(2, 6):
            length = 88 + (36 if version >= 4 else 0) + (23 if version == 5 else 0)
            values = ['imu'] + COMMON + ['opaque'] * (length - 13)
            values[82:88] = ['1', '2', '3', '4', '5', '6']
            event = self.events(log(version, [','.join(values)]))[-1]
            self.assertIsInstance(event, IgnoredEvent)
            self.assertEqual(event.counters.dropped, 2)
            values[82] = '-1'
            with self.assertRaises(FormatError):
                self.events(log(version, [','.join(values)]))

    def test_partial_calendar_and_empty_optionals_preserve_absence(self):
        event = self.events(log(rows=[changed(source_utc_time='', altitude_msl_m='',
            speed_m_s='', course_deg='', satellites='', fix_quality='')]))[-1]
        self.assertEqual(event.source_utc_date, '2024-02-29')
        self.assertIsNone(event.source_utc_time)
        self.assertIsNone(event.satellites)

    def test_anchor_equations_and_presence(self):
        good = dict(anchor_quality='1', anchor_sequence='1', anchor_receipt_ms='900',
            anchor_utc_ms='1700000000000', anchor_age_ms='100', has_utc_estimate='1',
            utc_estimate_ms='1700000000100', uncertainty_known='1', uncertainty_ms='0')
        self.assertEqual(self.events(log(rows=[changed(**good)]))[-1].timestamp.uncertainty_ms, 0)
        for field, value in [('anchor_age_ms', '99'), ('utc_estimate_ms', '1700000000101'),
                             ('has_utc_estimate', '0'), ('anchor_receipt_ms', '1001'),
                             ('anchor_sequence', '0'), ('uncertainty_known', '0')]:
            with self.subTest(field=field), self.assertRaises(FormatError):
                self.events(log(rows=[changed(**(good | {field: value}))]))

    def test_independent_fixtures(self):
        fixtures = Path(__file__).parent / 'fixtures/gpx'
        expectations = json.loads((fixtures / 'expectations.json').read_text())
        for name, expected in expectations.items():
            if name == 'conflicting_solution.csv':
                continue  # Writer compares solution identities.
            with self.subTest(name=name):
                data = (fixtures / name).read_bytes()
                if expected.get('strict') == 'fatal':
                    with self.assertRaises(FormatError) as caught:
                        self.events(data)
                    if 'field' in expected:
                        self.assertEqual(caught.exception.diagnostic.field, expected['field'])
                    if expected.get('salvage') == 'fatal':
                        with self.assertRaises(FormatError):
                            self.events(data, True)
                    else:
                        events = self.events(data, True)
                        self.assertTrue(any(isinstance(e, Diagnostic) for e in events))
                else:
                    events = self.events(data)
                    self.assertEqual(sum(isinstance(e, GpsEvent) for e in events), expected['gps_rows'])
                    if 'ignored_kinds' in expected:
                        self.assertEqual([e.kind for e in events if isinstance(e, IgnoredEvent)],
                                         expected['ignored_kinds'])

    def test_decimal_hostile_exponent_is_format_error(self):
        for value in ['1e' + '9' * 100, '1e-999999999', '1e0']:
            with self.subTest(value=value), self.assertRaises(FormatError):
                self.events(log(rows=[changed(speed_m_s=value)]))

    def test_exact_line_bound_and_control_bytes(self):
        self.events(log(rows=['x' * 4095]), True)  # 4096 bytes including LF is allowed.
        for row in ['x' * 4096, 'x\x00']:
            with self.subTest(length=len(row)):
                if len(row) > 4095:
                    with self.assertRaises(FormatError) as caught:
                        self.events(log(rows=[row]), True)
                    self.assertEqual(caught.exception.category, 'line_length')
                else:
                    with self.assertRaises(FormatError):
                        self.events(log(rows=[row]))

    def test_coordinate_source_precision_preserves_antimeridian_bounds(self):
        for value in ['179.999999999', '-179.999999999', '180.000000001',
                      '-180.000000001', '0.000000001']:
            with self.subTest(value=value):
                with self.assertRaises(FormatError) as caught:
                    self.events(log(rows=[changed(longitude_deg=value)]))
                self.assertEqual(caught.exception.diagnostic.field, 'longitude_deg')
                events = self.events(log(rows=[changed(longitude_deg=value)]), True)
                self.assertFalse(any(isinstance(e, GpsEvent) for e in events))
        with self.assertRaises(FormatError):
            self.events(log(rows=[changed(latitude_deg='89.999999999')]))
        for value, literal in [('179.99999999', '179.99999999'),
                ('-179.99999999', '-179.99999999'), ('180', '180.00000000'),
                ('-180.00000000', '-180.00000000')]:
            with self.subTest(value=value):
                event = self.events(log(rows=[changed(longitude_deg=value)]))[-1]
                self.assertEqual(event.longitude_deg, Decimal(value))
                self.assertEqual(format(event.longitude_deg, '.8f'), literal)

    def test_presence_and_syntax_rejections(self):
        cases = [changed(fix_valid='0'), changed(fix_receipt_ms=''),
            changed(latitude_deg=''), changed(latitude_deg='Infinity'),
            changed(uncertainty_known='1'), changed(has_utc_estimate='1'),
            changed(anchor_sequence='1'), changed(source_utc_date='1999-01-01'),
            changed(source_utc_time='12:00:00.000'), changed(accepted='+1'),
            changed(latitude_deg='"52.0"'), ','.join(GPS) + ',extra',
            ','.join(GPS[:-1])]
        for row in cases:
            with self.subTest(row=row[:12]), self.assertRaises(FormatError):
                self.events(log(rows=[row]))
        for version in range(1, 6):
            with self.subTest(version=version), self.assertRaises(FormatError):
                self.events(log(version, ['camera,' + ','.join(COMMON) + ',' * 27])
                            if version < 3 else log(version, ['unknown,' + ','.join(GPS)]))

    def test_absent_and_stale_fixes_remain_evidence(self):
        stale = self.events(log(rows=[changed(fix_validity='4')]))[-1]
        self.assertTrue(stale.fix_valid)
        self.assertEqual(stale.fix_validity, 4)
        missing = self.events(log(rows=[changed(fix_validity='0', fix_valid='0',
            latitude_deg='', longitude_deg='', fix_age_ms='', fix_receipt_ms='',
            source_utc_date='', source_utc_time='')]))[-1]
        self.assertIsNone(missing.latitude_deg)
        self.assertIsNone(missing.fix_receipt_ms)

    def test_foreign_session_cannot_hide_behind_other_row_errors(self):
        foreign = changed(session_id='8')
        for row in ['unknown,' + foreign, 'gps,' + foreign + ',"extra"',
                    'gps,' + foreign + ',\x00']:
            with self.subTest(row=row[:15]):
                with self.assertRaises(FormatError) as caught:
                    self.events(log(2, [row]), True)
                self.assertEqual(caught.exception.category, 'session')

    def test_unknown_kind_is_fatal_in_salvage(self):
        with self.assertRaises(FormatError) as caught:
            self.events(log(2, ['unknown,' + ','.join(GPS)]), True)
        self.assertEqual(caught.exception.category, 'version')

    def test_header_numeric_error_categories(self):
        for before, after, category in [(b'#session,7', b'#session,0', 'session'),
                (b'#session,7', b'#session,x', 'session'),
                (b'flush_records,8', b'flush_records,0', 'header'),
                (b'mount_attempts,2', b'mount_attempts,x', 'header')]:
            with self.subTest(after=after):
                with self.assertRaises(FormatError) as caught:
                    self.events(log().replace(before, after), True)
                self.assertEqual(caught.exception.category, category)

    def test_header_metadata_and_policy_rejections(self):
        for before, after in [(b'firmware,synthetic', b'firmware,bad token'),
                (b'#session,7', b'#session,0'), (b'queue_records,8', b'queue_records,9'),
                (b'flush_records,8', b'flush_records,0'),
                (b'mount_attempts,2', b'mount_attempts,4'),
                (b'write_retries,0', b'write_retries,1')]:
            with self.subTest(after=after), self.assertRaises(FormatError):
                self.events(log().replace(before, after), True)

    def test_diagnostics_never_echo_private_values(self):
        secret = '123.98765432'
        data = log(rows=[changed(latitude_deg=secret)])
        with self.assertRaises(FormatError) as caught:
            self.events(data)
        self.assertNotIn(secret, str(caught.exception))
        diagnostic = next(e for e in self.events(data, True) if isinstance(e, Diagnostic))
        self.assertNotIn(secret, repr(diagnostic))
        self.assertEqual(diagnostic.line, 5)
        self.assertEqual(diagnostic.field, 'latitude_deg')
        self.assertTrue(diagnostic.remedy)

    def test_incremental_consumption(self):
        source = io.BytesIO(log(rows=[','.join(GPS)] * 100))
        events = reader.read_events(source)
        self.assertIsInstance(next(events), Metadata)
        before = source.tell()
        self.assertIsInstance(next(events), GpsEvent)
        self.assertLess(source.tell() - before, 4096)
        self.assertLess(source.tell(), len(source.getvalue()))


if __name__ == '__main__':
    unittest.main()

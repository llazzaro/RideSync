"""Independent fixtures for the offline bookmark sidecar."""

import csv
import hashlib
import json
import subprocess
import sys
import tempfile
import tracemalloc
import unittest
from unittest import mock
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts' / 'export_bookmarks.py'
HEADER = ('schema,session_id,bookmark_id,monotonic_ms,source,gesture,anchor_quality,'
          'anchor_sequence,anchor_receipt_ms,anchor_utc_ms,anchor_age_ms,'
          'uncertainty_known,uncertainty_ms,utc_estimate_ms\n')


def bookmark(bookmark_id, time_ms, *, session=7, source=1, gesture=0,
             quality=0, counters='1,0,0,0,0,0'):
    common = [str(session), str(time_ms), '0', str(quality)]
    if quality == 0:
        common += ['', '', '', '0', '', '', '0', '']
    else:
        common += ['1', '1000', '1700000000000', '1', '20', str(time_ms - 1000),
                   '1' if quality == 1 else '0',
                   str(1700000000000 + time_ms - 1000) if quality == 1 else '']
    cells = ['bookmark'] + common + [''] * 17 + counters.split(',') + [str(bookmark_id), str(source), str(gesture)]
    return ','.join(cells) + '\n'


def source(*rows, tail='', version=6):
    return (f'#ridesync_telemetry,{version}\n'
            '#session,7,firmware,test,provenance,synthetic\n'
            '#policy,queue_records,8,flush_records,1,idle_flush,1,chunk_bytes,256,mount_attempts,1,write_retries,0\n'
            'session_id,monotonic_ms,monotonic_quality,anchor_quality,anchor_sequence,anchor_receipt_ms,anchor_utc_ms,uncertainty_known,uncertainty_ms,anchor_age_ms,has_utc_estimate,utc_estimate_ms,modem_state,uart_health,gnss_power,receiver_ready,fix_validity,fix_age_ms,fix_valid,latitude_deg,longitude_deg,altitude_msl_m,speed_m_s,course_deg,source_utc_date,source_utc_time,fix_receipt_ms,satellites,fix_quality,accepted,dropped,rejected,lost,written,flushed,bookmark_id,bookmark_source,bookmark_gesture\n'
            '#imu_layout,4,see_docs/motion_logging.md\n'
            '#camera_layout,3,see_docs/log_format.md\n'
            '#dynamic_layout,1,see_docs/dynamic_motion_estimator.md\n'
            + ('#camera_health_layout,1,see_docs/log_format.md\n' if version == 7 else '')
            + '#bookmark_layout,1,see_docs/log_format.md\n'
            + ''.join(rows) + tail)


class ExportBookmarksTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.input = self.root / 'source.csv'
        self.out = self.root / 'out'
        self.out.mkdir()

    def run_export(self, *, alignment=None):
        args = [sys.executable, str(SCRIPT), str(self.input), str(self.out)]
        if alignment is not None:
            args += ['--alignment', str(alignment)]
        return subprocess.run(args, capture_output=True, text=True, check=False)

    def alignment(self, **changes):
        clip = self.root / 'clip.bin'
        clip.write_bytes(b'synthetic footage identity')
        fields = {'path': str(clip), 'sha256': hashlib.sha256(clip.read_bytes()).hexdigest(),
                  'duration_seconds': '5', 'clip_zero_session_ms': 10000,
                  'project_start_seconds': '30', 'method': 'visible_sync',
                  'uncertainty_ms': 100}
        fields.update(changes)
        path = self.root / 'mapping.json'
        path.write_text(json.dumps({'schema': 'ridesync-alignment-v1', 'session_id': 7,
                                    'clips': [fields]}), encoding='ascii')
        return path

    def test_exact_sidecar_preserves_equal_time_and_missing_utc(self):
        self.input.write_text(source(bookmark(1, 12500), bookmark(2, 12500)), encoding='ascii')
        run = self.run_export()
        self.assertEqual(run.returncode, 0, run.stderr)
        sidecar = (self.out / 'ridesync-markers-v1.csv').read_text(encoding='ascii')
        self.assertTrue(sidecar.startswith(HEADER))
        rows = list(csv.DictReader(sidecar.splitlines()))
        self.assertEqual([r['bookmark_id'] for r in rows], ['1', '2'])
        self.assertEqual([r['monotonic_ms'] for r in rows], ['12500', '12500'])
        self.assertEqual([r['anchor_quality'] for r in rows], ['Missing', 'Missing'])
        self.assertEqual([r['utc_estimate_ms'] for r in rows], ['', ''])
        report = json.loads((self.out / 'ridesync-markers-report-v1.json').read_text())
        self.assertEqual(report['source_sha256'], hashlib.sha256(self.input.read_bytes()).hexdigest())
        self.assertEqual(report['accepted_markers'], 2)
        self.assertEqual(report['excluded_tail_rows'], 0)
        self.assertEqual(report['editor']['alignment'], 'unknown')
        self.assertEqual(report['editor']['placed'], 0)
        self.assertFalse((self.out / 'ridesync-reaper-v1.lua').exists())

    def test_invalid_middle_duplicate_zero_and_wrong_session_fail_without_outputs(self):
        invalid = [
            source(bookmark(1, 100), bookmark(1, 200)),
            source(bookmark(1, 100), bookmark(2, 200), bookmark(1, 300)),
            source(bookmark(0, 100)),
            source(bookmark(1, 100, session=8)),
            source(bookmark(1, 100), 'bookmark,bad\n'),
            source(bookmark(1, 100, source=2, gesture=0)),
        ]
        for data in invalid:
            with self.subTest(data=data[-45:]):
                self.input.write_text(data, encoding='ascii')
                run = self.run_export()
                self.assertNotEqual(run.returncode, 0)
                self.assertEqual(list(self.out.iterdir()), [])

    def test_uint_boundaries(self):
        self.input.write_text(source(bookmark(0xffffffff, 31536000000)), encoding='ascii')
        self.assertEqual(self.run_export().returncode, 0)
        self.assertIn(',4294967295,31536000000,',
                      (self.out / 'ridesync-markers-v1.csv').read_text())
        for path in self.out.iterdir():
            path.unlink()
        for invalid in (bookmark(0x100000000, 100), bookmark(1, 31536000001)):
            self.input.write_text(source(invalid), encoding='ascii')
            self.assertNotEqual(self.run_export().returncode, 0)
            self.assertEqual(list(self.out.iterdir()), [])

    def test_only_unterminated_final_row_can_be_excluded(self):
        self.input.write_text(source(bookmark(1, 100), tail='bookmark,7,200'), encoding='ascii')
        run = self.run_export()
        self.assertEqual(run.returncode, 0, run.stderr)
        report = json.loads((self.out / 'ridesync-markers-report-v1.json').read_text())
        self.assertEqual(report['accepted_markers'], 1)
        self.assertEqual(report['excluded_tail_rows'], 1)

    def test_empty_log_reports_counter_evidence_unavailable(self):
        self.input.write_text(source(), encoding='ascii')
        run = self.run_export()
        self.assertEqual(run.returncode, 0, run.stderr)
        report = json.loads((self.out / 'ridesync-markers-report-v1.json').read_text())
        self.assertEqual(report['accepted_markers'], 0)
        self.assertFalse(report['loss_evidence_available'])

    def test_first_record_power_cut_tail_is_excluded_with_source_hash(self):
        data = source(tail='bookmark,7,200')
        self.input.write_text(data, encoding='ascii')
        run = self.run_export()
        self.assertEqual(run.returncode, 0, run.stderr)
        report = json.loads((self.out / 'ridesync-markers-report-v1.json').read_text())
        self.assertEqual(report['accepted_markers'], 0)
        self.assertEqual(report['input_rows'], 0)
        self.assertEqual(report['excluded_tail_rows'], 1)
        self.assertFalse(report['loss_evidence_available'])
        self.assertEqual(report['source_sha256'], hashlib.sha256(data.encode('ascii')).hexdigest())
        self.assertEqual((self.out / 'ridesync-markers-v1.csv').read_text(), HEADER)

    def test_power_cut_tail_does_not_excuse_incomplete_metadata(self):
        self.input.write_text(source(tail='bookmark,7,200').replace(
            '#bookmark_layout,1,see_docs/log_format.md\n', ''), encoding='ascii')
        run = self.run_export()
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(list(self.out.iterdir()), [])

    def test_malformed_policy_and_quoted_numeric_field_fail(self):
        valid = source(bookmark(1, 100))
        invalid = [
            valid.replace('#policy,queue_records,8', '#policy,queue_records,9', 1),
            valid.replace('#session,7,firmware', '#session,7,unknown', 1),
            valid.replace(',1,1,0\n', ',"1",1,0\n', 1),
        ]
        for data in invalid:
            with self.subTest(data=data[-35:]):
                for path in self.out.iterdir():
                    path.unlink()
                self.input.write_text(data, encoding='ascii')
                self.assertNotEqual(self.run_export().returncode, 0)
                self.assertEqual(list(self.out.iterdir()), [])

    def test_invalid_timestamp_on_nonbookmark_row_rejects_whole_log(self):
        gps = bookmark(1, 100).replace('bookmark,', 'gps,', 1).rstrip('\n').split(',')
        gps[2] = 'not-a-time'
        gps[-3:] = ['', '', '']
        self.input.write_text(source(','.join(gps) + '\n', bookmark(2, 200)), encoding='ascii')
        run = self.run_export()
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(list(self.out.iterdir()), [])

    def test_malformed_gps_payload_in_middle_rejects_export(self):
        from test_gpx_reader import GPS

        gps = ['gps'] + GPS.copy() + ['', '', '']
        gps[20] = 'not_a_latitude'
        self.input.write_text(source(','.join(gps) + '\n', bookmark(1, 2000)), encoding='ascii')
        run = self.run_export()
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(list(self.out.iterdir()), [])

    def test_camera_marker_audit_does_not_create_portable_bookmark(self):
        marker = ['marker'] + bookmark(1, 100).rstrip('\n').split(',')[1:13]
        marker += ['1', '0', '9', '4', '1', '1', '1', '0', '1', 'owner_admission',
                   '0', '', ''] + ['1', '0', '0', '0', '0', '0']
        self.assertEqual(len(marker), 32)
        self.input.write_text(source(','.join(marker) + '\n', bookmark(2, 200)), encoding='ascii')
        run = self.run_export()
        self.assertEqual(run.returncode, 0, run.stderr)
        report = json.loads((self.out / 'ridesync-markers-report-v1.json').read_text())
        self.assertEqual(report['accepted_markers'], 1)
        self.assertEqual(report['input_rows'], 2)

    def test_camera_marker_receipt_must_match_owner_time(self):
        marker = ['marker'] + bookmark(1, 100).rstrip('\n').split(',')[1:13]
        marker += ['1', '0', '9', '4', '1', '1', '1', '0', '1', 'owner_admission',
                   '1', '200', '10'] + ['1', '0', '0', '0', '0', '0']
        self.input.write_text(source(','.join(marker) + '\n', bookmark(2, 200)), encoding='ascii')
        run = self.run_export()
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(list(self.out.iterdir()), [])

    def test_marker_phase_evidence_must_match_native_producer(self):
        base = ['marker'] + bookmark(1, 100).rstrip('\n').split(',')[1:13]
        base += ['1', '0', '9', '4', '1', '1', '1', '0', '1', 'owner_admission',
                 '0', '', ''] + ['1', '0', '0', '0', '0', '0']
        cases = (
            ('X5 wire ACK', {16: '1', 19: '2', 21: '0'}),
            ('failed zero connection generation', {17: '0', 19: '3', 20: '8', 21: '0'}),
            ('cancelled zero operation generation', {18: '0', 19: '4', 20: '9', 21: '0'}),
            ('failed without error', {19: '3', 20: '0', 21: '0'}),
            ('cancelled without error', {19: '4', 20: '0', 21: '0'}),
            ('cancelled with wrong error', {19: '4', 20: '8', 21: '0'}),
            ('request refused without error', {19: '0', 20: '0', 21: '0'}),
            ('attempt undelivered with transport error', {20: '8', 21: '0'}),
        )
        for label, changes in cases:
            with self.subTest(case=label):
                for path in self.out.iterdir():
                    path.unlink()
                marker = base.copy()
                for index, value in changes.items():
                    marker[index] = value
                self.input.write_text(source(','.join(marker) + '\n', bookmark(2, 200)),
                                      encoding='ascii')
                run = self.run_export()
                self.assertNotEqual(run.returncode, 0, label)
                self.assertEqual(list(self.out.iterdir()), [])

    def test_existing_outputs_are_untouched(self):
        self.input.write_text(source(bookmark(1, 100)), encoding='ascii')
        dest = self.out / 'ridesync-markers-v1.csv'
        dest.write_text('existing', encoding='ascii')
        run = self.run_export()
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(dest.read_text(encoding='ascii'), 'existing')
        self.assertEqual(sorted(p.name for p in self.out.iterdir()), [dest.name])

    def test_publication_collision_rolls_back_only_new_files(self):
        from scripts import export_bookmarks as module

        self.input.write_text(source(bookmark(1, 100)), encoding='ascii')
        original = self.input.read_bytes()
        real_link = module.os.link
        report_path = self.out / 'ridesync-markers-report-v1.json'

        def colliding_link(staged, destination):
            if destination == report_path:
                report_path.write_text('concurrent output', encoding='ascii')
            real_link(staged, destination)

        with mock.patch.object(module.os, 'link', side_effect=colliding_link):
            with self.assertRaises(FileExistsError):
                module.export(self.input, self.out)
        self.assertEqual(report_path.read_text(), 'concurrent output')
        self.assertEqual(self.input.read_bytes(), original)
        self.assertEqual(sorted(p.name for p in self.out.iterdir()), [report_path.name])

    def test_oversized_row_and_cli_path_errors_fail_without_outputs(self):
        self.input.write_text(source(bookmark(1, 100), tail='bookmark,' + '0' * 3072),
                              encoding='ascii')
        run = self.run_export()
        self.assertEqual(run.returncode, 2)
        self.assertIn('exceeds 3072 bytes', run.stderr)
        self.assertEqual(list(self.out.iterdir()), [])
        self.input.unlink()
        run = self.run_export()
        self.assertEqual(run.returncode, 2)
        self.assertIn('source file and output directory must exist', run.stderr)
        self.assertEqual(list(self.out.iterdir()), [])

    def test_invalid_or_absent_clip_offsets_never_create_outputs(self):
        self.input.write_text(source(bookmark(1, 12500)), encoding='ascii')
        cases = ({'sha256': '0' * 64}, {'clip_zero_session_ms': None},
                 {'project_start_seconds': '-1'}, {'duration_seconds': '0'},
                 {'method': 'unknown'}, {'uncertainty_ms': True})
        for changes in cases:
            with self.subTest(changes=changes):
                run = self.run_export(alignment=self.alignment(**changes))
                self.assertEqual(run.returncode, 2, run.stderr)
                self.assertEqual(list(self.out.iterdir()), [])
        mapping = self.alignment()
        data = json.loads(mapping.read_text())
        del data['clips'][0]['clip_zero_session_ms']
        mapping.write_text(json.dumps(data), encoding='ascii')
        self.assertEqual(self.run_export(alignment=mapping).returncode, 2)
        self.assertEqual(list(self.out.iterdir()), [])

    def test_chapters_map_independently_and_report_exact_duration_exclusion(self):
        self.input.write_text(source(bookmark(1, 10000), bookmark(2, 12500),
                                     bookmark(3, 15000)), encoding='ascii')
        mapping = self.alignment()
        data = json.loads(mapping.read_text())
        chapter = dict(data['clips'][0], clip_zero_session_ms=15000,
                       project_start_seconds='60')
        data['clips'].append(chapter)
        mapping.write_text(json.dumps(data), encoding='ascii')
        run = self.run_export(alignment=mapping)
        self.assertEqual(run.returncode, 0, run.stderr)
        report = json.loads((self.out / 'ridesync-markers-report-v1.json').read_text())
        self.assertEqual(report['editor']['placed'], 3)
        self.assertEqual(report['editor']['outside_clip'], 3)
        self.assertEqual([(p['bookmark_id'], p['clip_index'], p['project_seconds'])
                          for p in report['editor']['preview']],
                         [(1, 1, '30'), (2, 1, '32.5'), (3, 2, '60')])
        rows = list(csv.DictReader((self.out / 'ridesync-markers-v1.csv').read_text().splitlines()))
        self.assertEqual([row['bookmark_id'] for row in rows], ['1', '2', '3'])

    def test_report_preserves_available_loss_counter_maxima(self):
        self.input.write_text(source(bookmark(1, 100, counters='5,2,3,1,2,1'),
                                     bookmark(4, 200, counters='7,4,3,2,5,4')), encoding='ascii')
        run = self.run_export()
        self.assertEqual(run.returncode, 0, run.stderr)
        report = json.loads((self.out / 'ridesync-markers-report-v1.json').read_text())
        self.assertTrue(report['loss_evidence_available'])
        self.assertEqual(report['loss_evidence'], {'accepted': 7, 'dropped': 4, 'rejected': 3,
                                                  'lost': 2, 'written': 5, 'flushed': 4})
        self.assertEqual(report['accepted_markers'], 2)

    def test_camera_health_v7_requires_its_layout(self):
        self.input.write_text(source(bookmark(1, 100), version=7).replace(
            '#camera_health_layout,1,see_docs/log_format.md\n', ''), encoding='ascii')
        run = self.run_export()
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('missing bookmark source metadata', run.stderr)
        self.assertEqual(list(self.out.iterdir()), [])

    def test_camera_health_v7_row_is_validated_and_bookmark_exported(self):
        from test_camera_health_disk import fixture

        health = next(line for line in fixture().splitlines() if line.startswith('camera_health,'))
        cells = health.split(',')
        cells[1] = '7'
        self.input.write_text(source(','.join(cells) + '\n', bookmark(1, 100), version=7),
                              encoding='ascii')
        run = self.run_export()
        self.assertEqual(run.returncode, 0, run.stderr)
        report = json.loads((self.out / 'ridesync-markers-report-v1.json').read_text())
        self.assertEqual(report['source_schema'], '#ridesync_telemetry,7')
        self.assertEqual(report['input_rows'], 2)
        self.assertEqual(report['accepted_markers'], 1)
        for path in self.out.iterdir():
            path.unlink()
        cells[13] = '8'  # Invalid peer slot in the middle row.
        self.input.write_text(source(','.join(cells) + '\n', bookmark(1, 100), version=7),
                              encoding='ascii')
        run = self.run_export()
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(list(self.out.iterdir()), [])

    def test_mapping_positions_and_outside_exclusions(self):
        self.input.write_text(source(bookmark(1, 9000), bookmark(2, 12500),
                                     bookmark(3, 20000)), encoding='ascii')
        clip = self.root / 'clip.bin'
        clip.write_bytes(b'synthetic footage identity')
        mapping = self.root / 'mapping.json'
        mapping.write_text(json.dumps({
            'schema': 'ridesync-alignment-v1', 'session_id': 7,
            'clips': [{'path': str(clip), 'sha256': hashlib.sha256(clip.read_bytes()).hexdigest(),
                       'duration_seconds': '5', 'clip_zero_session_ms': 10000,
                       'project_start_seconds': '30', 'method': 'visible_sync',
                       'uncertainty_ms': 100}]}), encoding='ascii')
        run = self.run_export(alignment=mapping)
        self.assertEqual(run.returncode, 0, run.stderr)
        report = json.loads((self.out / 'ridesync-markers-report-v1.json').read_text())
        self.assertEqual(report['editor']['placed'], 1)
        self.assertEqual(report['editor']['outside_clip'], 2)
        script = (self.out / 'ridesync-reaper-v1.lua').read_text()
        self.assertIn('32.5', script)
        self.assertIn('RideSync 7:2', script)
        self.assertNotIn('RideSync 7:1', script)
        self.assertNotIn('RideSync 7:3', script)
        self.assertIn('AddProjectMarker2', script)
        self.assertTrue((self.out / 'ridesync-markers-v1.csv').exists())

    def test_fresh_and_expired_anchor_presence(self):
        self.input.write_text(source(bookmark(1, 2000, quality=1),
                                     bookmark(2, 3000, quality=2)), encoding='ascii')
        run = self.run_export()
        self.assertEqual(run.returncode, 0, run.stderr)
        with (self.out / 'ridesync-markers-v1.csv').open(encoding='ascii') as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual([row['anchor_quality'] for row in rows], ['Fresh', 'Expired'])
        self.assertEqual(rows[0]['utc_estimate_ms'], '1700000001000')
        self.assertEqual(rows[1]['utc_estimate_ms'], '')
        self.assertEqual(rows[1]['anchor_utc_ms'], '1700000000000')

    def test_fresh_anchor_without_estimate_is_rejected(self):
        row = bookmark(1, 2000, quality=1).rstrip('\n').split(',')
        row[11] = '0'
        row[12] = ''
        self.input.write_text(source(','.join(row) + '\n'), encoding='ascii')
        run = self.run_export()
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(list(self.out.iterdir()), [])

    def test_fresh_anchor_without_estimate_on_middle_gps_rejects_export(self):
        from test_gpx_reader import GPS

        gps = ['gps'] + GPS.copy() + ['', '', '']
        gps[4:13] = ['1', '1', '900', '1700000000000', '0', '', '100', '0', '']
        self.input.write_text(source(','.join(gps) + '\n', bookmark(1, 2000)), encoding='ascii')
        run = self.run_export()
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(list(self.out.iterdir()), [])

    def test_missing_and_expired_gps_anchors_cannot_claim_utc_estimate(self):
        from test_gpx_reader import GPS

        for quality in ('0', '2'):
            with self.subTest(anchor_quality=quality):
                gps = ['gps'] + GPS.copy() + ['', '', '']
                if quality == '2':
                    gps[4:13] = ['2', '1', '900', '1700000000000', '0', '', '100',
                                 '1', '1700000000100']
                else:
                    gps[11:13] = ['1', '1700000000100']
                self.input.write_text(source(','.join(gps) + '\n', bookmark(1, 2000)),
                                      encoding='ascii')
                run = self.run_export()
                self.assertNotEqual(run.returncode, 0)
                self.assertEqual(list(self.out.iterdir()), [])

    def test_large_input_has_bounded_peak_python_memory(self):
        with self.input.open('w', encoding='ascii') as stream:
            stream.write(source())
            for bookmark_id in range(1, 10001):
                stream.write(bookmark(bookmark_id, bookmark_id))
        from scripts import export_bookmarks as module
        tracemalloc.start()
        try:
            report = module.export(self.input, self.out)
            _, peak = tracemalloc.get_traced_memory()
        finally:
            tracemalloc.stop()
        self.assertEqual(report['accepted_markers'], 10000)
        self.assertLess(peak, 8 * 1024 * 1024)

    def test_production_formatter_to_portable_sidecar(self):
        from test_bookmark_core import SOURCE

        cpp = self.root / 'fixture.cpp'
        binary = self.root / 'fixture'
        cpp.write_text(SOURCE, encoding='ascii')
        built = subprocess.run([
            'c++', '-std=c++11', '-Iinclude', str(cpp),
            'src/session_clock.cpp', 'src/storage.cpp', 'src/telemetry_admission.cpp',
            'src/motion_estimator.cpp', 'src/dynamic_motion_estimator.cpp', '-o', str(binary),
        ], cwd=ROOT, capture_output=True, text=True, check=False)
        self.assertEqual(built.returncode, 0, built.stderr)
        logged = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, check=False)
        self.assertEqual(logged.returncode, 0, logged.stderr.decode())
        self.input.write_bytes(logged.stdout)
        run = self.run_export()
        self.assertEqual(run.returncode, 0, run.stderr)
        rows = list(csv.DictReader((self.out / 'ridesync-markers-v1.csv').read_text().splitlines()))
        self.assertEqual([row['bookmark_id'] for row in rows], ['1', '2', '3', '4', '5', '6', '8'])
        self.assertEqual(rows[2]['anchor_quality'], 'Fresh')
        self.assertEqual(rows[3]['anchor_quality'], 'Expired')

    def test_production_mixed_rows_are_validated_even_when_not_exported(self):
        from test_telemetry_disk import SOURCE
        from scripts.bookmark_row_validation import COMMON, IMU
        from scripts.export_bookmarks import INPUT_HEADER

        cpp = self.root / 'mixed.cpp'
        binary = self.root / 'mixed'
        cpp.write_text(SOURCE.replace('StorageFormat::MixedV2',
                                      'StorageFormat::BookmarkV6'), encoding='ascii')
        built = subprocess.run([
            'c++', '-std=c++11', '-Iinclude', str(cpp),
            'src/session_clock.cpp', 'src/storage.cpp', 'src/telemetry_admission.cpp',
            'src/motion_estimator.cpp', 'src/dynamic_motion_estimator.cpp', '-o', str(binary),
        ], cwd=ROOT, capture_output=True, text=True, check=False)
        self.assertEqual(built.returncode, 0, built.stderr)
        logged = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, check=False)
        self.assertEqual(logged.returncode, 0, logged.stderr.decode())
        original = logged.stdout.decode('ascii').splitlines()
        self.input.write_bytes(logged.stdout)
        self.assertEqual(self.run_export().returncode, 0)
        report = json.loads((self.out / 'ridesync-markers-report-v1.json').read_text())
        self.assertEqual(report['input_rows'], 5)
        for kind, field, value in [
            ('gps', 'modem_state', '9'),
            ('imu', 'sensor_state', '9'),
            ('config', 'calibration_time_known', '2'),
            ('health', 'event_length', '5'),
            ('control', 'sensor_time_ticks24', '1'),
        ]:
            with self.subTest(kind=kind):
                for path in self.out.iterdir():
                    path.unlink()
                lines = original.copy()
                index = next(i for i, line in enumerate(lines) if line.startswith(kind + ','))
                cells = lines[index].split(',')
                position = 1 + (INPUT_HEADER.split(',').index(field) if kind == 'gps'
                                else len(COMMON) + IMU.index(field))
                cells[position] = value
                lines[index] = ','.join(cells)
                self.input.write_text('\n'.join(lines) + '\n', encoding='ascii')
                self.assertNotEqual(self.run_export().returncode, 0)
                self.assertEqual(list(self.out.iterdir()), [])

    def test_production_v7_combined_health_camera_and_marker_rows(self):
        from test_camera_health_disk import SOURCE

        cpp = self.root / 'health.cpp'
        binary = self.root / 'health'
        cpp.write_text(SOURCE, encoding='ascii')
        built = subprocess.run([
            'c++', '-std=c++11', '-pthread', '-Iinclude', str(cpp),
            'src/session_clock.cpp', 'src/storage.cpp', 'src/telemetry_admission.cpp',
            'src/motion_estimator.cpp', 'src/dynamic_motion_estimator.cpp', '-o', str(binary),
        ], cwd=ROOT, capture_output=True, text=True, check=False)
        self.assertEqual(built.returncode, 0, built.stderr)
        logged = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, check=False)
        self.assertEqual(logged.returncode, 0, logged.stderr.decode())
        self.input.write_bytes(logged.stdout)
        run = self.run_export()
        self.assertEqual(run.returncode, 0, run.stderr)
        report = json.loads((self.out / 'ridesync-markers-report-v1.json').read_text())
        self.assertEqual(report['source_schema'], '#ridesync_telemetry,7')
        self.assertEqual(report['input_rows'], 9)
        self.assertEqual(report['accepted_markers'], 1)
        for path in self.out.iterdir():
            path.unlink()
        lines = logged.stdout.decode('ascii').splitlines()
        index = next(i for i, line in enumerate(lines) if line.startswith('camera,'))
        cells = lines[index].split(',')
        cells[35] = ''  # A camera kind counter is mandatory.
        lines[index] = ','.join(cells)
        self.input.write_text('\n'.join(lines) + '\n', encoding='ascii')
        self.assertNotEqual(self.run_export().returncode, 0)
        self.assertEqual(list(self.out.iterdir()), [])


if __name__ == '__main__':
    unittest.main()

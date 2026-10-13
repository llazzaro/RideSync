"""Exercise the offline command, publication boundaries and preservation."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/export_gpx.py'
FIXTURES = ROOT / 'test/fixtures/gpx'
GPX = '{http://www.topografix.com/GPX/1/1}'


class GpxCliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.source = self.directory / 'ride.csv'
        self.source.write_bytes((FIXTURES / 'gpsv1.csv').read_bytes())
        self.output = self.directory / 'ride.gpx'
        self.report = self.directory / 'ride.export.json'
        self.original = self.source.read_bytes()

    def args(self, *extra):
        return ['--input', str(self.source), '--output', str(self.output),
                '--report', str(self.report), *extra]

    def run_cli(self, *extra):
        return subprocess.run([sys.executable, str(SCRIPT), *self.args(*extra)],
                              capture_output=True, text=True, cwd=ROOT)

    def assert_preserved(self):
        self.assertEqual(self.source.read_bytes(), self.original)

    def load_cli(self):
        spec = importlib.util.spec_from_file_location('gpx_cli_test', SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_success_records_hash_provenance_and_exact_point_count(self):
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(ET.parse(self.output).findall('.//' + GPX + 'trkpt')), 2)
        data = json.loads(self.report.read_text())
        self.assertEqual(data['input']['sha256'], hashlib.sha256(self.original).hexdigest())
        self.assertEqual(data['input']['bytes'], len(self.original))
        self.assertEqual(data['input']['basename'], 'ride.csv')
        self.assertEqual(data['summary']['metadata']['session_id'], 42)
        self.assertEqual(data['summary']['emitted_points'], 2)
        self.assertEqual(data['options']['time_source'], 'solution')
        self.assertEqual(data['options']['max_gap_ms'], 10000)
        self.assertEqual(data['diagnostics'], [])
        self.assert_preserved()

    def test_all_schemas_use_the_same_cli(self):
        for name in ('gpsv1', 'mixedv2', 'camerav3', 'motionv4', 'motionv5'):
            with self.subTest(name=name):
                self.source.write_bytes((FIXTURES / (name + '.csv')).read_bytes())
                result = self.run_cli()
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(json.loads(self.report.read_text())['summary']['emitted_points'], 2)
                self.output.unlink()
                self.report.unlink()

    def test_bad_arguments_publish_nothing(self):
        for extra in (['--time-source', 'guess'], ['--max-gap-ms', '-1'],
                      ['--max-gap-ms', 'abc'], ['--unknown-option']):
            with self.subTest(extra=extra):
                result = self.run_cli(*extra)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertFalse(self.output.exists())
                self.assertFalse(self.report.exists())
        self.assert_preserved()

    def test_strict_error_and_zero_points_publish_nothing(self):
        for name in ('extra_column.csv', 'session_mismatch.csv'):
            with self.subTest(name=name):
                self.source.write_bytes((FIXTURES / name).read_bytes())
                result = self.run_cli()
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn('line', result.stderr)
                self.assertFalse(self.output.exists())
                self.assertFalse(self.report.exists())
        header = self.original.splitlines(keepends=True)[:4]
        self.source.write_bytes(b''.join(header))
        result = self.run_cli()
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertFalse(self.output.exists())
        self.assertFalse(self.report.exists())

    def test_salvage_tail_publishes_valid_prefix_and_exit_three(self):
        self.source.write_bytes(self.original + self.original.splitlines()[-1])
        result = self.run_cli('--salvage')
        self.assertEqual(result.returncode, 3, result.stderr)
        data = json.loads(self.report.read_text())
        self.assertEqual(data['summary']['truncated_rows'], 1)
        self.assertEqual(data['summary']['emitted_points'], 2)
        self.assertTrue(data['diagnostics'])
        self.assertNotIn('52.00000000', json.dumps(data['diagnostics']))

    def test_projection_conflict_obeys_strict_and_salvage(self):
        self.source.write_bytes((FIXTURES / 'conflicting_solution.csv').read_bytes())
        result = self.run_cli()
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertFalse(self.output.exists())
        result = self.run_cli('--salvage')
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertGreater(json.loads(self.report.read_text())['summary']['skipped_rows'], 0)

    def test_bad_fixes_untimed_points_and_duplicates_are_success(self):
        rows = self.original.splitlines(keepends=True)
        untimed = rows[4].decode().rstrip('\n').split(',')
        untimed[24:26] = ['', '']
        raw = (','.join(untimed) + '\n').encode()
        self.source.write_bytes(b''.join(rows[:4]) + raw + raw)
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        summary = json.loads(self.report.read_text())['summary']
        self.assertEqual(summary['untimed_points'], 1)
        self.assertEqual(summary['duplicate_points'], 1)
        self.assertIsNone(ET.parse(self.output).find('.//' + GPX + 'time'))

    def test_receipt_estimate_mode_is_explicit_and_reported(self):
        result = self.run_cli('--time-source', 'receipt-estimate')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIsNone(ET.parse(self.output).find('.//' + GPX + 'time'))
        self.assertEqual(json.loads(self.report.read_text())['options']['time_source'],
                         'receipt-estimate')

    def test_existing_outputs_and_aliases_are_preserved(self):
        for target in (self.output, self.report):
            with self.subTest(target=target):
                target.write_text('existing-data')
                result = self.run_cli()
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(target.read_text(), 'existing-data')
                target.unlink()
        for kind in ('symlink', 'hardlink'):
            with self.subTest(kind=kind):
                if kind == 'symlink':
                    self.output.symlink_to(self.source)
                else:
                    os.link(self.source, self.output)
                result = self.run_cli()
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assert_preserved()
                self.output.unlink()
        result = self.run_cli('--report', str(self.output))
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertFalse(self.output.exists())

    def test_dangling_output_symlink_is_not_overwritten(self):
        self.output.symlink_to(self.directory / 'missing')
        result = self.run_cli()
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertTrue(self.output.is_symlink())
        self.assertFalse((self.directory / 'missing').exists())

    def test_symlink_loop_is_actionable_and_preserved(self):
        self.output.symlink_to(self.output)
        result = self.run_cli()
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertNotIn('Traceback', result.stderr)
        self.assertTrue(self.output.is_symlink())
        self.assertFalse(self.report.exists())

    def test_unreadable_input_and_missing_parent_are_io_errors(self):
        self.source.unlink()
        result = self.run_cli()
        self.assertEqual(result.returncode, 4, result.stderr)
        self.assertFalse(self.output.exists())
        self.source.write_bytes(self.original)
        result = self.run_cli('--report', str(self.directory / 'absent/report.json'))
        self.assertEqual(result.returncode, 4, result.stderr)
        self.assertFalse(self.output.exists())
        self.assertEqual(sorted(p.name for p in self.directory.iterdir()), ['ride.csv'])

    def test_gpx_publication_race_preserves_new_existing_file(self):
        cli = self.load_cli()
        import tools.ride_export.publication as publication
        original_publish = publication.publish_exclusive

        def race(stage, target):
            if Path(target) == self.output:
                self.output.write_text('racing-user-data')
            return original_publish(stage, target)

        error = io.StringIO()
        with patch.object(cli, 'publish_exclusive', side_effect=race), \
                contextlib.redirect_stderr(error):
            code = cli.main(self.args())
        self.assertEqual(code, 4, error.getvalue())
        self.assertEqual(self.output.read_text(), 'racing-user-data')
        self.assertFalse(self.report.exists())
        self.assert_preserved()

    def test_report_publication_failure_names_published_gpx(self):
        cli = self.load_cli()
        import tools.ride_export.publication as publication
        original_publish = publication.publish_exclusive

        def fail_report(stage, target):
            if Path(target) == self.report:
                raise OSError('simulated report publication failure')
            return original_publish(stage, target)

        error = io.StringIO()
        with patch.object(cli, 'publish_exclusive', side_effect=fail_report), \
                contextlib.redirect_stderr(error):
            code = cli.main(self.args())
        self.assertEqual(code, 4, error.getvalue())
        self.assertTrue(self.output.exists())
        self.assertIn(str(self.output), error.getvalue())
        self.assertFalse(self.report.exists())
        self.assertEqual(sorted(p.name for p in self.directory.iterdir()), ['ride.csv', 'ride.gpx'])

    def test_report_write_failure_leaves_no_public_outputs(self):
        cli = self.load_cli()
        error = io.StringIO()
        with patch.object(cli.ReportWriter, 'finish', side_effect=OSError('disk full')), \
                contextlib.redirect_stderr(error):
            code = cli.main(self.args())
        self.assertEqual(code, 4, error.getvalue())
        self.assertFalse(self.output.exists())
        self.assertFalse(self.report.exists())
        self.assertEqual(sorted(p.name for p in self.directory.iterdir()), ['ride.csv'])

    def test_gpx_write_failure_leaves_no_public_outputs(self):
        cli = self.load_cli()
        error = io.StringIO()
        with patch.object(cli, 'write_gpx', side_effect=OSError('disk full')), \
                contextlib.redirect_stderr(error):
            code = cli.main(self.args())
        self.assertEqual(code, 4, error.getvalue())
        self.assertFalse(self.output.exists())
        self.assertFalse(self.report.exists())
        self.assertEqual(sorted(p.name for p in self.directory.iterdir()), ['ride.csv'])

    def test_input_changes_are_diagnosed_before_publication(self):
        cli = self.load_cli()
        real_writer = cli.write_gpx

        def mutate(events, output, **options):
            summary = real_writer(events, output, **options)
            with self.source.open('ab') as source:
                source.write(b'changed-after-read\n')
            return summary

        error = io.StringIO()
        with patch.object(cli, 'write_gpx', side_effect=mutate), \
                contextlib.redirect_stderr(error):
            code = cli.main(self.args())
        self.assertEqual(code, 4, error.getvalue())
        self.assertIn('immutable copy', error.getvalue())
        self.assertFalse(self.output.exists())
        self.assertFalse(self.report.exists())

    def test_report_publication_race_preserves_existing_report(self):
        cli = self.load_cli()
        import tools.ride_export.publication as publication
        real_publish = publication.publish_exclusive

        def race(stage, target):
            if Path(target) == self.report:
                self.report.write_text('racing-user-report')
            return real_publish(stage, target)

        with patch.object(cli, 'publish_exclusive', side_effect=race), \
                contextlib.redirect_stderr(io.StringIO()):
            code = cli.main(self.args())
        self.assertEqual(code, 4)
        self.assertTrue(self.output.exists())
        self.assertEqual(self.report.read_text(), 'racing-user-report')
        self.assert_preserved()

    def test_diagnostics_stream_with_bounded_incremental_memory(self):
        peaks = []
        rows = self.original.splitlines(keepends=True)
        profile = '''
import contextlib, importlib.util, io, json, sys, tracemalloc
spec = importlib.util.spec_from_file_location('gpx_profile', sys.argv[1])
cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cli)
tracemalloc.start()
with contextlib.redirect_stderr(io.StringIO()):
    code = cli.main(sys.argv[2:])
_, peak = tracemalloc.get_traced_memory()
print(json.dumps({'code': code, 'peak_bytes': peak}))
'''
        for count in (100, 20000):
            with self.subTest(rows=count):
                with self.source.open('wb') as source:
                    source.write(b''.join(rows[:5]))
                    for _ in range(count):
                        source.write(b'42,2\n')
                    source.write(rows[5])
                run = subprocess.run([sys.executable, '-c', profile, str(SCRIPT),
                    *self.args('--salvage')], cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(run.returncode, 0, run.stderr)
                measured = json.loads(run.stdout)
                self.assertEqual(measured['code'], 3)
                peaks.append(measured['peak_bytes'])
                report = json.loads(self.report.read_text())
                self.assertEqual(report['summary']['skipped_rows'], count)
                self.assertEqual(len(report['diagnostics']), count)
                self.output.unlink()
                self.report.unlink()
        self.assertLess(peaks[1] - peaks[0], 32 * 1024 * 1024, peaks)
        self.assertLess(peaks[1], 32 * 1024 * 1024, peaks)
        print(f'GPX Python allocation peaks: 100 discarded rows={peaks[0]} bytes; '
              f'20000 discarded rows={peaks[1]} bytes; delta={peaks[1] - peaks[0]} bytes')


if __name__ == '__main__':
    unittest.main()

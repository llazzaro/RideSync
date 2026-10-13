"""Execute generated Lua against marker storage; assert editor-facing behavior."""

import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from test_export_bookmarks import bookmark, source


ROOT = Path(__file__).resolve().parents[1]
LUA = shutil.which('lua5.4') or shutil.which('lua')
HARNESS = ROOT / 'test/fixtures/reaper/marker_store.lua'


class ReaperAdapterRegressionTests(unittest.TestCase):
    def execute_adapter(self, times, scenario):
        self.assertIsNotNone(LUA, 'Install Lua 5.4 to execute REAPER adapter regressions')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            clip = root / 'clip.bin'
            clip.write_bytes(b'synthetic clip identity')
            log = root / 'log.csv'
            log.write_text(source(*(bookmark(i, time) for i, time in enumerate(times, 1))))
            alignment = root / 'alignment.json'
            alignment.write_text(json.dumps({
                'schema': 'ridesync-alignment-v1', 'session_id': 7,
                'clips': [{
                    'path': str(clip), 'sha256': hashlib.sha256(clip.read_bytes()).hexdigest(),
                    'duration_seconds': '5', 'clip_zero_session_ms': 10000,
                    'project_start_seconds': '30', 'method': 'synthetic_fixed_offset',
                    'uncertainty_ms': 0,
                }],
            }))
            output = root / 'output'
            output.mkdir()
            export = subprocess.run([
                sys.executable, str(ROOT / 'scripts/export_bookmarks.py'),
                str(log), str(output), '--alignment', str(alignment),
            ], capture_output=True, text=True, check=False)
            self.assertEqual(export.returncode, 0, export.stderr)
            run = subprocess.run([
                LUA, str(HARNESS), str(output / 'ridesync-reaper-v1.lua'), scenario,
            ], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 0, run.stderr)
            return list(csv.DictReader(run.stdout.splitlines(), delimiter='\t'))

    def test_import_preserves_existing_marker_and_region_and_repeat_adds_nothing(self):
        rows = self.execute_adapter([10000, 12500, 14500], 'preservation')
        first = [{k: v for k, v in row.items() if k != 'phase'}
                 for row in rows if row['phase'] == 'first']
        repeat = [{k: v for k, v in row.items() if k != 'phase'}
                  for row in rows if row['phase'] == 'repeat']
        self.assertEqual(first, repeat)
        self.assertEqual(len(first), 5)
        self.assertEqual(first[:2], [
            {'kind': 'marker', 'id': '23', 'pos': '5.250000000', 'end': '0.000000000',
             'name': 'Existing marker KEEP', 'color': '123'},
            {'kind': 'region', 'id': '24', 'pos': '1.000000000', 'end': '2.000000000',
             'name': 'Existing region KEEP', 'color': '456'},
        ])
        self.assertEqual([(row['pos'], row['name']) for row in first[2:]], [
            ('30.000000000', 'RideSync 7:1 [clip 1]'),
            ('32.500000000', 'RideSync 7:2 [clip 1]'),
            ('34.500000000', 'RideSync 7:3 [clip 1]'),
        ])

    def test_equal_times_and_region_name_do_not_hide_distinct_markers(self):
        rows = self.execute_adapter([12500, 12500], 'collisions')
        first = [{k: v for k, v in row.items() if k != 'phase'}
                 for row in rows if row['phase'] == 'first']
        repeat = [{k: v for k, v in row.items() if k != 'phase'}
                  for row in rows if row['phase'] == 'repeat']
        self.assertEqual(first, repeat)
        self.assertEqual(len(first), 5)
        self.assertEqual([(row['kind'], row['pos'], row['name']) for row in first], [
            ('marker', '32.500000000', 'RideSync 7:1 [clip 1]'),
            ('region', '32.500000000', 'RideSync 7:2 [clip 1]'),
            ('marker', '32.500000000', 'Unrelated marker KEEP'),
            ('marker', '12.000000000', 'RideSync 7:2 [clip 1]'),
            ('marker', '32.500000000', 'RideSync 7:2 [clip 1]'),
        ])
        self.assertEqual([row['id'] for row in first[:4]], ['23', '24', '25', '26'])

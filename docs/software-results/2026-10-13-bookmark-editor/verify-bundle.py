"""Verify handoff hashes and reproduce the synthetic export without changing it."""

import csv
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


BUNDLE = Path(__file__).resolve().parent
ROOT = BUNDLE.parents[2]
for line in (BUNDLE / 'SHA256SUMS').read_text(encoding='ascii').splitlines():
    digest, relative = line.split('  ', 1)
    assert hashlib.sha256((BUNDLE / relative).read_bytes()).hexdigest() == digest, relative

with tempfile.TemporaryDirectory(prefix='ridesync-editor-acceptance-') as output:
    subprocess.run([
        sys.executable, str(ROOT / 'scripts/export_bookmarks.py'),
        str(BUNDLE / 'source.csv'), output, '--alignment', str(BUNDLE / 'alignment.json'),
    ], cwd=ROOT, check=True)
    for delivered in (BUNDLE / 'generated').iterdir():
        assert (Path(output) / delivered.name).read_bytes() == delivered.read_bytes(), delivered.name

report = json.loads((BUNDLE / 'generated/ridesync-markers-report-v1.json').read_text())
assert report['accepted_markers'] == report['input_rows'] == report['editor']['placed'] == 3
assert report['excluded_tail_rows'] == report['editor']['outside_clip'] == 0
with (BUNDLE / 'generated/ridesync-markers-v1.csv').open(encoding='ascii') as stream:
    rows = list(csv.DictReader(stream))
assert [row['monotonic_ms'] for row in rows] == ['10000', '12500', '14500']
assert [row['bookmark_id'] for row in rows] == ['1', '2', '3']
assert all(row['utc_estimate_ms'] == '' and row['anchor_quality'] == 'Missing' for row in rows)
expected = [('0', '30'), ('2.5', '32.5'), ('4.5', '34.5')]
for index, ((clip, project), row, preview) in enumerate(zip(expected, rows, report['editor']['preview']), 1):
    assert (Decimal(row['monotonic_ms']) - 10000) / 1000 == Decimal(clip)
    assert Decimal(30) + Decimal(clip) == Decimal(project)
    assert preview['clip_seconds'] == clip and preview['project_seconds'] == project
    assert preview['name'] == f'RideSync 7:{index} [clip 1]'
print('PASS bundle hashes, byte-identical fresh export, three independent arithmetic positions/names')

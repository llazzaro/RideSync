"""Host-check bench admission/deadlines against real Storage and SessionClock.

The fake owner controls scheduling and the final lifetime barrier; its memory
sink is test-only. No ESP32, filesystem, namespace receipt, or SD is accessed.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
BENCH = Path(__file__).resolve().parent


class BenchRunTest(unittest.TestCase):
    def test_one_shot_rows_close_barrier_refusals_and_deadlines(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'run'
            result = subprocess.run([
                'c++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'include'), '-I', str(BENCH),
                str(BENCH / 'test_run.cpp'), str(ROOT / 'src/storage.cpp'),
                str(ROOT / 'src/session_clock.cpp'), '-o', str(binary),
            ], capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(0, result.returncode, result.stderr)


if __name__ == '__main__':
    unittest.main()

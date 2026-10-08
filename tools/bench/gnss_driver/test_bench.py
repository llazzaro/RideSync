"""Exercise the bench with real production GNSS classes and host-only UART/time."""
from pathlib import Path
import subprocess
import tempfile
import unittest

BENCH = Path(__file__).resolve().parent
ROOT = BENCH.parents[2]


class GnssBenchTest(unittest.TestCase):
    def test_lifecycle_fault_and_control_progress(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'bench'
            command = ['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                       '-I', str(ROOT / 'include'), '-I', str(BENCH),
                       str(BENCH / 'test_bench.cpp')]
            command += [str(ROOT / 'src' / name) for name in
                        ('gnss_parser.cpp', 'session_clock.cpp', 'modem_gnss.cpp', 'gps_manager.cpp')]
            command += ['-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)

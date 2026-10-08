"""Host policy checks only; no camera, SDK simulation or physical qualification."""
from pathlib import Path
import subprocess
import tempfile
import unittest

BENCH = Path(__file__).resolve().parent


class CapturePolicyTest(unittest.TestCase):
    def test_enable_stop_deadlines_queue_reporting_and_sdk_owner_boundaries(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'capture'
            result = subprocess.run([
                'c++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                '-I', str(BENCH), '-I', str(BENCH.parents[2] / 'include'),
                str(BENCH / 'test_capture.cpp'), str(BENCH / 'shutter_codec.cpp'),
                '-o', str(binary),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()

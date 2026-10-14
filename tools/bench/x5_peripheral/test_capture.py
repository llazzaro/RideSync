"""Host policy checks only; no camera, SDK simulation or physical qualification."""
from pathlib import Path
import subprocess
import tempfile
import unittest

BENCH = Path(__file__).resolve().parent


class CapturePolicyTest(unittest.TestCase):
    def test_summary_uses_full_buffer_and_keeps_kind_counts(self):
        source = (BENCH / 'main.cpp').read_text()
        summary = source[source.index('void reportKindCounts('):source.index('\nvoid loop()')]
        harness = r'''
#include "capture.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace x5_probe;
struct Lock {};
struct LoopState {
  bool active, used, synced;
  uint16_t conn;
  StopReason reason;
};
struct Refusals { unsigned init = 0, open = 0, erase = 0; };
Capture capture;
SdkControl sdk_control;
ShutterControl shutter;
struct Wake { bool enabled() const { return false; } } wake;
bool private_hex = false, one_rs_legacy = false;
uint32_t last_summary = 0;
Refusals nvsRefusals() { return {}; }
bool ble_gap_adv_active() { return false; }
std::string reported;
bool emit(const char *line, size_t length) {
  reported.assign(line, length);
  return true;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            unit = Path(directory) / 'summary.cpp'
            binary = Path(directory) / 'summary'
            unit.write_text(harness + summary + r'''
int main() {
  LoopState state{false, false, false, 0xffff, StopReason::None};
  char line[768];
  reportSummary(1000, state, line);
  assert(reported.size() > 100);
  assert(reported.find("recording=UNKNOWN kind_counts=") != std::string::npos);
  assert(reported.back() == '\n');
  const auto first = reported;
  reportSummary(1001, state, line);
  assert(reported == first);
}
''')
            result = subprocess.run(['c++', '-std=c++11', '-I', str(BENCH),
                                     '-I', str(BENCH.parents[2] / 'include'),
                                     str(unit), '-o', str(binary)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

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

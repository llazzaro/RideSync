"""Deterministically schedule final publication at the worker stop-load boundary.

The temporary source copy alone receives a scheduling barrier; shipped code has
no test callbacks. The barrier does not alter queue or stop atomic values.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StorageStopTest(unittest.TestCase):
    def test_final_enqueue_after_empty_observation_is_drained(self):
        source = (ROOT / 'src/storage.cpp').read_text()
        source, count = re.subn(
            r'else if \((stop_\.load\([^)]*\))\)',
            r'else if ((testBeforeStop(), \1))', source,
        )
        self.assertEqual(1, count, 'worker stop boundary must be instrumented once')
        source = 'void testBeforeStop();\n' + source
        main = r'''
#include "storage.h"
#include <cassert>
#include <thread>
#include <mutex>
#include <condition_variable>
std::mutex mutex;
std::condition_variable cv;
bool armed = false, observed = false, released = false;
void testBeforeStop() {
  std::unique_lock<std::mutex> l(mutex);
  if (!armed) return;
  observed = true; cv.notify_one();
  cv.wait(l, [] { return released; });
}
struct Sink : ridesync::StorageSink {
  bool mount() override { return true; }
  bool openExclusive(const char *) override { return true; }
  size_t write(const char *, size_t n) override { return n; }
  bool flush() override { return true; }
  void close() override {}
};
int main() {
  using namespace ridesync;
  Sink sink; Storage storage(sink, {42, "fw", "synthetic"});
  for (int i = 0; i < 20; ++i) storage.workerStep();
  armed = true;
  std::thread worker([&] { storage.workerStep(); });
  { std::unique_lock<std::mutex> l(mutex); cv.wait(l, [] { return observed; }); }
  RecordTimestamp t; t.session_id = 42; t.monotonic_quality = MonotonicQuality::Valid;
  ModemSnapshot m; m.session_id = 42;
  assert(storage.enqueue(t, m)); storage.requestStop();
  { std::lock_guard<std::mutex> l(mutex); released = true; } cv.notify_one(); worker.join();
  for (int i = 0; i < 20; ++i) storage.workerStep();
  auto h = storage.health();
  assert(h.accepted == 1 && h.written == 1 && h.flushed == 1 && h.lost == 0);
  assert(h.stopped && !h.terminal);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / 'storage.cpp'
            cpp.write_text(source)
            harness = Path(folder) / 'main.cpp'
            harness.write_text(main)
            executable = Path(folder) / 'test'
            subprocess.run(['c++', '-std=c++11', '-pthread', '-I', str(ROOT / 'include'),
                            str(cpp), str(ROOT / 'src/motion_estimator.cpp'), str(harness), '-o', str(executable)], check=True,
                           capture_output=True)
            result = subprocess.run([str(executable)], capture_output=True, timeout=10)
            self.assertEqual(0, result.returncode, result.stderr.decode())

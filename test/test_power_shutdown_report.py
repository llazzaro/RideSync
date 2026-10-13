"""Bounded latest-only power reports; no serial/physical cut-time claims."""
import subprocess
import tempfile
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PowerReportTests(unittest.TestCase):
    def test_actual_console_routes_shutdown_and_reports_without_telemetry(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'console'
            subprocess.run(['clang++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                            '-DARDUINO_ARCH_ESP32', '-DRIDESYNC_BENCH_APPLICATION',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-I', str(ROOT / 'test/fixtures/power_shutdown/console'),
                            '-I', str(ROOT / 'include'),
                            str(ROOT / 'test/fixtures/power_shutdown/console.cpp'),
                            str(ROOT / 'src/bench_console.cpp'),
                            str(ROOT / 'src/bench_console_esp32.cpp'),
                            str(ROOT / 'src/power_shutdown_report.cpp'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_latest_snapshot_replaces_pending_and_consume_stays_bounded(self):
        program = r'''
#include "power_shutdown_report.h"
#include <cassert>
#include <cstring>
#include <string>
using namespace ridesync;
int main() {
  PowerReportBuffer b;assert(b.pending()==0);
  PowerReportSnapshot s;s.available=true;s.generation=7;s.reset_reason=3;
  s.policy.state=PowerShutdownState::OffPending;s.policy.cause=PowerShutdownCause::SupplyFail;
  s.policy.outcome=PowerShutdownOutcome::TimedOut;s.policy.elapsed_ms=5000;
  s.policy.qualified=true;s.storage.accepted=10;s.storage.written=8;s.storage.flushed=7;
  s.storage.dropped=2;s.storage.rejected=1;s.storage.lost=3;s.uncertain_rows=3;
  s.backend.off_attempted=true;s.backend.off_error=77;
  b.capture(s);assert(b.pending()>0&&b.pending()<PowerReportBuffer::capacity);
  std::string first(b.data(),b.pending());
  for(const char *field : {"available=1", "generation=7", "cause=2", "state=8", "outcome=3",
      "elapsed_ms=5000", "qualified=1", "reset_reason=3", "accepted=10", "written=8",
      "flushed=7", "dropped=2", "rejected=1", "lost=3", "uncertain=3", "off_error=77",
      "reserve_validity=0", "storage_error=0"})
    assert(first.find(field)!=std::string::npos);
  b.consume(5);assert(b.pending()==first.size()-5);
  s.generation=8;s.storage.accepted=11;b.capture(s);
  std::string newer(b.data(),b.pending());assert(newer.find("generation=8")!=std::string::npos);
  assert(newer.find("accepted=11")!=std::string::npos);
  b.consume(SIZE_MAX);assert(b.pending()==0);b.consume(1);assert(b.pending()==0);
  for(unsigned value=0;value<3;++value){
    s.observation.reserve=static_cast<PowerReserve>(value);s.storage_error=-73;
    b.capture(s);std::string reserve(b.data(),b.pending());
    assert(reserve.find("reserve_validity="+std::to_string(value))!=std::string::npos);
    assert(reserve.find("storage_error=-73")!=std::string::npos);
  }
  s.generation=s.policy.elapsed_ms=s.uncertain_rows=UINT32_MAX;
  s.storage.accepted=s.storage.written=s.storage.flushed=s.storage.dropped=s.storage.rejected=s.storage.lost=UINT32_MAX;
  s.backend.initialization_error=s.backend.read_error=s.backend.off_error=s.reset_reason=INT32_MIN;
  s.storage_error=INT32_MIN;
  b.capture(s);assert(b.pending()<PowerReportBuffer::capacity);
  assert(b.data()[b.pending()-1]=='\n');
  std::string text;
  while(b.pending()) {const auto n=b.pending()<3?b.pending():3;text.append(b.data(),n);b.consume(n);}
  assert(text.find("4294967295")!=std::string::npos && text.find("off_error=-2147483648")!=std::string::npos);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            source = temp / 'report.cpp'
            binary = temp / 'report'
            source.write_text(program)
            subprocess.run(['clang++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-I', str(ROOT / 'include'), str(source),
                            str(ROOT / 'src/power_shutdown_report.cpp'), '-o', str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()

"""Independent live diagnostic CSV checks; literal synthetic counts, no hardware."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from telemetry_parser import parse
ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "telemetry_admission.h"
#include "test/fixtures/motion/evidence.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace ridesync;
struct C : Clock { uint32_t now() const override { return 0; } };
struct S : StorageSink {
 std::string bytes;
 bool mount() override {return true;} bool openExclusive(const char*) override {return true;}
 size_t write(const char *p,size_t n) override { bytes.append(p,n); return n; }
 bool flush() override {return true;} void close() override {}
};
struct R : DynamicMotionReferenceSource {
 DynamicMotionReference referenceFor(const ImuEvidence &e) override {
  DynamicMotionReference r; if(e.frame_sequence==0) {r.externally_stationary=true;r.declaration=1;}return r;
 }
};
int main() {
 S sink; Storage s(sink,{42,"dynamic-fixture","synthetic",2,4,StorageFormat::MotionV5});
 C raw; SessionClock clock(raw,42,1000);ImuInbox inbox;R source;MotionAdmissionConfig options;
 options.requested=options.imu_qualified=true;options.estimator=motion_fixture::config();options.snapshot_max_age_ms=100;
 options.dynamic.enabled=true;options.dynamic.max_step_us=10000;options.dynamic.max_horizon_us=10000;options.dynamic_cadence_us=10000;options.dynamic_reference=&source;
 TelemetryAdmission a(clock,s,inbox,nullptr,options);
 for(unsigned n=0;n<3;++n) {
  auto e=motion_fixture::sample(42,0);e.sensor_epoch=1;e.frame_sequence=n;
  if(n)e.accel[0]=418;
  assert(a.event(e));for(unsigned j=0;j<500;++j)s.workerStep();
 }
 s.requestStop();for(unsigned j=0;j<500;++j)s.workerStep();
 assert(s.health().flushed==3);std::fwrite(sink.bytes.data(),1,sink.bytes.size(),stdout);
}
'''


class DynamicDisk(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        p = Path(cls.directory.name)
        (p/'fixture.cpp').write_text(SOURCE)
        subprocess.run(['c++', '-std=c++11', '-I'+str(ROOT/'include'), '-I'+str(ROOT), str(p/'fixture.cpp'),
                        *[str(ROOT/'src'/name) for name in ('telemetry_admission.cpp', 'storage.cpp', 'motion_estimator.cpp', 'dynamic_motion_estimator.cpp', 'session_clock.cpp')], '-o', str(p/'fixture')], check=True)
        cls.data = subprocess.check_output([str(p/'fixture')], text=True)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_literal_forward_acceleration_and_expired_horizon(self):
        rows = parse(self.data)
        self.assertEqual(3, len(rows))
        first, moving, expired = rows
        self.assertEqual('1', first['dynamic_declaration'])
        self.assertEqual('1', first['dynamic_qw'])
        self.assertEqual('0', moving['dynamic_declaration'])
        self.assertAlmostEqual(2, float(moving['dynamic_accel_x_mps2']), delta=.003)
        self.assertAlmostEqual(0, float(moving['dynamic_accel_z_mps2']), delta=.0001)
        self.assertEqual('1', moving['dynamic_quality'])
        self.assertEqual('1', moving['dynamic_timing_source'])
        self.assertEqual('9', expired['dynamic_fault'])
        self.assertEqual('0', expired['dynamic_numeric_available'])
        self.assertEqual('', expired['dynamic_roll_rad'])
        self.assertTrue(all(r['dynamic_lean_valid']=='0' and r['dynamic_acceleration_valid']=='0' for r in rows))

    def test_parser_refuses_trusted_flags_numeric_fault_and_missing_layout(self):
        lines = self.data.splitlines()
        index = next(i for i, line in enumerate(lines) if line.startswith('imu,'))
        for field, value in ((-14, '1'), (-19, '9'), (-12, 'nan')):
            changed = lines.copy()
            values = changed[index].split(',')
            values[field] = value
            changed[index] = ','.join(values)
            with self.assertRaises(ValueError):
                parse('\n'.join(changed)+'\n')
        with self.assertRaises(ValueError):
            parse(self.data.replace('#dynamic_layout,1,see_docs/dynamic_motion_estimator.md\n', ''))

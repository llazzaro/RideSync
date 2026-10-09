"""Independent MotionV4 parser/formatter checks; synthetic repository-owned evidence."""
import csv
from pathlib import Path
import subprocess
import tempfile
import unittest
from telemetry_parser import parse
ROOT = Path(__file__).resolve().parents[1]

SOURCE = r'''
#include "storage.h"
#include "camera_manager.h"
#include "test/fixtures/motion/evidence.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace ridesync;
struct C : Clock { uint32_t now() const override { return 0; } };
struct S : StorageSink {
 std::string bytes;
 bool mount() override {return true;} bool openExclusive(const char*) override {return true;}
 size_t write(const char *p,size_t n) override { assert(n<=256); bytes.append(p,n); return n; }
 bool flush() override {return true;} void close() override {}
};
int main() {
 S sink; Storage s(sink,{42,"motion-fixture","synthetic",2,4,StorageFormat::MotionV4});
 C raw; SessionClock clock(raw,42,1000); auto t=clock.snapshot();
 for (unsigned state=0; state<4; ++state) {
   auto e=motion_fixture::sample(42,0);
   MotionEvidence m; m.state=static_cast<MotionAdmission>(state);
   if(state==2) {
     m.config=motion_fixture::config(); m.snapshot_max_age_ms=100;
     m.reference.externally_stationary=true; m.reference.session_id=42;
     m.reference.config_generation=3; m.reference.batch_sequence=13; m.reference.declaration=1;
     m.estimate.measurements_valid=m.estimate.static_tilt_valid=true;
     m.estimate.specific_force_mps2.z=9.80665f;
   }
   assert(s.enqueueImu(t,e,false,m));
 }
 auto e=motion_fixture::sample(42,0);e.kind=RecordKind::ImuConfig;
 MotionEvidence m; m.state=MotionAdmission::Enabled; m.config=motion_fixture::config();m.snapshot_max_age_ms=100;
 assert(s.enqueueImu(t,e,false,m));
 e.kind=RecordKind::ImuHealth;assert(s.enqueueImu(t,e,false,m));
 e.kind=RecordKind::ImuControl;assert(s.enqueueImu(t,e,false,m));
 ModemSnapshot gps;gps.session_id=42;assert(s.enqueue(t,gps));
 s.requestStop();for(unsigned i=0;i<1000 && !s.health().stopped;++i)s.workerStep();
 assert(s.health().flushed==8 && s.health().stopped);
 std::fwrite(sink.bytes.data(),1,sink.bytes.size(),stdout);
 std::fprintf(stderr,"motion=%zu record=%zu storage=%zu\n",sizeof(MotionEvidence),sizeof(TelemetryRecord),sizeof(Storage));
}
'''

class MotionDiskTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        p = Path(cls.directory.name)
        (p/'fixture.cpp').write_text(SOURCE)
        subprocess.run(['c++','-std=c++11','-I'+str(ROOT/'include'),'-I'+str(ROOT),str(p/'fixture.cpp'),
                        str(ROOT/'src/storage.cpp'), str(ROOT/'src/motion_estimator.cpp'),str(ROOT/'src/session_clock.cpp'),'-o',str(p/'fixture')],check=True)
        cls.data = subprocess.check_output([str(p/'fixture')],text=True)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_paired_rows_preserve_raw_prefix_and_exact_presence(self):
        rows = parse(self.data)
        self.assertEqual(8,len(rows))
        samples = [r for r in rows if r['kind']=='imu']
        self.assertEqual(['0','1','2','3'],[r['motion_state'] for r in samples])
        for row in samples:
            self.assertEqual('2048',row['accel_z'])
            self.assertEqual('0',row['motion_dynamic_lean_valid'])
            self.assertEqual('0',row['motion_dynamic_acceleration_valid'])
            self.assertEqual('3',row['generation'])
            if row['motion_state']=='2':
                self.assertEqual('1',row['motion_static_tilt_valid'])
                self.assertAlmostEqual(9.80665,float(row['motion_force_z_mps2']),places=5)
                self.assertEqual('0',row['motion_roll_rad'])
            else:
                self.assertEqual('',row['motion_force_z_mps2'])
                self.assertEqual('',row['motion_roll_rad'])
                self.assertEqual('',row['motion_mount_id'])
        for line in self.data.splitlines():
            if line.startswith(('imu,','config,','health,','control,')):
                self.assertEqual(124,len(next(csv.reader([line]))))
            elif line.startswith('gps,'):
                self.assertEqual(36,len(next(csv.reader([line]))))
        config = next(r for r in rows if r['kind']=='config')
        self.assertEqual('0',config['motion_reference_declaration'])
        self.assertEqual('',config['motion_force_z_mps2'])

    def test_layout_and_partial_record_rejections(self):
        for bad in (self.data.replace('#ridesync_telemetry,4','#ridesync_telemetry,5'),
                    self.data.replace('#imu_layout,4,see_docs/motion_logging.md\n',''),
                    self.data.replace('#camera_layout,3,see_docs/log_format.md\n',''),
                    self.data[:-1]):
            with self.assertRaises(ValueError): parse(bad)

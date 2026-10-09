"""Independent MotionV4 parser/formatter checks; synthetic repository-owned evidence."""
import csv
from pathlib import Path
import subprocess
import tempfile
import unittest
from telemetry_parser import parse, COMMON, IMU
ROOT = Path(__file__).resolve().parents[1]

SOURCE = r'''
#include "telemetry_admission.h"
#include "camera_manager.h"
#include "test/fixtures/motion/evidence.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <string>
using namespace ridesync;
static_assert(Storage::kCapacity==8 && Storage::kMaxRowBytes==3072 && Storage::kChunkBytes==256,
              "storage resource contract changed");
static_assert(sizeof(MotionEvidence)<=160 && sizeof(TelemetryRecord)<=704,
              "paired payload bound changed");
struct C : Clock { uint32_t now() const override { return 0; } };
struct S : StorageSink {
 std::string bytes;
 bool mount() override {return true;} bool openExclusive(const char*) override {return true;}
 size_t write(const char *p,size_t n) override { assert(n<=256); bytes.append(p,n); return n; }
 bool flush() override {return true;} void close() override {}
};
struct Reference : StaticMotionReferenceSource {
 StaticMotionReference referenceFor(const ImuEvidence &e) override {
  StaticMotionReference r; r.externally_stationary=true; r.session_id=e.session_id;
  r.config_generation=e.config.generation; r.batch_sequence=e.batch_sequence; r.declaration=1; return r;
 }
};
int main(int argc, char **) {
 S sink; Storage s(sink,{42,"motion-fixture","synthetic",2,4,StorageFormat::MotionV4});
 C raw; SessionClock clock(raw,42,1000); auto t=clock.snapshot();
 ImuInbox inbox; Reference source; MotionAdmissionConfig options;
 options.requested=options.imu_qualified=true; options.estimator=motion_fixture::config();options.snapshot_max_age_ms=100;
 TelemetryAdmission admission(clock,s,inbox,nullptr,options,&source);
 if(argc>1) {
  auto e=motion_fixture::sample(42,0);
  e.batch_sequence=e.frame_sequence=e.byte_position=e.sensor_epoch=UINT32_MAX;
  e.sensor_time_present=false; e.sensor_time_ticks24=0xffffff;
  e.drain_known=true; e.drain_start_millis32=e.drain_end_millis32=UINT32_MAX;
  e.config.generation=e.config.sensor_id=UINT32_MAX;
  for(unsigned i=0;i<3;++i) e.accel[i]=e.gyro[i]=32766;
  MotionEvidence m; m.state=MotionAdmission::Enabled; m.config=motion_fixture::config(); m.snapshot_max_age_ms=60000;
  m.estimate.measurements_valid=true;
  m.estimate.specific_force_mps2.x=std::numeric_limits<float>::max();
  m.estimate.specific_force_mps2.y=-std::numeric_limits<float>::max();
  m.estimate.specific_force_mps2.z=std::numeric_limits<float>::min();
  m.estimate.angular_rate_rad_s=m.estimate.specific_force_mps2;
  assert(s.enqueueImu(t,e,false,m)); s.requestStop();
  for(unsigned i=0;i<1000 && !s.health().stopped;++i)s.workerStep();
  assert(s.health().flushed==1);std::fwrite(sink.bytes.data(),1,sink.bytes.size(),stdout);return 0;
 }
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
   if(state==2) {
     assert(admission.event(e)); admission.withdrawMotionReference(); admission.revokeMotion();
   } else assert(s.enqueueImu(t,e,false,m));
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
                        str(ROOT/'src/telemetry_admission.cpp'),str(ROOT/'src/storage.cpp'), str(ROOT/'src/motion_estimator.cpp'),str(ROOT/'src/session_clock.cpp'),'-o',str(p/'fixture')],check=True)
        cls.data = subprocess.check_output([str(p/'fixture')],text=True)
        cls.extreme = subprocess.check_output([str(p/'fixture'),'extreme'],text=True)

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

    def test_strict_motion_presence_and_qualification_mutations(self):
        # Literal V4 suffix positions, independently specified from production.
        edits = {1:['2'],2:['0','60001'],3:['2'],4:['0'],8:['0'],10:['-1'],
                 20:['43'],21:['4'],22:['14'],23:['0'],24:['0'],25:['0'],26:['1'],
                 28:['','nan','inf','1e999']}
        lines=self.data.splitlines()
        target=next(i for i,line in enumerate(lines) if line.startswith('imu,') and next(csv.reader([line]))[88]=='2')
        for field, values in edits.items():
            for value in values:
                with self.subTest(field=field,value=value):
                    cells=next(csv.reader([lines[target]]));cells[88+field]=value
                    modified=lines[:];modified[target]=','.join(cells)
                    with self.assertRaises(ValueError):parse('\n'.join(modified)+'\n')
        inactive=next(i for i,line in enumerate(lines) if line.startswith('imu,') and next(csv.reader([line]))[88]=='0')
        for field in (1,19,28):
            cells=next(csv.reader([lines[inactive]]));cells[88+field]='1'
            modified=lines[:];modified[inactive]=','.join(cells)
            with self.assertRaises(ValueError):parse('\n'.join(modified)+'\n')

    def test_extreme_finite_rows_fit_fixed_buffer(self):
        rows=parse(self.extreme)
        self.assertEqual(1,len(rows))
        self.assertTrue(all(len(line.encode('ascii'))+1 < 3072 for line in self.extreme.splitlines()))
        self.assertLess(1848+36*21+1,3072)
        self.assertEqual('0',rows[0]['motion_static_tilt_valid'])

    def test_rejects_malformed_raw_conversion_and_receipt(self):
        lines=self.data.splitlines()
        target=next(i for i,line in enumerate(lines) if line.startswith('imu,') and next(csv.reader([line]))[88]=='2')
        names=['kind']+COMMON+IMU
        for field,value in [('accel_gain_x_denominator','0'),('gyro_gain_z_numerator','NaN'),
                            ('accel_offset_x','inf'),('receipt_millis32','NaN'),
                            ('accel_scale_numerator','-1'),('sensor_id','-1'),('timing_flags','16'),
                            ('receipt_known','0'),('timing_flags','1'),('event_count_lower_bound','2')]:
            cells=next(csv.reader([lines[target]]));cells[names.index(field)]=value
            modified=lines[:];modified[target]=','.join(cells)
            with self.subTest(field=field,value=value), self.assertRaises(ValueError):
                parse('\n'.join(modified)+'\n')

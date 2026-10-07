"""Synthetic author-created evidence, not a physical FIFO/card/ride capture.

Independent parser checks actual Storage serialization. No vendor bytes/code or
private locations/identifiers are reused; repository MIT license applies.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from telemetry_parser import parse
ROOT = Path(__file__).resolve().parents[1]

SOURCE = r'''
#include "telemetry_admission.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace ridesync;
struct C : Clock { uint32_t value=0; uint32_t now() const override { return value; } };
struct S : StorageSink {
  std::string bytes;
  bool mount() override { return true; } bool openExclusive(const char *) override { return true; }
  size_t write(const char *p,size_t n) override { bytes.append(p,n); return n; }
  bool flush() override { return true; } void close() override {}
};
int main(int argc,char **) {
 S sink; Storage s(sink,{42,"fixture-fw","synthetic",2,4,StorageFormat::MixedV2});
 C raw; SessionClock clock(raw,42,1000); ImuInbox inbox; TelemetryAdmission a(clock,s,inbox);
 assert(clock.anchor({2026,1,1,0,0,0,0}));
 ImuEvidence e; e.session_id=42; e.kind=RecordKind::ImuConfig; e.config.generation=7;
 e.config.sensor_id=10; e.config.accel_range_mg=16000; e.config.gyro_range_mdps=2000000;
 e.config.accel_scale_numerator=1; e.config.accel_scale_denominator=2048;
 e.config.gyro_scale_numerator=125; e.config.gyro_scale_denominator=2048;
 assert(a.event(e));
 if(argc>1) {
   e.config.calibration_offsets_known=true; e.config.calibration_gains_known=true;
   e.config.calibration_time_known=true; e.config.calibration_temperature_known=true;
   e.config.calibration_utc_ms=INT64_MIN; e.config.calibration_temperature_millic=INT32_MIN;
   e.config.generation=UINT32_MAX; e.config.sensor_id=UINT32_MAX;
   for(unsigned i=0;i<3;++i) {
     e.config.accel_offset[i]=INT16_MIN; e.config.gyro_offset[i]=INT16_MAX;
     e.config.accel_gain_numerator[i]=UINT32_MAX; e.config.accel_gain_denominator[i]=UINT32_MAX;
     e.config.gyro_gain_numerator[i]=UINT32_MAX; e.config.gyro_gain_denominator[i]=UINT32_MAX;
   }
 }
 e.kind=RecordKind::ImuSample; e.accel[0]=-32768; e.accel[1]=-1; e.accel[2]=0;
 e.gyro[0]=1; e.gyro[1]=123; e.gyro[2]=32767;
 e.receipt_known=true; e.receipt_millis32=0xffffffff; e.drain_known=true;
 e.drain_start_millis32=0xffffffff; e.drain_end_millis32=0; e.timing_flags=4;
 assert(a.event(e));
 assert(clock.anchor({2026,1,2,0,0,0,0}));
 e.kind=RecordKind::ImuControl; e.event_code=5; e.event_length=3;
 e.event_bytes[0]=0; e.event_bytes[1]=0; e.event_bytes[2]=0;
 e.sensor_time_present=true; e.sensor_time_ticks24=0; assert(a.event(e));
 e.kind=RecordKind::ImuHealth; e.event_code=2; e.event_length=0; e.sensor_time_present=false;
 assert(a.event(e));
 ModemSnapshot gps; gps.session_id=42; assert(a.gps(gps));
 a.requestStop(); inbox.finish(); a.tick();
 for(int i=0;i<500;++i) s.workerStep();
 assert(s.health().flushed==5); assert(s.health().stopped);
 std::fwrite(sink.bytes.data(),1,sink.bytes.size(),stdout);
 std::fprintf(stderr,"sizes evidence=%zu batch=%zu record=%zu inbox=%zu storage=%zu admission=%zu\n",
 sizeof(ImuEvidence),sizeof(ImuBatch),sizeof(TelemetryRecord),sizeof(ImuInbox),sizeof(Storage),sizeof(TelemetryAdmission));
}
'''


class TelemetryDiskTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        path = Path(cls.directory.name)
        (path / 'fixture.cpp').write_text(SOURCE)
        subprocess.run(['c++', '-std=c++11', '-pthread', '-I'+str(ROOT/'include'),
                        str(path/'fixture.cpp'), str(ROOT/'src/storage.cpp'),
                        str(ROOT/'src/session_clock.cpp'), str(ROOT/'src/telemetry_admission.cpp'),
                        '-o',str(path/'fixture')],check=True)
        cls.data = subprocess.check_output([str(path/'fixture')],text=True)
        cls.max_data = subprocess.check_output([str(path/'fixture'),'maximum'],text=True)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_schema_config_counts_zero_tick_and_old_anchor(self):
        rows = parse(self.data)
        self.assertEqual(['config','imu','control','health','gps'],[r['kind'] for r in rows])
        sample = rows[1]
        self.assertEqual('7',sample['generation'])
        self.assertEqual('',sample['accel_offset_x'])
        self.assertEqual(('1','2048'),(sample['accel_scale_numerator'],sample['accel_scale_denominator']))
        self.assertEqual(['-32768','-1','0','1','123','32767'],
                         [sample[p+'_'+axis] for p in ('accel','gyro') for axis in ('x','y','z')])
        self.assertEqual('4294967295',sample['receipt_millis32'])
        self.assertEqual('0',sample['drain_end_millis32'])
        self.assertEqual('1',sample['anchor_sequence'])
        self.assertEqual('2',rows[2]['anchor_sequence'])
        self.assertEqual('0',rows[2]['sensor_time_ticks24'])
        self.assertEqual('000000',rows[2]['event_bytes'])
        self.assertEqual('',sample['acquisition_ms'])

    def test_known_calibration_extremes_fit_bounded_rows(self):
        sample = parse(self.max_data)[1]
        self.assertEqual('-32768',sample['accel_offset_x'])
        self.assertEqual('4294967295',sample['gyro_gain_z_denominator'])
        self.assertEqual('-9223372036854775808',sample['calibration_utc_ms'])
        self.assertEqual('-2147483648',sample['calibration_temperature_millic'])
        self.assertLessEqual(max(map(len,self.max_data.splitlines())),1847)

    def test_unsupported_version_and_partial_row_fail_closed(self):
        for data in (self.data.replace('#ridesync_telemetry,2','#ridesync_telemetry,3'),self.data[:-1]):
            with self.assertRaises(ValueError): parse(data)

    def test_malformed_count_presence_and_payload_fail_closed(self):
        for before,after in (('-32768,','-32769,'), ('000000,1,0','0000,1,0'),
                             ('4294967295,1,4294967295','4294967295,0,4294967295')):
            self.assertIn(before,self.data)
            with self.assertRaises(ValueError): parse(self.data.replace(before,after))

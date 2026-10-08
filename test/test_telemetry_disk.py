"""Synthetic author-created evidence, not a physical FIFO/card/ride capture.

Independent parser checks actual Storage serialization. No vendor bytes/code or
private locations/identifiers are reused; repository MIT license applies.
"""
import csv
from pathlib import Path
import subprocess
import tempfile
import unittest
from telemetry_parser import CAMERA, COMMON, IMU, parse
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


TIMESTAMP_SOURCE = r'''
#include "telemetry_admission.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace ridesync;
struct C : Clock { uint32_t value=0; uint32_t now() const override {return value;} };
struct S : StorageSink {
 std::string bytes;
 bool mount() override {return true;} bool openExclusive(const char *) override {return true;}
 size_t write(const char *p,size_t n) override {bytes.append(p,n);return n;}
 bool flush() override {return true;} void close() override {}
};
int main(int argc,char **) {
 S legacy_sink,mixed_sink;
 Storage legacy(legacy_sink,{42,"timestamp-fw","synthetic"});
 Storage mixed(mixed_sink,{42,"timestamp-fw","synthetic",2,4,StorageFormat::MixedV2});
 C raw; SessionClock clock(raw,42,100);
 ModemSnapshot gps;gps.session_id=42;
 ImuEvidence imu;imu.session_id=42;
 auto emit=[&](bool correct=false) {
   const auto t=clock.snapshot();
   assert(legacy.enqueue(t,gps));assert(mixed.enqueue(t,gps));assert(mixed.enqueueImu(t,imu));
   if(correct) assert(clock.anchor({2026,1,4,0,0,0,0}));
   for(unsigned i=0;i<100;++i) {legacy.workerStep();mixed.workerStep();}
 };
 emit();
 raw.value=10;assert(clock.anchor({2026,1,1,0,0,0,0}));raw.value=20;emit();
 raw.value=30;assert(clock.anchor({2026,1,2,0,0,0,0},true,0));emit();
 raw.value=50;assert(clock.anchor({2026,1,3,0,0,0,0},true,123));raw.value=60;emit();
 raw.value=150;emit();raw.value=151;emit(true);
 legacy.requestStop();mixed.requestStop();
 for(unsigned i=0;i<100;++i) {legacy.workerStep();mixed.workerStep();}
 assert(legacy.health().flushed==6 && mixed.health().flushed==12);
 const auto &bytes=argc>1?legacy_sink.bytes:mixed_sink.bytes;
 std::fwrite(bytes.data(),1,bytes.size(),stdout);
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
        (path / 'timestamps.cpp').write_text(TIMESTAMP_SOURCE)
        subprocess.run(['c++', '-std=c++11', '-pthread', '-I'+str(ROOT/'include'),
                        str(path/'timestamps.cpp'), str(ROOT/'src/storage.cpp'),
                        str(ROOT/'src/session_clock.cpp'), str(ROOT/'src/telemetry_admission.cpp'),
                        '-o',str(path/'timestamps')],check=True)
        cls.timestamps = subprocess.check_output([str(path/'timestamps')],text=True)
        cls.gps_timestamps = subprocess.check_output([str(path/'timestamps'),'gps'],text=True)
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

    def test_timestamp_variants_preserve_gps_v1_bytes_and_mixed_associations(self):
        self.assertEqual((ROOT/'test/fixtures/telemetry/gps_v1_timestamp_variants.csv').read_text(),
                         self.gps_timestamps)
        rows = parse(self.timestamps)
        self.assertEqual(['gps','imu']*6,[r['kind'] for r in rows])
        expected = [
            ['42','0','0','0','','','','0','','','0',''],
            ['42','20','0','1','1','10','1767225600000','0','','10','1','1767225600010'],
            ['42','30','0','1','2','30','1767312000000','1','0','0','1','1767312000000'],
            ['42','60','0','1','3','50','1767398400000','1','123','10','1','1767398400010'],
            ['42','150','0','1','3','50','1767398400000','1','123','100','1','1767398400100'],
            ['42','151','0','2','3','50','1767398400000','1','123','101','0',''],
        ]
        for index, timestamp in enumerate(expected):
            for row in rows[index*2:index*2+2]:
                self.assertEqual(timestamp,[row[field] for field in COMMON])

    def test_nonboolean_calibration_presence_is_rejected(self):
        for flag in ('calibration_offsets_known', 'calibration_gains_known'):
            for invalid in ('2', '-1', ''):
                with self.subTest(flag=flag, invalid=invalid):
                    lines = self.data.splitlines()
                    index = next(i for i, line in enumerate(lines) if line.startswith('imu,'))
                    fields = next(csv.reader([lines[index]]))
                    fields[(['kind'] + COMMON + IMU).index(flag)] = invalid
                    lines[index] = ','.join(fields)
                    with self.assertRaisesRegex(ValueError, 'coefficient presence'):
                        parse('\n'.join(lines) + '\n')

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

    def test_camera_v3_keeps_gps_imu_and_distinct_ack(self):
        source = r'''
#include "storage.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace ridesync;
struct S : StorageSink {
 std::string bytes;
 bool mount() override {return true;} bool openExclusive(const char *) override {return true;}
 size_t write(const char *p,size_t n) override {bytes.append(p,n);return n;}
 bool flush() override {return true;} void close() override {}
};
int main() {
 S sink; Storage storage(sink,{42,"fw","synthetic",2,4,StorageFormat::CameraV3});
 RecordTimestamp t;t.session_id=42;t.monotonic_quality=MonotonicQuality::Valid;t.monotonic_ms=12;
 ModemSnapshot gps;gps.session_id=42;assert(storage.enqueue(t,gps));
 ImuEvidence imu;imu.session_id=42;imu.kind=RecordKind::ImuHealth;
 assert(storage.enqueueImu(t,imu));
 CameraEvidence e;e.session_id=42;e.peer_id=301;e.model=CameraModel::HERO12_BLACK;
 e.kind=CameraEventKind::WireAck;e.operation=Operation::Start;e.intent_id=4;
 e.connection_generation=2;e.operation_generation=7;
 e.ack_domain=CameraAckDomain::Classic;e.ack_action=CameraAckAction::ShutterOn;
 e.event_receipt_known=true;e.event_receipt_ms=8;e.event_receipt_age_ms=4;
 assert(storage.enqueueCamera(t,e));
 e.kind=CameraEventKind::RequestRefused;e.operation=Operation::Wake;e.intent_id=0;
 e.connection_generation=e.operation_generation=0;e.ack_domain=CameraAckDomain::None;
 e.ack_action=CameraAckAction::None;e.error=CameraError::InvalidPolicy;
 assert(storage.enqueueCamera(t,e));
 storage.requestStop();for(unsigned i=0;i<500;++i)storage.workerStep();
 assert(storage.health().flushed==4 && storage.health().stopped);
 std::fwrite(sink.bytes.data(),1,sink.bytes.size(),stdout);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'camera.cpp').write_text(source)
            subprocess.run(['c++','-std=c++11','-I'+str(ROOT/'include'),
                            str(path/'camera.cpp'),str(ROOT/'src/storage.cpp'),
                            '-o',str(path/'camera')],check=True)
            data = subprocess.check_output([str(path/'camera')],text=True)
        rows = parse(data)
        self.assertEqual(['gps','health','camera','camera'],[row['kind'] for row in rows])
        self.assertEqual('owner_admission',rows[2]['time_domain'])
        self.assertEqual('0',rows[2]['radio_receipt_known'])
        self.assertEqual('0',rows[2]['acquisition_known'])
        self.assertEqual('1',rows[2]['event_receipt_known'])
        self.assertEqual('8',rows[2]['event_receipt_ms'])
        self.assertEqual('4',rows[2]['event_receipt_age_ms'])
        self.assertEqual('301',rows[2]['peer_id'])
        self.assertEqual('0',rows[2]['peer_slot'])
        self.assertEqual('4',rows[2]['intent_id'])
        self.assertEqual('10',rows[3]['error'])
        camera_columns = ['kind'] + COMMON + CAMERA
        for field, invalid in [('peer_slot','8'), ('peer_id','0'), ('peer_id','4294967296'),
                               ('ack_domain','2'), ('ack_action','0'),
                               ('time_domain','radio_receipt'),
                               ('radio_receipt_known','1'), ('event_receipt_known','0'),
                               ('event_receipt_age_ms','5'),
                               ('intent_id','0')]:
            with self.subTest(field=field, invalid=invalid):
                lines = data.splitlines()
                index = next(i for i, line in enumerate(lines) if line.startswith('camera,'))
                values = next(csv.reader([lines[index]]))
                values[camera_columns.index(field)] = invalid
                lines[index] = ','.join(values)
                with self.assertRaises(ValueError): parse('\n'.join(lines) + '\n')
        with self.assertRaises(ValueError): parse(data.replace('#ridesync_telemetry,3',
                                                                  '#ridesync_telemetry,4'))
        with self.assertRaises(ValueError): parse(data[:-1])

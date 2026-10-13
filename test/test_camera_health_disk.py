"""Synthetic independent V7 schema fixtures and real bounded logger evidence."""
import csv
from pathlib import Path
import subprocess
import tempfile
import unittest
from telemetry_parser import COMMON, parse
ROOT = Path(__file__).resolve().parents[1]
FIELDS = ('peer_slot peer_id model group_generation intent_id connection_generation operation_generation '
          'episode transition_sequence transition_kind reason severity capability validity profile field historical value unit '
          'threshold_enter threshold_clear time_domain sample_receipt_known sample_receipt_ms sample_age_ms '
          'radio_receipt_known radio_receipt_ms acquisition_known acquisition_ms '
          'transition_dropped transition_coalesced history_incomplete accepted dropped rejected lost written flushed').split()
HEADER = ('#ridesync_telemetry,7\n#imu_layout,4,see_docs/motion_logging.md\n'
          '#camera_layout,3,see_docs/log_format.md\n'
          '#dynamic_layout,1,see_docs/dynamic_motion_estimator.md\n'
          '#camera_health_layout,1,see_docs/log_format.md\n'
          '#bookmark_layout,1,see_docs/log_format.md\n')
VALUES = ['42','100','0','0','','','','0','','','0','',
          '0','301','4','7','9','2','3','1','10','0','3','2','2','1','3','1','1','20','2',
          '20','25','owner_admission','1','90','10','0','','0','','0','0','0','1','0','0','0','0','0']

def fixture(**changes):
    row = dict(zip(COMMON + FIELDS, VALUES)); row.update(changes)
    return HEADER + 'camera_health,' + ','.join(row[f] for f in COMMON + FIELDS) + ',,,\n'

class HealthSchemaTest(unittest.TestCase):
    def test_independent_v7_fixture_roundtrip_and_retirement(self):
        row = parse(fixture())[0]
        self.assertEqual(('301','7','9','2','3','1','10'), tuple(row[f] for f in
                         ('peer_id','group_generation','intent_id','connection_generation',
                          'operation_generation','episode','transition_sequence')))
        self.assertEqual('owner_admission', row['time_domain'])
        self.assertEqual('3', parse(fixture(transition_kind='3',severity='0'))[0]['transition_kind'])
        self.assertEqual('2', parse(fixture(transition_kind='2',severity='0'))[0]['transition_kind'])
        self.assertEqual('0', parse(fixture(intent_id='0',operation_generation='0'))[0]['intent_id'])
    def test_valid_recording_rejects_unknown_zero_without_rejecting_unknown_history(self):
        recording = dict(reason='2',field='0',unit='1',value='0')
        with self.assertRaises(ValueError): parse(fixture(**recording))
        for value in ('1','2'):
            self.assertEqual(value,parse(fixture(**{**recording,'value':value}))[0]['value'])
        self.assertEqual('0',parse(fixture(**{**recording,'validity':'0'}))[0]['value'])
        self.assertEqual('',parse(fixture(**{**recording,'validity':'0','historical':'0',
            'value':'','sample_receipt_known':'0','sample_receipt_ms':'','sample_age_ms':''}))[0]['value'])
    def test_receive_edge_coalescing_marks_history_incomplete_without_storage_loss(self):
        row = parse(fixture(transition_coalesced='2',history_incomplete='1'))[0]
        self.assertEqual('1',row['history_incomplete'])
    def test_malformed_new_layout_fails_closed(self):
        for field, invalid in [('peer_slot','8'),('peer_id','0'),('model','9'),('transition_sequence','0'),
                ('transition_kind','4'),('reason','8'),('severity','4'),('capability','3'),
                ('validity','3'),('profile','4'),('profile','1'),('field','5'),('field','0'),('historical','2'),('historical','0'),('unit','6'),('value','18446744073709551616'),
                ('value','101'),('unit','3'),('severity','0'),('sample_age_ms','11'),
                ('time_domain','radio_receipt'),('radio_receipt_known','1'),('acquisition_known','1'),
                ('sample_receipt_known','0'),('history_incomplete','1'),('transition_dropped','1')]:
            with self.subTest(field=field):
                with self.assertRaises(ValueError): parse(fixture(**{field:invalid}))
        for data in [fixture()[:-1],fixture().replace(',7\n',',8\n'),
                     fixture().replace('#camera_health_layout,1,see_docs/log_format.md\n',''),
                     fixture().replace('camera_health,','camera,')]:
            with self.assertRaises(ValueError): parse(data)


SOURCE = r'''
#include "camera_health_logger.h"
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
int main() {
 S sink; Storage storage(sink,{42,"fw","synthetic",2,4,StorageFormat::CameraHealthV7});
 C raw; SessionClock clock(raw,42,1000); ImuInbox inbox; CameraInbox camera;
 TelemetryAdmission admission(clock,storage,inbox,&camera);
 CameraHealthEvidence e;e.peer_id=301;e.model=CameraModel::HERO12_BLACK;
 auto &t=e.transition;t.peer=0;t.group_generation=7;t.intent_id=9;t.connection=2;t.operation=3;
 t.episode=1;t.sequence=10;t.kind=HealthTransitionKind::Enter;t.reason=HealthReason::BatteryLow;
 t.severity=HealthSeverity::Warning;t.capability=CapabilityState::Supported;
 t.field=HealthField::BatteryPercent;t.historical=true;
 t.validity=HealthValidity::Valid;t.profile=HealthProfile::Hero12V1;t.value=20;
 t.unit=HealthUnit::Percent;t.threshold_enter=20;t.threshold_clear=25;t.received_ms=90;t.age_ms=5;
 raw.value=100;assert(admission.health(e));
 t.kind=HealthTransitionKind::SeverityChanged;t.severity=HealthSeverity::Critical;t.value=5;
 t.sequence=11;assert(admission.health(e));
 t.kind=HealthTransitionKind::Recovered;t.severity=HealthSeverity::None;t.value=25;
 t.sequence=12;assert(admission.health(e));
 t.kind=HealthTransitionKind::Retired;t.validity=HealthValidity::Unknown;
 t.sequence=13;assert(admission.health(e));
 t.sequence=0;assert(!admission.health(e));t.sequence=14;
 e.model=CameraModel::Unknown;assert(!admission.health(e));e.model=CameraModel::HERO12_BLACK;
 auto h=storage.kindHealth(RecordKind::CameraHealth);assert(h.accepted==4 && h.rejected==2);
 ModemSnapshot gps;gps.session_id=42;assert(admission.gps(gps));
 ImuEvidence imu;imu.session_id=42;assert(admission.event(imu));
 for(int i=0;i<500;++i)storage.workerStep();
 CameraEvidence c;c.session_id=42;c.peer_id=301;c.model=CameraModel::HERO12_BLACK;
 c.kind=CameraEventKind::RequestAccepted;c.intent_id=1;assert(camera.publish(c));
 for(int i=0;i<500;++i){admission.tick();storage.workerStep();}
 auto bookmark=admission.bookmark(BookmarkSource::Serial,BookmarkGesture::Serial);
 assert(bookmark.outcome==BookmarkOutcome::Admitted);
 CameraMarkerEvidence marker;marker.session_id=42;marker.peer_id=301;
 marker.model=CameraModel::HERO12_BLACK;marker.bookmark_id=bookmark.bookmark_id;
 marker.kind=CameraMarkerKind::RequestRefused;marker.error=CameraError::Unsupported;
 assert(storage.enqueueCameraMarker(clock.snapshot(),marker));
 admission.requestStop();inbox.finish();camera.finish();admission.tick();
 for(int i=0;i<500;++i)storage.workerStep();
 assert(storage.health().flushed==9 && storage.health().stopped);
 S old;Storage legacy(old,{42,"fw","synthetic",2,4,StorageFormat::MotionV5});
 assert(!legacy.enqueueHealth(clock.snapshot(),e));
 static_assert(sizeof(TelemetryRecord)<=888,"no Storage queue growth");
 static_assert(sizeof(CameraHealthEvidence)<=96,"bounded health evidence");
 std::fwrite(sink.bytes.data(),1,sink.bytes.size(),stdout);
}
'''

class HealthDiskTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory();path=Path(cls.directory.name)
        (path/'fixture.cpp').write_text(SOURCE)
        subprocess.run(['c++','-std=c++11','-pthread','-I'+str(ROOT/'include'),str(path/'fixture.cpp'),
                        *[str(ROOT/'src'/f) for f in ('storage.cpp','telemetry_admission.cpp',
                            'session_clock.cpp','motion_estimator.cpp','dynamic_motion_estimator.cpp')],
                        '-o',str(path/'fixture')],check=True)
        cls.data=subprocess.check_output([str(path/'fixture')],text=True)
    @classmethod
    def tearDownClass(cls): cls.directory.cleanup()
    def test_valid_recording_zero_is_rejected_by_real_serializer(self):
        source = r'''#include "storage.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace ridesync;
struct S : StorageSink {
 std::string bytes;
 bool mount() override {return true;} bool openExclusive(const char*) override {return true;}
 size_t write(const char *p,size_t n) override {bytes.append(p,n);return n;}
 bool flush() override {return true;} void close() override {}
};
int main() {
 S sink;Storage storage(sink,{42,"fw","synthetic",2,4,StorageFormat::CameraHealthV7});
 RecordTimestamp timestamp;timestamp.session_id=42;timestamp.monotonic_quality=MonotonicQuality::Valid;
 CameraHealthEvidence e;e.peer_id=301;e.model=CameraModel::HERO12_BLACK;e.sample_receipt_known=true;
 auto &t=e.transition;t.sequence=1;t.episode=1;t.reason=HealthReason::RecordingEvidenceLost;
 t.severity=HealthSeverity::Warning;t.capability=CapabilityState::Supported;
 t.validity=HealthValidity::Valid;t.profile=HealthProfile::Hero12V1;t.field=HealthField::Recording;
 t.unit=HealthUnit::RecordingState;t.historical=true;t.value=0;
 assert(!storage.enqueueHealth(timestamp,e,false));
 t.value=1;assert(storage.enqueueHealth(timestamp,e,false));
 t.sequence=2;t.value=2;assert(storage.enqueueHealth(timestamp,e,false));
 t.sequence=3;t.validity=HealthValidity::Unknown;t.value=0;
 assert(storage.enqueueHealth(timestamp,e,false));
 t.sequence=4;t.historical=false;e.sample_receipt_known=false;
 assert(storage.enqueueHealth(timestamp,e,false));
 storage.requestStop();for(unsigned i=0;i<500;++i)storage.workerStep();
 assert(storage.kindHealth(RecordKind::CameraHealth).rejected==1);
 std::fwrite(sink.bytes.data(),1,sink.bytes.size(),stdout);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory);(path/'recording.cpp').write_text(source)
            subprocess.run(['c++','-std=c++11','-I'+str(ROOT/'include'),str(path/'recording.cpp'),
                str(ROOT/'src/storage.cpp'),str(ROOT/'src/motion_estimator.cpp'),'-o',str(path/'recording')],check=True)
            data=subprocess.check_output([str(path/'recording')],text=True)
        rows=parse(data)
        self.assertEqual(['1','2','0',''],[row['value'] for row in rows])
        self.assertEqual(['1','1','0','0'],[row['validity'] for row in rows])
    def test_real_serialization_retains_gps_camera_and_motion_rows(self):
        rows=parse(self.data)
        self.assertEqual(['camera_health']*4+['gps','imu','camera','bookmark','marker'],[r['kind'] for r in rows])
        self.assertEqual(['0','1','2','3'],[r['transition_kind'] for r in rows[:4]])
        self.assertEqual(['10','11','12','13'],[r['transition_sequence'] for r in rows[:4]])
        self.assertEqual(['10']*4,[r['sample_age_ms'] for r in rows[:4]])
        self.assertEqual(['90']*4,[r['sample_receipt_ms'] for r in rows[:4]])
        self.assertIn('dynamic_requested',rows[5]);self.assertIn('motion_state',rows[5])
        self.assertNotIn('01:02:03',self.data)
        self.assertEqual(rows[7]['bookmark_id'],rows[8]['bookmark_id'])

LOGGER_SOURCE = r'''
#include "camera_health_logger.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace ridesync;
struct C : Clock { uint32_t value=0; uint32_t now() const override {return value;} };
struct S : StorageSink {
 std::string bytes; bool fail=false;
 bool mount() override {return true;} bool openExclusive(const char *) override {return true;}
 size_t write(const char *p,size_t n) override {if(fail)return 0;bytes.append(p,n);return n;}
 bool flush() override {return true;} void close() override {}
};
struct Output : CameraHealthTransitionSink {
 unsigned count=0;uint32_t previous=0,dropped=0;
 void transition(const CameraHealthTransition &t,uint32_t id,CameraModel model,uint32_t loss,uint32_t) override {
   assert(t.sequence>previous && id==301 && model==CameraModel::HERO12_BLACK);
   previous=t.sequence;++count;dropped=loss;
 }
};
int main(int argc,char **) {
 C raw;SessionClock clock(raw,42,1000);S sink;
 Storage storage(sink,{42,"fw","synthetic",2,4,StorageFormat::CameraHealthV7});
 ImuInbox imu;CameraInbox camera;TelemetryAdmission admission(clock,storage,imu,&camera);
 if(argc>2) {
   CameraHealthEvaluator evaluator;CameraHealthLogger logger(evaluator,admission);
   assert(logger.configurePeer(0,301,CameraModel::X5));
   evaluator.configure(0,CameraModel::X5,2,true,0);
   QualifiedHealthSample sample;sample.connection=2;sample.sequence=1;sample.received_ms=0;
   sample.field=HealthField::Recording;sample.value=2;sample.capability=CapabilityState::Supported;
   sample.validity=HealthValidity::Valid;sample.profile=HealthProfile::X5Ce80;
   sample.unit=HealthUnit::RecordingState;assert(evaluator.observe(0,sample,0));
   Token token;token.connection=2;token.operation=3;
   evaluator.healthIntent(0,Operation::Start,9,7,false,token,0,1000);
   raw.value=5001;evaluator.tick(raw.value);admission.tick();
   for(unsigned i=0;i<100;++i)storage.workerStep();
   admission.requestStop();imu.finish();camera.finish();admission.tick();assert(!admission.stopped());
   raw.value=5002;evaluator.healthRetired(0,token,raw.value);logger.finish();admission.tick();
   assert(admission.stopped());for(unsigned i=0;i<100;++i)storage.workerStep();
   assert(storage.kindHealth(RecordKind::CameraHealth).flushed==2);
   CameraHealthEvidence late;assert(!admission.health(late));
   std::fwrite(sink.bytes.data(),1,sink.bytes.size(),stdout);return 0;
 }
 CameraHealthEvaluator evaluator;Output output;CameraHealthLogger logger(evaluator,admission,&output);
 assert(logger.attached());assert(logger.configurePeer(0,301,CameraModel::HERO12_BLACK));
 assert(!logger.configurePeer(1,301,CameraModel::HERO12_BLACK));
 evaluator.configure(0,CameraModel::HERO12_BLACK,2,true,raw.value,1);
 QualifiedHealthSample sample;sample.field=HealthField::Overheating;sample.connection=2;
 sample.capability=CapabilityState::Supported;sample.validity=HealthValidity::Valid;
 sample.profile=HealthProfile::Hero12V1;sample.unit=HealthUnit::Boolean;
 auto observe=[&](uint64_t value,uint32_t time) {
   raw.value=time;sample.received_ms=time;sample.value=value;++sample.sequence;
   assert(evaluator.observe(0,sample,raw.value));
 };
 for(unsigned i=0;i<12;++i){observe(1,i*15000);observe(0,i*15000+5000);observe(0,i*15000+10000);}
 observe(1,180000);
 assert(evaluator.pendingTransitions()==8 && evaluator.dropped()>0);
 assert(evaluator.severity(0,HealthReason::Overheating)==HealthSeverity::Critical);
 auto pending=evaluator.pendingTransitions();
 ModemSnapshot gps;gps.session_id=42;
 for(unsigned i=0;i<6;++i)assert(admission.gps(gps));
 CameraEvidence c;c.session_id=42;c.peer_id=301;c.model=CameraModel::HERO12_BLACK;
 c.kind=CameraEventKind::RequestAccepted;c.intent_id=1;
 for(unsigned i=0;i<3;++i)assert(camera.publish(c));
 assert(admission.tick()==2); // Existing camera quota still progresses separately.
 assert(evaluator.pendingTransitions()==pending-2 && output.count==2);
 assert(storage.kindHealth(RecordKind::CameraHealth).dropped>=evaluator.dropped()+2);
 assert(admission.gps(gps));assert(admission.gps(gps)); // GPS reserve remains available.
 assert(!logger.configurePeer(0,302,CameraModel::HERO12_BLACK));
 for(unsigned i=0;i<500;++i)storage.workerStep();
 if(argc>1) sink.fail=true;
 while(evaluator.pendingTransitions()){
   auto before=output.count;admission.tick();assert(output.count-before<=2);
   for(unsigned i=0;i<100;++i)storage.workerStep();
 }
 assert(output.count==8 && output.dropped>0);
 assert(evaluator.severity(0,HealthReason::Overheating)==HealthSeverity::Critical);
 admission.requestStop();assert(logger.stopRequested());imu.finish();camera.finish();admission.tick();
 assert(!admission.stopped()); // Producer finish follows final publication.
 logger.finish();admission.tick();assert(admission.stopped());
 for(unsigned i=0;i<500;++i)storage.workerStep();
 if(argc>1)assert(storage.kindHealth(RecordKind::CameraHealth).lost>0);
 else assert(storage.kindHealth(RecordKind::CameraHealth).flushed>0);
 std::fwrite(sink.bytes.data(),1,sink.bytes.size(),stdout);
 std::fprintf(stderr,"health sizes evaluator=%zu logger=%zu evidence=%zu record=%zu storage=%zu admission=%zu\n",
 sizeof(CameraHealthEvaluator),sizeof(CameraHealthLogger),sizeof(CameraHealthEvidence),sizeof(TelemetryRecord),sizeof(Storage),sizeof(TelemetryAdmission));
}
'''

class HealthLoggerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory();path=Path(cls.directory.name)
        (path/'logger.cpp').write_text(LOGGER_SOURCE)
        subprocess.run(['c++','-std=c++11','-pthread','-fsanitize=address,undefined',
                        '-I'+str(ROOT/'include'),str(path/'logger.cpp'),
                        *[str(ROOT/'src'/f) for f in ('storage.cpp','telemetry_admission.cpp',
                            'camera_health.cpp','camera_health_logger.cpp',
                            'session_clock.cpp','motion_estimator.cpp','dynamic_motion_estimator.cpp')],
                        '-o',str(path/'logger')],check=True)
        cls.path=path
    @classmethod
    def tearDownClass(cls):cls.directory.cleanup()
    def test_flood_and_storage_pressure_keep_current_snapshot_and_quotas(self):
        data=subprocess.check_output([str(self.path/'logger')],text=True)
        rows=parse(data);health=[r for r in rows if r['kind']=='camera_health']
        self.assertTrue(health)
        self.assertTrue(all(r['history_incomplete']=='1' for r in health))
        self.assertTrue(all(int(r['transition_dropped'])>0 for r in health))
        self.assertEqual(8,len([r for r in rows if r['kind']=='gps']))
    def test_final_retirement_preserves_raw_zero_historical_sample(self):
        data=subprocess.check_output([str(self.path/'logger'),'retirement','x5'],text=True)
        rows=parse(data)
        self.assertEqual(['0','3'],[r['transition_kind'] for r in rows])
        self.assertEqual(['2','2'],[r['reason'] for r in rows])
        self.assertEqual(['5001','5002'],[r['sample_age_ms'] for r in rows])
        for row in rows:
            self.assertEqual(('301','9','7','1','2','3','0','1'),tuple(row[f] for f in
                ('peer_id','intent_id','group_generation','episode','connection_generation',
                 'operation_generation','sample_receipt_ms','sample_receipt_known')))
    def test_media_failure_reports_loss_without_suppressing_output(self):
        subprocess.run([str(self.path/'logger'),'failure'],check=True,stdout=subprocess.DEVNULL)


OLD_SOURCE = r'''
#include "storage.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace ridesync;
struct S : StorageSink {
 std::string bytes;
 bool mount() override {return true;} bool openExclusive(const char *) override {return true;}
 size_t write(const char *p,size_t n) override {bytes.append(p,n);return n;}
 bool flush() override {return true;} void close() override {}
};
int main(int argc,char **argv) {
 assert(argc==2);unsigned version=std::atoi(argv[1]);
 S sink;Storage s(sink,{42,"fw","synthetic",2,4,static_cast<StorageFormat>(version-1)});
 RecordTimestamp t;t.session_id=42;t.monotonic_quality=MonotonicQuality::Valid;t.monotonic_ms=12;
 ModemSnapshot gps;gps.session_id=42;assert(s.enqueue(t,gps));
 if(version>=2){ImuEvidence imu;imu.session_id=42;imu.accel[0]=-123;assert(s.enqueueImu(t,imu));}
 if(version>=3){CameraEvidence c;c.session_id=42;c.peer_id=301;c.model=CameraModel::HERO12_BLACK;
 c.kind=CameraEventKind::RequestRefused;c.error=CameraError::Unsupported;assert(s.enqueueCamera(t,c));}
 s.requestStop();for(unsigned i=0;i<500;++i)s.workerStep();
 std::fwrite(sink.bytes.data(),1,sink.bytes.size(),stdout);
}
'''

class OldByteContractTest(unittest.TestCase):
    def test_versions_one_through_five_equal_committed_serializer_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory);(path/'fixture.cpp').write_text(OLD_SOURCE)
            baseline=path/'baseline';baseline.mkdir()
            for filename in subprocess.check_output(['git','ls-tree','-r','--name-only','HEAD','include'],
                                                    cwd=ROOT,text=True).splitlines() + ['src/storage.cpp']:
                target=baseline/Path(filename).name
                target.write_bytes(subprocess.check_output(['git','show','HEAD:'+filename],cwd=ROOT))
            outputs=[]
            for name,inc,source in [('old',baseline,baseline/'storage.cpp'),
                                    ('new',ROOT/'include',ROOT/'src/storage.cpp')]:
                binary=path/name
                subprocess.run(['c++','-std=c++11','-I'+str(inc),'-I'+str(ROOT/'include'),
                                str(path/'fixture.cpp'),str(source),str(ROOT/'src/motion_estimator.cpp'),
                                '-o',str(binary)],check=True)
                outputs.append([subprocess.check_output([str(binary),str(v)]) for v in range(1,6)])
            latest=path/'new'
            rows6=subprocess.check_output([str(latest),'6'],text=True)
            rows7=subprocess.check_output([str(latest),'7'],text=True)
            self.assertEqual([line for line in rows6.splitlines() if not line.startswith('#')],
                             [line for line in rows7.splitlines() if not line.startswith('#')])
            self.assertEqual([r['kind'] for r in parse(rows6)], [r['kind'] for r in parse(rows7)])
            for version,(old,new) in enumerate(zip(*outputs),1):
                with self.subTest(version=version):self.assertEqual(old,new)

if __name__ == '__main__': unittest.main()

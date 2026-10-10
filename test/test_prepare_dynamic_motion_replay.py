"""Replay preparation tests using the independent production MotionV4 fixture."""
import copy
import csv
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from telemetry_parser import COMMON, IMU, MOTION, parse
import test_motion_disk as fixture

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('prepare_replay', ROOT/'scripts/prepare_dynamic_motion_replay.py')
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)


class PrepareReplayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixture.MotionDiskTest.setUpClass()
        cls.sample = next(row for row in parse(fixture.MotionDiskTest.data)
                          if row['kind'] == 'imu' and row['motion_static_tilt_valid'] == '1')

    @classmethod
    def tearDownClass(cls):
        fixture.MotionDiskTest.tearDownClass()

    def row(self, frame=1, **changes):
        row = copy.deepcopy(self.sample)
        row.update(frame_sequence=str(frame), batch_sequence=str(frame), byte_position='0',
                   motion_reference_batch=str(frame), motion_reference_declaration=str(frame),
                   accel_odr_millihz='200000', gyro_odr_millihz='200000',
                   dropped='0', rejected='0', lost='0')
        row.update({key: str(value) for key, value in changes.items()})
        return row

    def data(self, rows):
        result = io.StringIO()
        result.write('#ridesync_telemetry,4\n#camera_layout,3,see_docs/log_format.md\n'
                     '#imu_layout,4,see_docs/motion_logging.md\n')
        writer = csv.writer(result, lineterminator='\n')
        for row in rows:
            writer.writerow([row[field] for field in ['kind']+COMMON+IMU+MOTION])
        return result.getvalue()

    def prepare(self, rows, anchor=1):
        return replay.prepare(self.data(rows), anchor, 'nominal-odr')

    def event(self, code, batch=1, position=13, kind='control', **changes):
        row = self.row(0, kind=kind, batch_sequence=batch, byte_position=position,
                       event_code=code)
        for field in ['accel_x','accel_y','accel_z','gyro_x','gyro_y','gyro_z']+MOTION[28:36]:
            row[field] = ''
        for field in MOTION[19:28]:
            row[field] = '0'
        row.update({key: str(value) for key, value in changes.items()})
        return row

    def test_real_formatter_vectors_and_one_explicit_anchor(self):
        rows = self.prepare([self.row(), self.row(2), self.row(3)], anchor=2)
        self.assertEqual([0, 5000], [row['sample_time_us'] for row in rows])
        self.assertEqual([1, 0], [row['stationary'] for row in rows])
        self.assertEqual([2, 0], [row['declaration'] for row in rows])
        self.assertEqual(self.sample['motion_force_z_mps2'], rows[0]['force_z_mps2'])
        self.assertEqual(['modelled']*2, [row['timing_source'] for row in rows])

    def test_normal_trailer_and_end_do_not_fragment_chain(self):
        trailer = self.event(5, sensor_time_present=1, sensor_time_ticks24=0,
                             event_length=3, event_bytes='000000')
        rows = self.prepare([self.row(), trailer, self.event(0, position=17), self.row(2)])
        self.assertEqual([0, 0], [row['discontinuity'] for row in rows])

    def test_faults_and_gap_mark_next_sample_without_reanchor(self):
        for event in [self.event(4, event_count=255, event_count_lower_bound=1),
                      self.event(8), self.event(9), self.event(0, kind='health'),
                      self.event(0, kind='config')]:
            with self.subTest(event=event['event_code'], kind=event['kind']):
                rows = self.prepare([self.row(), event, self.row(2)])
                self.assertEqual(1, rows[1]['discontinuity'])
                self.assertEqual(0, rows[1]['stationary'])
        self.assertEqual(1, self.prepare([self.row(), self.row(3)])[1]['discontinuity'])

    def test_config_changes_and_per_kind_loss(self):
        rows = self.prepare([self.row(), self.event(0, dropped=7), self.row(2)])
        self.assertEqual(0, rows[1]['discontinuity'])  # per-kind totals differ normally
        rows = self.prepare([self.row(), self.row(2, dropped=1)])
        self.assertEqual(1, rows[1]['discontinuity'])
        rows = self.prepare([self.row(), self.row(2, accel_filter=9)])
        self.assertEqual(1, rows[1]['discontinuity'])

    def test_epoch_and_timing_faults_are_not_bridged(self):
        for changes in [{'sensor_epoch': 2}, {'timing_flags': 1}, {'timing_flags': 4}]:
            row = self.row(2, motion_static_tilt_valid=0, motion_roll_rad='',
                           motion_pitch_rad='', **changes)
            self.assertEqual(1, self.prepare([self.row(), row])[1]['discontinuity'])
        with self.assertRaises(ValueError):
            self.prepare([self.row(), self.row()])  # ambiguous frame selection
        with self.assertRaises(ValueError):
            replay.prepare(self.data([self.row()]), 1, 'receipt-time')

    def test_rational_rounding_does_not_accumulate_error(self):
        rows = [self.row(i, accel_odr_millihz=300000, gyro_odr_millihz=300000)
                for i in range(1, 5)]
        self.assertEqual([0, 3333, 6667, 10000],
                         [row['sample_time_us'] for row in self.prepare(rows)])

    def test_invalid_or_absent_anchor_and_malformed_input_fail(self):
        for data, anchor in [(self.data([self.row()]), 9),
                             (self.data([self.row(motion_static_tilt_valid=0,
                                                  motion_roll_rad='', motion_pitch_rad='')]), 1),
                             (self.data([self.row()])[:-1], 1),
                             (self.data([self.row(accel_odr_millihz=100000)]), 1)]:
            with self.subTest(anchor=anchor):
                with self.assertRaises(ValueError):
                    replay.prepare(data, anchor, 'nominal-odr')

    def test_production_formatter_to_actual_dynamic_core(self):
        source_code = r"""
#include "storage.h"
#include "test/fixtures/motion/evidence.h"
#include <cstdio>
#include <string>
using namespace ridesync;
struct Sink : StorageSink {
 std::string bytes;
 bool mount() override {return true;} bool openExclusive(const char*) override {return true;}
 size_t write(const char *p,size_t n) override {bytes.append(p,n); return n;}
 bool flush() override {return true;} void close() override {}
};
int main() {
 Sink sink; Storage storage(sink,{42,"replay-fixture","synthetic",2,4,StorageFormat::MotionV4});
 for(unsigned i=0;i<3;++i) {
  auto evidence=motion_fixture::sample(42,100+i*5);
  evidence.sensor_epoch=1; evidence.frame_sequence=i==2?4:i+1;
  evidence.batch_sequence=i+1;
  evidence.config.accel_odr_millihz=evidence.config.gyro_odr_millihz=200000;
  MotionEvidence motion; motion.state=MotionAdmission::Enabled;
  motion.config=motion_fixture::config(); motion.snapshot_max_age_ms=100;
  motion.reference.externally_stationary=true; motion.reference.session_id=42;
  motion.reference.config_generation=3; motion.reference.batch_sequence=i+1;
  motion.reference.declaration=i+1;
  motion.estimate.measurements_valid=motion.estimate.static_tilt_valid=true;
  motion.estimate.specific_force_mps2.z=9.80665f;
  RecordTimestamp time; time.session_id=42; time.monotonic_quality=MonotonicQuality::Valid;
  if(!storage.enqueueImu(time,evidence,false,motion)) return 1;
 }
 storage.requestStop(); for(unsigned i=0;i<1000 && !storage.health().stopped;++i)storage.workerStep();
 std::fwrite(sink.bytes.data(),1,sink.bytes.size(),stdout);
}
"""
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            cpp, executable = directory/'fixture.cpp', directory/'fixture'
            cpp.write_text(source_code)
            subprocess.run(['c++', '-std=c++11', '-I'+str(ROOT/'include'), '-I'+str(ROOT),
                            str(cpp), str(ROOT/'src/storage.cpp'), str(ROOT/'src/session_clock.cpp'),
                            str(ROOT/'src/motion_estimator.cpp'),
                            '-o', str(executable)], check=True)
            original = subprocess.check_output([str(executable)], text=True)
            self.assertTrue(all(row['sensor_epoch'] == '1' for row in parse(original)))
            source, normalized, output = directory/'motion.csv', directory/'input.csv', directory/'result.csv'
            source.write_text(original)
            subprocess.run(['python3', str(ROOT/'scripts/prepare_dynamic_motion_replay.py'),
                            '--input', str(source), '--output', str(normalized),
                            '--anchor-frame', '1', '--timing-model', 'nominal-odr'], check=True)
            subprocess.run(['python3', str(ROOT/'scripts/replay_dynamic_motion.py'),
                            '--input', str(normalized), '--output', str(output), '--experimental'],
                           check=True, capture_output=True)
            rows = list(csv.DictReader(io.StringIO(output.read_text())))
            self.assertEqual(['unreliable', 'unreliable', 'invalid'],
                             [row['quality'] for row in rows])
            self.assertEqual(['1', '1', '0'], [row['numeric_available'] for row in rows])
            self.assertEqual('', rows[2]['q_w'])
            self.assertEqual('', rows[2]['linear_z_mps2'])
            for row in rows:
                self.assertEqual('0', row['dynamic_lean_valid'])
                self.assertEqual('0', row['dynamic_acceleration_valid'])

    def test_cli_refuses_overwrite_and_creates_nothing_on_bad_input(self):
        with tempfile.TemporaryDirectory() as directory:
            source, target = Path(directory)/'in.csv', Path(directory)/'out.csv'
            source.write_text(self.data([self.row()]))
            command = [str(ROOT/'scripts/prepare_dynamic_motion_replay.py'), '--input', str(source),
                       '--output', str(target), '--anchor-frame', '1', '--timing-model', 'nominal-odr']
            subprocess.run(['python3']+command, check=True)
            prior = target.read_bytes()
            self.assertNotEqual(0, subprocess.run(['python3']+command, capture_output=True).returncode)
            self.assertEqual(prior, target.read_bytes())
            target.unlink()
            source.write_text('malformed\n')
            self.assertNotEqual(0, subprocess.run(['python3']+command, capture_output=True).returncode)
            self.assertFalse(target.exists())

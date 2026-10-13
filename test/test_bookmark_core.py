"""Synthetic owner admission and V6 serialization checks; no device or SD claim."""
import csv
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

SOURCE = r'''
#include "telemetry_admission.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace ridesync;
struct Raw : Clock { uint32_t ms = 0; uint32_t now() const override { return ms; } };
struct Sink : StorageSink {
  std::string bytes;
  bool mount() override { return true; }
  bool openExclusive(const char *) override { return true; }
  size_t write(const char *p, size_t n) override { bytes.append(p, n); return n; }
  bool flush() override { return true; }
  void close() override {}
};
struct FailedSink : Sink { bool mount() override { return false; } };
int main() {
  Raw raw; Sink sink;
  Storage storage(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::BookmarkV6});
  SessionClock clock(raw, 42, 20); ImuInbox imu;
  TelemetryAdmission owner(clock, storage, imu);
  auto first = owner.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
  assert(first.outcome == BookmarkOutcome::Admitted && first.bookmark_id == 1);
  assert(first.timestamp.session_id == 42 && first.timestamp.monotonic_ms == 0);
  auto second = owner.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
  assert(second.outcome == BookmarkOutcome::Admitted && second.bookmark_id == 2);
  assert(second.timestamp.monotonic_ms == first.timestamp.monotonic_ms);
  assert(clock.anchor({2026, 1, 1, 0, 0, 0, 0}));
  raw.ms = 10;
  auto third = owner.bookmark(BookmarkSource::Physical, BookmarkGesture::Double);
  assert(third.outcome == BookmarkOutcome::Admitted && third.bookmark_id == 3);
  assert(third.timestamp.anchor.sequence == 1 && third.timestamp.has_utc_estimate);
  assert(clock.anchor({2026, 1, 2, 0, 0, 0, 0}));
  raw.ms = 31;
  auto expired = owner.bookmark(BookmarkSource::Physical, BookmarkGesture::Short);
  assert(expired.outcome == BookmarkOutcome::Admitted && expired.bookmark_id == 4);
  assert(expired.timestamp.anchor_quality == AnchorQuality::Expired);
  assert(!expired.timestamp.has_utc_estimate);
  auto invalid = owner.bookmark(BookmarkSource::Serial, BookmarkGesture::Double);
  assert(invalid.outcome == BookmarkOutcome::InvalidInput && invalid.bookmark_id == 0);
  auto invalid_source = owner.bookmark(static_cast<BookmarkSource>(3), BookmarkGesture::Serial);
  assert(invalid_source.outcome == BookmarkOutcome::InvalidInput && !invalid_source.bookmark_id);
  auto wrong = owner.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial, 77);
  assert(wrong.outcome == BookmarkOutcome::Inactive && wrong.bookmark_id == 0);
  auto fifth = owner.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
  assert(fifth.bookmark_id == 5);
  auto sixth = owner.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
  assert(sixth.bookmark_id == 6 && sixth.outcome == BookmarkOutcome::Admitted);
  auto full = owner.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
  assert(full.outcome == BookmarkOutcome::Overflow && full.bookmark_id == 7);
  assert(storage.kindHealth(RecordKind::Bookmark).accepted == 6);
  assert(storage.kindHealth(RecordKind::Bookmark).dropped == 1);
  for (int i = 0; i < 1000; ++i) storage.workerStep();
  auto after = owner.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
  assert(after.outcome == BookmarkOutcome::Admitted && after.bookmark_id == 8);
  ImuEvidence max_imu; max_imu.session_id = 42; max_imu.kind = RecordKind::ImuSample;
  max_imu.batch_sequence = max_imu.frame_sequence = max_imu.byte_position = UINT32_MAX;
  max_imu.sensor_epoch = max_imu.receipt_millis32 = max_imu.drain_start_millis32 = UINT32_MAX;
  max_imu.drain_end_millis32 = UINT32_MAX;
  max_imu.receipt_known = max_imu.drain_known = true;
  auto &v = max_imu.config;
  v.generation = v.sensor_id = v.mount_id = v.calibration_id = UINT32_MAX;
  v.sensor_state = v.mount_state = v.calibration_state = Qualification::Qualified;
  v.accel_range_mg = v.gyro_range_mdps = UINT32_MAX;
  v.accel_scale_numerator = v.accel_scale_denominator = UINT32_MAX;
  v.gyro_scale_numerator = v.gyro_scale_denominator = UINT32_MAX;
  v.accel_odr_millihz = v.gyro_odr_millihz = UINT32_MAX;
  v.accel_filter = v.gyro_filter = v.calibration_method = UINT32_MAX;
  v.calibration_time_known = v.calibration_temperature_known = true;
  v.calibration_utc_ms = INT64_MIN; v.calibration_temperature_millic = INT32_MIN;
  v.calibration_offsets_known = v.calibration_gains_known = true;
  for (unsigned i = 0; i < 3; ++i) {
    max_imu.accel[i] = INT16_MIN; max_imu.gyro[i] = INT16_MAX;
    v.accel_offset[i] = INT16_MIN; v.gyro_offset[i] = INT16_MAX;
    v.accel_gain_numerator[i] = v.accel_gain_denominator[i] = UINT32_MAX;
    v.gyro_gain_numerator[i] = v.gyro_gain_denominator[i] = UINT32_MAX;
  }
  assert(storage.enqueueImu(clock.snapshot(), max_imu));
  owner.requestStop();
  auto stopping = owner.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
  assert(stopping.outcome == BookmarkOutcome::StorageUnavailable && stopping.bookmark_id == 9);
  imu.finish(); owner.tick();
  for (int i = 0; i < 1000; ++i) storage.workerStep();
  auto h = storage.kindHealth(RecordKind::Bookmark);
  assert(h.accepted == 7 && h.dropped == 2 && h.flushed == 7);
  {
    Raw old_raw; Sink old_sink;
    Storage old(old_sink, {43, "fw", "synthetic", 2, 4, StorageFormat::MotionV5});
    SessionClock old_clock(old_raw, 43, 20); ImuInbox old_imu;
    TelemetryAdmission old_owner(old_clock, old, old_imu);
    auto disabled = old_owner.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
    assert(disabled.outcome == BookmarkOutcome::DisabledFormat && !disabled.bookmark_id);
    assert(old.kindHealth(RecordKind::Bookmark).rejected == 1);
  }
  {
    Raw wrap_raw; wrap_raw.ms = UINT32_MAX - 3; Sink wrap_sink;
    Storage wrap(wrap_sink, {44, "fw", "synthetic", 2, 4, StorageFormat::BookmarkV6});
    SessionClock wrap_clock(wrap_raw, 44, 20); ImuInbox wrap_imu;
    TelemetryAdmission wrap_owner(wrap_clock, wrap, wrap_imu);
    wrap_raw.ms = 5;
    auto wrapped = wrap_owner.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
    assert(wrapped.outcome == BookmarkOutcome::Admitted && wrapped.timestamp.monotonic_ms == 9);
    BookmarkIdSequence sequence(UINT32_MAX);
    assert(sequence.take() == UINT32_MAX && sequence.take() == 0);
  }
  {
    Raw duration_raw; Sink duration_sink;
    Storage duration(duration_sink, {45, "fw", "synthetic", 2, 4, StorageFormat::BookmarkV6});
    SessionClock duration_clock(duration_raw, 45, 20); ImuInbox duration_imu;
    TelemetryAdmission duration_owner(duration_clock, duration, duration_imu);
    for (int i = 0; i < 17; ++i) {
      duration_raw.ms += 0x70000000u;
      duration_clock.snapshot();
    }
    auto invalid_time = duration_owner.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
    assert(invalid_time.outcome == BookmarkOutcome::InvalidTime && invalid_time.bookmark_id == 1);
  }
  {
    Raw reset_raw; Sink reset_sink;
    Storage reset_storage(reset_sink, {47, "fw", "synthetic", 2, 4, StorageFormat::BookmarkV6});
    SessionClock reset_clock(reset_raw, 47, 20); ImuInbox reset_imu;
    TelemetryAdmission reset_owner(reset_clock, reset_storage, reset_imu);
    assert(reset_clock.reset(48));
    auto wrong_session = reset_owner.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
    assert(wrong_session.outcome == BookmarkOutcome::Inactive && !wrong_session.bookmark_id);
    assert(reset_storage.kindHealth(RecordKind::Bookmark).accepted == 0);
  }
  {
    Raw dead_raw; FailedSink dead_sink;
    Storage dead_storage(dead_sink, {48, "fw", "synthetic", 2, 4, StorageFormat::BookmarkV6});
    SessionClock dead_clock(dead_raw, 48, 20); ImuInbox dead_imu;
    TelemetryAdmission dead_owner(dead_clock, dead_storage, dead_imu);
    dead_storage.workerStep(); dead_storage.workerStep();
    assert(dead_storage.health().terminal);
    auto unavailable = dead_owner.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
    assert(unavailable.outcome == BookmarkOutcome::StorageUnavailable && unavailable.bookmark_id == 1);
    assert(dead_storage.kindHealth(RecordKind::Bookmark).dropped == 1);
  }
  {
    Raw marker_raw; Sink marker_sink;
    Storage marker_storage(marker_sink, {46, "fw", "synthetic", 2, 4, StorageFormat::BookmarkV6});
    SessionClock marker_clock(marker_raw, 46, 20); ImuInbox marker_imu; CameraInbox camera;
    TelemetryAdmission marker_owner(marker_clock, marker_storage, marker_imu, &camera);
    CameraMarkerEvidence evidence;
    evidence.session_id = 46; evidence.bookmark_id = 1; evidence.peer_id = 301;
    evidence.model = CameraModel::HERO12_BLACK; evidence.kind = CameraMarkerKind::Attempt;
    evidence.connection_generation = 2; evidence.operation_generation = 3;
    evidence.delivery_admitted = true; evidence.event_receipt_known = true;
    evidence.event_receipt_raw32 = 50;
    marker_raw.ms = 100;
    assert(camera.publishMarker(evidence));
    assert(marker_owner.tick() == 1);
    evidence.model = CameraModel::Unknown;
    assert(!camera.publishMarker(evidence));
    evidence.model = CameraModel::HERO12_BLACK;
    evidence.event_receipt_known = false; evidence.event_receipt_raw32 = 0;
    evidence.error = CameraError::Busy;
    assert(!marker_storage.enqueueCameraMarker(marker_clock.snapshot(), evidence));
    marker_imu.finish(); camera.finish(); marker_owner.requestStop(); marker_owner.tick();
    for (int i = 0; i < 1000; ++i) marker_storage.workerStep();
    auto marker_health = marker_storage.kindHealth(RecordKind::CameraMarker);
    assert(marker_health.accepted == 1 && marker_health.rejected == 2 &&
           marker_health.flushed == 1);
    auto start = marker_sink.bytes.find("\nmarker,");
    assert(start != std::string::npos);
    auto end = marker_sink.bytes.find('\n', start + 1);
    auto row = marker_sink.bytes.substr(start + 1, end - start - 1);
    std::fprintf(stderr, "marker_csv=%s\n", row.c_str());
  }
  std::fwrite(sink.bytes.data(), 1, sink.bytes.size(), stdout);
  size_t longest = 0, begin = 0;
  for (size_t i = 0; i < sink.bytes.size(); ++i) if (sink.bytes[i] == '\n') {
    if (sink.bytes.compare(begin, 9, "bookmark,") == 0 ||
        sink.bytes.compare(begin, 4, "imu,") == 0) {
      size_t row = i + 1 - begin;
      if (row > longest) longest = row;
    }
    begin = i + 1;
  }
  assert(longest <= Storage::kMaxRowBytes);
  std::fprintf(stderr, "sizeof BookmarkEvidence=%zu TelemetryRecord=%zu Storage=%zu TelemetryAdmission=%zu CameraInbox=%zu longest_row=%zu\n",
               sizeof(BookmarkEvidence), sizeof(TelemetryRecord), sizeof(Storage),
               sizeof(TelemetryAdmission), sizeof(CameraInbox), longest);
}
'''


class BookmarkCoreTest(unittest.TestCase):
    def test_owner_receipts_and_v6_rows(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'fixture.cpp'
            binary = Path(tmp) / 'fixture'
            source.write_text(SOURCE)
            subprocess.run(['c++', '-std=c++11', '-Iinclude', str(source),
                            'src/session_clock.cpp', 'src/storage.cpp',
                            'src/telemetry_admission.cpp', 'src/motion_estimator.cpp',
                            'src/dynamic_motion_estimator.cpp', '-o', str(binary)],
                           cwd=ROOT, check=True)
            done = subprocess.run([str(binary)], cwd=ROOT, check=True,
                                  capture_output=True, text=True)
        self.assertIn('#ridesync_telemetry,6\n', done.stdout)
        self.assertIn('#bookmark_layout,1,see_docs/log_format.md\n', done.stdout)
        lines = done.stdout.splitlines()
        header = next(csv.reader([next(line for line in lines if line.startswith('session_id,'))]))
        rows = [next(csv.reader([line])) for line in lines if line.startswith('bookmark,')]
        self.assertEqual(7, len(rows))
        self.assertEqual(['bookmark_id', 'bookmark_source', 'bookmark_gesture'], header[-3:])
        self.assertTrue(all(len(row) == len(header) + 1 for row in rows))
        self.assertEqual(['1', '2', '3', '4', '5', '6', '8'], [row[-3] for row in rows])
        self.assertEqual(['3', '2', '3'], rows[2][-3:])
        self.assertEqual('', rows[0][header.index('utc_estimate_ms') + 1])
        self.assertEqual('1', rows[2][header.index('anchor_sequence') + 1])
        self.assertEqual('1', rows[2][header.index('has_utc_estimate') + 1])
        self.assertEqual('0', rows[3][header.index('has_utc_estimate') + 1])
        self.assertTrue(all(value == '' for row in rows for value in row[13:30]))
        self.assertIn('sizeof BookmarkEvidence=', done.stderr)
        marker = next(csv.reader([next(line[11:] for line in done.stderr.splitlines()
                                      if line.startswith('marker_csv='))]))
        self.assertEqual(32, len(marker))
        self.assertEqual(['marker', '46', '100', '0'], marker[:4])
        self.assertEqual(['1', '0', '301', '4', '2', '3', '1', '0', '1', 'owner_admission',
                          '1', '50', '50'], marker[13:26])


if __name__ == '__main__':
    unittest.main()

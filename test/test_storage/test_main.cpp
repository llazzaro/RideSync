#include "storage.h"
#include <condition_variable>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <unity.h>
#include <vector>
using namespace ridesync;
struct Sink : StorageSink {
  std::string bytes;
  bool mount_ok = true, open_ok = true, flush_ok = true;
  size_t limit = 9999;
  unsigned mounts = 0, opens = 0, writes = 0, flushes = 0, closes = 0;
  bool mount() override {
    ++mounts;
    return mount_ok;
  }
  bool openExclusive(const char *) override {
    ++opens;
    return open_ok;
  }
  size_t write(const char *p, size_t n) override {
    ++writes;
    n = n < limit ? n : limit;
    bytes.append(p, n);
    return n;
  }
  bool flush() override {
    ++flushes;
    return flush_ok;
  }
  void close() override { ++closes; }
};
RecordTimestamp stamp() {
  RecordTimestamp t;
  t.session_id = 42;
  t.monotonic_quality = MonotonicQuality::Valid;
  t.monotonic_ms = 100;
  return t;
}
ModemSnapshot sample() {
  ModemSnapshot s;
  s.session_id = 42;
  return s;
}
void drain(Storage &s) {
  for (unsigned i = 0; i < 100; ++i)
    s.workerStep();
}
std::vector<std::string> fields(const std::string &log) {
  auto start = log.rfind('\n', log.size() - 2) + 1;
  std::string row = log.substr(start, log.size() - start - 1);
  std::vector<std::string> out;
  size_t offset = 0;
  for (;;) {
    auto end = row.find(',', offset);
    out.push_back(row.substr(offset, end - offset));
    if (end == std::string::npos)
      return out;
    offset = end + 1;
  }
}
void missing_fix_and_copied_anchor() {
  Sink sink;
  Storage s(sink, {42, "test-fw", "synthetic", 2});
  auto t = stamp();
  auto m = sample();
  t.anchor_quality = AnchorQuality::Expired;
  t.anchor.sequence = 7;
  t.anchor.utc_ms = 946684800000LL;
  t.anchor.receipt_ms = 20;
  t.anchor_age_ms = 80;
  TEST_ASSERT_TRUE(s.enqueue(t, m));
  t.anchor.sequence = 9;
  drain(s);
  TEST_ASSERT_EQUAL_UINT32(1, s.health().written);
  TEST_ASSERT_EQUAL_UINT32(1, s.health().flushed);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, sink.bytes.find("ridesync_gps,1"));
  auto f = fields(sink.bytes);
  TEST_ASSERT_EQUAL_UINT32(35, f.size());
  TEST_ASSERT_EQUAL_STRING("7", f[4].c_str());
  TEST_ASSERT_EQUAL_STRING("946684800000", f[6].c_str());
  TEST_ASSERT_EQUAL_STRING("", f[11].c_str());
  TEST_ASSERT_EQUAL_STRING("0", f[18].c_str());
  TEST_ASSERT_EQUAL_STRING("", f[19].c_str());
  TEST_ASSERT_EQUAL_STRING("", f[20].c_str());
}
void queue_saturation_and_validation() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic", 2});
  for (unsigned i = 0; i < Storage::kCapacity; ++i)
    TEST_ASSERT_TRUE(s.enqueue(stamp(), sample()));
  TEST_ASSERT_FALSE(s.enqueue(stamp(), sample()));
  TEST_ASSERT_EQUAL_UINT32(1, s.health().dropped);
  auto m = sample();
  m.session_id = 43;
  TEST_ASSERT_FALSE(s.enqueue(stamp(), m));
  m = sample();
  m.fix.valid = true;
  m.validity = FixValidity::Valid;
  m.fix.latitude_degrees = std::numeric_limits<double>::quiet_NaN();
  TEST_ASSERT_FALSE(s.enqueue(stamp(), m));
  TEST_ASSERT_EQUAL_UINT32(2, s.health().rejected);
  TEST_ASSERT_EQUAL_UINT32(0, sink.mounts);
  drain(s);
  TEST_ASSERT_EQUAL_UINT32(Storage::kCapacity, s.health().written);
}
void mount_retry_cap_and_collision() {
  Sink sink;
  sink.mount_ok = false;
  Storage s(sink, {42, "fw", "synthetic", 2});
  s.enqueue(stamp(), sample());
  drain(s);
  TEST_ASSERT_EQUAL_UINT32(2, sink.mounts);
  TEST_ASSERT_EQUAL_UINT32(1, s.health().lost);
  TEST_ASSERT_TRUE(s.health().terminal);
  Sink collision;
  collision.open_ok = false;
  Storage c(collision, {42, "fw", "synthetic", 2});
  c.enqueue(stamp(), sample());
  drain(c);
  TEST_ASSERT_EQUAL_UINT32(1, collision.opens);
  TEST_ASSERT_EQUAL_UINT32(0, collision.writes);
}
void partial_and_full_card_never_replay() {
  for (size_t limit : {size_t(0), size_t(3)}) {
    Sink sink;
    sink.limit = limit;
    Storage s(sink, {42, "fw", "synthetic", 2});
    s.enqueue(stamp(), sample());
    drain(s);
    TEST_ASSERT_EQUAL_UINT32(1, sink.writes);
    TEST_ASSERT_TRUE(s.health().terminal);
    TEST_ASSERT_EQUAL_UINT32(1, s.health().lost);
    TEST_ASSERT_FALSE(s.enqueue(stamp(), sample()));
    TEST_ASSERT_EQUAL_UINT32(1, s.health().dropped);
  }
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic", 2});
  s.workerStep();
  s.workerStep();
  s.workerStep();
  sink.limit = 5;
  s.enqueue(stamp(), sample());
  drain(s);
  TEST_ASSERT_EQUAL_UINT32(0, s.health().written);
  TEST_ASSERT_EQUAL_UINT32(1, s.health().lost);
}
void flush_failure_exposes_uncertain_records() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic", 2});
  s.enqueue(stamp(), sample());
  sink.flush_ok = false;
  drain(s);
  TEST_ASSERT_EQUAL_UINT32(1, s.health().written);
  TEST_ASSERT_EQUAL_UINT32(0, s.health().flushed);
  TEST_ASSERT_EQUAL_UINT32(1, s.health().lost);
  TEST_ASSERT_TRUE(s.health().terminal);
}
struct BlockedSink : Sink {
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false, release = false;
  bool mount() override {
    std::unique_lock<std::mutex> l(mutex);
    entered = true;
    cv.notify_one();
    cv.wait(l, [&] { return release; });
    return Sink::mount();
  }
};
void blocked_filesystem_does_not_block_producer_or_control() {
  BlockedSink sink;
  Storage s(sink, {42, "fw", "synthetic", 2});
  std::thread worker([&] { s.workerStep(); });
  {
    std::unique_lock<std::mutex> l(sink.mutex);
    sink.cv.wait(l, [&] { return sink.entered; });
  }
  unsigned control = 0;
  for (unsigned i = 0; i < Storage::kCapacity + 4; ++i) {
    s.enqueue(stamp(), sample());
    ++control;
  }
  TEST_ASSERT_EQUAL_UINT32(Storage::kCapacity + 4, control);
  TEST_ASSERT_EQUAL_UINT32(4, s.health().dropped);
  TEST_ASSERT_EQUAL_UINT32(0, s.health().progress);
  {
    std::lock_guard<std::mutex> l(sink.mutex);
    sink.release = true;
  }
  sink.cv.notify_one();
  worker.join();
  drain(s);
  TEST_ASSERT_EQUAL_UINT32(Storage::kCapacity, s.health().written);
}
void absent_anchor_columns_and_retained_fix_are_explicit() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic", 2});
  auto m = sample();
  m.validity = FixValidity::Stale;
  m.fix.valid = true;
  m.fix.latitude_degrees = 52.25;
  m.fix.longitude_degrees = 4.75;
  m.fix.receipt_monotonic_ms = 50;
  m.age_available = true;
  m.age_ms = 50;
  m.fix.utc_date.available = true;
  m.fix.utc_date.value.year = 2026;
  m.fix.utc_date.value.month = 10;
  m.fix.utc_date.value.day = 7;
  TEST_ASSERT_TRUE(s.enqueue(stamp(), m));
  m.fix.latitude_degrees = 1;
  drain(s);
  auto f = fields(sink.bytes);
  TEST_ASSERT_EQUAL_UINT32(35, f.size());
  TEST_ASSERT_EQUAL_STRING("0", f[7].c_str());
  TEST_ASSERT_EQUAL_STRING("0", f[10].c_str());
  TEST_ASSERT_EQUAL_STRING("4", f[16].c_str());
  TEST_ASSERT_EQUAL_STRING("50", f[17].c_str());
  TEST_ASSERT_EQUAL_STRING("52.25000000", f[19].c_str());
  TEST_ASSERT_EQUAL_STRING("2026-10-07", f[24].c_str());
  TEST_ASSERT_EQUAL_STRING("50", f[26].c_str());
}
void invalid_associations_and_metrics_are_rejected() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic"});
  auto t = stamp();
  auto m = sample();
  t.anchor_quality = AnchorQuality::Fresh;
  t.anchor.sequence = 1;
  t.anchor.utc_ms = 946684800000LL;
  t.anchor.receipt_ms = 50;
  t.anchor_age_ms = 50;
  t.has_utc_estimate = true;
  t.utc_estimate_ms = 1;
  TEST_ASSERT_FALSE(s.enqueue(t, m));
  t = stamp();
  m.fix.speed_metres_per_second.available = true;
  m.fix.speed_metres_per_second.value = -1;
  TEST_ASSERT_FALSE(s.enqueue(t, m));
  m = sample();
  m.fix.utc_date.available = true;
  m.fix.utc_date.value.year = 2026;
  m.fix.utc_date.value.month = 2;
  m.fix.utc_date.value.day = 29;
  TEST_ASSERT_FALSE(s.enqueue(t, m));
  m = sample();
  m.age_available = true;
  m.age_ms = 101;
  TEST_ASSERT_FALSE(s.enqueue(t, m));
  m = sample();
  t.monotonic_quality = MonotonicQuality::DurationExceeded;
  TEST_ASSERT_FALSE(s.enqueue(t, m));
  t = stamp();
  m = sample();
  m.state = static_cast<ModemState>(7);
  TEST_ASSERT_FALSE(s.enqueue(t, m));
  m = sample();
  m.health = static_cast<UartHealth>(7);
  TEST_ASSERT_FALSE(s.enqueue(t, m));
  TEST_ASSERT_EQUAL_UINT32(7, s.health().rejected);
}
void stop_drains_and_flushes_then_closes() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic"});
  s.enqueue(stamp(), sample());
  s.requestStop();
  TEST_ASSERT_FALSE(s.enqueue(stamp(), sample()));
  drain(s);
  TEST_ASSERT_EQUAL_UINT32(1, s.health().written);
  TEST_ASSERT_EQUAL_UINT32(1, s.health().flushed);
  TEST_ASSERT_EQUAL_UINT32(1, sink.closes);
  TEST_ASSERT_TRUE(s.health().stopped);
  TEST_ASSERT_FALSE(s.health().terminal);
}
void flush_threshold_and_late_write_failure_track_cached_loss() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic", 2, 3});
  for (unsigned i = 0; i < 4; ++i)
    s.enqueue(stamp(), sample());
  for (unsigned i = 0; i < 100 && s.health().written < 3; ++i)
    s.workerStep();
  TEST_ASSERT_EQUAL_UINT32(3, s.health().written);
  TEST_ASSERT_EQUAL_UINT32(0, s.health().flushed);
  s.workerStep();
  TEST_ASSERT_EQUAL_UINT32(3, s.health().flushed);
  sink.limit = 0;
  drain(s);
  TEST_ASSERT_TRUE(s.health().terminal);
  TEST_ASSERT_EQUAL_UINT32(1, s.health().lost);
  TEST_ASSERT_EQUAL_UINT32(3, s.health().written);
}
struct BlockedWriteSink : Sink {
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false, release = false;
  size_t write(const char *p, size_t n) override {
    {
      std::unique_lock<std::mutex> l(mutex);
      entered = true;
      cv.notify_one();
      cv.wait(l, [&] { return release; });
    }
    return Sink::write(p, n);
  }
};
void blocked_write_leaves_queue_and_control_available() {
  BlockedWriteSink sink;
  Storage s(sink, {42, "fw", "synthetic"});
  s.workerStep();
  s.workerStep();
  std::thread worker([&] { s.workerStep(); });
  {
    std::unique_lock<std::mutex> l(sink.mutex);
    sink.cv.wait(l, [&] { return sink.entered; });
  }
  const auto before = s.health().progress;
  unsigned control = 0;
  for (unsigned i = 0; i < Storage::kCapacity + 1; ++i) {
    s.enqueue(stamp(), sample());
    ++control;
  }
  TEST_ASSERT_EQUAL_UINT32(9, control);
  TEST_ASSERT_EQUAL_UINT32(before, s.health().progress);
  TEST_ASSERT_EQUAL_UINT32(1, s.health().dropped);
  {
    std::lock_guard<std::mutex> l(sink.mutex);
    sink.release = true;
  }
  sink.cv.notify_one();
  worker.join();
  drain(s);
  TEST_ASSERT_EQUAL_UINT32(8, s.health().flushed);
}
void stale_no_fix_payload_is_logged_without_coordinates() {
  Sink sink;
  Storage s(sink, {42, "fw", "synthetic"});
  auto m = sample();
  m.validity = FixValidity::Stale;
  m.age_available = true;
  m.age_ms = 100;
  TEST_ASSERT_TRUE(s.enqueue(stamp(), m));
  drain(s);
  auto f = fields(sink.bytes);
  TEST_ASSERT_EQUAL_STRING("4", f[16].c_str());
  TEST_ASSERT_EQUAL_STRING("0", f[18].c_str());
  TEST_ASSERT_EQUAL_STRING("", f[19].c_str());
}
void metadata_rejects_csv_injection() {
  Sink sink;
  Storage s(sink, {42, "fw\nmalicious", "synthetic", 2});
  TEST_ASSERT_FALSE(s.enqueue(stamp(), sample()));
  drain(s);
  TEST_ASSERT_EQUAL_UINT32(0, sink.mounts);
  TEST_ASSERT_EQUAL_UINT32(1, s.health().rejected);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(missing_fix_and_copied_anchor);
  RUN_TEST(queue_saturation_and_validation);
  RUN_TEST(mount_retry_cap_and_collision);
  RUN_TEST(partial_and_full_card_never_replay);
  RUN_TEST(flush_failure_exposes_uncertain_records);
  RUN_TEST(blocked_filesystem_does_not_block_producer_or_control);
  RUN_TEST(absent_anchor_columns_and_retained_fix_are_explicit);
  RUN_TEST(invalid_associations_and_metrics_are_rejected);
  RUN_TEST(stop_drains_and_flushes_then_closes);
  RUN_TEST(flush_threshold_and_late_write_failure_track_cached_loss);
  RUN_TEST(blocked_write_leaves_queue_and_control_available);
  RUN_TEST(stale_no_fix_payload_is_logged_without_coordinates);
  RUN_TEST(metadata_rejects_csv_injection);
  return UNITY_END();
}

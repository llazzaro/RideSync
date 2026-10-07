#include "imu_bus.h"
#include "imu_manager.h"
#include <string>
#include <unity.h>
using namespace ridesync;
struct Port : ImuPort {
  bool init_ok = true, read_ok = true, flush_ok = true;
  uint8_t bytes[112]{};
  uint16_t length = 0;
  unsigned reads = 0, flushes = 0;
  bool begin(ImuConfig &c) override {
    c.generation = 1;
    return init_ok;
  }
  bool read(uint8_t *out, uint16_t capacity, uint16_t &n, uint8_t &flags) override {
    ++reads;
    n = length;
    flags = 0;
    if (!read_ok || n > capacity)
      return false;
    for (unsigned i = 0; i < n; ++i)
      out[i] = bytes[i];
    return true;
  }
  bool flush() override {
    ++flushes;
    return flush_ok;
  }
};
struct Capture : ImuEmitter {
  ImuEvidence records[32];
  unsigned n = 0;
  void emit(const ImuEvidence &e) override { records[n++] = e; }
};
void paired_and_controls() {
  // Author-created synthetic bytes, gyro then accel; never a hardware capture.
  uint8_t bytes[] = {0x8c, 1, 0, 0xff, 0xff, 0xff, 0x7f, 0, 8, 1, 0x80, 0,   0,
                     0x44, 0, 0, 0,    0x40, 255,  0x48, 1, 2, 3, 4,    0x80};
  Capture out;
  ImuEvidence base;
  base.session_id = 42;
  ImuFifoCodec codec;
  TEST_ASSERT_FALSE(codec.decode(bytes, sizeof(bytes), base, out));
  TEST_ASSERT_EQUAL(4, out.n);
  TEST_ASSERT_EQUAL(-1, out.records[0].gyro[1]);
  TEST_ASSERT_EQUAL(-32767, out.records[0].accel[1]);
  TEST_ASSERT_EQUAL(8, out.records[0].timing_flags);
  TEST_ASSERT_TRUE(out.records[1].sensor_time_present);
  TEST_ASSERT_EQUAL(0, out.records[1].sensor_time_ticks24);
  TEST_ASSERT_EQUAL(13, out.records[1].byte_position);
  TEST_ASSERT_TRUE(out.records[2].event_count_lower_bound);
}
void malformed_barrier() {
  for (uint8_t header :
       {uint8_t(0x8d), uint8_t(0x88), uint8_t(0x84), uint8_t(0x55), uint8_t(0x8c)}) {
    uint8_t bytes[] = {header, 0, 0, 0, 0, 0, 0};
    Capture out;
    ImuFifoCodec codec;
    ImuEvidence base;
    TEST_ASSERT_FALSE(codec.decode(bytes, sizeof(bytes), base, out));
    TEST_ASSERT_EQUAL(1, out.n);
    TEST_ASSERT_EQUAL(RecordKind::ImuControl, out.records[0].kind);
    TEST_ASSERT_BITS(1, 1, out.records[0].timing_flags);
  }
}
void time_ambiguity() {
  Capture out;
  ImuFifoCodec codec;
  ImuEvidence base;
  uint8_t bytes[] = {0x44, 1, 0, 0};
  TEST_ASSERT_TRUE(codec.decode(bytes, 4, base, out));
  TEST_ASSERT_TRUE(codec.decode(bytes, 4, base, out));
  TEST_ASSERT_BITS(2, 2, out.records[1].timing_flags);
  bytes[1] = 0;
  TEST_ASSERT_TRUE(codec.decode(bytes, 4, base, out));
  TEST_ASSERT_BITS(4, 4, out.records[2].timing_flags);
}
void worker_fault_stop() {
  Port port;
  ImuInbox inbox;
  HealthProgress progress;
  ImuManager worker(port, inbox, progress, 42, {});
  port.init_ok = false;
  worker.step(0);
  TEST_ASSERT_EQUAL(1, progress.generation());
  TEST_ASSERT_EQUAL(0, port.reads);
  worker.step(1);
  TEST_ASSERT_EQUAL(2, progress.generation());
  TEST_ASSERT_EQUAL(0, port.reads);
  worker.stop();
  worker.step(2);
  TEST_ASSERT_TRUE(progress.isFinished());
}
void error_and_recovery() {
  Port port;
  ImuInbox inbox;
  HealthProgress progress;
  ImuManager worker(port, inbox, progress, 42, {});
  worker.step(0);
  port.read_ok = false;
  worker.step(1);
  TEST_ASSERT_EQUAL(0, port.flushes);
  worker.step(2);
  TEST_ASSERT_EQUAL(1, port.flushes);
  port.read_ok = true;
  port.length = 1;
  port.bytes[0] = 0x55;
  worker.step(3);
  worker.step(4);
  TEST_ASSERT_EQUAL(2, port.flushes);
  worker.stop();
  worker.step(5);
  TEST_ASSERT_TRUE(progress.isFinished());
}
struct Bus : ImuBus {
  unsigned received = 0, sent = 0;
  bool select = true;
  bool selectRegister(uint8_t) override { return select; }
  uint32_t receive(uint8_t *p, uint32_t n) override {
    for (unsigned i = 0; i < received && i < n; ++i)
      p[i] = 42;
    return received;
  }
  uint32_t transmit(uint8_t, const uint8_t *, uint32_t) override { return sent; }
};
void strict_bus() {
  Bus bus;
  StrictImuBus strict(bus);
  uint8_t bytes[4] = {1, 2, 3, 4};
  bus.received = 3;
  TEST_ASSERT_FALSE(strict.read(0x26, bytes, 4));
  TEST_ASSERT_EQUAL(1, bytes[0]); // failed read never exposes partial destination
  bus.received = 4;
  TEST_ASSERT_TRUE(strict.read(0x26, bytes, 4));
  TEST_ASSERT_EQUAL(42, bytes[3]);
  TEST_ASSERT_FALSE(strict.read(0x26, bytes, 113));
  bus.sent = 3;
  TEST_ASSERT_FALSE(strict.write(1, bytes, 4));
}
void long_gap_and_accounting() {
  Capture out;
  ImuFifoCodec codec;
  ImuEvidence base;
  base.receipt_known = true;
  uint8_t time[] = {0x44, 2, 0, 0};
  codec.decode(time, 4, base, out);
  base.receipt_millis32 = 655360;
  time[1] = 3;
  codec.decode(time, 4, base, out);
  TEST_ASSERT_BITS(4, 4, out.records[1].timing_flags);
  uint8_t skip[] = {0x40, 255};
  codec.decode(skip, 2, base, out);
  uint8_t partial[] = {0x8c};
  codec.decode(partial, 1, base, out);
  TEST_ASSERT_EQUAL(255, codec.health().skipped_lower_bound);
  TEST_ASSERT_EQUAL(1, codec.health().partial);
}
struct Sink : StorageSink {
  std::string bytes;
  bool mount() override { return true; }
  bool openExclusive(const char *) override { return true; }
  size_t write(const char *p, size_t n) override {
    bytes.append(p, n);
    return n;
  }
  bool flush() override { return true; }
  void close() override {}
};
struct RawClock : Clock {
  uint32_t now() const override { return 100; }
};
void acquisition_overflow_gps_and_shutdown() {
  Port port;
  ImuInbox inbox;
  HealthProgress progress;
  ImuManager worker(port, inbox, progress, 42, {});
  Sink sink;
  Storage storage(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::MixedV2});
  RawClock raw;
  SessionClock clock(raw, 42, 1000);
  TelemetryAdmission admission(clock, storage, inbox);
  worker.step(0); // config
  port.length = 13;
  port.bytes[0] = 0x8c;
  for (unsigned i = 1; i <= 8; ++i)
    worker.step(i);
  TEST_ASSERT_EQUAL(5, inbox.dropped(RecordKind::ImuSample));
  ModemSnapshot gps;
  gps.session_id = 42;
  TEST_ASSERT_TRUE(admission.gps(gps));
  port.read_ok = false;
  worker.step(9);
  port.flush_ok = false;
  worker.step(10);
  for (unsigned i = 0; i < 4; ++i)
    admission.tick();
  for (unsigned i = 0; i < 200; ++i)
    storage.workerStep();
  TEST_ASSERT_EQUAL(1, storage.kindHealth(RecordKind::Gps).flushed);
  TEST_ASSERT_EQUAL(3, storage.kindHealth(RecordKind::ImuSample).flushed);
  TEST_ASSERT_EQUAL(0, storage.kindHealth(RecordKind::ImuSample).rejected);
  TEST_ASSERT_TRUE(admission.gps(gps));
  admission.requestStop();
  worker.step(11);
  for (unsigned i = 0; i < 100; ++i) {
    admission.tick();
    storage.workerStep();
  }
  TEST_ASSERT_TRUE(admission.stopped());
  TEST_ASSERT_TRUE(storage.health().stopped);
  TEST_ASSERT_TRUE(progress.isFinished());
  TEST_ASSERT_EQUAL(2, storage.kindHealth(RecordKind::Gps).flushed);
}
void exhausted_recovery_is_observational() {
  Port port;
  ImuInbox inbox;
  HealthProgress progress;
  ImuManager worker(port, inbox, progress, 42, {});
  worker.step(0);
  port.read_ok = false;
  for (unsigned i = 1; i < 20; ++i)
    worker.step(i);
  TEST_ASSERT_EQUAL(3, port.reads);
  TEST_ASSERT_EQUAL(3, worker.health().read_errors);
  TEST_ASSERT_EQUAL(2, worker.health().recovery_flushes);
  TEST_ASSERT_EQUAL(2, port.flushes);
  TEST_ASSERT_EQUAL(DeviceHealth::RetryExhausted, progress.outcome());
}
void configuration_change_retires_unknown_profile() {
  Port port;
  ImuInbox inbox;
  HealthProgress progress;
  ImuManager worker(port, inbox, progress, 42, {});
  worker.step(0);
  port.length = 5;
  port.bytes[0] = 0x48;
  worker.step(1);
  for (unsigned i = 2; i < 8; ++i)
    worker.step(i);
  TEST_ASSERT_EQUAL(1, port.reads);
  TEST_ASSERT_EQUAL(DeviceHealth::RetryExhausted, progress.outcome());
  worker.stop();
  worker.step(8);
  TEST_ASSERT_TRUE(progress.isFinished());
}
void invalid_buffers_fail_closed() {
  Capture out;
  ImuFifoCodec codec;
  ImuEvidence base;
  uint8_t data[113]{};
  TEST_ASSERT_FALSE(codec.decode(data, 113, base, out));
  TEST_ASSERT_EQUAL(1, out.n);
  TEST_ASSERT_FALSE(codec.decode(nullptr, 1, base, out));
  TEST_ASSERT_EQUAL(2, out.n);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(configuration_change_retires_unknown_profile);
  RUN_TEST(invalid_buffers_fail_closed);
  RUN_TEST(exhausted_recovery_is_observational);
  RUN_TEST(acquisition_overflow_gps_and_shutdown);
  RUN_TEST(long_gap_and_accounting);
  RUN_TEST(strict_bus);
  RUN_TEST(paired_and_controls);
  RUN_TEST(malformed_barrier);
  RUN_TEST(time_ambiguity);
  RUN_TEST(worker_fault_stop);
  RUN_TEST(error_and_recovery);
  return UNITY_END();
}

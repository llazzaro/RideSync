#include "bench.h"
#include "reporter.h"
#include <cassert>
#include <string>
using namespace gnss_bench;
using namespace ridesync;
struct Time : Clock, MicroClock {
  uint32_t ms = 0;
  uint64_t us = 0;
  uint32_t now() const override { return ms; }
  uint64_t microsNow() const override { return us; }
};
struct Port : BenchPort {
  std::string rx, tx;
  unsigned begins = 0, reads = 0, calls = 0;
  bool begin_ok = true;
  bool begin() override {
    ++begins;
    return begin_ok;
  }
  size_t available() override {
    ++calls;
    return rx.size();
  }
  int read() override {
    ++calls;
    if (rx.empty())
      return -1;
    ++reads;
    const auto c = rx.front();
    rx.erase(0, 1);
    return c;
  }
  size_t writable() override {
    ++calls;
    return 128;
  }
  size_t write(const char *p, size_t n) override {
    ++calls;
    tx.append(p, n);
    return n;
  }
};
struct Rig {
  Time time;
  Port port;
  Bench bench{time, time, port};
  void tick(uint32_t ms) {
    time.ms = ms;
    time.us = uint64_t(ms) * 1000;
    bench.loop();
  }
  void feed(const char *p, uint32_t ms) {
    port.rx += p;
    tick(ms);
  }
  void healthy() {
    assert(bench.start());
    tick(0);
    assert(port.tx == "AT\r");
    feed("OK\r\n", 1);
    assert(port.tx == "AT\rAT+CGNSSPWR=1\r");
    assert(!bench.suppress());
    feed("OK\r\n", 2);
    assert(bench.snapshot().gnss_power_enabled);
    assert(!bench.snapshot().receiver_ready);
    feed("+CGNSSPWR: READY!\r\n", 3);
    feed("+CGPSINFO: ,,,,,,,,\r\n", 4);
    assert(!bench.suppress()); // Provisional data is not a completed exchange.
    feed("OK\r\n", 5);
    assert(bench.snapshot().validity == FixValidity::NoFix);
    assert(bench.baseline());
  }
};
static void
test_blocked_console() { // A blocked console cannot queue unbounded summaries or hold control work.
  struct Console : Output {
    size_t room = 0, largest = 0;
    std::string sent;
    size_t writable() override { return room; }
    size_t write(const char *data, size_t n) override {
      assert(n <= room);
      assert(n <= 64);
      largest = std::max(largest, n);
      sent.append(data, n);
      return n;
    }
  } out;
  Reporter reporter;
  const std::string line(600, 's');
  assert(reporter.queue(line.data(), line.size()));
  assert(!reporter.queue("next", 4));
  Rig r;
  for (unsigned n = 0; n < 10; ++n) {
    reporter.service(out);
    r.tick(n);
  }
  assert(out.sent.empty());
  assert(r.bench.heartbeats() == 10);
  out.room = 17;
  reporter.service(out);
  assert(out.sent.size() == 17);
  out.room = 1000;
  while (reporter.pending())
    reporter.service(out);
  assert(out.sent == line);
  assert(out.largest == 64);
  assert(!reporter.queue(line.data(), 769));
}

static void test_idle_cancel() { // Idle is no IO; X before A is terminal and never opens UART.
  Rig r;
  r.tick(0);
  r.tick(1000);
  assert(r.port.calls == 0);
  assert(r.port.begins == 0);
  assert(r.bench.heartbeats() == 2);
  assert(!r.bench.suppress());
  r.bench.cancel();
  assert(!r.bench.start());
  r.tick(2000);
  assert(r.port.calls == 0);
  assert(r.port.begins == 0);
}

static void
test_duplicate_start() { // Duplicate A cannot reopen/replay; cancellation prevents reads/writes.
  Rig r;
  assert(r.bench.start());
  assert(!r.bench.start());
  r.tick(0);
  assert(r.port.begins == 1);
  const auto calls = r.port.calls;
  r.bench.cancel();
  r.port.rx = "OK\r\n";
  r.tick(1000);
  r.tick(120000);
  assert(r.port.calls == calls);
  assert(r.port.tx == "AT\r");
  assert(!r.bench.start());
}

static void
test_finite_cap() { // Cap is checked before service; timeout is never a baseline/success.
  Rig r;
  assert(r.bench.start());
  r.tick(0);
  r.tick(10000);
  assert(r.bench.snapshot().state == ModemState::Desynchronized);
  assert(r.bench.snapshot().health == UartHealth::Timeout);
  r.tick(119999);
  const auto calls = r.port.calls;
  r.tick(120000);
  r.tick(120001);
  assert(r.bench.stopReason() == StopReason::Cap);
  assert(r.port.calls == calls);
  assert(!r.bench.baseline());
  assert(!r.bench.suppressed());
  assert(r.bench.heartbeats() == 5);
}

static void
test_real_no_fix() { // Real healthy NoFix permits a bounded permanent injected receive fault.
  Rig r;
  r.healthy();
  assert(r.bench.suppress());
  assert(!r.bench.suppress());
  r.port.rx = std::string(200, 'x');
  const auto reads = r.port.reads;
  r.tick(6);
  assert(r.port.reads - reads == 64);
  assert(r.bench.discarded() == 64);
  r.tick(7);
  r.tick(8);
  r.tick(9);
  assert(r.bench.discarded() == 200);
  r.tick(1005);
  assert(r.port.tx == "AT\rAT+CGNSSPWR=1\rAT+CGPSINFO\rAT+CGPSINFO\r");
  r.tick(3004);
  assert(r.bench.snapshot().validity == FixValidity::Stale);
  r.tick(11005);
  assert(r.bench.snapshot().state == ModemState::Desynchronized);
  assert(r.bench.snapshot().health == UartHealth::Timeout);
  const auto tx = r.port.tx;
  r.feed("OK\r\n+CGNSSPWR: READY!\r\n", 11006);
  r.tick(20000);
  assert(r.port.tx == tx);
  assert(r.bench.suppressed()); // no resume/barrier/restart
  assert(r.bench.heartbeats() == 15);
  assert(r.bench.maxLoopGapUs() == 8994000);
  r.bench.cancel();
  const auto calls = r.port.calls;
  r.tick(21000);
  assert(r.port.calls == calls); // even discard stops after terminal cancel
}

static void
test_warm_timeout() { // Warm GNSS without READY remains production timeout, never synthetic.
  Rig r;
  assert(r.bench.start());
  r.tick(0);
  r.feed("OK\r\n", 1);
  r.feed("OK\r\n", 2);
  r.tick(15002);
  assert(!r.bench.snapshot().receiver_ready);
  assert(r.bench.snapshot().state == ModemState::Failed);
  assert(!r.bench.baseline());
}

static void
test_bounded_reporting() { // Reporting drops preserve bounded fixed-buffer behavior and no payload.
  Rig r;
  r.healthy();
  char tiny[4], line[768];
  assert(r.bench.report(tiny, sizeof(tiny)) == 0);
  assert(r.bench.summaryDrops() == 1);
  const auto n = r.bench.report(line, sizeof(line));
  assert(n > 0);
  assert(n < sizeof(line));
  assert(std::string(line).find("baseline=1") != std::string::npos);
  assert(std::string(line).find("CGPSINFO") == std::string::npos);
  r.bench.reportDropped();
  assert(r.bench.summaryDrops() == 2);
}

static void test_port_refusal() {
  Rig r;
  r.port.begin_ok = false;
  assert(!r.bench.start());
  r.tick(0);
  assert(r.bench.stopReason() == StopReason::OpenFailed);
  assert(r.port.calls == 0);
  assert(!r.bench.start());
  assert(r.port.begins == 1);
}

int main() {
  test_blocked_console();
  test_idle_cancel();
  test_duplicate_start();
  test_finite_cap();
  test_real_no_fix();
  test_warm_timeout();
  test_bounded_reporting();
  test_port_refusal();
}

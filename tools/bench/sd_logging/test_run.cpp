#include "bench_run.h"
#include "camera_manager.h"
#include "session_identity.h"
#include <cassert>
#include <string>

using namespace ridesync;
struct TestClock : Clock {
  uint32_t raw = 0;
  uint32_t now() const override { return raw; }
};
// Test-only stand-ins: run real Storage IO steps without an SD/card or SDK.
struct MemorySink : StorageSink {
  std::string bytes;
  bool mount() override { return true; }
  bool openExclusive(const char *) override { return true; }
  size_t write(const char *data, size_t size) override {
    bytes.append(data, size);
    return size;
  }
  bool flush() override { return true; }
  void close() override {}
};
struct TestOwner {
  MemorySink memory;
  Storage *storage = nullptr;
  IdentityAllocation result;
  unsigned starts = 0, binds = 0, cancels = 0;
  bool admit = true, bind_admit = true, finished = false, hold_finish = false;
  int error = 0;
  TestOwner() {
    result.status = IdentityStatus::Committed;
    result.id = 0x100000001ULL;
  }
  bool start() {
    ++starts;
    return admit;
  }
  IdentityAllocation allocation() const { return result; }
  StorageSink &sink() { return memory; }
  bool bind(Storage &s) {
    ++binds;
    if (!bind_admit)
      return false;
    storage = &s;
    return true;
  }
  void cancel() {
    ++cancels;
    if (storage)
      storage->requestStop();
  }
  bool workerFinished() const { return finished; }
  int ioError() const { return error; }
  void drain() {
    if (!storage)
      return;
    for (unsigned i = 0; i < 100; ++i)
      storage->workerStep();
    if (storage->health().stopped && !hold_finish)
      finished = true;
  }
};
using Run = sd_bench::Run<TestOwner>;
void tick(Run &run, TestOwner &owner, TestClock &clock, uint32_t now) {
  clock.raw = now;
  owner.drain();
  run.service(now);
  owner.drain();
}
int main() {
  {
    TestOwner owner;
    TestClock clock;
    Run run(owner, clock);
    run.service(60000); // Waiting for W does not allocate or start a deadline.
    assert(owner.starts == 0 && !run.used());
    clock.raw = 60000;
    assert(run.start(clock.raw));
    assert(!run.start(clock.raw));
    run.service(clock.raw);
    assert(owner.starts == 1 && owner.binds == 1 && run.rows() == 0);
    for (uint32_t i = 1; i <= 4; ++i) {
      tick(run, owner, clock, 60000 + i * 1000);
      assert(run.rows() == i && run.lastRowMs() == i * 1000);
    }
    // A stopped Storage alone is NOT the worker lifetime barrier.
    owner.finished = false;
    owner.hold_finish = true;
    run.service(clock.raw);
    assert(!run.done() && !run.failed());
    owner.hold_finish = false;
    owner.drain();
    run.service(clock.raw);
    assert(run.done() && owner.finished && !run.start(clock.raw));
    const auto h = run.health();
    assert(h.accepted == 4 && h.written == 4 && h.flushed == 4);
    assert(h.stopped && !h.terminal && !h.dropped && !h.rejected && !h.lost);
    const auto &csv = owner.memory.bytes;
    assert(csv.find("provenance,bench_missing_gnss") != std::string::npos);
    assert(csv.find("4294967297,1000,0,0,") != std::string::npos);
    assert(csv.find("4294967297,4000,0,0,") != std::string::npos);
    assert(owner.binds == 1 && owner.starts == 1);
  }
  {
    TestOwner owner;
    owner.admit = false;
    TestClock clock;
    Run run(owner, clock);
    assert(!run.start(0));
    run.service(50000);
    assert(run.failed() && !run.done() && !run.workerStarted());
    assert(!run.start(50000) && owner.starts == 1 && owner.binds == 0);
  }
  {
    TestOwner owner;
    owner.result.status = IdentityStatus::LedgerCorrupt;
    TestClock clock;
    Run run(owner, clock);
    assert(run.start(0));
    run.service(0);
    assert(run.failed() && owner.binds == 0 && owner.cancels == 1);
  }
  {
    TestOwner owner;
    owner.result.status = IdentityStatus::Pending;
    TestClock clock;
    Run run(owner, clock);
    assert(run.start(0));
    run.service(14999);
    assert(!run.failed());
    run.service(15000);
    assert(run.failed() && owner.cancels == 1 && owner.binds == 0);
    // Late allocation cannot admit rows after cancellation.
    owner.result.status = IdentityStatus::Committed;
    run.service(16000);
    assert(owner.binds == 0 && run.rows() == 0 && !run.done());
  }
  {
    TestOwner owner;
    TestClock clock;
    Run run(owner, clock);
    assert(run.start(0));
    run.service(0); // Bound worker stalls before the first completed row.
    clock.raw = 15000;
    run.service(clock.raw);
    assert(run.failed() && owner.cancels == 1 && run.rows() == 0);
  }
  {
    TestOwner owner;
    owner.hold_finish = true;
    TestClock clock;
    Run run(owner, clock);
    assert(run.start(0));
    run.service(0);
    for (uint32_t i = 1; i <= 4; ++i)
      tick(run, owner, clock, i * 1000);
    clock.raw = 29999;
    run.service(clock.raw);
    assert(!run.failed() && !run.done());
    clock.raw = 30000;
    run.service(clock.raw);
    assert(run.failed() && !run.done() && owner.cancels == 1);
    owner.hold_finish = false;
    owner.drain();
    run.service(30001);
    assert(owner.finished && run.failed() && !run.done());
  }
  {
    TestOwner owner;
    TestClock clock;
    Run run(owner, clock);
    assert(run.start(0));
    run.service(0);
    owner.error = 5;
    tick(run, owner, clock, 1000);
    assert(run.failed() && run.rows() == 0 && owner.cancels == 1);
  }
  {
    TestOwner owner;
    owner.bind_admit = false;
    TestClock clock;
    Run run(owner, clock);
    assert(run.start(0));
    run.service(0);
    assert(run.failed() && owner.binds == 1 && owner.cancels == 1 && !run.rows());
  }
  {
    // A close error must fail even when all four rows were already flushed.
    TestOwner owner;
    TestClock clock;
    Run run(owner, clock);
    assert(run.start(0));
    run.service(0);
    for (uint32_t i = 1; i <= 4; ++i)
      tick(run, owner, clock, i * 1000);
    assert(run.health().flushed == 4 && owner.finished);
    owner.error = 5;
    run.service(clock.raw);
    assert(run.failed() && !run.done() && owner.cancels == 1);
  }
  {
    // No synthetic catch-up timestamps after a late control pass; only one row.
    TestOwner owner;
    TestClock clock;
    Run run(owner, clock);
    assert(run.start(0));
    run.service(0);
    tick(run, owner, clock, 4000);
    assert(run.rows() == 1 && run.lastRowMs() == 4000);
    tick(run, owner, clock, 4001);
    assert(run.rows() == 1);
  }
}

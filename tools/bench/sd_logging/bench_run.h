#pragma once
#include "session_clock.h"
#include "session_identity.h"
#include "storage.h"
#include <new>

namespace sd_bench {
// Single control context only. Owner, Clock, and this object have boot lifetime.
// After start, this object is never destroyed, including on cancellation or a
// blocked SDK call. Only the owner worker touches the SD/filesystem.
template <typename Owner> class Run {
public:
  static constexpr uint32_t kRows = 4, kRowPeriodMs = 1000;
  static constexpr uint32_t kStartupMs = 15000, kOverallMs = 30000;
  Run(Owner &owner, ridesync::Clock &clock) : owner_(owner), clock_(clock) {}
  bool start(uint32_t now) {
    if (used_)
      return false;
    used_ = true;
    began_ = now;
    if (!owner_.start()) {
      failed_ = true;
      reason_ = "worker_refused";
      return false; // No worker was created; never wait on its completion flag.
    }
    started_ = true;
    return true;
  }
  void service(uint32_t now) {
    if (!started_ || failed_ || done_)
      return;
    const auto h = health();
    if (owner_.ioError() || h.terminal || h.dropped || h.rejected || h.lost) {
      fail("storage_error");
      return;
    }
    if (uint32_t(now - began_) >= kOverallMs) {
      fail("overall_timeout");
      return;
    }
    // Allocation AND the first actual row write must return within startup.
    if (!h.written && uint32_t(now - began_) >= kStartupMs) {
      fail("startup_timeout");
      return;
    }
    if (!storage_) {
      const auto a = owner_.allocation();
      if (a.status == ridesync::IdentityStatus::Pending)
        return;
      if (a.status != ridesync::IdentityStatus::Committed || !a.id) {
        fail("identity_refused");
        return;
      }
      session_clock_ = new (clock_memory_) ridesync::SessionClock(clock_, a.id, 1000);
      storage_ = new (storage_memory_)
          ridesync::Storage(owner_.sink(), {a.id, "sd_bench_v1", "bench_missing_gnss", 1, 4});
      if (!storage_->configValid() || !owner_.bind(*storage_)) {
        fail("bind_refused");
        return;
      }
      next_row_ = now;
    }
    if (owner_.workerFinished()) {
      // Recopy AFTER the acquire lifetime barrier, including the final close
      // error: a pre-barrier snapshot can miss writes/flush/close publication.
      const auto final = health();
      if (rows_ == kRows && stopping_ && final.stopped && !final.terminal &&
          final.accepted == kRows && final.written == kRows && final.flushed == kRows &&
          !final.dropped && !final.rejected && !final.lost && !owner_.ioError())
        done_ = true;
      else
        fail("final_verification_failed");
      return;
    }
    if (h.stopped) {
      // Storage may have stopped while its worker is still finishing cleanup.
      if (!stopping_)
        fail("unexpected_stop");
      return;
    }
    if (!stopping_ && uint32_t(now - next_row_) >= kRowPeriodMs) {
      const auto timestamp = session_clock_->snapshot();
      ridesync::ModemSnapshot sample;
      sample.session_id = timestamp.session_id;
      // Default Missing validity, Disabled modem/UART, unavailable UTC/position.
      if (!storage_->enqueue(timestamp, sample)) {
        fail("enqueue_refused");
        return;
      }
      ++rows_;
      last_row_ms_ = timestamp.monotonic_ms;
      next_row_ = now; // No backdating or bursts to synthesize missed periods.
      if (rows_ == kRows) {
        stopping_ = true;
        storage_->requestStop();
      }
    }
  }
  ridesync::StorageHealth health() const {
    return storage_ ? storage_->health() : ridesync::StorageHealth{};
  }
  bool used() const { return used_; }
  bool workerStarted() const { return started_; }
  bool done() const { return done_; }
  bool failed() const { return failed_; }
  bool stopping() const { return stopping_; }
  uint32_t rows() const { return rows_; }
  uint64_t lastRowMs() const { return last_row_ms_; }
  const char *reason() const { return reason_; }

private:
  void fail(const char *reason) {
    failed_ = true; // Admission stops before cancellation is published.
    reason_ = reason;
    if (storage_)
      storage_->requestStop();
    owner_.cancel();
  }
  Owner &owner_;
  ridesync::Clock &clock_;
  alignas(ridesync::SessionClock) unsigned char clock_memory_[sizeof(ridesync::SessionClock)];
  alignas(ridesync::Storage) unsigned char storage_memory_[sizeof(ridesync::Storage)];
  ridesync::SessionClock *session_clock_ = nullptr;
  ridesync::Storage *storage_ = nullptr;
  uint32_t began_ = 0, next_row_ = 0, rows_ = 0;
  uint64_t last_row_ms_ = 0;
  bool used_ = false, started_ = false, stopping_ = false, failed_ = false, done_ = false;
  const char *reason_ = "none";
};
} // namespace sd_bench

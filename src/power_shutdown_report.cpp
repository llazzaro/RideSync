#include "power_shutdown_report.h"
#include <cstdio>
namespace ridesync {
void PowerReportBuffer::capture(const PowerReportSnapshot &s) {
  const auto &p = s.policy;
  const auto &b = s.backend;
  const auto &h = s.storage;
  const int count = std::snprintf(
      bytes_, sizeof(bytes_),
      "\nBENCH power available=%u generation=%u state=%u cause=%u outcome=%u elapsed_ms=%u "
      "ready=%u reserve_validity=%u supply=%u qualified=%u active=%u sealed=%u complete=%u "
      "io_error=%u terminal_loss=%u storage_error=%d "
      "reset_reason=%d startup_refused=%u telemetry_fault=%u accepted=%u written=%u flushed=%u "
      "dropped=%u rejected=%u lost=%u uncertain=%u backend_ready=%u backend_refused=%u "
      "init_error=%d read_error=%d off_attempted=%u off_succeeded=%u off_error=%d\n",
      unsigned(s.available), s.generation, unsigned(p.state), unsigned(p.cause),
      unsigned(p.outcome), p.elapsed_ms, unsigned(p.protected_ready),
      unsigned(s.observation.reserve), unsigned(s.observation.supply), unsigned(p.qualified),
      unsigned(p.automatic_active), unsigned(p.sealed), unsigned(p.teardown_complete),
      unsigned(p.io_error), unsigned(p.terminal_loss), int(s.storage_error), int(s.reset_reason),
      unsigned(s.startup_refused), s.telemetry_fault, h.accepted, h.written, h.flushed, h.dropped,
      h.rejected, h.lost, s.uncertain_rows, unsigned(b.ready), unsigned(b.refused),
      int(b.initialization_error), int(b.read_error), unsigned(b.off_attempted),
      unsigned(b.off_succeeded), int(b.off_error));
  cursor_ = 0;
  size_ = count <= 0 ? 0 : static_cast<size_t>(count);
  if (size_ >= sizeof(bytes_))
    size_ = sizeof(bytes_) - 1;
}
void PowerReportBuffer::consume(size_t bytes) { cursor_ += bytes < pending() ? bytes : pending(); }
} // namespace ridesync

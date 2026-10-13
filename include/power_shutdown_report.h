#pragma once
#include "power_shutdown_esp32.h"
#include "storage.h"
#include <cstddef>
namespace ridesync {
struct PowerReportSnapshot {
  PowerShutdownStatus policy;
  PowerObservation observation;
  PowerBackendStatus backend;
  StorageHealth storage{};
  uint32_t uncertain_rows = 0, generation = 0;
  int32_t reset_reason = 0, storage_error = 0;
  unsigned telemetry_fault = 0;
  bool available = false, startup_refused = false;
};
// Latest-only best-effort serial evidence. No queue/allocation/IO; replacing a
// pending snapshot may lose its partial report. This must never delay hardware OFF.
class PowerReportBuffer {
public:
  static constexpr size_t capacity = 768;
  void capture(const PowerReportSnapshot &);
  const char *data() const { return bytes_ + cursor_; }
  size_t pending() const { return size_ - cursor_; }
  void consume(size_t bytes);

private:
  char bytes_[capacity]{};
  size_t size_ = 0, cursor_ = 0;
};
} // namespace ridesync

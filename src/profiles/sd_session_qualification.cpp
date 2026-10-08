// Retained compile-only witness. No application calls this entrypoint. It links
// the actual private SD task, committed-ID clock/Storage construction and bind.
// It deliberately supplies no board, namespace, camera or telemetry activation.
#if defined(ARDUINO_ARCH_ESP32) && defined(RIDESYNC_SD_HANDOFF_COMPILE)
#include "camera_manager.h"
#include "session_clock.h"
#include "storage_sd.h"
extern "C" ridesync::IdentityAllocation
ridesync_sd_session_qualification(SPIClass &spi, const ridesync::QualifiedSdConfig &config,
                                  ridesync::Clock &raw_clock, bool stop) {
  static ridesync::ArduinoSdStorage owner(spi, config);
  static const bool started = owner.start();
  (void)started;
  if (stop)
    owner.cancel();
  const auto allocation = owner.allocation();
  if (!stop && allocation.status == ridesync::IdentityStatus::Committed &&
      !owner.workerFinished()) {
    static ridesync::SessionClock clock(raw_clock, allocation.id, 10000);
    static ridesync::Storage storage(owner.sink(), {allocation.id, "qualification", "compile"});
    static const bool bound = owner.bind(storage);
    (void)bound;
    (void)clock.snapshot();
  }
  return allocation;
}
#endif

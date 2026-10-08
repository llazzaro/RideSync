// Isolated bench entry point. Never linked into the production application.
#include "bench_run.h"
#include "storage_sd.h"
#include <Arduino.h>
#include <SPI.h>
#include <cstdarg>
#include <cstdio>
#include <esp_err.h>

#if !defined(RIDESYNC_BENCH_NAMESPACE) || RIDESYNC_BENCH_NAMESPACE == 0 ||                         \
    RIDESYNC_BENCH_NAMESPACE > 0xffffffffUL
#error "A reserved nonzero uint32 RIDESYNC_BENCH_NAMESPACE is required"
#endif
#if !defined(RIDESYNC_BENCH_WRITE_OPT_IN) || RIDESYNC_BENCH_WRITE_OPT_IN != 1
#error "Explicit bench-only RIDESYNC_BENCH_WRITE_OPT_IN=1 is required"
#endif

// Pinned Arduino initArduino() otherwise initializes NVS before setup and can
// erase it after certain errors. This isolated sketch has NO NVS consumers:
// skip that call entirely, including the core's error-triggered formatting path.
// This wrapper is only linked by this bench project's explicit linker flags.
extern "C" esp_err_t __wrap_nvs_flash_init(void) { return ESP_OK; }

namespace {
constexpr int kPowerEnable = 12, kSck = 14, kMiso = 2, kMosi = 15, kCs = 13;
class ArduinoClock : public ridesync::Clock {
public:
  uint32_t now() const override { return millis(); }
} raw_clock;
ridesync::QualifiedSdConfig benchConfig() {
  ridesync::QualifiedSdConfig c;
  // Controlled bench acknowledgments ONLY. These do not commission or qualify
  // the production profile, electrical routing, or physical durability.
  c.opt_in = true;
  c.wiring_card_qualified = true;
  c.exclusive_volume = true;
  c.namespace_commissioned = true;
  c.commissioned_namespace = RIDESYNC_BENCH_NAMESPACE;
  c.chip_select = kCs;
  c.frequency_hz = 1000000;
  return c;
}
// Static lifetime, including after timeout: never destroy a blocked worker's
// owner, sink, clock, queue or Storage. No SD calls occur during construction.
ridesync::ArduinoSdStorage owner(SPI, benchConfig());
sd_bench::Run<ridesync::ArduinoSdStorage> run(owner, raw_clock);
uint32_t last_loop = 0, last_report = 0, max_gap = 0, reported_rows = 0;
bool reported_allocation = false, reported_final = false, reported_cleanup = false;

// Bounded, whole-message nonblocking serial admission. Lack of TX room drops a
// periodic health report; final events are retried. No filesystem/SDK SD IO here.
bool emit(const char *format, ...) {
  char line[320];
  va_list args;
  va_start(args, format);
  const int n = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (n <= 0 || size_t(n) >= sizeof(line) || Serial.availableForWrite() < n)
    return false;
  return Serial.write(reinterpret_cast<const uint8_t *>(line), size_t(n)) == size_t(n);
}
const char *state() {
  return run.failed()          ? "FAILED"
         : run.done()          ? "DONE"
         : run.stopping()      ? "STOPPING"
         : run.workerStarted() ? "RUNNING"
                               : "WAIT_W";
}
} // namespace

void setup() {
  Serial.setTxBufferSize(1024);
  Serial.begin(115200);
  digitalWrite(kPowerEnable, HIGH);
  pinMode(kPowerEnable, OUTPUT);
  SPI.begin(kSck, kMiso, kMosi, kCs);
  delay(1000);
  last_loop = millis();
  last_report = last_loop;
  emit("SD_LOGGING: bench only; modem/BLE/IMU disabled; send W once to allocate/write 4 "
       "missing-GNSS rows\n");
}
void loop() {
  const uint32_t now = millis(), gap = now - last_loop;
  last_loop = now;
  if (gap > max_gap)
    max_gap = gap;
  // At most one serial byte per control pass; repeat W never reuses/reallocates.
  if (Serial.available() && Serial.read() == 'W') {
    if (run.used())
      emit("SD_LOGGING: W ignored; one shot per boot\n");
    else {
      run.start(now);
      emit("SD_LOGGING: START requested; startup=15000ms overall=30000ms\n");
    }
  }
  run.service(now);
  if (run.workerStarted() && !reported_allocation) {
    const auto a = owner.allocation(); // Copy only; no worker-owned filesystem access.
    if (a.status != ridesync::IdentityStatus::Pending)
      reported_allocation =
          emit("SD_LOGGING: allocation_status=%u counter=%lu identity_error=%d\n",
               unsigned(a.status), static_cast<unsigned long>(uint32_t(a.id)), a.error);
  }
  if (run.rows() != reported_rows && emit("SD_LOGGING: ROW accepted=%lu monotonic_ms=%llu\n",
                                          static_cast<unsigned long>(run.rows()),
                                          static_cast<unsigned long long>(run.lastRowMs())))
    reported_rows = run.rows();
  if ((run.done() || run.failed()) && !reported_final) {
    const bool finished = run.workerStarted() && owner.workerFinished();
    reported_final = emit("SD_LOGGING: %s reason=%s worker_finished=%u io=%d\n", state(),
                          run.reason(), finished, owner.ioError());
  }
  if (run.failed() && run.workerStarted() && owner.workerFinished() && !reported_cleanup)
    reported_cleanup = emit("SD_LOGGING: cleanup_finished; FAILED remains; reset required\n");
  if (uint32_t(now - last_report) >= 1000) {
    last_report = now;
    const auto h = run.health();
    const uint32_t generation = owner.progress().generation();
    const auto outcome = owner.progress().outcome();
    const bool finished = run.workerStarted() && owner.workerFinished();
    emit("SD_LOGGING: %s gap_ms=%lu max_gap_ms=%lu a=%lu d=%lu r=%lu w=%lu f=%lu l=%lu p=%lu t=%u "
         "s=%u gen=%lu outcome=%u io=%d finished=%u\n",
         state(), static_cast<unsigned long>(gap), static_cast<unsigned long>(max_gap),
         static_cast<unsigned long>(h.accepted), static_cast<unsigned long>(h.dropped),
         static_cast<unsigned long>(h.rejected), static_cast<unsigned long>(h.written),
         static_cast<unsigned long>(h.flushed), static_cast<unsigned long>(h.lost),
         static_cast<unsigned long>(h.progress), h.terminal, h.stopped,
         static_cast<unsigned long>(generation), unsigned(outcome), owner.ioError(), finished);
  }
  delay(1); // Yield while the independent production worker owns all SD IO.
}

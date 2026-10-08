#include "bench.h"
#include "modem_arduino.h"
#include "nvs_guard.h"
#include "reporter.h"
#include <Arduino.h>
#include <esp_timer.h>

namespace {
using namespace gnss_bench;
struct Time : Clock, MicroClock {
  uint32_t now() const override { return millis(); }
  uint64_t microsNow() const override { return uint64_t(esp_timer_get_time()); }
} time_source;
HardwareSerial modem_serial(2);
class Port : public BenchPort {
public:
  Port() : uart_(modem_serial) {}
  bool begin() override {
    QualifiedModemPins pins;
    // Bench-only experimental routing opt-in. No measured qualification claim.
    // Power/reset/DTR pins remain unset and ArduinoGnssPower is never used.
    pins.pins_qualified = true;
    pins.documentary_profile_opt_in = true;
    pins.tx = 26;
    pins.rx = 27;
    pins.baud = 115200;
    return uart_.begin(pins) && bool(modem_serial);
  }
  size_t available() override { return uart_.available(); }
  int read() override { return uart_.read(); }
  size_t writable() override { return uart_.writable(); }
  size_t write(const char *data, size_t length) override { return uart_.write(data, length); }

private:
  ArduinoModemUart uart_;
} port;
Bench bench(time_source, time_source, port); // Retained through cap/cancel/errors.
struct Console : Output {
  size_t writable() override { return size_t(Serial.availableForWrite()); }
  size_t write(const char *data, size_t length) override {
    return Serial.write(reinterpret_cast<const uint8_t *>(data), length);
  }
} console;
Reporter reporter;
uint32_t last_report = 0;
unsigned last_state = UINT32_MAX, last_health = UINT32_MAX, last_validity = UINT32_MAX;
unsigned last_power = UINT32_MAX, last_ready = UINT32_MAX, last_stop = UINT32_MAX;

void summary() {
  char line[768];
  size_t size = bench.report(line, sizeof(line));
  if (!size)
    return;
  const auto refusals = nvsRefusals();
  // Replace newline with non-sensitive startup refusal counters.
  --size;
  const int extra = snprintf(line + size, sizeof(line) - size, " nvs=%u/%u/%u\n", refusals.init,
                             refusals.open, refusals.erase);
  if (extra < 0 || size_t(extra) >= sizeof(line) - size ||
      !reporter.queue(line, size + size_t(extra)))
    bench.reportDropped();
}
} // namespace
void setup() {
  Serial.begin(115200);
  // Idle: UART2 stays unopened; no modem power/reset/commands are issued.
}
void loop() {
  bool requested = false;
  // Bounded control input. X is terminal when consumed, including before A.
  for (unsigned n = 0; n < 16 && Serial.available(); ++n) {
    const int command = Serial.read();
    if (command == 'A') {
      bench.start();
      requested = true;
    } else if (command == 'X') {
      bench.cancel();
      requested = true;
    } else if (command == 'R') {
      bench.suppress();
      requested = true;
    }
  }
  bench.loop(); // Heartbeat outside production gps.tick, even after terminal stop.
  const auto state = bench.snapshot();
  const unsigned power = unsigned(state.gnss_power_enabled);
  const unsigned ready = unsigned(state.receiver_ready);
  const unsigned stop = unsigned(bench.stopReason());
  const bool changed = last_state != unsigned(state.state) ||
                       last_health != unsigned(state.health) ||
                       last_validity != unsigned(state.validity) || last_power != power ||
                       last_ready != ready || last_stop != stop;
  last_state = unsigned(state.state);
  last_health = unsigned(state.health);
  last_validity = unsigned(state.validity);
  last_power = power;
  last_ready = ready;
  last_stop = stop;
  const uint32_t now = millis();
  if (requested || changed || uint32_t(now - last_report) >= 1000) {
    last_report = now;
    summary();
  }
  reporter.service(console); // <=64 bytes/loop, admitted only by free TX capacity.
}

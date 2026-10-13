#include "power_shutdown_esp32.h"
#include <cassert>
#include <vector>
using namespace ridesync;
struct Call {
  int operation, pin, level;
};
struct Port : PowerGpioPort {
  std::vector<Call> calls;
  int fail_at = -1;
  int levels[40] = {};
  int32_t call(int op, int pin, int level = 0) {
    calls.push_back({op, pin, level});
    return int(calls.size()) == fail_at ? 73 : 0;
  }
  int32_t configureInput(int pin) override { return call(1, pin); }
  int32_t read(int pin, bool &level) override {
    level = levels[pin];
    return call(2, pin);
  }
  int32_t preloadOutput(int pin, bool level) override { return call(3, pin, level); }
  int32_t enableOutput(int pin) override { return call(4, pin); }
  int32_t writeOutput(int pin, bool level) override { return call(5, pin, level); }
};
PowerBackendConfig qualified() {
  PowerBackendConfig c;
  c.enabled = c.electrical_qualified = c.reservations_complete = c.supply_profile_qualified = true;
  c.independent_cutoff_qualified = c.off_enabled = c.off_qualified = c.off_startup_safe = true;
  c.electrical_profile = "synthetic-protected-logic";
  c.provenance = "fixture-only";
  c.independent_timer_ms = 7000;
  c.acc.pin = 34;
  c.acc.polarity = PowerPolarity::ActiveHigh;
  c.acc.pull = PowerExternalPull::Down;
  c.power_fail.pin = 39;
  c.power_fail.polarity = PowerPolarity::ActiveLow;
  c.power_fail.pull = PowerExternalPull::Up;
  c.reserve_ready.pin = 23;
  c.reserve_ready.polarity = PowerPolarity::ActiveHigh;
  c.reserve_ready.pull = PowerExternalPull::Down;
  c.off_pin = 32;
  c.off_polarity = PowerPolarity::ActiveHigh;
  return c;
}
void refusal(const PowerBackendConfig &c) {
  Port p;
  Esp32PowerBackend b(p, c);
  assert(!b.initialize());
  assert(!b.initialize());
  auto o = b.observe(123);
  assert(o.acc == PowerSignal::Unknown);
  assert(o.reserve == PowerReserve::Unknown && o.supply == PowerSupply::Unknown);
  assert(!b.requestOff());
  assert(p.calls.empty());
}
int main() {
  refusal({});
  auto c = qualified();
  assert(validatePowerBackendConfig(c));
  // GPIO36 is hardwired to the fitted board's SOLAR_IN divider. An apparently
  // complete active-bus map cannot make it a protected digital power input.
  auto solar = c;
  solar.reserve_ready.pin = 23;
  solar.acc.pin = 36;
  assert(!validatePowerBackendConfig(solar));
  refusal(solar); // Repeated initialize/observe/OFF must remain zero IO.
  for (unsigned flag = 0; flag < 9; ++flag) {
    auto invalid = c;
    switch (flag) {
    case 0:
      invalid.electrical_qualified = false;
      break;
    case 1:
      invalid.reservations_complete = false;
      break;
    case 2:
      invalid.supply_profile_qualified = false;
      break;
    case 3:
      invalid.independent_cutoff_qualified = false;
      break;
    case 4:
      invalid.off_qualified = false;
      break;
    case 5:
      invalid.off_startup_safe = false;
      break;
    case 6:
      invalid.electrical_profile = nullptr;
      break;
    case 7:
      invalid.provenance = "";
      break;
    case 8:
      invalid.independent_timer_ms = 0;
      break;
    }
    refusal(invalid);
  }
  auto invalid = c;
  invalid.independent_timer_ms = 0x80000000u;
  refusal(invalid);
  for (int pin = -2; pin <= 40; ++pin) {
    auto input = c;
    input.acc.pin = pin;
    const bool allowed = pin == 18 || pin == 19 || pin == 21 || pin == 22 || pin == 32 || pin == 34;
    // 23/39 already belong to independent status signals;32 belongs to OFF.
    assert(validatePowerBackendConfig(input) == (allowed && pin != 32));
    if (!validatePowerBackendConfig(input))
      refusal(input);
    auto output = c;
    output.off_pin = pin;
    const bool output_allowed = pin == 18 || pin == 19 || pin == 21 || pin == 22 || pin == 32;
    assert(validatePowerBackendConfig(output) == output_allowed);
    if (!output_allowed)
      refusal(output);
  }
  for (int pin : {34, 39, 23, 32}) {
    invalid = c;
    invalid.reserved_pins = uint64_t(1) << pin;
    refusal(invalid);
  }
  for (int pin : {20, 24, 28, 29, 30, 31, 40, 63}) {
    invalid = c;
    invalid.reserved_pins = uint64_t(1) << pin;
    refusal(invalid);
  }
  for (int i = 0; i < 3; ++i) {
    invalid = c;
    auto *input = i == 0 ? &invalid.acc : i == 1 ? &invalid.power_fail : &invalid.reserve_ready;
    input->polarity = PowerPolarity::Unspecified;
    refusal(invalid);
    input->polarity = static_cast<PowerPolarity>(255);
    refusal(invalid);
    input->polarity = PowerPolarity::ActiveHigh;
    input->pull = PowerExternalPull::Unspecified;
    refusal(invalid);
    input->pull = static_cast<PowerExternalPull>(255);
    refusal(invalid);
  }
  invalid = c;
  invalid.off_polarity = PowerPolarity::Unspecified;
  refusal(invalid);
  invalid = c;
  invalid.off_polarity = static_cast<PowerPolarity>(255);
  refusal(invalid);
  invalid = c;
  invalid.power_fail.pin = invalid.acc.pin;
  refusal(invalid);
  invalid = c;
  invalid.reserve_ready.pin = invalid.power_fail.pin;
  refusal(invalid);
  invalid = c;
  invalid.off_pin = invalid.acc.pin;
  refusal(invalid);
  Port p;
  p.levels[34] = 1;
  p.levels[39] = 1;
  p.levels[23] = 1;
  Esp32PowerBackend b(p, c);
  assert(b.initialize());
  assert(b.initialize());
  assert(p.calls.size() == 5 && p.calls[0].operation == 3 && p.calls[0].level == 0);
  assert(p.calls[1].operation == 4); // OFF latch inactive before enabling driver.
  auto o = b.observe(1);
  assert(o.acc == PowerSignal::Active && o.power_fail == PowerSignal::Inactive);
  assert(o.reserve == PowerReserve::Ready && o.supply == PowerSupply::Usable && !o.voltage_valid);
  p.levels[39] = 0;
  o = b.observe(2);
  assert(o.power_fail == PowerSignal::Active && o.supply == PowerSupply::Usable);
  p.levels[23] = 0;
  o = b.observe(3);
  assert(o.reserve == PowerReserve::NotReady && o.supply == PowerSupply::Unusable);
  p.levels[39] = 1;
  o = b.observe(4);
  assert(o.supply == PowerSupply::Usable);
  p.levels[34] = 0;
  o = b.observe(5);
  assert(o.acc == PowerSignal::Inactive);
  const auto before = p.calls.size();
  assert(b.requestOff());
  assert(b.requestOff());
  assert(p.calls.size() == before + 1 && p.calls.back().operation == 5 &&
         p.calls.back().level == 1);
  assert(b.status().off_attempted && b.status().off_succeeded);
  for (int error = 1; error <= 5; ++error) {
    Port port;
    port.fail_at = error;
    Esp32PowerBackend failed(port, c);
    assert(!failed.initialize());
    assert(failed.status().initialization_error == 73);
    auto count = port.calls.size();
    assert(!failed.initialize());
    failed.observe(1);
    assert(!failed.requestOff());
    assert(port.calls.size() == count);
  }
  for (int error = 1; error <= 3; ++error) {
    Port port;
    Esp32PowerBackend read(port, c);
    assert(read.initialize());
    port.levels[34] = 1;
    port.levels[39] = 0;
    port.levels[23] = 1;
    port.fail_at = int(port.calls.size()) + error;
    auto observation = read.observe(1);
    assert(observation.io_error && read.status().read_error == 73);
    if (error == 1)
      assert(observation.acc == PowerSignal::Unknown);
    if (error == 2)
      assert(observation.power_fail == PowerSignal::Unknown &&
             observation.supply == PowerSupply::Unknown);
    if (error == 3)
      assert(observation.reserve == PowerReserve::Unknown &&
             observation.supply == PowerSupply::Unknown);
    port.fail_at = -1;
    observation = read.observe(2);
    assert(!observation.io_error && read.status().read_error == 0);
  }
  Port off;
  Esp32PowerBackend failoff(off, c);
  assert(failoff.initialize());
  off.fail_at = 6;
  assert(!failoff.requestOff());
  assert(!failoff.requestOff());
  assert(off.calls.size() == 6);
  assert(failoff.status().off_error == 73 && failoff.status().off_attempted &&
         !failoff.status().off_succeeded);
  c.off_polarity = PowerPolarity::ActiveLow;
  Port low;
  Esp32PowerBackend active_low(low, c);
  assert(active_low.initialize());
  assert(low.calls.front().level == 1);
  assert(active_low.requestOff());
  assert(low.calls.back().level == 0);
  c.off_enabled = false;
  c.off_pin = -1;
  c.off_polarity = PowerPolarity::Unspecified;
  c.off_qualified = c.off_startup_safe = false;
  Port monitor;
  Esp32PowerBackend monitoring(monitor, c);
  assert(monitoring.initialize());
  assert(monitor.calls.size() == 3);
  assert(!monitoring.requestOff());
  assert(monitor.calls.size() == 3);
}

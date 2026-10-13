#include "power_shutdown_esp32.h"
#include <cassert>
#include <driver/gpio.h>
#include <vector>
using namespace ridesync;
std::vector<int> events;
int sdk_error = 0, sdk_level = 1;
int gpio_config(const gpio_config_t *c) {
  assert(c->pull_up_en == GPIO_PULLUP_DISABLE && c->pull_down_en == GPIO_PULLDOWN_DISABLE);
  assert(c->intr_type == GPIO_INTR_DISABLE);
  assert(c->pin_bit_mask && !(c->pin_bit_mask & (c->pin_bit_mask - 1)));
  events.push_back(c->mode == GPIO_MODE_INPUT ? 1 : 2);
  return sdk_error;
}
int gpio_set_level(gpio_num_t pin, uint32_t level) {
  assert(pin == 32 && level <= 1);
  events.push_back(level ? 4 : 3);
  return sdk_error;
}
int gpio_get_level(gpio_num_t pin) {
  assert(pin == 34 || pin == 39 || pin == 23);
  events.push_back(5);
  return sdk_level;
}
int main() {
  Esp32PowerGpioPort gpio;
  PowerBackendConfig disabled;
  Esp32PowerBackend off(gpio, disabled);
  assert(!off.initialize());
  off.observe(0);
  assert(!off.requestOff());
  assert(events.empty());
  PowerBackendConfig c;
  c.enabled = c.electrical_qualified = c.reservations_complete = true;
  c.supply_profile_qualified = c.independent_cutoff_qualified = true;
  c.independent_timer_ms = 7000;
  c.off_enabled = c.off_qualified = c.off_startup_safe = true;
  c.off_pin = 32;
  c.off_polarity = PowerPolarity::ActiveHigh;
  c.electrical_profile = "synthetic";
  c.provenance = "SDK fixture";
  c.acc.pin = 34;
  c.power_fail.pin = 39;
  c.reserve_ready.pin = 23;
  c.acc.polarity = c.reserve_ready.polarity = PowerPolarity::ActiveHigh;
  c.power_fail.polarity = PowerPolarity::ActiveLow;
  c.acc.pull = c.power_fail.pull = c.reserve_ready.pull = PowerExternalPull::Up;
  auto solar = c;
  solar.acc.pin = 36;
  solar.reserve_ready.pin = 23;
  Esp32PowerBackend hardwired(gpio, solar);
  assert(!hardwired.initialize());
  hardwired.observe(0);
  assert(!hardwired.requestOff() && events.empty());
  Esp32PowerBackend b(gpio, c);
  assert(b.initialize());
  assert((events == std::vector<int>{3, 2, 1, 1, 1}));
  auto o = b.observe(0);
  assert(o.supply == PowerSupply::Usable && o.reserve == PowerReserve::Ready);
  sdk_level = 2;
  o = b.observe(1);
  assert(o.acc == PowerSignal::Unknown && o.reserve == PowerReserve::Unknown);
  assert(o.io_error && b.status().read_error == ESP_ERR_INVALID_RESPONSE);
  sdk_level = 1;
  sdk_error = 77;
  assert(!b.requestOff());
  assert(b.status().off_error == 77);
  auto n = events.size();
  assert(!b.requestOff());
  assert(events.size() == n);
  assert(gpio.configureInput(34) == 77);
  assert(gpio.enableOutput(32) == 77);
  assert(gpio.preloadOutput(32, false) == 77);
  assert(gpio.writeOutput(32, true) == 77);
}

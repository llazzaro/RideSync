#include "power_shutdown_esp32.h"
namespace ridesync {
namespace {
bool polarityValid(PowerPolarity p) {
  return p == PowerPolarity::ActiveHigh || p == PowerPolarity::ActiveLow;
}
bool availablePin(int pin, bool output) {
  // Fitted WROVER module: reject straps, modem, SD, supply branch, flash/PSRAM,
  // console and GPIO35/GPIO36's battery/SOLAR_IN board dividers even when absent
  // from the active bus map.
  switch (pin) {
  case 18:
  case 19:
  case 21:
  case 22:
  case 23:
  case 32:
    return true;
  case 34:
  case 39:
    return !output;
  default:
    return false;
  }
}
bool inputQualified(const PowerInputPin &p) {
  return availablePin(p.pin, false) && polarityValid(p.polarity) &&
         (p.pull == PowerExternalPull::Up || p.pull == PowerExternalPull::Down);
}
bool declared(const char *s) { return s && *s; }
bool activeHigh(PowerPolarity p) { return p == PowerPolarity::ActiveHigh; }
} // namespace
bool validatePowerBackendConfig(const PowerBackendConfig &c) {
  const uint64_t silicon_pins =
      0xfffffu | (uint64_t(7) << 21) | (uint64_t(7) << 25) | (uint64_t(0xff) << 32);
  if (!c.enabled || !c.electrical_qualified || !c.reservations_complete ||
      !c.supply_profile_qualified || !c.independent_cutoff_qualified ||
      !declared(c.electrical_profile) || !declared(c.provenance) ||
      (c.reserved_pins & ~silicon_pins) || !c.independent_timer_ms ||
      c.independent_timer_ms >= 0x80000000u || !inputQualified(c.acc) ||
      !inputQualified(c.power_fail) || !inputQualified(c.reserve_ready))
    return false;
  uint64_t used = c.reserved_pins;
  const int inputs[] = {c.acc.pin, c.power_fail.pin, c.reserve_ready.pin};
  for (int pin : inputs) {
    const auto bit = uint64_t(1) << pin;
    if (used & bit)
      return false;
    used |= bit;
  }
  if (!c.off_enabled)
    return c.off_pin == -1 && c.off_polarity == PowerPolarity::Unspecified;
  return c.off_qualified && c.off_startup_safe && availablePin(c.off_pin, true) &&
         polarityValid(c.off_polarity) && !(used & (uint64_t(1) << c.off_pin));
}
bool Esp32PowerBackend::initialize() {
  if (status_.attempted)
    return status_.ready;
  status_.attempted = true;
  if (!validatePowerBackendConfig(config_)) {
    status_.refused = config_.enabled;
    return false;
  }
  // The private electrical profile must establish reset/startup/off-state safety.
  // Preload the inactive OFF latch before enabling the output driver.
  if (config_.off_enabled) {
    status_.initialization_error =
        gpio_.preloadOutput(config_.off_pin, !activeHigh(config_.off_polarity));
    if (status_.initialization_error)
      return false;
    status_.initialization_error = gpio_.enableOutput(config_.off_pin);
    if (status_.initialization_error)
      return false;
  }
  const PowerInputPin inputs[] = {config_.acc, config_.power_fail, config_.reserve_ready};
  for (const auto &p : inputs) {
    status_.initialization_error = gpio_.configureInput(p.pin);
    if (status_.initialization_error)
      return false;
  }
  status_.ready = true;
  return true;
}
PowerSignal Esp32PowerBackend::readSignal(const PowerInputPin &p) {
  bool high = false;
  const auto error = gpio_.read(p.pin, high);
  if (error) {
    if (!status_.read_error)
      status_.read_error = error;
    return PowerSignal::Unknown;
  }
  return high == activeHigh(p.polarity) ? PowerSignal::Active : PowerSignal::Inactive;
}
PowerObservation Esp32PowerBackend::observe(uint32_t) {
  PowerObservation o;
  if (!status_.ready)
    return o;
  status_.read_error = 0;
  o.acc = readSignal(config_.acc);
  // Emergency input is read before reserve; no ADC/IO worker/ISR can delay it.
  o.power_fail = readSignal(config_.power_fail);
  const auto ready = readSignal(config_.reserve_ready);
  o.reserve = ready == PowerSignal::Active     ? PowerReserve::Ready
              : ready == PowerSignal::Inactive ? PowerReserve::NotReady
                                               : PowerReserve::Unknown;
  // This mapping is an explicitly qualified held-up-rail contract, not a battery guess.
  if (o.power_fail == PowerSignal::Inactive ||
      (o.power_fail == PowerSignal::Active && o.reserve == PowerReserve::Ready))
    o.supply = PowerSupply::Usable;
  else if (o.power_fail == PowerSignal::Active && o.reserve == PowerReserve::NotReady)
    o.supply = PowerSupply::Unusable;
  o.io_error = status_.read_error != 0;
  return o; // Fresh reads only: unavailable/error readiness is never cached/replayed.
}
bool Esp32PowerBackend::requestOff() {
  if (!status_.ready || !config_.off_enabled)
    return false;
  if (status_.off_attempted)
    return status_.off_succeeded;
  status_.off_attempted = true;
  status_.off_error = gpio_.writeOutput(config_.off_pin, activeHigh(config_.off_polarity));
  status_.off_succeeded = status_.off_error == 0;
  return status_.off_succeeded; // Successful GPIO write does not prove power removal.
}
} // namespace ridesync
#if defined(ARDUINO_ARCH_ESP32)
#include <driver/gpio.h>
namespace ridesync {
namespace {
int32_t configureGpio(int pin, gpio_mode_t mode) {
  gpio_config_t c = {};
  c.pin_bit_mask = uint64_t(1) << pin;
  c.mode = mode;
  c.pull_up_en = GPIO_PULLUP_DISABLE;
  c.pull_down_en = GPIO_PULLDOWN_DISABLE;
  c.intr_type = GPIO_INTR_DISABLE;
  return gpio_config(&c);
}
} // namespace
int32_t Esp32PowerGpioPort::configureInput(int pin) { return configureGpio(pin, GPIO_MODE_INPUT); }
int32_t Esp32PowerGpioPort::read(int pin, bool &high) {
  // gpio_get_level has no esp_err_t channel: only observed logic 0/1 is available.
  const int level = gpio_get_level(static_cast<gpio_num_t>(pin));
  if (level != 0 && level != 1)
    return ESP_ERR_INVALID_RESPONSE;
  high = level != 0;
  return 0;
}
int32_t Esp32PowerGpioPort::preloadOutput(int pin, bool high) { return writeOutput(pin, high); }
int32_t Esp32PowerGpioPort::enableOutput(int pin) { return configureGpio(pin, GPIO_MODE_OUTPUT); }
int32_t Esp32PowerGpioPort::writeOutput(int pin, bool high) {
  return gpio_set_level(static_cast<gpio_num_t>(pin), high ? 1 : 0);
}
} // namespace ridesync
#endif

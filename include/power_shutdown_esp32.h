#pragma once
#include "power_shutdown.h"
#include <cstdint>
namespace ridesync {
enum class PowerPolarity : uint8_t { Unspecified, ActiveLow, ActiveHigh };
enum class PowerExternalPull : uint8_t { Unspecified, Up, Down };
struct PowerInputPin {
  int pin = -1;
  PowerPolarity polarity = PowerPolarity::Unspecified;
  PowerExternalPull pull = PowerExternalPull::Unspecified;
};
// Source/private commissioning only. Persistent user settings cannot supply these facts.
// Inputs are protected 3.3V logic with evidenced external pulls, never vehicle voltage.
struct PowerBackendConfig {
  bool enabled = false, electrical_qualified = false, reservations_complete = false;
  // Source evidence must establish the held-up supply truth table: fail inactive
  // means usable; fail active plus reserve Ready means usable; fail active plus
  // reserve NotReady means unusable. Unknown status cannot establish either claim.
  bool supply_profile_qualified = false, independent_cutoff_qualified = false;
  bool off_enabled = false, off_qualified = false, off_startup_safe = false;
  const char *electrical_profile = nullptr, *provenance = nullptr;
  uint64_t reserved_pins = 0;
  // Hardware-enforced from first fault/ACC fall, never reset by bounce or MCU reset.
  // Composition verifies this exceeds policy detection+drain+cut margins.
  uint32_t independent_timer_ms = 0;
  PowerInputPin acc, power_fail, reserve_ready;
  int off_pin = -1;
  PowerPolarity off_polarity = PowerPolarity::Unspecified;
};
bool validatePowerBackendConfig(const PowerBackendConfig &);
struct PowerBackendStatus {
  bool attempted = false, ready = false, refused = false;
  bool off_attempted = false, off_succeeded = false;
  int32_t initialization_error = 0, read_error = 0, off_error = 0;
};
class PowerEventBackend {
public:
  virtual ~PowerEventBackend() = default;
  virtual bool initialize() = 0;
  virtual PowerObservation observe(uint32_t now_ms) = 0;
  virtual bool requestOff() = 0;
  virtual PowerBackendStatus status() const = 0;
};
// Serialized owner-only IO. Return the exact SDK error (zero is success).
class PowerGpioPort {
public:
  virtual ~PowerGpioPort() = default;
  virtual int32_t configureInput(int pin) = 0;
  virtual int32_t read(int pin, bool &high) = 0;
  virtual int32_t preloadOutput(int pin, bool high) = 0;
  virtual int32_t enableOutput(int pin) = 0;
  virtual int32_t writeOutput(int pin, bool high) = 0;
};
class Esp32PowerBackend final : public PowerEventBackend {
public:
  Esp32PowerBackend(PowerGpioPort &gpio, const PowerBackendConfig &config)
      : gpio_(gpio), config_(config) {}
  bool initialize() override;
  PowerObservation observe(uint32_t now_ms) override;
  bool requestOff() override;
  PowerBackendStatus status() const override { return status_; }

private:
  PowerSignal readSignal(const PowerInputPin &);
  PowerGpioPort &gpio_;
  const PowerBackendConfig config_;
  PowerBackendStatus status_;
};
#if defined(ARDUINO_ARCH_ESP32)
class Esp32PowerGpioPort final : public PowerGpioPort {
public:
  int32_t configureInput(int pin) override;
  int32_t read(int pin, bool &high) override;
  int32_t preloadOutput(int pin, bool high) override;
  int32_t enableOutput(int pin) override;
  int32_t writeOutput(int pin, bool high) override;
};
#endif
} // namespace ridesync

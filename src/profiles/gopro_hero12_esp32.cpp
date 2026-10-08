#include "profiles/gopro_hero12_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "ble_esp32.h"
#include <Arduino.h>

namespace ridesync {
namespace {
struct MillisClock final : Clock {
  uint32_t now() const override { return millis(); }
};
} // namespace
Hero12Runtime hero12Runtime() {
  static MillisClock clock;
  static Hero12Adapter adapter(Esp32BleHost::instance(), clock);
  static CameraManager manager(clock, adapter, Hero12Adapter::managerPolicy());
  static bool attached = (adapter.attach(manager), true);
  (void)attached;
  return {adapter, manager};
}
} // namespace ridesync
extern "C" ridesync::Hero12Runtime ridesync_hero12_runtime() { return ridesync::hero12Runtime(); }
extern "C" void ridesync_hero12_service() { ridesync::hero12Runtime().adapter.service(); }
#endif

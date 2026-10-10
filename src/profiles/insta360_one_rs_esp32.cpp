#include "profiles/insta360_one_rs_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "ble_esp32.h"
#include <Arduino.h>
namespace ridesync {
namespace {
struct OneRsClock final : Clock {
  uint32_t now() const override { return millis(); }
};
} // namespace
OneRsRuntime oneRsRuntime() {
  static OneRsClock clock;
  static OneRsAdapter adapter(Esp32BleHost::instance(), clock);
  static CameraManager manager(clock, adapter, OneRsAdapter::managerPolicy());
  static RecordingManager group(manager, clock);
  static bool attached = (adapter.attach(manager), adapter.attachGroup(group), true);
  (void)attached;
  return {adapter, manager, group};
}
} // namespace ridesync
extern "C" ridesync::OneRsRuntime ridesync_one_rs_runtime() { return ridesync::oneRsRuntime(); }
extern "C" void ridesync_one_rs_service() { ridesync::oneRsRuntime().adapter.service(); }
#endif

#include "profiles/insta360_go3s_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "ble_esp32.h"
#include <Arduino.h>
namespace ridesync {
namespace {
struct Go3sClock final : Clock {
  uint32_t now() const override { return millis(); }
};
} // namespace
Go3sRuntime go3sRuntime() {
  static Go3sClock clock;
  static Go3sAdapter adapter(Esp32BleHost::instance(), clock);
  static CameraManager manager(clock, adapter, Go3sAdapter::managerPolicy());
  static RecordingManager group(manager, clock);
  static bool attached = (adapter.attach(manager), adapter.attachGroup(group), true);
  (void)attached;
  return {adapter, manager, group};
}
} // namespace ridesync
extern "C" ridesync::Go3sRuntime ridesync_go3s_runtime() { return ridesync::go3sRuntime(); }
extern "C" void ridesync_go3s_service() { ridesync::go3sRuntime().adapter.service(); }
#endif

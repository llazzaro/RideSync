#include "profiles/mixed_camera_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "ble_esp32.h"
#include "x5_peripheral_esp32.h"
#include "x5_wake_esp32.h"
#include <Arduino.h>
namespace ridesync {
namespace {
struct MillisClock final : Clock {
  uint32_t now() const override { return millis(); }
};
} // namespace
MixedCameraRuntime mixedCameraRuntime() {
  static MillisClock clock;
  static Esp32X5Peripheral peripheral;
  static MixedCameraAdapter adapter(Esp32BleHost::instance(), peripheral, clock, &x5WakeRadio());
  static CameraManager manager(clock, adapter, MixedCameraAdapter::managerPolicy());
  static RecordingManager group(manager, clock);
  static bool attached = (adapter.attach(manager, group), true);
  (void)attached;
  return {adapter, manager, group};
}
} // namespace ridesync
extern "C" ridesync::MixedCameraRuntime ridesync_mixed_camera_runtime() {
  return ridesync::mixedCameraRuntime();
}
#endif

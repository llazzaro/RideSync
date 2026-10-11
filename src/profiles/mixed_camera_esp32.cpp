#include "profiles/mixed_camera_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "ble_esp32.h"
#include "x5_peripheral_esp32.h"
#include "x5_wake_esp32.h"
#include <Arduino.h>
#include <cstdlib>
#include <esp_heap_caps.h>
#include <new>
namespace ridesync {
namespace {
struct MillisClock final : Clock {
  uint32_t now() const override { return millis(); }
};
struct Owner {
  MillisClock clock;
  Esp32X5Peripheral peripheral;
  MixedCameraAdapter adapter;
  CameraManager manager;
  RecordingManager group;
  Owner()
      : adapter(Esp32BleHost::instance(), peripheral, clock, &x5WakeRadio()),
        manager(clock, adapter, MixedCameraAdapter::managerPolicy()), group(manager, clock) {
    adapter.attach(manager, group);
  }
};
Owner *owner = nullptr;
bool attempted = false;
} // namespace
bool ensureMixedCameraRuntime() {
  if (attempted)
    return owner != nullptr;
  attempted = true;
  // Callback owners stay in internal RAM for this boot. Failed allocation is
  // terminal and precedes peripheral IO; never free an active transport owner.
  void *memory = heap_caps_malloc(sizeof(Owner), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!memory)
    return false;
  owner = new (memory) Owner;
  return true;
}
MixedCameraRuntime mixedCameraRuntime() {
  if (!ensureMixedCameraRuntime())
    std::abort(); // Reference ABI callers cannot receive a partial runtime.
  return {owner->adapter, owner->manager, owner->group};
}
} // namespace ridesync
extern "C" ridesync::MixedCameraRuntime ridesync_mixed_camera_runtime() {
  return ridesync::mixedCameraRuntime();
}
#endif

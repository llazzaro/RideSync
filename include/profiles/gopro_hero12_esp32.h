#pragma once
#include "profiles/gopro_hero12.h"
#if defined(ARDUINO_ARCH_ESP32)
namespace ridesync {
// Opt-in boot-lifetime composition. Construction has no BLE I/O. Commissioning
// must supply source and store proof before adapter.start(true, true).
struct Hero12Runtime {
  Hero12Adapter &adapter;
  CameraManager &manager;
};
Hero12Runtime hero12Runtime();
} // namespace ridesync
extern "C" ridesync::Hero12Runtime ridesync_hero12_runtime();
// Serialized owner-loop entry point, retained in the opt-in compile image.
extern "C" void ridesync_hero12_service();
#endif

#pragma once
#include "camera_event_session.h"
#include "profiles/gopro_hero12.h"
#if defined(ARDUINO_ARCH_ESP32)
namespace ridesync {
// Opt-in boot-lifetime composition. Construction has no BLE I/O. Commissioning
// must supply source and store proof before adapter.start(true, true).
struct Hero12Runtime {
  Hero12Adapter &adapter;
  CameraManager &manager;
  RecordingManager &group;
};
Hero12Runtime hero12Runtime();
bool hero12BindSession(CameraEventSession &session);
bool hero12UnbindStoppedSession();
} // namespace ridesync
extern "C" ridesync::Hero12Runtime ridesync_hero12_runtime();
// Serialized owner-loop entry point, retained in the opt-in compile image.
extern "C" void ridesync_hero12_service();
#endif

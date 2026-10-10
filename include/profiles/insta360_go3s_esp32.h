#pragma once
#include "profiles/insta360_go3s.h"
#include "recording_manager.h"
#if defined(ARDUINO_ARCH_ESP32)
namespace ridesync {
struct Go3sRuntime {
  Go3sAdapter &adapter;
  CameraManager &manager;
  RecordingManager &group;
};
// Boot-lifetime objects on the existing shared host. No constructor performs
// radio/NVS I/O; caller commissions peers and explicitly starts the adapter.
Go3sRuntime go3sRuntime();
} // namespace ridesync
extern "C" ridesync::Go3sRuntime ridesync_go3s_runtime();
extern "C" void ridesync_go3s_service();
#endif

#pragma once
#include "profiles/insta360_one_rs.h"
#include "recording_manager.h"
#if defined(ARDUINO_ARCH_ESP32)
namespace ridesync {
struct OneRsRuntime {
  OneRsAdapter &adapter;
  CameraManager &manager;
  RecordingManager &group;
};
// Boot-lifetime owner on the shared host. Constructors perform no radio/store
// operations; the caller commissions peers and explicitly starts the adapter.
OneRsRuntime oneRsRuntime();
} // namespace ridesync
extern "C" ridesync::OneRsRuntime ridesync_one_rs_runtime();
extern "C" void ridesync_one_rs_service();
#endif

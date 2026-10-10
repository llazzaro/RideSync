#pragma once
#include "profiles/mixed_camera.h"
#if defined(ARDUINO_ARCH_ESP32)
namespace ridesync {
struct MixedCameraRuntime {
  MixedCameraAdapter &adapter;
  CameraManager &manager;
  RecordingManager &group;
};
MixedCameraRuntime mixedCameraRuntime();
} // namespace ridesync
extern "C" ridesync::MixedCameraRuntime ridesync_mixed_camera_runtime();
#endif

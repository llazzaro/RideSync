#pragma once
#include "x5_runtime.h"
#if defined(ARDUINO_ARCH_ESP32)
namespace ridesync {
X5Runtime &x5Runtime();
void x5MilestoneBegin(bool safe_mode);
void x5MilestoneService(bool admission_allowed);
} // namespace ridesync
extern "C" ridesync::X5Runtime *ridesync_x5_runtime();
#endif

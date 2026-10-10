#pragma once
#include "x5_wake.h"
#if defined(ARDUINO_ARCH_ESP32)
namespace ridesync {
// Lazy host startup on an isolated boot-lifetime worker; no radio work at boot.
WakeRadio &x5WakeRadio();
} // namespace ridesync
extern "C" ridesync::WakeRadio *ridesync_x5_wake_radio();
#endif

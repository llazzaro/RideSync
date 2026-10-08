#pragma once
#include <cstdint>
namespace insta360_fixture {
// Independent literal from MIT marcelpallares config.h at c76e1403, SHUTTER_CMD.
// Community CE82 remote-to-camera button event, not an X5 capture or Start/Stop.
constexpr uint8_t kShutterEvent[] = {0xfc, 0xef, 0xfe, 0x86, 0x00, 0x03, 0x01, 0x02, 0x00};
} // namespace insta360_fixture

#pragma once
#include <array>
#include <cstdint>
namespace ridesync {
namespace insta360 {
using ShutterEvent = std::array<uint8_t, 9>;
// CE82 remote-to-camera shutter button/toggle, dependent on camera mode/state.
// Non-idempotent: never automatically resend after ambiguous delivery/reset.
// This does not encode explicit Start/Stop, ACK, state, length or sequence fields.
// No receive decoder or recording observation is supported by this primitive.
ShutterEvent encodeShutterEvent();
} // namespace insta360
} // namespace ridesync

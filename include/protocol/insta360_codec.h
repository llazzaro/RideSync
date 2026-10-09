#pragma once
#include "camera_profile.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace ridesync {
namespace insta360 {
using ShutterEvent = std::array<uint8_t, 9>;
// CE82 remote-to-camera shutter button/toggle, dependent on camera mode/state.
// Non-idempotent: never automatically resend after ambiguous delivery/reset.
// This does not encode explicit Start/Stop, ACK, state, length or sequence fields.
// No receive decoder or recording observation is supported by this primitive.
ShutterEvent encodeShutterEvent();
// Explicit capture-qualified format; this does not enable a camera/transport.
enum class Ce80DisplayProfile : uint8_t { Disabled, X5CapturedDisplayV1 };
struct Ce80DisplayConfig {
  Ce80DisplayProfile profile = Ce80DisplayProfile::Disabled;
};
enum class Ce80Direction : uint8_t { CameraToRemote, RemoteToCamera };
enum class Ce80DisplayError : uint8_t {
  None,
  Disabled,
  UnsupportedProfile,
  WrongDirection,
  InvalidSize,
  InvalidBuffer,
  UnsupportedHeader,
  LengthMismatch,
  UnsupportedDisplay
};
enum class Ce80DisplayKind : uint8_t { Unknown, Elapsed, Settings, Remaining };
enum class Ce80CameraMode : uint8_t { Unknown, Video, Photo };
struct Ce80DisplayResult {
  Ce80DisplayError error = Ce80DisplayError::Disabled;
  Ce80DisplayKind kind = Ce80DisplayKind::Unknown;
  Ce80CameraMode mode = Ce80CameraMode::Unknown;
  RecordingState recording = RecordingState::Unknown;
  uint32_t elapsed_seconds = 0;
};
// Pure interpretation of one complete, stable readable CE81 write. Direction
// must come independently from the actual GATT route. Default disabled; rejects
// unknown profile/direction, size outside6..256 (local cap), null buffer, magic,
// then declared length before body access. Unknown types expose no body/state.
// Supports typed .HH:MM:SS elapsed text and exact captured 5.7K|30 / 72MP|MEGA
// settings only. Remaining runtime/count is never Stopped. Errors return fresh
// Unknown kind/mode/state and seconds0. No raw text, identities or pointers escape.
// This result is display evidence, not an ACK, query or connection-fresh state.
// Caller owns link identity/generation, freshness, correlation and mode-safe
// command admission. Silence, missing frames and disconnect do not imply Stop.
Ce80DisplayResult decodeCe80Display(const Ce80DisplayConfig &, Ce80Direction, const uint8_t *,
                                    size_t);
} // namespace insta360
} // namespace ridesync

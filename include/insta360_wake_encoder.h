#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace ridesync {
namespace insta360 {
// Source wire selection, not camera-model qualification.
enum class WakeProfile : uint8_t { Disabled, M5WakeV1 };
enum class WakeCodecError : uint8_t {
  None,
  Disabled,
  UnsupportedProfile,
  InvalidIdentifier,
  UnsupportedName
};
struct WakeIdentifierResult {
  WakeCodecError error = WakeCodecError::Disabled;
  std::array<uint8_t, 6> bytes{};
};
struct WakeEncoding {
  WakeCodecError error = WakeCodecError::Disabled;
  std::array<uint8_t, 31> advertisement{};
  std::array<uint8_t, 25> scan_response{};
};
// Exact source name form only; MAC addresses are never wake identifiers.
WakeIdentifierResult deriveWakeIdentifier(WakeProfile, const char *, size_t);
// Profile validation precedes pointer access; failure arrays are all zero.
WakeEncoding encodeWake(WakeProfile, const uint8_t *, size_t);
} // namespace insta360
} // namespace ridesync

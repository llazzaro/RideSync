// Field facts: MIT M5 fork c76e140396de8b2404cdd36d17cf0d1a251a9dcc
// camera.h / ble_handlers.h, and MIT ESP32 example
// 83d4748b68d6ee5fd4414994a9e26b7d2f21364b Insta_BLE.ino.
// AD framing is RideSync's explicit legacy layout, not a camera capture.
#include "insta360_wake_encoder.h"
#include <cstring>
namespace ridesync {
namespace insta360 {
namespace {
WakeCodecError profileError(WakeProfile p) {
  return p == WakeProfile::Disabled   ? WakeCodecError::Disabled
         : p == WakeProfile::M5WakeV1 ? WakeCodecError::None
                                      : WakeCodecError::UnsupportedProfile;
}
WakeEncoding failure(WakeCodecError error) {
  WakeEncoding out;
  out.error = error;
  return out;
}
} // namespace
WakeEncoding encodeWake(WakeProfile profile, const uint8_t *id, size_t length) {
  const auto error = profileError(profile);
  if (error != WakeCodecError::None)
    return failure(error);
  if (!id || length != 6)
    return failure(WakeCodecError::InvalidIdentifier);
  for (size_t i = 0; i < 6; ++i)
    if (id[i] < 32 || id[i] > 126)
      return failure(WakeCodecError::InvalidIdentifier);
  WakeEncoding out;
  out.error = WakeCodecError::None;
  out.advertisement = {{2,    1,    6,    27,   255, 0x4c, 0,    2,    0x15, 9, 0x4f,
                        0x52, 0x42, 0x49, 0x54, 9,   0xff, 0x0f, 0,    0,    0, 0,
                        0,    0,    0,    0,    0,   0,    0,    0xe4, 1}};
  std::memcpy(out.advertisement.data() + 19, id, 6);
  out.scan_response = {{3, 3, 0x80, 0xce, 20, 9}};
  const char name[] = "Insta360 GPS Remote";
  static_assert(sizeof(name) == 20, "Exact legacy source name");
  std::memcpy(out.scan_response.data() + 6, name, sizeof(name) - 1);
  return out;
}
WakeIdentifierResult deriveWakeIdentifier(WakeProfile profile, const char *name, size_t length) {
  WakeIdentifierResult out;
  out.error = profileError(profile);
  if (out.error != WakeCodecError::None)
    return out;
  out.error = WakeCodecError::UnsupportedName;
  if (!name || length != 9 || std::memcmp(name, "X5 ", 3) != 0)
    return out;
  const auto encoded = encodeWake(profile, reinterpret_cast<const uint8_t *>(name + 3), 6);
  out.error = encoded.error;
  if (out.error == WakeCodecError::None)
    std::memcpy(out.bytes.data(), name + 3, 6);
  return out;
}
} // namespace insta360
} // namespace ridesync

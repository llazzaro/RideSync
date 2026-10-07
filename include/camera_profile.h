#pragma once
#include <cstdint>
namespace ridesync {
enum class CameraFamily { Unknown, Insta360, GoPro };
enum class CameraModel { Unknown, X5, GO3S, ONE_RS, HERO12_BLACK };
enum class AddressType { Unknown, Public, Random };
enum class RecordingState { Unknown, Stopped, Recording };
enum class CapabilityState { Unknown, Unsupported, Supported };
struct Capabilities {
  CapabilityState start = CapabilityState::Unknown;
  CapabilityState stop = CapabilityState::Unknown;
  CapabilityState query = CapabilityState::Unknown;
  CapabilityState wake = CapabilityState::Unknown;
  CapabilityState gps = CapabilityState::Unknown;
};
} // namespace ridesync

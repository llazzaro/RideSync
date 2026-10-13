#pragma once
#include <cstdint>
namespace ridesync {
enum class CameraFamily { Unknown, Insta360, GoPro, DJI };
enum class CameraModel {
  Unknown,
  X5,
  GO3S,
  ONE_RS,
  HERO12_BLACK,
  DJI_ACTION4,
  DJI_ACTION5_PRO,
  DJI_ACTION6,
  DJI_OSMO360
};
enum class AddressType { Unknown, Public, Random };
enum class RecordingState { Unknown, Stopped, Recording };
enum class CapabilityState { Unknown, Unsupported, Supported };
// Health qualification is independent from command/query support.
enum class HealthField : uint8_t {
  Recording,
  BatteryPercent,
  RemainingVideoSeconds,
  SdRemainingKilobytes,
  Overheating,
  Count
};
enum class HealthValidity : uint8_t { Unknown, Valid, Unavailable };
enum class HealthProfile : uint8_t { None, X5Ce80, DjiStatus, Hero12V1 };
enum class HealthUnit : uint8_t { None, RecordingState, Percent, Seconds, Kilobytes, Boolean };
struct QualifiedHealthSample {
  uint64_t value = 0;
  uint32_t received_ms = 0;
  uint32_t sequence = 0;
  uint32_t connection = 0;
  HealthField field = HealthField::Recording;
  CapabilityState capability = CapabilityState::Unknown;
  HealthValidity validity = HealthValidity::Unknown;
  HealthProfile profile = HealthProfile::None;
  HealthUnit unit = HealthUnit::None;
};
struct Capabilities {
  CapabilityState start = CapabilityState::Unknown;
  CapabilityState stop = CapabilityState::Unknown;
  CapabilityState query = CapabilityState::Unknown;
  CapabilityState wake = CapabilityState::Unknown;
  CapabilityState gps = CapabilityState::Unknown;
  CapabilityState hilight = CapabilityState::Unsupported;
};
} // namespace ridesync

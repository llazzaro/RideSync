#pragma once
#include "camera_profile.h"
#include <array>
#include <cstddef>
#include <string>
namespace ridesync {
constexpr size_t kMaxCameras = 8;
struct CameraConfig {
  std::string name;
  CameraFamily family = CameraFamily::Unknown;
  CameraModel model = CameraModel::Unknown;
  std::string identifier;
  AddressType address_type = AddressType::Unknown;
  std::string wake_identifier;
  bool enabled = true;
  bool gps_telemetry = false;
};
struct SourceConfig {
  std::array<CameraConfig, kMaxCameras> cameras;
  size_t count = 0;
  size_t capacity = kMaxCameras;
};
enum class ConfigError {
  None,
  InvalidCapacity,
  CapacityExceeded,
  InvalidName,
  UnknownModel,
  FamilyMismatch,
  MissingIdentifier,
  InvalidIdentifier,
  DuplicateIdentifier,
  InvalidAddressType,
  InvalidWakeIdentifier
};
struct ConfigResult {
  ConfigError error;
  size_t index;
  const char *message;
  bool ok() const { return error == ConfigError::None; }
};
ConfigResult validate(const SourceConfig &config);
} // namespace ridesync

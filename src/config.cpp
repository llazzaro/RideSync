#include "config.h"
#include <cctype>
namespace ridesync {
namespace {
bool validMac(const std::string &s) {
  if (s.size() != 17)
    return false;
  for (size_t i = 0; i < s.size(); ++i) {
    if (i % 3 == 2 ? s[i] != ':' : !std::isxdigit(static_cast<unsigned char>(s[i])))
      return false;
  }
  return true;
}
bool sameMac(const std::string &a, const std::string &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  return true;
}
bool validInsta360WakeSuffix(const std::string &s) {
  if (s.size() != 6)
    return false;
  for (unsigned char byte : s)
    if (byte < 0x20 || byte > 0x7e)
      return false;
  return true;
}
ConfigResult validateModel(const CameraConfig &p, size_t i) {
  struct ModelFamily {
    CameraModel model;
    CameraFamily family;
  };
  constexpr ModelFamily models[] = {{CameraModel::X5, CameraFamily::Insta360},
                                    {CameraModel::GO3S, CameraFamily::Insta360},
                                    {CameraModel::ONE_RS, CameraFamily::Insta360},
                                    {CameraModel::HERO12_BLACK, CameraFamily::GoPro},
                                    {CameraModel::DJI_ACTION4, CameraFamily::DJI},
                                    {CameraModel::DJI_ACTION5_PRO, CameraFamily::DJI},
                                    {CameraModel::DJI_ACTION6, CameraFamily::DJI},
                                    {CameraModel::DJI_OSMO360, CameraFamily::DJI}};
  CameraFamily expected = CameraFamily::Unknown;
  for (const auto &model : models)
    if (p.model == model.model) {
      expected = model.family;
      break;
    }
  if (expected == CameraFamily::Unknown)
    return {ConfigError::UnknownModel, i, "select a supported camera model"};
  if (p.family != expected)
    return {ConfigError::FamilyMismatch, i, "family does not match model"};
  if (expected == CameraFamily::DJI && (p.gps_telemetry || !p.wake_identifier.empty()))
    return {ConfigError::UnsupportedCapability, i, "DJI GPS and wake are unsupported"};
  return {ConfigError::None, 0, "valid"};
}
bool validAddressType(const CameraConfig &p) {
  if (p.address_type != AddressType::Unknown && p.address_type != AddressType::Public &&
      p.address_type != AddressType::Random)
    return false;
  return !(p.enabled || !p.identifier.empty()) || p.address_type != AddressType::Unknown;
}
bool validWakeIdentifier(const CameraConfig &p) {
  return p.wake_identifier.empty() || validMac(p.wake_identifier) ||
         (p.family == CameraFamily::Insta360 && validInsta360WakeSuffix(p.wake_identifier));
}
ConfigResult validateCameraIdentity(const CameraConfig &p, size_t i) {
  if (p.enabled && p.identifier.empty())
    return {ConfigError::MissingIdentifier, i, "enabled camera requires BLE address"};
  if (!p.identifier.empty() && !validMac(p.identifier))
    return {ConfigError::InvalidIdentifier, i,
            "identifier must be six hexadecimal octets separated by colons"};
  if (!validAddressType(p))
    return {ConfigError::InvalidAddressType, i, "set public or random BLE address type"};
  if (!validWakeIdentifier(p))
    return {ConfigError::InvalidWakeIdentifier, i,
            "wake identifier must be empty, legacy MAC syntax, or six printable ASCII bytes for "
            "Insta360"};
  return {ConfigError::None, 0, "valid"};
}
ConfigResult validateCamera(const CameraConfig &p, size_t i) {
  if (p.name.empty() || p.name.size() > 64)
    return {ConfigError::InvalidName, i, "name must contain 1..64 bytes"};
  const auto model = validateModel(p, i);
  if (!model.ok())
    return model;
  return validateCameraIdentity(p, i);
}
} // namespace
ConfigResult validate(const SourceConfig &c) {
  if (c.capacity == 0 || c.capacity > kMaxCameras)
    return {ConfigError::InvalidCapacity, 0, "capacity must be 1..8"};
  if (c.count > c.capacity)
    return {ConfigError::CapacityExceeded, 0, "camera count exceeds configured capacity"};
  for (size_t i = 0; i < c.count; ++i) {
    const auto &p = c.cameras[i];
    const auto result = validateCamera(p, i);
    if (!result.ok())
      return result;
    for (size_t j = 0; j < i; ++j)
      if (!p.identifier.empty() && sameMac(p.identifier, c.cameras[j].identifier))
        return {ConfigError::DuplicateIdentifier, i, "BLE address is already configured"};
  }
  return {ConfigError::None, 0, "valid"};
}
} // namespace ridesync

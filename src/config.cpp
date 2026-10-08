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
} // namespace
ConfigResult validate(const SourceConfig &c) {
  if (c.capacity == 0 || c.capacity > kMaxCameras)
    return {ConfigError::InvalidCapacity, 0, "capacity must be 1..8"};
  if (c.count > c.capacity)
    return {ConfigError::CapacityExceeded, 0, "camera count exceeds configured capacity"};
  for (size_t i = 0; i < c.count; ++i) {
    const auto &p = c.cameras[i];
    if (p.name.empty() || p.name.size() > 64)
      return {ConfigError::InvalidName, i, "name must contain 1..64 bytes"};
    CameraFamily expected;
    switch (p.model) {
    case CameraModel::X5:
    case CameraModel::GO3S:
    case CameraModel::ONE_RS:
      expected = CameraFamily::Insta360;
      break;
    case CameraModel::HERO12_BLACK:
      expected = CameraFamily::GoPro;
      break;
    default:
      return {ConfigError::UnknownModel, i, "select X5, GO3S, ONE_RS or HERO12_BLACK"};
    }
    if (p.family != expected)
      return {ConfigError::FamilyMismatch, i, "family does not match model"};
    if (p.enabled && p.identifier.empty())
      return {ConfigError::MissingIdentifier, i, "enabled camera requires BLE address"};
    if (!p.identifier.empty() && !validMac(p.identifier))
      return {ConfigError::InvalidIdentifier, i,
              "identifier must be six hexadecimal octets separated by colons"};
    if ((p.address_type != AddressType::Unknown && p.address_type != AddressType::Public &&
         p.address_type != AddressType::Random) ||
        ((p.enabled || !p.identifier.empty()) && p.address_type == AddressType::Unknown))
      return {ConfigError::InvalidAddressType, i, "set public or random BLE address type"};
    if (!p.wake_identifier.empty() && !validMac(p.wake_identifier) &&
        !(p.family == CameraFamily::Insta360 && validInsta360WakeSuffix(p.wake_identifier)))
      return {ConfigError::InvalidWakeIdentifier, i,
              "wake identifier must be empty, legacy MAC syntax, or six printable ASCII bytes for "
              "Insta360"};
    for (size_t j = 0; j < i; ++j)
      if (!p.identifier.empty() && sameMac(p.identifier, c.cameras[j].identifier))
        return {ConfigError::DuplicateIdentifier, i, "BLE address is already configured"};
  }
  return {ConfigError::None, 0, "valid"};
}
} // namespace ridesync

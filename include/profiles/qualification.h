#pragma once
#include "ble_remote.h"
namespace ridesync {
namespace qualification {
inline bool addressBytes(const std::array<uint8_t, 6> &address, bool reject_all_ff = true) {
  bool nonzero = false, all_ff = true;
  for (auto byte : address) {
    nonzero |= byte != 0;
    all_ff &= byte == 255;
  }
  return nonzero && (!reject_all_ff || !all_ff);
}
inline bool staticIdentity(const BondIdentity &identity, bool reject_all_ff = true) {
  return addressBytes(identity.address, reject_all_ff) &&
         (identity.type != IdentityType::RandomStatic || (identity.address[5] & 0xc0) == 0xc0);
}
inline bool resolvedIdentity(const BondIdentity &identity, bool reject_all_ff = true) {
  return identity.verified &&
         (identity.type == IdentityType::Public || identity.type == IdentityType::RandomStatic) &&
         staticIdentity(identity, reject_all_ff);
}
template <typename Byte> inline bool printable(const Byte *bytes, size_t size, uint8_t minimum) {
  for (size_t i = 0; i < size; ++i)
    if (bytes[i] < minimum || bytes[i] > 0x7e)
      return false;
  return true;
}
} // namespace qualification
} // namespace ridesync

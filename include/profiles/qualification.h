#pragma once
#include "ble_remote.h"
#include "camera_manager.h"
namespace ridesync {
namespace qualification {
inline bool recordingOperation(Operation op) {
  return op == Operation::Start || op == Operation::Stop;
}
inline bool cameraCommand(Operation op) { return recordingOperation(op) || op == Operation::Query; }
inline bool usableToken(Token token) { return token.valid() && token.hasRoom(); }
template <typename Bytes> inline bool firmwareBounds(const Bytes &bytes, size_t size) {
  return size && size <= bytes.size();
}
inline int hexDigit(char b) {
  if (b >= '0' && b <= '9')
    return b - '0';
  if (b >= 'A' && b <= 'F')
    return b - 'A' + 10;
  if (b >= 'a' && b <= 'f')
    return b - 'a' + 10;
  return -1;
}
template <typename Identifier>
inline bool identifierAddress(const Identifier &identifier, const BondIdentity &id,
                              bool separators) {
  if (identifier.size() != 17)
    return false;
  for (size_t n = 0; n < 6; ++n) {
    const int hi = hexDigit(identifier[n * 3]), lo = hexDigit(identifier[n * 3 + 1]);
    if (hi < 0 || lo < 0 || (separators && n != 5 && identifier[n * 3 + 2] != ':') ||
        id.address[5 - n] != uint8_t(hi * 16 + lo))
      return false;
  }
  return true;
}
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

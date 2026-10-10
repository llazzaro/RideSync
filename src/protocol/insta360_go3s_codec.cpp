#include "protocol/insta360_go3s_codec.h"
#include <cstring>
namespace ridesync {
namespace go3s {
namespace {
uint16_t crc(const uint8_t *p, size_t n) {
  uint16_t value = 0xffff;
  for (size_t i = 0; i < n; ++i) {
    value ^= p[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      value = (value >> 1) ^ ((value & 1) ? 0xa001 : 0);
  }
  return value;
}
uint16_t le16(const uint8_t *p) { return p[0] | (static_cast<uint16_t>(p[1]) << 8); }
void seal(Packet &p) {
  const uint16_t c = crc(p.bytes.data(), p.size);
  p.bytes[p.size++] = c & 0xff;
  p.bytes[p.size++] = c >> 8;
}
} // namespace
Packet encode(Command command, uint8_t seq, const char *auth, size_t auth_size) {
  Packet p;
  if (!seq || seq == 255)
    return p;
  uint16_t code = 0;
  uint8_t payload[36] = {};
  size_t count = 0;
  switch (command) {
  case Command::Authorize:
    if (!auth || !auth_size || auth_size > 32)
      return p;
    for (size_t i = 0; i < auth_size; ++i)
      if (auth[i] < 0x20 || auth[i] > 0x7e)
        return p;
    code = 0x27;
    payload[count++] = 0x0a;
    payload[count++] = static_cast<uint8_t>(auth_size);
    std::memcpy(payload + count, auth, auth_size);
    count += auth_size;
    payload[count++] = 0x10;
    payload[count++] = 2;
    break;
  case Command::VideoMode: {
    code = 2;
    const uint8_t normal[] = {0x0a, 4, 8, 0x29, 0x10, 0};
    count = sizeof(normal);
    std::memcpy(payload, normal, count);
    break;
  }
  case Command::StartVideo:
    code = 4;
    payload[0] = 8;
    payload[1] = 1;
    count = 2;
    break;
  case Command::Stop:
    code = 5;
    break;
  default:
    return p;
  }
  const uint8_t inner = static_cast<uint8_t>(16 + count);
  p.bytes[0] = 0xff;
  p.bytes[1] = 7;
  p.bytes[2] = 0x40;
  p.bytes[3] = inner;
  p.bytes[5] = inner;
  p.bytes[9] = 4;
  p.bytes[12] = code & 0xff;
  p.bytes[13] = code >> 8;
  p.bytes[14] = 2;
  p.bytes[15] = seq;
  p.bytes[18] = 0x80;
  std::memcpy(p.bytes.data() + 21, payload, count);
  p.size = 21 + count;
  seal(p);
  return p;
}
Packet encodeSync() {
  Packet p;
  p.bytes[0] = 0xff;
  p.bytes[1] = 7;
  p.bytes[2] = 0x41;
  p.bytes[3] = 7;
  p.size = 12;
  seal(p);
  return p;
}
Decoded decode(const uint8_t *p, size_t size) {
  Decoded d;
  if (!p || size < 7 || size > kReceiveCapacity || p[0] != 0xff || p[1] != 6 ||
      size != static_cast<size_t>(le16(p + 3)) + 7 || crc(p, size - 2) != le16(p + size - 2))
    return d;
  if (p[2] == 0x41) {
    if (size == 7)
      return d;
    d.kind = Kind::Sync;
    return d;
  }
  if (p[2] != 0x40 || size < 23 || le16(p + 5) != size - 7 || p[7] || p[8] || p[9] != 4 || p[10] ||
      p[11] || p[14] != 2 || p[16] || p[17] || (p[18] != 0x80 && p[18] != 0xc0) || p[19] || p[20])
    return d;
  d.kind = Kind::Response;
  d.status = le16(p + 12);
  d.sequence = p[15];
  return d;
}
Decoded Receiver::push(uint8_t value) {
  if (!used_ && value != 0xff)
    return {};
  bytes_[used_++] = value;
  if (used_ == 5) {
    expected_ = static_cast<size_t>(le16(bytes_.data() + 3)) + 7;
    if (expected_ > bytes_.size()) {
      reset();
      return {};
    }
  }
  if (expected_ && used_ == expected_) {
    const auto d = decode(bytes_.data(), used_);
    reset();
    return d;
  }
  Decoded d;
  d.kind = Kind::Pending;
  return d;
}
} // namespace go3s
} // namespace ridesync

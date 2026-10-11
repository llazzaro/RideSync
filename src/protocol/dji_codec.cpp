#include "protocol/dji_codec.h"
namespace ridesync {
namespace dji {
namespace {
uint16_t le16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t le32(const uint8_t *p) { return uint32_t(le16(p)) | uint32_t(le16(p + 2)) << 16; }
void write(uint8_t *p, uint32_t value, unsigned count) {
  for (unsigned i = 0; i < count; ++i)
    p[i] = uint8_t(value >> (8 * i));
}
// Reflected CRC16/IBM and CRC32/IEEE, DJI seed 0x3aa3, no final xor.
uint32_t checksum(const uint8_t *p, size_t size, uint32_t polynomial) {
  uint32_t remainder = 0x3aa3;
  for (size_t i = 0; i < size; ++i) {
    remainder ^= p[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      remainder = (remainder >> 1) ^ ((remainder & 1) ? polynomial : 0);
  }
  return remainder;
}
Packet base(uint16_t seq, uint8_t type, uint8_t set, uint8_t id, size_t payload) {
  Packet p;
  p.size = 18 + payload;
  p.bytes[0] = 0xaa;
  write(&p.bytes[1], p.size, 2);
  p.bytes[3] = type;
  write(&p.bytes[8], seq, 2);
  p.bytes[12] = set;
  p.bytes[13] = id;
  return p;
}
Packet seal(Packet p) {
  write(&p.bytes[10], checksum(p.bytes.data(), 10, 0xa001), 2);
  write(&p.bytes[p.size - 4], checksum(p.bytes.data(), p.size - 4, 0xedb88320), 4);
  return p;
}
bool video(uint8_t mode) {
  // Deliberately narrow: other status modes can contain still/submode ambiguity.
  return mode == 1 || mode == 0x38 || mode == 0x41;
}
Decoded decodeConnectionPayload(Decoded d, const uint8_t *payload, size_t n) {
  if (d.response) {
    if (n != 9)
      return {};
    d.kind = Kind::ConnectionResponse;
    d.device_id = le32(payload);
    d.return_code = payload[4];
  } else {
    if (n != 33 || payload[4] > 16)
      return {};
    d.kind = Kind::ConnectionRequest;
    d.device_id = le32(payload);
    d.verify_mode = payload[26];
    d.verify_data = le16(payload + 27);
  }

  return d;
}
Decoded decodeAckPayload(Decoded d, const uint8_t *payload, size_t n) {
  // Record ACK: one-byte demo structure and five-byte documented structure.
  if (n != 5 && !(d.cmd_id == 3 && n == 1))
    return {};
  d.kind = d.cmd_id == 3 ? Kind::RecordResponse : Kind::ModeResponse;
  d.return_code = payload[0];

  return d;
}
Decoded decodeStatusPayload(Decoded d, const uint8_t *payload, size_t n) {
  if (d.response || n != 38)
    return {};
  d.kind = Kind::Status;
  d.camera_mode = payload[0];
  d.camera_status = payload[1];
  d.power_mode = payload[28];
  d.temperature = payload[30];
  if (video(payload[0]) && payload[29] == payload[0] && payload[28] == 0 && payload[30] <= 1) {
    if (payload[1] == 3)
      d.recording = RecordingState::Recording;
    else if (payload[1] <= 2)
      d.recording = RecordingState::Stopped;
  }

  return d;
}
bool validHeader(const uint8_t *p) {
  return !(p[3] & 0xc0) && p[4] == 0 && checksum(p, 10, 0xa001) == le16(p + 10);
}
bool validEnvelope(const uint8_t *p, size_t size) {
  if (!p || size < 18 || size > kReceiveCapacity)
    return false;
  return p[0] == 0xaa && le16(p + 1) == size && validHeader(p) &&
         checksum(p, size - 4, 0xedb88320) == le32(p + size - 4);
}
} // namespace
Packet encodeConnection(uint16_t sequence, uint32_t controller_id,
                        const std::array<uint8_t, 6> &mac, uint8_t verify_mode,
                        uint16_t verify_data) {
  if (verify_mode > 1)
    return {};
  auto p = base(sequence, 2, 0, 0x19, 33);
  write(&p.bytes[14], controller_id, 4);
  p.bytes[18] = 6;
  for (size_t i = 0; i < mac.size(); ++i)
    p.bytes[19 + i] = mac[i];
  p.bytes[40] = verify_mode;
  write(&p.bytes[41], verify_data, 2);
  return seal(p);
}
Packet encodeAuthorizationResponse(uint16_t sequence, uint32_t controller_id, uint8_t return_code,
                                   uint32_t camera_index) {
  auto p = base(sequence, 0x20, 0, 0x19, 9);
  write(&p.bytes[14], controller_id, 4);
  p.bytes[18] = return_code;
  write(&p.bytes[19], camera_index, 4);
  return seal(p);
}
Packet encodeRecord(uint16_t sequence, uint32_t device_id, bool start) {
  auto p = base(sequence, 2, 0x1d, 3, 9);
  write(&p.bytes[14], device_id, 4);
  p.bytes[18] = start ? 0 : 1;
  return seal(p);
}
Packet encodeStatusSubscription(uint16_t sequence, bool periodic) {
  auto p = base(sequence, 0, 0x1d, 5, 6);
  p.bytes[14] = periodic ? 3 : 1;
  p.bytes[15] = 20;
  return seal(p);
}
Packet encodeVideoMode(uint16_t sequence, uint32_t device_id) {
  auto p = base(sequence, 2, 0x1d, 4, 9);
  write(&p.bytes[14], device_id, 4);
  p.bytes[18] = 1;
  return seal(p);
}
Decoded decode(const uint8_t *p, size_t size) {
  if (!validEnvelope(p, size))
    return {};
  Decoded d;
  d.kind = Kind::Unknown;
  d.sequence = le16(p + 8);
  d.cmd_set = p[12];
  d.cmd_id = p[13];
  d.cmd_type = p[3];
  d.response = (p[3] & 0x20) != 0;
  d.reply_type = p[3] & 0x1f;
  const size_t n = size - 18;
  const uint8_t *payload = p + 14;
  if (d.cmd_set == 0 && d.cmd_id == 0x19)
    return decodeConnectionPayload(d, payload, n);
  if (d.cmd_set == 0x1d && (d.cmd_id == 3 || d.cmd_id == 4) && d.response)
    return decodeAckPayload(d, payload, n);
  if (d.cmd_set == 0x1d && d.cmd_id == 2)
    return decodeStatusPayload(d, payload, n);
  return d;
}
Decoded Receiver::push(uint8_t byte) {
  if (used_ == 0 && byte != 0xaa)
    return {};
  bytes_[used_++] = byte;
  if (used_ == 3) {
    expected_ = le16(bytes_.data() + 1);
    if (expected_ < 18 || expected_ > kReceiveCapacity) {
      reset();
      return {};
    }
  }
  if (used_ == 12 && !validHeader(bytes_.data())) {
    reset();
    return {};
  }
  if (expected_ && used_ == expected_) {
    auto d = decode(bytes_.data(), used_);
    reset();
    return d;
  }
  Decoded pending;
  pending.kind = Kind::Pending;
  return pending;
}
} // namespace dji
} // namespace ridesync

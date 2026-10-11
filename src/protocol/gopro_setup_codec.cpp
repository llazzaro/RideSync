#include "protocol/gopro_setup_codec.h"
#include <string.h>

namespace ridesync {
namespace gopro {
namespace {
// Read a canonical protobuf uint64. Ten bytes are allowed only when byte ten is 0/1.
bool varint(const uint8_t *bytes, size_t size, size_t &at, uint64_t &value) {
  value = 0;
  for (unsigned shift = 0; shift < 70; shift += 7) {
    if (at == size)
      return false;
    const uint8_t byte = bytes[at++];
    if (shift == 63 && byte > 1)
      return false;
    value |= static_cast<uint64_t>(byte & 0x7f) << shift;
    if (!(byte & 0x80))
      return shift == 0 || byte != 0; // no redundant terminal zero
  }
  return false;
}

bool unsignedBigEndian(const Message &raw, OwnedField field, uint64_t &value) {
  if (!field.size || field.size > 8)
    return false;
  value = 0;
  for (size_t i = 0; i < field.size; ++i)
    value = (value << 8) | raw.bytes[field.offset + i];
  return true;
}
struct ParsedSetup {
  bool present = false, unknown_fields = false, unknown_enum = false;
  uint64_t result = 0;
};
bool readWireValue(unsigned wire, const Message &message, size_t &at, uint64_t &value) {
  switch (wire) {
  case 0:
    return varint(message.bytes, message.size, at, value);
  case 1:
  case 5: {
    const size_t fixed = wire == 1 ? 8 : 4;
    if (fixed > message.size - at)
      return false;
    at += fixed;
    return true;
  }
  case 2:
    if (!varint(message.bytes, message.size, at, value) || value > message.size - at)
      return false;
    at += static_cast<size_t>(value);
    return true;
  default:
    return false;
  }
}
bool validField(uint64_t field, unsigned wire) {
  return field != 0 && field <= 0x1fffffff && wire <= 5 && wire != 3 && wire != 4 &&
         (field != 1 || wire == 0);
}
bool parseSetupFields(const Message &message, ParsedSetup &parsed) {
  size_t at = 2;
  while (at < message.size) {
    uint64_t key = 0, value = 0;
    if (!varint(message.bytes, message.size, at, key))
      return false;
    const uint64_t field = key >> 3;
    const unsigned wire = key & 7;
    if (!validField(field, wire) || !readWireValue(wire, message, at, value))
      return false;
    if (field != 1)
      parsed.unknown_fields = true;
    else {
      parsed.present = true;
      parsed.result = value; // singular scalar: last value wins
      if (value > 6)
        parsed.unknown_enum = true; // fail closed even if a later duplicate is known
    }
  }
  return parsed.present;
}
bool setupRoute(SetupOperation operation, Channel &route, uint8_t &feature, uint8_t &action) {
  switch (operation) {
  case SetupOperation::PairingFinish:
    feature = 0x03;
    action = 0x81;
    route = Channel::Management;
    return true;
  case SetupOperation::ClaimExternalControl:
    feature = 0xf1;
    action = 0xe9;
    return true;
  default:
    return false;
  }
}
bool readIdentityFields(const Message &message, uint8_t id, OwnedField *fields, size_t count) {
  size_t at = 2;
  for (size_t f = 0; f < count; ++f) {
    if (at == message.size)
      return false;
    const size_t length = message.bytes[at++];
    if (length > message.size - at || (id == 0x51 && length == 0))
      return false;
    fields[f].offset = static_cast<uint16_t>(at);
    fields[f].size = static_cast<uint16_t>(length);
    at += length;
  }
  if (id == 0x3c)
    return message.size - at == 0 || message.size - at == 11;
  return at == message.size;
}
bool applyIdentity(IdentityResponse &response, const OwnedField *fields) {
  uint64_t model = 0, major = 0, minor = 0;
  if (response.id == 0x3c) {
    if (!unsignedBigEndian(response.raw, fields[0], model))
      return false;
    response.model = model;
    response.model_known = true;
  } else {
    if (!unsignedBigEndian(response.raw, fields[0], major) ||
        !unsignedBigEndian(response.raw, fields[1], minor))
      return false;
    response.api_major = major;
    response.api_minor = minor;
    response.api_known = true;
  }
  return true;
}
} // namespace

bool encodeSetup(SetupOperation operation, SetupPacket &packet, bool extended) {
  packet = SetupPacket();
  uint8_t payload[14] = {};
  size_t size = 0;
  switch (operation) {
  case SetupOperation::PairingFinish: {
    // Required enum zero is deliberately encoded; RideSync is a bounded local name.
    static const uint8_t name[] = {'R', 'i', 'd', 'e', 'S', 'y', 'n', 'c'};
    packet.channel = Channel::Management;
    packet.feature = 0x03;
    packet.action = 0x01;
    payload[0] = 0x03;
    payload[1] = 0x01;
    payload[2] = 0x08;
    payload[3] = 0x00;
    payload[4] = 0x12;
    payload[5] = sizeof(name);
    memcpy(payload + 6, name, sizeof(name));
    size = 6 + sizeof(name);
    break;
  }
  case SetupOperation::ClaimExternalControl:
    packet.channel = Channel::Command;
    packet.feature = 0xf1;
    packet.action = 0x69;
    payload[0] = 0xf1;
    payload[1] = 0x69;
    payload[2] = 0x08;
    payload[3] = 0x02;
    size = 4;
    break;
  default:
    return false;
  }
  const size_t header = extended ? 2 : 1;
  packet.bytes[0] = extended ? 0x20 : static_cast<uint8_t>(size);
  if (extended)
    packet.bytes[1] = static_cast<uint8_t>(size);
  memcpy(packet.bytes + header, payload, size);
  packet.size = header + size;
  return true;
}

SetupResponse decodeSetup(Channel channel, const Message &message, SetupOperation expected) {
  SetupResponse response;
  if (message.size > sizeof(message.bytes)) {
    response.outcome = Outcome::Oversize;
    return response;
  }
  response.raw.size = message.size;
  memcpy(response.raw.bytes, message.bytes, message.size);
  if (message.size < 2)
    return response;
  uint8_t feature = 0, action = 0;
  Channel route = Channel::Command;
  if (!setupRoute(expected, route, feature, action)) {
    response.outcome = Outcome::Unknown;
    return response;
  }
  if (channel != route || message.bytes[0] != feature || message.bytes[1] != action) {
    response.outcome = Outcome::Unknown;
    return response;
  }
  // Malformed tails never publish a result or setup success.
  ParsedSetup parsed;
  if (!parseSetupFields(message, parsed))
    return response;
  response.outcome = Outcome::Complete;
  response.domain = SetupResultDomain::GenericProtobuf;
  response.result_present = true;
  response.result = parsed.result;
  response.known_result = !parsed.unknown_enum && parsed.result <= 6;
  response.success = response.known_result && parsed.result == 1;
  response.unknown_fields = parsed.unknown_fields;
  return response;
}

IdentityResponse decodeIdentity(Channel channel, const Message &message) {
  IdentityResponse response;
  if (message.size > sizeof(message.bytes)) {
    response.outcome = Outcome::Oversize;
    return response;
  }
  response.raw.size = message.size;
  memcpy(response.raw.bytes, message.bytes, message.size);
  if (message.size < 2)
    return response;
  response.id = message.bytes[0];
  response.classic_result = message.bytes[1];
  if (channel != Channel::Command || (response.id != 0x3c && response.id != 0x51)) {
    response.outcome = Outcome::Unknown;
    return response;
  }
  if (response.classic_result != 0) {
    response.outcome = Outcome::Complete;
    return response;
  }
  const size_t count = response.id == 0x3c ? 7 : 2;
  OwnedField fields[7] = {};
  if (!readIdentityFields(message, response.id, fields, count) || !applyIdentity(response, fields))
    return response;
  memcpy(response.fields, fields, sizeof(fields));
  response.field_count = static_cast<uint8_t>(count);
  response.outcome = Outcome::Complete;
  return response;
}

} // namespace gopro
} // namespace ridesync

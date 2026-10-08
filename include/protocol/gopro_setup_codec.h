#pragma once
#include "protocol/gopro_codec.h"

namespace ridesync {
namespace gopro {

enum class SetupOperation { PairingFinish, ClaimExternalControl };
enum class SetupResultDomain { None, GenericProtobuf };

// Outbound pairing needs 15/16 bytes; classic Packet remains eight bytes.
struct SetupPacket {
  Channel channel = Channel::Command;
  uint8_t feature = 0, action = 0;
  uint8_t bytes[20] = {};
  size_t size = 0;
};

struct SetupResponse {
  Outcome outcome = Outcome::Invalid;
  SetupResultDomain domain = SetupResultDomain::None;
  bool result_present = false, known_result = false, success = false;
  bool unknown_fields = false;
  uint64_t result = 0;
  Message raw;
};

bool encodeSetup(SetupOperation operation, SetupPacket &packet, bool extended = true);
// Only the expected logical route and feature/action are eligible for completion.
SetupResponse decodeSetup(Channel channel, const Message &message, SetupOperation expected);

struct OwnedField {
  uint16_t offset = 0, size = 0; // byte slice into IdentityResponse::raw
};
struct IdentityResponse {
  Outcome outcome = Outcome::Invalid;
  uint8_t id = 0, classic_result = 0;
  Message raw;
  OwnedField fields[7];
  uint8_t field_count = 0;
  bool model_known = false, api_known = false;
  uint64_t model = 0, api_major = 0, api_minor = 0;
};
// Decodes the existing classic 3C/51 response body without assuming an installed identity.
IdentityResponse decodeIdentity(Channel channel, const Message &message);

} // namespace gopro
} // namespace ridesync

// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy was not distributed with this file, obtain one
// at https://mozilla.org/MPL/2.0/.
// Layout attributed to arsfabula/Insta360-Remote-CIQ, BLE Barrel/BLEBarrel.mc,
// cmdStartRec/cmdStopRec/sendCMD/onCharacteristicChanged at
// 39c51b3aa7c453227831d811355899371bbb8b94.
// Validation is RideSync policy. Unknown bytes remain opaque source literals.
#include "protocol/insta360_be80_codec.h"
namespace ridesync {
namespace insta360 {
Be80CommandResult encodeRecordingCommand(const Be80ControlConfig &config,
                                         Be80RecordingCommand command, uint8_t sequence) {
  Be80CommandResult result;
  if (config.profile == Be80ControlProfile::Disabled)
    return result;
  if (config.profile != Be80ControlProfile::GarminBe80ControlV1) {
    result.error = Be80CommandError::UnsupportedProfile;
    return result;
  }
  if (command != Be80RecordingCommand::StartVideo && command != Be80RecordingCommand::Stop) {
    result.error = Be80CommandError::InvalidCommand;
    return result;
  }
  if (sequence == 0 || sequence == 255) {
    result.error = Be80CommandError::InvalidSequence;
    return result;
  }
  // Source cmdStartRec: sequence at byte10 is overwritten by sendCMD. Fixed
  // source cmdStopRec differs only at bytes7 and16; no receive schema inferred.
  result.bytes = {{0x12, 0, 0, 0, 4, 0, 0, 4, 0, 2, sequence, 0, 0, 0x80, 0, 0, 8, 1}};
  if (command == Be80RecordingCommand::Stop) {
    result.bytes[7] = 5;
    result.bytes[16] = 0x10;
  }
  result.error = Be80CommandError::None;
  result.size = result.bytes.size();
  return result;
}
Be80EnvelopeResult decodeBe80Envelope(const Be80ControlConfig &config, Be80Direction direction,
                                      const uint8_t *data, size_t size) {
  Be80EnvelopeResult result;
  if (config.profile == Be80ControlProfile::Disabled)
    return result;
  if (config.profile != Be80ControlProfile::GarminBe80ControlV1) {
    result.error = Be80EnvelopeError::UnsupportedProfile;
    return result;
  }
  if (direction != Be80Direction::CameraToRemote) {
    result.error = Be80EnvelopeError::WrongDirection;
    return result;
  }
  if (size < 18 || size > result.bytes.size()) {
    result.error = Be80EnvelopeError::InvalidSize;
    return result;
  }
  if (!data) {
    result.error = Be80EnvelopeError::InvalidBuffer;
    return result;
  }
  // onCharacteristicChanged documents the first two bytes as total length,
  // little endian, and responseSig at bytes2..6. No further schema is inferred.
  const size_t declared = static_cast<size_t>(data[0]) | (static_cast<size_t>(data[1]) << 8);
  if (declared != size) {
    result.error = Be80EnvelopeError::LengthMismatch;
    return result;
  }
  const uint8_t signature[5] = {0, 0, 4, 0, 0};
  for (size_t i = 0; i < sizeof signature; ++i) {
    if (data[i + 2] != signature[i]) {
      result.error = Be80EnvelopeError::UnsupportedHeader;
      return result;
    }
  }
  for (size_t i = 0; i < size; ++i)
    result.bytes[i] = data[i];
  result.size = size;
  result.error = Be80EnvelopeError::None;
  return result;
}
} // namespace insta360
} // namespace ridesync

// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy was not distributed with this file, obtain one
// at https://mozilla.org/MPL/2.0/.
// Layout attributed to arsfabula/Insta360-Remote-CIQ, BLE Barrel/BLEBarrel.mc,
// cmdStartRec/cmdStopRec/sendCMD at 39c51b3aa7c453227831d811355899371bbb8b94.
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
} // namespace insta360
} // namespace ridesync

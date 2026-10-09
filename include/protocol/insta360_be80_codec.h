// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy was not distributed with this file, obtain one
// at https://mozilla.org/MPL/2.0/.
// Wire-derived from arsfabula/Insta360-Remote-CIQ, BLE Barrel/BLEBarrel.mc,
// cmdStartRec/cmdStopRec/sendCMD at 39c51b3aa7c453227831d811355899371bbb8b94.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace ridesync {
namespace insta360 {
// Source-qualified wire format, not a model/firmware capability declaration.
enum class Be80ControlProfile : uint8_t { Disabled, GarminBe80ControlV1 };
struct Be80ControlConfig {
  Be80ControlProfile profile = Be80ControlProfile::Disabled;
};
enum class Be80RecordingCommand : uint8_t { StartVideo, Stop };
enum class Be80CommandError : uint8_t {
  None,
  Disabled,
  UnsupportedProfile,
  InvalidCommand,
  InvalidSequence
};
struct Be80CommandResult {
  Be80CommandError error = Be80CommandError::Disabled;
  size_t size = 0;
  std::array<uint8_t, 18> bytes{};
};
// Pure request encoding. Caller owns sequence1..254 and delivery uncertainty.
// Does not send, retry, observe state, or assert camera-level idempotence.
// Disabled/unknown profile, invalid command, then invalid sequence are checked
// in that order. Every error returns size0 and all-zero fixed storage.
Be80CommandResult encodeRecordingCommand(const Be80ControlConfig &, Be80RecordingCommand,
                                         uint8_t sequence);
} // namespace insta360
} // namespace ridesync

// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy was not distributed with this file, obtain one
// at https://mozilla.org/MPL/2.0/.
// Wire-derived from arsfabula/Insta360-Remote-CIQ, BLE Barrel/BLEBarrel.mc,
// cmdStartRec/cmdStopRec/sendCMD/onCharacteristicChanged at
// 39c51b3aa7c453227831d811355899371bbb8b94.
#pragma once
#include "camera_profile.h"
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

// Only the pinned reference's >=18-byte response-envelope family. Short
// keepalives and other formats are not supported; 256 is a local storage cap.
constexpr size_t kBe80EnvelopeCapacity = 256;
enum class Be80Direction : uint8_t { CameraToRemote, RemoteToCamera };
enum class Be80EnvelopeError : uint8_t {
  None,
  Disabled,
  UnsupportedProfile,
  WrongDirection,
  InvalidSize,
  InvalidBuffer,
  LengthMismatch,
  UnsupportedHeader
};
struct Be80EnvelopeResult {
  Be80EnvelopeError error = Be80EnvelopeError::Disabled;
  size_t size = 0;
  RecordingState recording = RecordingState::Unknown;
  std::array<uint8_t, kBe80EnvelopeCapacity> bytes{};
};
// Caller supplies one complete frame and its independently known route. This
// stateless decoder does not assemble fragments or authenticate their origin.
// It validates little-endian total length at bytes0..1 and source responseSig
// at bytes2..6. Remaining bytes, including command/sequence, stay opaque.
// Success is an owned envelope, never an ACK or recording observation.
// Errors expose size0/all-zero storage/Unknown. Precedence: profile, direction,
// size18..256, buffer, total length, signature. Size is checked before dereference.
Be80EnvelopeResult decodeBe80Envelope(const Be80ControlConfig &, Be80Direction, const uint8_t *,
                                      size_t);
} // namespace insta360
} // namespace ridesync

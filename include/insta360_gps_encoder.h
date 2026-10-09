// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy was not distributed with this file, obtain one
// at https://mozilla.org/MPL/2.0/.
// Wire profile derived from arsfabula/Insta360-Remote-CIQ, BLE Barrel/BLEBarrel.mc
// at 39c51b3aa7c453227831d811355899371bbb8b94. See docs/gps_protocol.md.
#pragma once
#include "modem_gnss.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace ridesync {
namespace insta360 {
enum class GpsWireProfile { Disabled, GarminBe80VideoV1 };
struct GpsEncoderConfig {
  GpsWireProfile profile = GpsWireProfile::Disabled;
  uint32_t max_age_ms = 0;
};
enum class GpsEncodingError {
  None,
  Disabled,
  InvalidConfig,
  InvalidSequence,
  InvalidClock,
  InvalidFix,
  MissingField,
  InvalidCoordinate,
  InvalidUtc,
  InvalidMetric,
  UnsupportedAltitude
};
struct GpsEncodingResult {
  GpsEncodingError error = GpsEncodingError::Disabled;
  size_t size = 0;
  std::array<uint8_t, 71> bytes{};
};
// Pure experimental encoding of copied observations. No transport or sequence
// ownership; source-derived software does not qualify a camera model. Errors
// return size zero and all-zero bytes. Required fields are never synthesized.
GpsEncodingResult encodeGps(const GpsEncoderConfig &, const RecordTimestamp &now,
                            const ModemSnapshot &, uint8_t sequence);
} // namespace insta360
} // namespace ridesync

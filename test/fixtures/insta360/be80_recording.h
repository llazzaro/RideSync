// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy was not distributed with this file, obtain one
// at https://mozilla.org/MPL/2.0/.
// Independently specified wire literals from arsfabula/Insta360-Remote-CIQ,
// BLE Barrel/BLEBarrel.mc, cmdStartRec/cmdStopRec and sendCMD at
// 39c51b3aa7c453227831d811355899371bbb8b94. Source-derived, not captures.
#pragma once
#include <cstdint>
namespace insta360_fixture {
// 18-byte StartVideo request with caller sequence1 at offset10.
constexpr uint8_t kBe80StartVideo[18] = {0x12, 0, 0, 0, 4,    0, 0, 4, 0,
                                         2,    1, 0, 0, 0x80, 0, 0, 8, 1};
// 18-byte Stop request with caller sequence254 at offset10.
constexpr uint8_t kBe80Stop[18] = {0x12, 0, 0, 0, 4, 0, 0, 5, 0, 2, 254, 0, 0, 0x80, 0, 0, 0x10, 1};
} // namespace insta360_fixture

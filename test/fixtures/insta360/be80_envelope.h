// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy was not distributed with this file, obtain one
// at https://mozilla.org/MPL/2.0/.
// Header facts: arsfabula/Insta360-Remote-CIQ, BLE Barrel/BLEBarrel.mc,
// onCharacteristicChanged at 39c51b3aa7c453227831d811355899371bbb8b94.
// SYNTHETIC source-specified envelope, not a capture or known state message.
// Bytes7..17 deliberately arbitrary; no semantics or incoming sequence assigned.
#pragma once
#include <cstdint>
namespace insta360_fixture {
constexpr uint8_t kBe80Envelope[18] = {0x12, 0,    0,    0,    4,    0,    0,    0x10, 0xa5,
                                       0,    0xff, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
} // namespace insta360_fixture

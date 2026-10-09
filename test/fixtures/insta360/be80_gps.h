// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy was not distributed with this file, obtain one
// at https://mozilla.org/MPL/2.0/.
// Layout derived from arsfabula/Insta360-Remote-CIQ, BLE Barrel/BLEBarrel.mc,
// sendPosition/sendCMD at 39c51b3aa7c453227831d811355899371bbb8b94.
// Synthetic, independently calculated values; neither packet is a camera capture.
#pragma once
#include <cstdint>
namespace insta360_fixture {
// UTC 2000-01-01T00:00:00Z = 946684800 seconds, LE 80 43 6d 38.
// Prefix 0..17; UTC 18..21; opaque 22..28; lat 29..36/N37;
// lon 38..45/W46; speed 47..54; course 55..62; altitude 63..70.
// lat1, lon-2, speed3 m/s, course90 degrees, altitude4 m; sequence1.
constexpr uint8_t kGpsNorthWest[71] = {
    0x47, 0,    0,    0,    4, 0, 0,    0x35, 0,    2,    1,    0, 0, 0x80, 0, 0,    0x0a, 0x35,
    0x80, 0x43, 0x6d, 0x38, 0, 0, 0,    0,    0,    0,    0x41, 0, 0, 0,    0, 0,    0,    0xf0,
    0x3f, 'N',  0,    0,    0, 0, 0,    0,    0,    0x40, 'W',  0, 0, 0,    0, 0,    0,    8,
    0x40, 0,    0,    0,    0, 0, 0x80, 0x56, 0x40, 0,    0,    0, 0, 0,    0, 0x10, 0x40};
// lat-1, lon2, same metrics/UTC; sequence254. Full independent literal.
constexpr uint8_t kGpsSouthEast[71] = {
    0x47, 0,    0,    0,    4, 0, 0,    0x35, 0,    2,    254,  0, 0, 0x80, 0, 0,    0x0a, 0x35,
    0x80, 0x43, 0x6d, 0x38, 0, 0, 0,    0,    0,    0,    0x41, 0, 0, 0,    0, 0,    0,    0xf0,
    0x3f, 'S',  0,    0,    0, 0, 0,    0,    0,    0x40, 'E',  0, 0, 0,    0, 0,    0,    8,
    0x40, 0,    0,    0,    0, 0, 0x80, 0x56, 0x40, 0,    0,    0, 0, 0,    0, 0x10, 0x40};
} // namespace insta360_fixture

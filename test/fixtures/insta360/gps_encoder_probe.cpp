// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy was not distributed with this file, obtain one
// at https://mozilla.org/MPL/2.0/.
// Wire-derived synthetic probe for arsfabula/Insta360-Remote-CIQ layout at
// 39c51b3aa7c453227831d811355899371bbb8b94, sendPosition/sendCMD.
// Independent caller; no production/test packet helpers or captured locations.
#include "insta360_gps_encoder.h"
#include <cmath>
#include <cstdio>
#include <cstring>
int main(int argc, char **argv) {
  using namespace ridesync;
  using namespace ridesync::insta360;
  if (argc != 2)
    return 2;
  GpsEncoderConfig config;
  config.profile = GpsWireProfile::GarminBe80VideoV1;
  config.max_age_ms = 1000;
  RecordTimestamp now;
  now.session_id = 7;
  now.monotonic_quality = MonotonicQuality::Valid;
  now.monotonic_ms = 2000;
  ModemSnapshot snapshot;
  snapshot.session_id = 7;
  snapshot.validity = FixValidity::Valid;
  snapshot.age_available = true;
  snapshot.age_ms = 1000;
  auto &f = snapshot.fix;
  f.valid = true;
  f.receipt_monotonic_ms = 1000;
  f.utc_date.available = f.utc_time.available = true;
  f.utc_date.value.year = 2000;
  f.utc_date.value.month = f.utc_date.value.day = 1;
  f.speed_metres_per_second.available = f.course_degrees.available = true;
  f.altitude_msl_metres.available = true;
  f.latitude_degrees = 1;
  f.longitude_degrees = -2;
  f.speed_metres_per_second.value = 3;
  f.course_degrees.value = 90;
  f.altitude_msl_metres.value = 4;
  if (std::strcmp(argv[1], "south-east") == 0) {
    f.latitude_degrees = -1;
    f.longitude_degrees = 2;
  } else if (std::strcmp(argv[1], "zero") == 0) {
    f.latitude_degrees = f.longitude_degrees = f.speed_metres_per_second.value =
        f.course_degrees.value = f.altitude_msl_metres.value = -0.0;
  } else if (std::strcmp(argv[1], "subnormal") == 0) {
    f.speed_metres_per_second.value = f.altitude_msl_metres.value = std::ldexp(1.0, -149);
  } else if (std::strcmp(argv[1], "precision") == 0) {
    f.latitude_degrees = 1.1;
    f.longitude_degrees = -2.2;
    f.speed_metres_per_second.value = 3.3;
    f.course_degrees.value = 90.1;
    f.altitude_msl_metres.value = 4.4;
  } else if (std::strcmp(argv[1], "ordinary") != 0) {
    return 2;
  }
  const auto result = encodeGps(config, now, snapshot, 1);
  if (result.error != GpsEncodingError::None || result.size != 71)
    return 1;
  for (uint8_t byte : result.bytes)
    std::printf("%02x", static_cast<unsigned>(byte));
  std::printf("\n");
  return 0;
}

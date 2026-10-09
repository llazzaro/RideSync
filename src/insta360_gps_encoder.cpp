// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy was not distributed with this file, obtain one
// at https://mozilla.org/MPL/2.0/.
// Layout/ordinary precision derived from arsfabula/Insta360-Remote-CIQ,
// BLE Barrel/BLEBarrel.mc, sendPosition/toDouble/sendCMD at
// 39c51b3aa7c453227831d811355899371bbb8b94. New validation/calendar/IEEE
// conversion are RideSync policy; no camera qualification is implied.
#include "insta360_gps_encoder.h"
#include <cmath>
#include <cstring>
#include <limits>
namespace ridesync {
namespace insta360 {
namespace {
static_assert(sizeof(float) == 4 && sizeof(double) == 8, "GPS needs binary32/binary64 widths");
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<float>::radix == 2 &&
                  std::numeric_limits<double>::radix == 2 &&
                  std::numeric_limits<float>::digits == 24 &&
                  std::numeric_limits<double>::digits == 53,
              "GPS needs IEEE binary32/binary64");
static_assert(std::tuple_size<decltype(GpsEncodingResult::bytes)>::value == 71,
              "GPS packet must remain 71 bytes");
GpsEncodingResult failure(GpsEncodingError error) {
  GpsEncodingResult result;
  result.error = error;
  return result;
}
bool leap(uint16_t year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
bool utcSeconds(const GnssDate &date, const GnssTime &time, uint32_t &seconds) {
  if (date.year < 2000 || date.year > 2099 || date.month < 1 || date.month > 12 || date.day < 1 ||
      time.hour > 23 || time.minute > 59 || time.second > 59 || time.centisecond > 99)
    return false;
  constexpr uint8_t months[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const unsigned daysInMonth =
      months[date.month - 1] + (date.month == 2 && leap(date.year) ? 1 : 0);
  if (date.day > daysInMonth)
    return false;
  uint64_t days = 10957; // Days from Unix epoch to 2000-01-01.
  for (uint16_t year = 2000; year < date.year; ++year)
    days += leap(year) ? 366 : 365;
  for (uint8_t month = 1; month < date.month; ++month)
    days += months[month - 1] + (month == 2 && leap(date.year) ? 1 : 0);
  days += date.day - 1;
  const uint64_t value =
      days * 86400 + uint64_t(time.hour) * 3600 + uint64_t(time.minute) * 60 + time.second;
  if (value > std::numeric_limits<uint32_t>::max())
    return false;
  seconds = static_cast<uint32_t>(value);
  return true;
}
bool narrowScalar(double value, double &wire) {
  if (std::fabs(value) > std::numeric_limits<float>::max())
    return false;
  const float f = static_cast<float>(value);
  if (!std::isfinite(f) || (value != 0 && f == 0))
    return false;
  wire = f == 0 ? 0.0 : static_cast<double>(f);
  return true;
}
void writeLe(uint8_t *out, uint64_t bits, size_t count) {
  for (size_t n = 0; n < count; ++n)
    out[n] = static_cast<uint8_t>(bits >> (8 * n));
}
void writeDouble(uint8_t *out, double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  writeLe(out, bits, 8);
}
} // namespace
GpsEncodingResult encodeGps(const GpsEncoderConfig &config, const RecordTimestamp &now,
                            const ModemSnapshot &snapshot, uint8_t sequence) {
  if (config.profile == GpsWireProfile::Disabled)
    return failure(GpsEncodingError::Disabled);
  if (config.profile != GpsWireProfile::GarminBe80VideoV1 || config.max_age_ms == 0 ||
      config.max_age_ms > 60000)
    return failure(GpsEncodingError::InvalidConfig);
  if (sequence == 0 || sequence == 255)
    return failure(GpsEncodingError::InvalidSequence);
  if (now.session_id == 0 || now.monotonic_quality != MonotonicQuality::Valid ||
      now.monotonic_ms > SessionClock::kMaxDurationMs)
    return failure(GpsEncodingError::InvalidClock);
  const auto &fix = snapshot.fix;
  if (snapshot.session_id != now.session_id || snapshot.validity != FixValidity::Valid ||
      !fix.valid || !snapshot.age_available || fix.receipt_monotonic_ms > now.monotonic_ms)
    return failure(GpsEncodingError::InvalidFix);
  const uint64_t age = now.monotonic_ms - fix.receipt_monotonic_ms;
  if (age != snapshot.age_ms || age > config.max_age_ms)
    return failure(GpsEncodingError::InvalidFix);
  const double lat = fix.latitude_degrees, lon = fix.longitude_degrees;
  if (!std::isfinite(lat) || !std::isfinite(lon) || lat < -90 || lat > 90 || lon < -180 ||
      lon > 180)
    return failure(GpsEncodingError::InvalidCoordinate);
  if (!fix.utc_date.available || !fix.utc_time.available ||
      !fix.speed_metres_per_second.available || !fix.course_degrees.available ||
      !fix.altitude_msl_metres.available)
    return failure(GpsEncodingError::MissingField);
  uint32_t seconds = 0;
  if (!utcSeconds(fix.utc_date.value, fix.utc_time.value, seconds))
    return failure(GpsEncodingError::InvalidUtc);
  const double speed = fix.speed_metres_per_second.value, course = fix.course_degrees.value,
               altitude = fix.altitude_msl_metres.value;
  if (!std::isfinite(speed) || speed < 0 || !std::isfinite(course) || course < 0 || course >= 360 ||
      !std::isfinite(altitude))
    return failure(GpsEncodingError::InvalidMetric);
  if (altitude < 0)
    return failure(GpsEncodingError::UnsupportedAltitude);
  double wireLat = 0, wireLon = 0, wireSpeed = 0, wireCourse = 0, wireAltitude = 0;
  if (!narrowScalar(std::fabs(lat), wireLat) || !narrowScalar(std::fabs(lon), wireLon) ||
      !narrowScalar(speed, wireSpeed) || !narrowScalar(course, wireCourse) ||
      !narrowScalar(altitude, wireAltitude) || wireCourse >= 360)
    return failure(GpsEncodingError::InvalidMetric);
  GpsEncodingResult result;
  constexpr uint8_t prefix[] = {0x47, 0, 0, 0, 4,    0, 0, 0x35, 0,
                                2,    0, 0, 0, 0x80, 0, 0, 0x0a, 0x35};
  std::memcpy(result.bytes.data(), prefix, sizeof(prefix));
  result.bytes[10] = sequence;
  writeLe(result.bytes.data() + 18, seconds, 4);
  result.bytes[28] = 0x41; // Opaque filler, other bytes 22..27 remain zero.
  writeDouble(result.bytes.data() + 29, wireLat);
  result.bytes[37] = lat < 0 ? 'S' : 'N';
  writeDouble(result.bytes.data() + 38, wireLon);
  result.bytes[46] = lon < 0 ? 'W' : 'E';
  writeDouble(result.bytes.data() + 47, wireSpeed);
  writeDouble(result.bytes.data() + 55, wireCourse);
  writeDouble(result.bytes.data() + 63, wireAltitude);
  result.error = GpsEncodingError::None;
  result.size = result.bytes.size();
  return result;
}
} // namespace insta360
} // namespace ridesync

// Link-only data anchor: retaining the pure encoder does not call it or enable
// a camera capability. The opt-in target adds -u for this symbol explicitly.
extern "C" {
extern ridesync::insta360::GpsEncodingResult (*const ridesync_insta360_gps_encoder_backend)(
    const ridesync::insta360::GpsEncoderConfig &, const ridesync::RecordTimestamp &,
    const ridesync::ModemSnapshot &, uint8_t) = &ridesync::insta360::encodeGps;
}

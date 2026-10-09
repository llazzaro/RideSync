#include "../fixtures/insta360/be80_gps.h"
#include "insta360_gps_encoder.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <unity.h>
using namespace ridesync;
using namespace ridesync::insta360;
struct Inputs {
  GpsEncoderConfig config;
  RecordTimestamp now;
  ModemSnapshot snapshot;
};
Inputs validInputs() {
  Inputs i;
  i.config.profile = GpsWireProfile::GarminBe80VideoV1;
  i.config.max_age_ms = 1000;
  i.now.session_id = 7;
  i.now.monotonic_quality = MonotonicQuality::Valid;
  i.now.monotonic_ms = 2000;
  i.snapshot.session_id = 7;
  i.snapshot.validity = FixValidity::Valid;
  i.snapshot.age_available = true;
  i.snapshot.age_ms = 1000;
  auto &f = i.snapshot.fix;
  f.valid = true;
  f.receipt_monotonic_ms = 1000;
  f.latitude_degrees = 1;
  f.longitude_degrees = -2;
  f.utc_date.available = f.utc_time.available = true;
  f.utc_date.value.year = 2000;
  f.utc_date.value.month = f.utc_date.value.day = 1;
  f.speed_metres_per_second.available = f.course_degrees.available = true;
  f.altitude_msl_metres.available = true;
  f.speed_metres_per_second.value = 3;
  f.course_degrees.value = 90;
  f.altitude_msl_metres.value = 4;
  return i;
}
GpsEncodingResult success(const Inputs &i, uint8_t sequence = 1) {
  const auto r = encodeGps(i.config, i.now, i.snapshot, sequence);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GpsEncodingError::None), static_cast<int>(r.error));
  TEST_ASSERT_EQUAL_UINT(71, r.size);
  return r;
}
void expectError(const Inputs &i, GpsEncodingError e, uint8_t sequence = 1) {
  const auto r = encodeGps(i.config, i.now, i.snapshot, sequence);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(e), static_cast<int>(r.error));
  TEST_ASSERT_EQUAL_UINT(0, r.size);
  const uint8_t zero[71] = {};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, r.bytes.data(), 71);
}
// Independent little-endian test decoder, no production helpers.
double scalar(const GpsEncodingResult &r, size_t offset) {
  uint64_t bits = 0;
  for (size_t n = 0; n < 8; ++n)
    bits |= uint64_t(r.bytes[offset + n]) << (8 * n);
  double value = 0;
  std::memcpy(&value, &bits, 8);
  return value;
}
uint32_t epoch(const GpsEncodingResult &r) {
  return uint32_t(r.bytes[18]) | (uint32_t(r.bytes[19]) << 8) | (uint32_t(r.bytes[20]) << 16) |
         (uint32_t(r.bytes[21]) << 24);
}
void literal_packets_both_hemispheres_and_sequence_endpoints() {
  auto i = validInputs();
  auto r = success(i);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(insta360_fixture::kGpsNorthWest, r.bytes.data(), 71);
  i.snapshot.fix.latitude_degrees = -1;
  i.snapshot.fix.longitude_degrees = 2;
  r = success(i, 254);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(insta360_fixture::kGpsSouthEast, r.bytes.data(), 71);
}
void config_and_sequence_rejections_are_atomic() {
  auto i = validInputs();
  i.config = GpsEncoderConfig{};
  expectError(i, GpsEncodingError::Disabled, 0);
  i.config.profile = static_cast<GpsWireProfile>(99);
  expectError(i, GpsEncodingError::InvalidConfig, 0);
  i = validInputs();
  i.config.max_age_ms = 0;
  expectError(i, GpsEncodingError::InvalidConfig);
  i.config.max_age_ms = 60001;
  expectError(i, GpsEncodingError::InvalidConfig);
  i = validInputs();
  expectError(i, GpsEncodingError::InvalidSequence, 0);
  expectError(i, GpsEncodingError::InvalidSequence, 255);
  i.config.max_age_ms = 60000;
  success(i);
  i.snapshot.age_ms = 1;
  i.snapshot.fix.receipt_monotonic_ms = 1999;
  i.config.max_age_ms = 1;
  success(i);
}
void invalid_clocks_fail_before_fix() {
  auto i = validInputs();
  i.now.session_id = 0;
  expectError(i, GpsEncodingError::InvalidClock);
  for (auto q : {MonotonicQuality::InvalidSession, MonotonicQuality::DurationExceeded}) {
    i = validInputs();
    i.now.monotonic_quality = q;
    expectError(i, GpsEncodingError::InvalidClock);
  }
  i = validInputs();
  i.now.monotonic_ms = SessionClock::kMaxDurationMs + 1;
  expectError(i, GpsEncodingError::InvalidClock);
  --i.now.monotonic_ms;
  i.snapshot.fix.receipt_monotonic_ms = i.now.monotonic_ms - 1000;
  success(i);
}
void copied_fix_validity_and_checked_age() {
  for (auto v :
       {FixValidity::Missing, FixValidity::NoFix, FixValidity::Invalid, FixValidity::Stale}) {
    auto i = validInputs();
    i.snapshot.validity = v;
    expectError(i, GpsEncodingError::InvalidFix);
  }
  auto i = validInputs();
  i.snapshot.fix.valid = false;
  expectError(i, GpsEncodingError::InvalidFix);
  i = validInputs();
  i.snapshot.age_available = false;
  expectError(i, GpsEncodingError::InvalidFix);
  i = validInputs();
  i.snapshot.session_id = 8;
  expectError(i, GpsEncodingError::InvalidFix);
  i = validInputs();
  i.snapshot.fix.receipt_monotonic_ms = 2001;
  expectError(i, GpsEncodingError::InvalidFix);
  i = validInputs();
  i.snapshot.age_ms = 999;
  expectError(i, GpsEncodingError::InvalidFix);
  i = validInputs();
  success(i);
  ++i.now.monotonic_ms;
  ++i.snapshot.age_ms;
  expectError(i, GpsEncodingError::InvalidFix);
  i = validInputs();
  i.now.monotonic_ms += uint64_t(1) << 32;
  i.snapshot.fix.receipt_monotonic_ms += uint64_t(1) << 32;
  success(i);
  i = validInputs();
  i.snapshot.fix.receipt_monotonic_ms = 0;
  i.snapshot.age_ms = 2000;
  i.config.max_age_ms = 2000;
  success(i);
}
void coordinate_limits_and_nonfinite_values() {
  for (double sign : {-1.0, 1.0}) {
    auto i = validInputs();
    i.snapshot.fix.latitude_degrees = sign * 90;
    i.snapshot.fix.longitude_degrees = sign * 180;
    auto r = success(i);
    TEST_ASSERT_EQUAL_INT(sign < 0 ? 'S' : 'N', r.bytes[37]);
    TEST_ASSERT_EQUAL_INT(sign < 0 ? 'W' : 'E', r.bytes[46]);
    TEST_ASSERT_TRUE(scalar(r, 29) == 90);
    TEST_ASSERT_TRUE(scalar(r, 38) == 180);
    i.snapshot.fix.latitude_degrees = std::nextafter(sign * 90, sign * 100);
    expectError(i, GpsEncodingError::InvalidCoordinate);
    i = validInputs();
    i.snapshot.fix.longitude_degrees = std::nextafter(sign * 180, sign * 200);
    expectError(i, GpsEncodingError::InvalidCoordinate);
  }
  for (double v :
       {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()}) {
    auto i = validInputs();
    i.snapshot.fix.latitude_degrees = v;
    expectError(i, GpsEncodingError::InvalidCoordinate);
    i = validInputs();
    i.snapshot.fix.longitude_degrees = v;
    expectError(i, GpsEncodingError::InvalidCoordinate);
  }
}
void every_missing_required_field_combination() {
  for (unsigned mask = 1; mask < 32; ++mask) {
    auto i = validInputs();
    auto &f = i.snapshot.fix;
    f.utc_date.available = !(mask & 1);
    f.utc_time.available = !(mask & 2);
    f.speed_metres_per_second.available = !(mask & 4);
    f.course_degrees.available = !(mask & 8);
    f.altitude_msl_metres.available = !(mask & 16);
    expectError(i, GpsEncodingError::MissingField);
  }
}
void gregorian_dates_and_independent_epochs() {
  struct DateCase {
    uint16_t y;
    uint8_t m, d;
    uint32_t seconds;
  };
  const DateCase cases[] = {{2000, 1, 1, 946684800},
                            {2000, 2, 29, 951782400},
                            {2004, 2, 29, 1078012800},
                            {2099, 12, 31, 4102358400u}};
  for (const auto &c : cases) {
    auto i = validInputs();
    auto &d = i.snapshot.fix.utc_date.value;
    d.year = c.y;
    d.month = c.m;
    d.day = c.d;
    TEST_ASSERT_EQUAL_UINT32(c.seconds, epoch(success(i)));
  }
  const DateCase bad[] = {{1999, 1, 1, 0}, {2100, 1, 1, 0},  {2000, 0, 1, 0},  {2000, 13, 1, 0},
                          {2000, 1, 0, 0}, {2000, 4, 31, 0}, {2001, 2, 29, 0}, {2000, 2, 30, 0}};
  for (const auto &c : bad) {
    auto i = validInputs();
    auto &d = i.snapshot.fix.utc_date.value;
    d.year = c.y;
    d.month = c.m;
    d.day = c.d;
    expectError(i, GpsEncodingError::InvalidUtc);
  }
}
void source_utc_time_bounds_and_centisecond_omission() {
  for (int n = 0; n < 4; ++n) {
    auto i = validInputs();
    auto &t = i.snapshot.fix.utc_time.value;
    if (n == 0)
      t.hour = 24;
    if (n == 1)
      t.minute = 60;
    if (n == 2)
      t.second = 60;
    if (n == 3)
      t.centisecond = 100;
    expectError(i, GpsEncodingError::InvalidUtc);
  }
  auto i = validInputs();
  auto baseline = success(i);
  i.snapshot.fix.utc_time.value.centisecond = 99;
  i.now.has_utc_estimate = true;
  i.now.utc_estimate_ms = -123;
  i.now.anchor.utc_ms = 456;
  i.now.anchor_quality = AnchorQuality::Fresh;
  auto r = success(i);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(baseline.bytes.data(), r.bytes.data(), 71);
  i.snapshot.fix.utc_date.value.year = 2099;
  i.snapshot.fix.utc_date.value.month = 12;
  i.snapshot.fix.utc_date.value.day = 31;
  auto &t = i.snapshot.fix.utc_time.value;
  t.hour = 23;
  t.minute = 59;
  t.second = 59;
  TEST_ASSERT_EQUAL_UINT32(4102444799u, epoch(success(i)));
}
double &field(Inputs &i, unsigned n) {
  auto &f = i.snapshot.fix;
  if (n == 0)
    return f.latitude_degrees;
  if (n == 1)
    return f.longitude_degrees;
  if (n == 2)
    return f.speed_metres_per_second.value;
  if (n == 3)
    return f.course_degrees.value;
  return f.altitude_msl_metres.value;
}
void invalid_metrics_and_signed_altitude() {
  for (unsigned n = 2; n < 5; ++n) {
    for (double v :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity()}) {
      auto i = validInputs();
      field(i, n) = v;
      expectError(i, GpsEncodingError::InvalidMetric);
    }
  }
  auto i = validInputs();
  field(i, 2) = -1;
  expectError(i, GpsEncodingError::InvalidMetric);
  i = validInputs();
  field(i, 3) = -1;
  expectError(i, GpsEncodingError::InvalidMetric);
  field(i, 3) = 360;
  expectError(i, GpsEncodingError::InvalidMetric);
  i = validInputs();
  field(i, 4) = -1;
  expectError(i, GpsEncodingError::UnsupportedAltitude);
}
void narrowing_rejects_lost_values_and_course_rounding() {
  for (unsigned n = 0; n < 5; ++n) {
    auto i = validInputs();
    field(i, n) = std::numeric_limits<double>::denorm_min();
    expectError(i, GpsEncodingError::InvalidMetric);
  }
  for (unsigned n : {2u, 4u}) {
    auto i = validInputs();
    field(i, n) = std::numeric_limits<double>::max();
    expectError(i, GpsEncodingError::InvalidMetric);
    field(i, n) = std::numeric_limits<float>::max();
    auto r = success(i);
    TEST_ASSERT_TRUE(scalar(r, n == 2 ? 47 : 63) == double(std::numeric_limits<float>::max()));
  }
  auto i = validInputs();
  field(i, 3) = std::nextafter(360.0, 0.0);
  expectError(i, GpsEncodingError::InvalidMetric);
  // Range-stage errors take precedence over narrowing for coordinates/course.
  i = validInputs();
  field(i, 0) = std::numeric_limits<double>::max();
  expectError(i, GpsEncodingError::InvalidCoordinate);
  i = validInputs();
  field(i, 1) = std::numeric_limits<double>::max();
  expectError(i, GpsEncodingError::InvalidCoordinate);
  i = validInputs();
  field(i, 3) = std::numeric_limits<double>::max();
  expectError(i, GpsEncodingError::InvalidMetric);
}
void zeros_subnormals_and_binary32_precision() {
  const size_t offsets[] = {29, 38, 47, 55, 63};
  for (double zero : {0.0, -0.0}) {
    auto i = validInputs();
    for (unsigned n = 0; n < 5; ++n)
      field(i, n) = zero;
    auto r = success(i);
    const uint8_t bytes[8] = {};
    for (auto offset : offsets)
      TEST_ASSERT_EQUAL_HEX8_ARRAY(bytes, r.bytes.data() + offset, 8);
    TEST_ASSERT_EQUAL_INT('N', r.bytes[37]);
    TEST_ASSERT_EQUAL_INT('E', r.bytes[46]);
  }
  for (unsigned n = 0; n < 5; ++n) {
    auto i = validInputs();
    field(i, n) = std::ldexp(1.0, -149);
    TEST_ASSERT_TRUE(scalar(success(i), offsets[n]) == std::ldexp(1.0, -149));
    field(i, n) = std::numeric_limits<float>::min();
    TEST_ASSERT_TRUE(scalar(success(i), offsets[n]) == double(std::numeric_limits<float>::min()));
    field(i, n) = 1.1;
    TEST_ASSERT_TRUE(scalar(success(i), offsets[n]) == 1.10000002384185791015625);
  }
}
void error_precedence_clears_all_bytes() {
  auto i = validInputs();
  i.config.profile = GpsWireProfile::Disabled;
  i.config.max_age_ms = 0;
  i.now.session_id = 0;
  i.snapshot.fix.valid = false;
  i.snapshot.fix.latitude_degrees = 91;
  i.snapshot.fix.utc_date.available = false;
  i.snapshot.fix.utc_time.value.hour = 24;
  field(i, 2) = -1;
  field(i, 4) = -1;
  expectError(i, GpsEncodingError::Disabled, 0);
  i.config.profile = static_cast<GpsWireProfile>(99);
  expectError(i, GpsEncodingError::InvalidConfig, 0);
  i.config.profile = GpsWireProfile::GarminBe80VideoV1;
  i.config.max_age_ms = 1000;
  expectError(i, GpsEncodingError::InvalidSequence, 0);
  expectError(i, GpsEncodingError::InvalidClock);
  i.now.session_id = 7;
  expectError(i, GpsEncodingError::InvalidFix);
  i.snapshot.fix.valid = true;
  expectError(i, GpsEncodingError::InvalidCoordinate);
  i.snapshot.fix.latitude_degrees = 1;
  expectError(i, GpsEncodingError::MissingField);
  i.snapshot.fix.utc_date.available = true;
  expectError(i, GpsEncodingError::InvalidUtc);
  i.snapshot.fix.utc_time.value.hour = 0;
  expectError(i, GpsEncodingError::InvalidMetric);
  field(i, 2) = std::numeric_limits<double>::max();
  expectError(i, GpsEncodingError::UnsupportedAltitude);
  field(i, 4) = 4;
  expectError(i, GpsEncodingError::InvalidMetric);
  field(i, 2) = 3;
  success(i);
  field(i, 2) = -1;
  field(i, 4) = std::numeric_limits<double>::quiet_NaN();
  expectError(i, GpsEncodingError::InvalidMetric);
}
void input_observations_and_output_copies_are_immutable() {
  const auto i = validInputs();
  const auto original = success(i);
  const auto again = success(i);
  auto copy = original;
  copy.bytes.fill(0);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(insta360_fixture::kGpsNorthWest, original.bytes.data(), 71);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(original.bytes.data(), again.bytes.data(), 71);
  TEST_ASSERT_TRUE(i.snapshot.fix.latitude_degrees == 1 && i.snapshot.fix.longitude_degrees == -2);
  TEST_ASSERT_EQUAL_UINT64(1000, i.snapshot.fix.receipt_monotonic_ms);
  TEST_ASSERT_EQUAL_UINT64(1000, i.snapshot.age_ms);
  TEST_ASSERT_EQUAL_UINT64(2000, i.now.monotonic_ms);
  TEST_ASSERT_EQUAL_UINT64(7, i.now.session_id);
  TEST_ASSERT_EQUAL_UINT(1000, i.config.max_age_ms);
  auto varied = i;
  varied.snapshot.fix.satellites.available = true;
  varied.snapshot.fix.satellites.value = 42;
  varied.snapshot.fix.fix_quality.available = true;
  varied.snapshot.fix.fix_quality.value = 255;
  const auto ignored = success(varied);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(original.bytes.data(), ignored.bytes.data(), 71);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(literal_packets_both_hemispheres_and_sequence_endpoints);
  RUN_TEST(config_and_sequence_rejections_are_atomic);
  RUN_TEST(invalid_clocks_fail_before_fix);
  RUN_TEST(copied_fix_validity_and_checked_age);
  RUN_TEST(coordinate_limits_and_nonfinite_values);
  RUN_TEST(every_missing_required_field_combination);
  RUN_TEST(gregorian_dates_and_independent_epochs);
  RUN_TEST(source_utc_time_bounds_and_centisecond_omission);
  RUN_TEST(invalid_metrics_and_signed_altitude);
  RUN_TEST(narrowing_rejects_lost_values_and_course_rounding);
  RUN_TEST(zeros_subnormals_and_binary32_precision);
  RUN_TEST(error_precedence_clears_all_bytes);
  RUN_TEST(input_observations_and_output_copies_are_immutable);
  return UNITY_END();
}

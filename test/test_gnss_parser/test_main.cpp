#include "../fixtures/gnss/responses.h"
#include "gnss_parser.h"
#include <cmath>
#include <cstring>
#include <string>
#include <unity.h>
using namespace ridesync;
void near(double tolerance, double expected, double actual) {
  TEST_ASSERT_TRUE(std::fabs(expected - actual) <= tolerance);
}
void equal(double expected, double actual) { near(1e-12, expected, actual); }
GnssParseResult parse(const char *s) { return parseGnssLine(s, std::strlen(s), 1234567890123ULL); }
void error(const char *s, GnssError expected) {
  auto r = parse(s);
  TEST_ASSERT_EQUAL((int)GnssStatus::ParseError, (int)r.status);
  TEST_ASSERT_EQUAL((int)expected, (int)r.error);
  TEST_ASSERT_FALSE(r.fix.valid);
  TEST_ASSERT_FALSE(r.fix.utc_date.available);
  TEST_ASSERT_FALSE(r.fix.altitude_msl_metres.available);
}
void manual_example_normalizes_documented_fields() {
  auto r = parse(kManualCgpsinfo);
  TEST_ASSERT_EQUAL((int)GnssStatus::Fix, (int)r.status);
  TEST_ASSERT_EQUAL((int)GnssError::None, (int)r.error);
  TEST_ASSERT_TRUE(r.fix.valid);
  near(1e-9, 31.2223881, r.fix.latitude_degrees);
  near(1e-9, 121.3539010666667, r.fix.longitude_degrees);
  TEST_ASSERT_TRUE(r.fix.altitude_msl_metres.available);
  near(1e-9, 44.1, r.fix.altitude_msl_metres.value);
  TEST_ASSERT_TRUE(r.fix.speed_metres_per_second.available);
  equal(0, r.fix.speed_metres_per_second.value);
  TEST_ASSERT_TRUE(r.fix.course_degrees.available);
  equal(0, r.fix.course_degrees.value);
  TEST_ASSERT_TRUE(r.fix.utc_date.available);
  TEST_ASSERT_EQUAL(2011, r.fix.utc_date.value.year);
  TEST_ASSERT_EQUAL(3, r.fix.utc_date.value.month);
  TEST_ASSERT_EQUAL(25, r.fix.utc_date.value.day);
  TEST_ASSERT_TRUE(r.fix.utc_time.available);
  TEST_ASSERT_EQUAL(7, r.fix.utc_time.value.hour);
  TEST_ASSERT_EQUAL(28, r.fix.utc_time.value.minute);
  TEST_ASSERT_EQUAL(9, r.fix.utc_time.value.second);
  TEST_ASSERT_EQUAL(33, r.fix.utc_time.value.centisecond);
  TEST_ASSERT_FALSE(r.fix.satellites.available);
  TEST_ASSERT_FALSE(r.fix.fix_quality.available);
  TEST_ASSERT_EQUAL_UINT64(1234567890123ULL, r.fix.receipt_monotonic_ms);
}
void observed_no_fix_is_not_parse_error() {
  for (auto s : {kObservedCgpsinfoNoFix, kObservedCgnssinfoNoFix}) {
    auto r = parse(s);
    TEST_ASSERT_EQUAL((int)GnssStatus::NoFix, (int)r.status);
    TEST_ASSERT_EQUAL((int)GnssError::None, (int)r.error);
    TEST_ASSERT_FALSE(r.fix.valid);
    TEST_ASSERT_FALSE(r.fix.utc_date.available);
    TEST_ASSERT_FALSE(r.fix.utc_time.available);
    TEST_ASSERT_FALSE(r.fix.course_degrees.available);
    TEST_ASSERT_FALSE(r.fix.satellites.available);
    TEST_ASSERT_EQUAL_UINT64(1234567890123ULL, r.fix.receipt_monotonic_ms);
  }
  error("+CGPSINFO: ,,,,,,,", GnssError::WrongFieldCount);
  error("+CGPSINFO: ,,,,,,,,,", GnssError::WrongFieldCount);
  error("+CGPSINFO: ,,,,250311,072809.33,,,", GnssError::MissingCoordinate);
}
void synthetic_hemispheres_zero_and_units() {
  auto r = parse("+CGPSINFO:1230.000000,S,04515.000000,W,,,,-10,0");
  TEST_ASSERT_EQUAL((int)GnssError::InvalidMetric, (int)r.error);
  r = parse("+CGPSINFO:1230.000000,S,04515.000000,W,,,-10,10,359.9");
  TEST_ASSERT_TRUE(r.fix.valid);
  equal(-12.5, r.fix.latitude_degrees);
  equal(-45.25, r.fix.longitude_degrees);
  equal(-10, r.fix.altitude_msl_metres.value);
  near(1e-9, 5.144444444444444, r.fix.speed_metres_per_second.value);
  equal(359.9, r.fix.course_degrees.value);
  for (auto s : {"+CGPSINFO:0000.000000,N,00000.000000,E,,,,,",
                 "+CGPSINFO:0000.000000,S,00000.000000,W,,,,,"}) {
    r = parse(s);
    TEST_ASSERT_TRUE(r.fix.valid);
    equal(0, r.fix.latitude_degrees);
    equal(0, r.fix.longitude_degrees);
    TEST_ASSERT_FALSE(r.fix.altitude_msl_metres.available);
    TEST_ASSERT_FALSE(r.fix.speed_metres_per_second.available);
    TEST_ASSERT_FALSE(r.fix.course_degrees.available);
    TEST_ASSERT_FALSE(r.fix.utc_date.available);
    TEST_ASSERT_FALSE(r.fix.utc_time.available);
  }
  TEST_ASSERT_TRUE(parse("+CGPSINFO:9000.000000,N,18000.000000,E,,,,,").fix.valid);
}
void synthetic_utc_boundaries_and_missingness() {
  struct Case {
    const char *date;
    const char *time;
    int year, month, day, hour, second, fraction;
  };
  const Case cases[] = {{"311299", "235959.99", 2099, 12, 31, 23, 59, 99},
                        {"010100", "000000.00", 2000, 1, 1, 0, 0, 0},
                        {"290224", "120001.01", 2024, 2, 29, 12, 1, 1}};
  for (const auto &c : cases) {
    std::string s =
        std::string("+CGPSINFO:0000.000000,N,00000.000000,E,") + c.date + "," + c.time + ",,,";
    auto r = parse(s.c_str());
    TEST_ASSERT_TRUE(r.fix.valid);
    TEST_ASSERT_EQUAL(c.year, r.fix.utc_date.value.year);
    TEST_ASSERT_EQUAL(c.month, r.fix.utc_date.value.month);
    TEST_ASSERT_EQUAL(c.day, r.fix.utc_date.value.day);
    TEST_ASSERT_EQUAL(c.hour, r.fix.utc_time.value.hour);
    TEST_ASSERT_EQUAL(c.second, r.fix.utc_time.value.second);
    TEST_ASSERT_EQUAL(c.fraction, r.fix.utc_time.value.centisecond);
  }
  auto r = parse("+CGPSINFO:0000.000000,N,00000.000000,E,010100,,,,");
  TEST_ASSERT_TRUE(r.fix.utc_date.available);
  TEST_ASSERT_FALSE(r.fix.utc_time.available);
  r = parse("+CGPSINFO:0000.000000,N,00000.000000,E,,000000.00,,,");
  TEST_ASSERT_FALSE(r.fix.utc_date.available);
  TEST_ASSERT_TRUE(r.fix.utc_time.available);
  for (auto date : {"290223", "310424", "000124", "011324", "01010", "010100x"}) {
    std::string s = std::string("+CGPSINFO:0000.000000,N,00000.000000,E,") + date + ",000000.00,,,";
    error(s.c_str(), GnssError::InvalidDate);
  }
  for (auto time :
       {"240000.00", "006000.00", "000060.00", "000000", "000000.0", "000000.000", "000000.xx"}) {
    std::string s = std::string("+CGPSINFO:0000.000000,N,00000.000000,E,010100,") + time + ",,,";
    error(s.c_str(), GnssError::InvalidTime);
  }
}
void synthetic_malformed_coordinates_and_metrics() {
  for (auto coord : {"9060.000000", "9000.000001", "9100.000000", "1260.000000", "-1230.000000",
                     "1230", "1230.00000", "1230.000000x", "NaN", "Inf", "1e3"}) {
    std::string s = std::string("+CGPSINFO:") + coord + ",N,00000.000000,E,,,,,";
    error(s.c_str(), GnssError::InvalidCoordinate);
  }
  for (auto s : {"+CGPSINFO:0000.000000,X,00000.000000,E,,,,,",
                 "+CGPSINFO:0000.000000,N,18000.000001,E,,,,,",
                 "+CGPSINFO:0000.000000,N,00000.000000,N,,,,,"})
    error(s, GnssError::InvalidCoordinate);
  for (auto metric : {"NaN", "inf", "1x", "1e2", "+1", "1.", ".1", "--1"}) {
    for (int field = 0; field < 3; ++field) {
      std::string s = "+CGPSINFO:0000.000000,N,00000.000000,E,,,";
      for (int i = 0; i < 3; ++i) {
        if (i)
          s += ',';
        s += i == field ? metric : "0";
      }
      error(s.c_str(), GnssError::InvalidMetric);
    }
  }
  error("+CGPSINFO:0000.000000,N,00000.000000,E,,,0,-1,0", GnssError::InvalidMetric);
  error("+CGPSINFO:0000.000000,N,00000.000000,E,,,0,1,-1", GnssError::InvalidMetric);
  error("+CGPSINFO:0000.000000,N,00000.000000,E,,,0,1,360", GnssError::InvalidMetric);
  error("+CGPSINFO:0000.000000,N,00000.000000,E,,,,", GnssError::WrongFieldCount);
}
void bounded_lines_and_unsupported_schemas() {
  error("OK", GnssError::UnknownResponse);
  error("+CGNSSINFO:2,09,05,00,3113.330650,N,12121.262554,E,131117,091918.00,32.9,0.0,255.0,1.1,0."
        "8,0.7",
        GnssError::UnsupportedSchema);
  error("+CGNSSINFO:2,09,05,00,00,31.22,N,121.35,E,131117,091918.00,32.9,0.0,255.0,1.1,0.8,0.7",
        GnssError::UnsupportedSchema);
  error("+CGNSSINFO:,,,,,,,,,,,,,,,", GnssError::UnsupportedSchema);
  std::string s(kGnssMaxLineBytes + 1, 'x');
  error(s.c_str(), GnssError::LineTooLong);
  s = "+CGPSINFO:" + std::string(kGnssMaxFieldBytes + 1, '1') + ",N,00000.000000,E,,,,,";
  error(s.c_str(), GnssError::FieldTooLong);
  TEST_ASSERT_EQUAL((int)GnssError::InvalidInput, (int)parseGnssLine(nullptr, 1, 0).error);
  TEST_ASSERT_EQUAL((int)GnssError::InvalidInput, (int)parseGnssLine("", 0, 0).error);
  s = kManualCgpsinfo;
  s[20] = '\0';
  TEST_ASSERT_EQUAL((int)GnssError::InvalidInput, (int)parseGnssLine(s.data(), s.size(), 0).error);
  s = std::string(kManualCgpsinfo) + "\r\n";
  TEST_ASSERT_TRUE(parse(s.c_str()).fix.valid);
  s = std::string(kManualCgpsinfo) + "\r\nOK\r\n";
  error(s.c_str(), GnssError::InvalidInput);
  // Explicit length supports a non-null-terminated UART line.
  char raw[] = {'+', 'C', 'G', 'P', 'S', 'I', 'N', 'F', 'O',
                ':', ',', ',', ',', ',', ',', ',', ',', ','};
  TEST_ASSERT_EQUAL((int)GnssStatus::NoFix, (int)parseGnssLine(raw, sizeof raw, 42).status);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(manual_example_normalizes_documented_fields);
  RUN_TEST(observed_no_fix_is_not_parse_error);
  RUN_TEST(synthetic_hemispheres_zero_and_units);
  RUN_TEST(synthetic_utc_boundaries_and_missingness);
  RUN_TEST(synthetic_malformed_coordinates_and_metrics);
  RUN_TEST(bounded_lines_and_unsupported_schemas);
  return UNITY_END();
}

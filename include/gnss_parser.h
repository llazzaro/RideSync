#ifndef RIDESYNC_GNSS_PARSER_H
#define RIDESYNC_GNSS_PARSER_H
#include <cstddef>
#include <cstdint>
namespace ridesync {
constexpr size_t kGnssMaxLineBytes = 256;
constexpr size_t kGnssMaxFieldBytes = 24;
template <typename T> struct GnssOptional {
  bool available = false;
  T value{};
};
struct GnssDate {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
};
struct GnssTime {
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint8_t centisecond = 0;
};
struct GnssFix {
  bool valid = false;
  double latitude_degrees = 0;
  double longitude_degrees = 0;
  GnssOptional<double> altitude_msl_metres;
  GnssOptional<double> speed_metres_per_second;
  GnssOptional<double> course_degrees;
  GnssOptional<GnssDate> utc_date;
  GnssOptional<GnssTime> utc_time;
  GnssOptional<uint16_t> satellites;
  GnssOptional<uint8_t> fix_quality;
  uint64_t receipt_monotonic_ms = 0;
};
enum class GnssStatus { Fix, NoFix, ParseError };
enum class GnssError {
  None,
  InvalidInput,
  LineTooLong,
  UnknownResponse,
  UnsupportedSchema,
  WrongFieldCount,
  FieldTooLong,
  MissingCoordinate,
  InvalidCoordinate,
  InvalidDate,
  InvalidTime,
  InvalidMetric
};
struct GnssParseResult {
  GnssStatus status = GnssStatus::ParseError;
  GnssError error = GnssError::InvalidInput;
  GnssFix fix;
};
// One complete response line (optional trailing CR/LF), never a whole AT exchange.
// CGPSINFO uses the SIMCom V1.09 documentary schema; hardware remains unverified.
// Date yy means 2000+yy, not a clock inference. No epoch/session conversion.
GnssParseResult parseGnssLine(const char *line, size_t length, uint64_t receipt_monotonic_ms);
} // namespace ridesync
#endif

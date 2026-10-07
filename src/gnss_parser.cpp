#include "gnss_parser.h"
#include <cstring>

namespace ridesync {
namespace {
struct Field {
  const char *data = nullptr;
  size_t size = 0;
};
bool digit(char c) { return c >= '0' && c <= '9'; }
bool digits(const Field &f, size_t start, size_t count) {
  if (start + count > f.size)
    return false;
  for (size_t i = start; i < start + count; ++i)
    if (!digit(f.data[i]))
      return false;
  return true;
}
unsigned pair(const Field &f, size_t start) {
  return unsigned(f.data[start] - '0') * 10 + unsigned(f.data[start + 1] - '0');
}
// Deliberately narrow decimal grammar: no locale, exponent, sign except altitude,
// NaN/Inf, or ignored trailing junk. Bounded 24-digit accumulation stays finite.
bool decimal(const Field &f, bool allow_negative, double &value) {
  if (!f.size)
    return false;
  size_t i = 0;
  bool negative = f.data[0] == '-';
  if (negative) {
    if (!allow_negative)
      return false;
    ++i;
  }
  size_t start = i;
  double whole = 0;
  while (i < f.size && digit(f.data[i]))
    whole = whole * 10 + (f.data[i++] - '0');
  if (i == start)
    return false;
  if (i < f.size && f.data[i] == '.') {
    start = ++i;
    double scale = 0.1;
    while (i < f.size && digit(f.data[i])) {
      whole += (f.data[i++] - '0') * scale;
      scale *= 0.1;
    }
    if (i == start)
      return false;
  }
  if (i != f.size)
    return false;
  value = negative ? -whole : whole;
  return true;
}
bool coordinate(const Field &f, const Field &hemisphere, bool latitude, double &value) {
  const size_t degree_digits = latitude ? 2 : 3;
  const size_t decimal_point = degree_digits + 2;
  if (f.size != decimal_point + 7 || f.data[decimal_point] != '.' || !digits(f, 0, decimal_point) ||
      !digits(f, decimal_point + 1, 6) || hemisphere.size != 1)
    return false;
  const char h = hemisphere.data[0];
  if (latitude ? (h != 'N' && h != 'S') : (h != 'E' && h != 'W'))
    return false;
  unsigned degrees = 0;
  for (size_t i = 0; i < degree_digits; ++i)
    degrees = degrees * 10 + (f.data[i] - '0');
  Field minute_field;
  minute_field.data = f.data + degree_digits;
  minute_field.size = f.size - degree_digits;
  double minutes = 0;
  if (!decimal(minute_field, false, minutes))
    return false;
  const unsigned limit = latitude ? 90 : 180;
  if (minutes >= 60 || degrees > limit || (degrees == limit && minutes != 0))
    return false;
  value = degrees + minutes / 60;
  if (h == 'S' || h == 'W')
    value = -value;
  return true;
}
bool date(const Field &f, GnssDate &value) {
  if (f.size != 6 || !digits(f, 0, 6))
    return false;
  value.day = pair(f, 0);
  value.month = pair(f, 2);
  value.year = 2000 + pair(f, 4);
  if (value.month < 1 || value.month > 12 || value.day < 1)
    return false;
  const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  unsigned max_day = days[value.month - 1];
  if (value.month == 2 && value.year % 4 == 0 && (value.year % 100 != 0 || value.year % 400 == 0))
    ++max_day;
  return value.day <= max_day;
}
bool time(const Field &f, GnssTime &value) {
  if (f.size != 9 || f.data[6] != '.' || !digits(f, 0, 6) || !digits(f, 7, 2))
    return false;
  value.hour = pair(f, 0);
  value.minute = pair(f, 2);
  value.second = pair(f, 4);
  value.centisecond = pair(f, 7);
  // Leap seconds require a separate policy; do not silently normalize to tomorrow.
  return value.hour < 24 && value.minute < 60 && value.second < 60;
}
GnssParseResult result(GnssStatus status, GnssError error, uint64_t receipt) {
  GnssParseResult r;
  r.status = status;
  r.error = error;
  r.fix.receipt_monotonic_ms = receipt;
  return r;
}
} // namespace

GnssParseResult parseGnssLine(const char *line, size_t length, uint64_t receipt_monotonic_ms) {
  const auto fail = [receipt_monotonic_ms](GnssError e) {
    return result(GnssStatus::ParseError, e, receipt_monotonic_ms);
  };
  if (!line || !length)
    return fail(GnssError::InvalidInput);
  if (length > kGnssMaxLineBytes)
    return fail(GnssError::LineTooLong);
  // Permit a line terminator, but never consume embedded lines or NUL bytes.
  if (length && line[length - 1] == '\n')
    --length;
  if (length && line[length - 1] == '\r')
    --length;
  for (size_t i = 0; i < length; ++i)
    if (line[i] < ' ' || line[i] > '~')
      return fail(GnssError::InvalidInput);
  const char gps_prefix[] = "+CGPSINFO:";
  const char gnss_prefix[] = "+CGNSSINFO:";
  bool is_gnss = length >= sizeof(gnss_prefix) - 1 &&
                 std::memcmp(line, gnss_prefix, sizeof(gnss_prefix) - 1) == 0;
  bool is_gps = length >= sizeof(gps_prefix) - 1 &&
                std::memcmp(line, gps_prefix, sizeof(gps_prefix) - 1) == 0;
  if (!is_gnss && !is_gps)
    return fail(GnssError::UnknownResponse);
  size_t start = is_gnss ? sizeof(gnss_prefix) - 1 : sizeof(gps_prefix) - 1;
  while (start < length && line[start] == ' ')
    ++start;
  // Maximum published CGNSSINFO variant has 17 fields; none is selected here.
  Field fields[17];
  size_t count = 0;
  bool all_empty = true;
  for (size_t i = start; i <= length; ++i) {
    if (i != length && line[i] != ',')
      continue;
    if (count == 17)
      return fail(GnssError::WrongFieldCount);
    if (i - start > kGnssMaxFieldBytes)
      return fail(GnssError::FieldTooLong);
    fields[count].data = line + start;
    fields[count].size = i - start;
    all_empty = all_empty && fields[count].size == 0;
    ++count;
    start = i + 1;
  }
  if (is_gnss) {
    if (count == 9 && all_empty)
      return result(GnssStatus::NoFix, GnssError::None, receipt_monotonic_ms);
    return fail(GnssError::UnsupportedSchema);
  }
  if (count != 9)
    return fail(GnssError::WrongFieldCount);
  if (all_empty)
    return result(GnssStatus::NoFix, GnssError::None, receipt_monotonic_ms);
  for (size_t i = 0; i < 4; ++i)
    if (!fields[i].size)
      return fail(GnssError::MissingCoordinate);
  GnssParseResult r = result(GnssStatus::Fix, GnssError::None, receipt_monotonic_ms);
  if (!coordinate(fields[0], fields[1], true, r.fix.latitude_degrees) ||
      !coordinate(fields[2], fields[3], false, r.fix.longitude_degrees))
    return fail(GnssError::InvalidCoordinate);
  if (fields[4].size) {
    if (!date(fields[4], r.fix.utc_date.value))
      return fail(GnssError::InvalidDate);
    r.fix.utc_date.available = true;
  }
  if (fields[5].size) {
    if (!time(fields[5], r.fix.utc_time.value))
      return fail(GnssError::InvalidTime);
    r.fix.utc_time.available = true;
  }
  GnssOptional<double> *metrics[] = {&r.fix.altitude_msl_metres, &r.fix.speed_metres_per_second,
                                     &r.fix.course_degrees};
  for (size_t i = 0; i < 3; ++i) {
    if (!fields[i + 6].size)
      continue;
    if (!decimal(fields[i + 6], i == 0, metrics[i]->value))
      return fail(GnssError::InvalidMetric);
    if (i == 2 && metrics[i]->value >= 360)
      return fail(GnssError::InvalidMetric);
    metrics[i]->available = true;
  }
  if (r.fix.speed_metres_per_second.available)
    r.fix.speed_metres_per_second.value *= 1852.0 / 3600.0;
  r.fix.valid = true;
  return r;
}
} // namespace ridesync

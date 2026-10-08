#include "motion_estimator.h"
#include <cmath>

namespace ridesync {
namespace {
constexpr double gravity = 9.80665;
constexpr double radians_per_degree = 0.017453292519943295;
bool rotationValid(const float *r) {
  for (unsigned i = 0; i < 9; ++i)
    if (!std::isfinite(r[i]))
      return false;
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = 0; j < 3; ++j) {
      double dot = 0;
      for (unsigned k = 0; k < 3; ++k)
        dot += double(r[i * 3 + k]) * r[j * 3 + k];
      if (std::fabs(dot - (i == j ? 1.0 : 0.0)) > 0.0001)
        return false;
    }
  const double determinant = double(r[0]) * (double(r[4]) * r[8] - double(r[5]) * r[7]) -
                             double(r[1]) * (double(r[3]) * r[8] - double(r[5]) * r[6]) +
                             double(r[2]) * (double(r[3]) * r[7] - double(r[4]) * r[6]);
  return determinant > 0;
}
bool positiveRatio(uint32_t numerator, uint32_t denominator) {
  return numerator != 0 && denominator != 0;
}
bool endpoint(int16_t count) { return count <= -32767 || count >= 32767; }
bool convert(const int16_t *raw, const int16_t *offset, const uint32_t *gain_n,
             const uint32_t *gain_d, uint32_t scale_n, uint32_t scale_d, double unit,
             const float *rotation, MotionVector &result) {
  if (!positiveRatio(scale_n, scale_d))
    return false;
  double sensor[3]{};
  for (unsigned i = 0; i < 3; ++i) {
    if (endpoint(raw[i]) || !positiveRatio(gain_n[i], gain_d[i]))
      return false;
    sensor[i] = (double(raw[i]) - offset[i]) * (double(scale_n) / scale_d) *
                (double(gain_n[i]) / gain_d[i]) * unit;
  }
  float body[3]{};
  for (unsigned i = 0; i < 3; ++i) {
    double value = 0;
    for (unsigned j = 0; j < 3; ++j)
      value += rotation[i * 3 + j] * sensor[j];
    body[i] = float(value);
    if (!std::isfinite(body[i]))
      return false;
  }
  result.x = body[0];
  result.y = body[1];
  result.z = body[2];
  return true;
}
double norm(const MotionVector &v) {
  return std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z);
}
} // namespace
MotionEstimate MotionEstimator::update(const ImuEvidence &e,
                                       const StaticMotionReference &reference) {
  reset();
  const bool fresh = reference.declaration > last_declaration_;
  if (fresh)
    last_declaration_ = reference.declaration;
  const auto &c = e.config;
  if (e.kind != RecordKind::ImuSample || !e.session_id || !c.generation || !c.sensor_id ||
      !c.mount_id || !c.calibration_id || c.sensor_state != Qualification::Qualified ||
      c.mount_state != Qualification::Qualified ||
      c.calibration_state != Qualification::Qualified || !config_.mount_qualified ||
      c.mount_id != config_.mount_id || c.calibration_id != config_.calibration_id ||
      !config_.residual_calibration_qualified ||
      config_.convention != MotionCalibrationConvention::ResidualCountsOffsetThenGain ||
      !c.calibration_offsets_known || !c.calibration_gains_known ||
      config_.accel_compensation < 1 || config_.accel_compensation > 2 ||
      config_.gyro_compensation < 1 || config_.gyro_compensation > 2 ||
      c.accel_offset_compensation != config_.accel_compensation ||
      c.gyro_offset_compensation != config_.gyro_compensation || (e.timing_flags & 8) ||
      !rotationValid(config_.sensor_to_body))
    return estimate_;
  MotionVector force, rate;
  if (!convert(e.accel, c.accel_offset, c.accel_gain_numerator, c.accel_gain_denominator,
               c.accel_scale_numerator, c.accel_scale_denominator, gravity, config_.sensor_to_body,
               force) ||
      !convert(e.gyro, c.gyro_offset, c.gyro_gain_numerator, c.gyro_gain_denominator,
               c.gyro_scale_numerator, c.gyro_scale_denominator, radians_per_degree,
               config_.sensor_to_body, rate))
    return estimate_;
  estimate_.specific_force_mps2 = force;
  estimate_.angular_rate_rad_s = rate;
  estimate_.measurements_valid = true;
  if (!fresh || !reference.externally_stationary || reference.session_id != e.session_id ||
      reference.config_generation != c.generation || reference.batch_sequence != e.batch_sequence ||
      !e.receipt_known || (e.timing_flags & 7))
    return estimate_;
  // Sanity vetoes only: these conditions cannot establish stationarity.
  if (std::fabs(norm(force) - gravity) > 0.1 * gravity || norm(rate) > 0.05)
    return estimate_;
  const double yz = std::hypot(double(force.y), double(force.z));
  if (yz < 0.000001 * gravity)
    return estimate_;
  estimate_.roll_rad = float(std::atan2(double(force.y), double(force.z)));
  estimate_.pitch_rad = float(std::atan2(-double(force.x), yz));
  estimate_.static_tilt_valid = true;
  return estimate_;
}
} // namespace ridesync

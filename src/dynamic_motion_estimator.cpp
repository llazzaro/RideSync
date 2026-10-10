#include "dynamic_motion_estimator.h"
#include <cmath>
#include <limits>
namespace ridesync {
namespace {
constexpr double gravity = 9.80665;
bool finite(const MotionVector &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
double norm(const MotionVector &v) {
  return std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z);
}
} // namespace
bool DynamicMotionEstimator::configValid(const DynamicMotionConfig &c) {
  return c.max_step_us && c.max_horizon_us && c.max_step_us <= c.max_horizon_us &&
         c.max_horizon_us <= kMaxHorizonUs;
}
void DynamicMotionEstimator::reset() {
  initialized_ = false;
  initialized_us_ = 0;
  previous_ = {};
  quaternion_ = {{1, 0, 0, 0}};
  estimate_ = {};
  // Retain the consumed declaration: reset cannot renew external evidence.
}
DynamicMotionEstimate DynamicMotionEstimator::fail(DynamicMotionFault fault) {
  reset();
  estimate_.fault = fault;
  return estimate_;
}
DynamicMotionFault DynamicMotionEstimator::validate(const DynamicMotionInput &i) const {
  if (!config_.enabled)
    return DynamicMotionFault::Disabled;
  if (!configValid(config_))
    return DynamicMotionFault::Configuration;
  if (i.discontinuity)
    return DynamicMotionFault::Discontinuity;
  if (!i.session_id || !i.sensor_id || !i.config_generation || !i.mount_id || !i.calibration_id ||
      !i.sensor_epoch)
    return DynamicMotionFault::Identity;
  if (i.timing_source != DynamicTimingSource::ModelledCadence &&
      i.timing_source != DynamicTimingSource::QualifiedAcquisition)
    return DynamicMotionFault::Timing;
  if (!i.measurements_valid || !finite(i.specific_force_mps2) || !finite(i.angular_rate_rad_s))
    return DynamicMotionFault::Measurement;
  return DynamicMotionFault::None;
}
DynamicMotionEstimate DynamicMotionEstimator::initialize(const DynamicMotionInput &i,
                                                         const DynamicMotionReference &r) {
  reset();
  const bool fresh = r.declaration > last_declaration_;
  if (fresh)
    last_declaration_ = r.declaration;
  const auto fault = validate(i);
  if (fault != DynamicMotionFault::None)
    return fail(fault);
  if (!fresh || !r.externally_stationary)
    return fail(DynamicMotionFault::Reference);
  // These sanity vetoes do not establish stationarity or gravity observability.
  if (std::fabs(norm(i.specific_force_mps2) - gravity) > gravity * .1 ||
      norm(i.angular_rate_rad_s) > .05)
    return fail(DynamicMotionFault::Reference);
  const auto &f = i.specific_force_mps2;
  const double yz = std::hypot(double(f.y), double(f.z));
  if (yz < gravity * .000001)
    return fail(DynamicMotionFault::Reference);
  const double roll = std::atan2(double(f.y), double(f.z)), pitch = std::atan2(-double(f.x), yz);
  const double cr = std::cos(roll * .5), sr = std::sin(roll * .5);
  const double cp = std::cos(pitch * .5), sp = std::sin(pitch * .5);
  // q_BtoLocal = q_y(pitch) q_x(roll), relative yaw gauge zero.
  quaternion_ = {{cp * cr, cp * sr, sp * cr, -sp * sr}};
  initialized_ = true;
  initialized_us_ = i.sample_time_us;
  previous_ = i;
  return publish(i, 0);
}
DynamicMotionEstimate DynamicMotionEstimator::publish(const DynamicMotionInput &i,
                                                      uint32_t elapsed) {
  DynamicMotionEstimate out;
  const double w = quaternion_[0], x = quaternion_[1], y = quaternion_[2], z = quaternion_[3];
  for (unsigned n = 0; n < 4; ++n)
    out.quaternion_wxyz[n] = float(quaternion_[n]);
  // R_BtoLocal transpose applied to local +Z gives upward specific force at rest.
  out.gravity_body_mps2.x = float(gravity * 2 * (x * z - w * y));
  out.gravity_body_mps2.y = float(gravity * 2 * (y * z + w * x));
  out.gravity_body_mps2.z = float(gravity * (1 - 2 * (x * x + y * y)));
  out.linear_acceleration_body_mps2.x = i.specific_force_mps2.x - out.gravity_body_mps2.x;
  out.linear_acceleration_body_mps2.y = i.specific_force_mps2.y - out.gravity_body_mps2.y;
  out.linear_acceleration_body_mps2.z = i.specific_force_mps2.z - out.gravity_body_mps2.z;
  if (!finite(out.gravity_body_mps2) || !finite(out.linear_acceleration_body_mps2))
    return fail(DynamicMotionFault::Measurement);
  const double sin_pitch = 2 * (w * y - z * x);
  // Euler roll and relative yaw are ambiguous near +/-90deg pitch. Numeric
  // quaternion/vector output remains available, but unavailable angles are zero.
  if (std::fabs(sin_pitch) < 1 - .000001) {
    out.roll_rad = float(std::atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)));
    out.pitch_rad = float(std::asin(sin_pitch));
    out.angles_available = true;
  }
  out.elapsed_us = elapsed;
  out.timing_source = i.timing_source;
  out.quality = DynamicMotionQuality::Unreliable;
  out.fault = DynamicMotionFault::None;
  out.numeric_available = true;
  estimate_ = out;
  return estimate_;
}
DynamicMotionEstimate DynamicMotionEstimator::update(const DynamicMotionInput &i) {
  const auto fault = validate(i);
  if (fault != DynamicMotionFault::None)
    return fail(fault);
  if (!initialized_)
    return fail(DynamicMotionFault::Uninitialized);
  const auto &p = previous_;
  if (i.session_id != p.session_id || i.sensor_id != p.sensor_id ||
      i.config_generation != p.config_generation || i.mount_id != p.mount_id ||
      i.calibration_id != p.calibration_id || i.sensor_epoch != p.sensor_epoch)
    return fail(DynamicMotionFault::Identity);
  if (i.timing_source != p.timing_source || i.sample_time_us <= p.sample_time_us)
    return fail(DynamicMotionFault::Timing);
  if (p.sequence == std::numeric_limits<uint32_t>::max() || i.sequence != p.sequence + 1)
    return fail(DynamicMotionFault::Sequence);
  const uint64_t interval = i.sample_time_us - p.sample_time_us;
  if (interval > config_.max_step_us)
    return fail(DynamicMotionFault::Timing);
  const uint64_t elapsed = i.sample_time_us - initialized_us_;
  if (elapsed > config_.max_horizon_us)
    return fail(DynamicMotionFault::Horizon);
  const auto &v = i.angular_rate_rad_s;
  const double rate = norm(v), half_angle = rate * double(interval) * .0000005;
  const double scale = rate == 0 ? double(interval) * .0000005 : std::sin(half_angle) / rate;
  const double dw = std::cos(half_angle), dx = double(v.x) * scale, dy = double(v.y) * scale,
               dz = double(v.z) * scale;
  const double w = quaternion_[0], x = quaternion_[1], y = quaternion_[2], z = quaternion_[3];
  // Current sample rate is constant over the preceding declared interval.
  // Body-frame increments multiply on the right; no force correction or bias adaptation.
  quaternion_ = {{w * dw - x * dx - y * dy - z * dz, w * dx + x * dw + y * dz - z * dy,
                  w * dy - x * dz + y * dw + z * dx, w * dz + x * dy - y * dx + z * dw}};
  double length = 0;
  for (auto q : quaternion_)
    length += q * q;
  length = std::sqrt(length);
  if (!std::isfinite(length) || length == 0)
    return fail(DynamicMotionFault::Measurement);
  for (auto &q : quaternion_)
    q /= length;
  previous_ = i;
  return publish(i, static_cast<uint32_t>(elapsed));
}
} // namespace ridesync

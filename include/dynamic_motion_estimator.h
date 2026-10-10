#pragma once
#include "motion_types.h"
#include <array>
#include <cstdint>
namespace ridesync {
enum class DynamicTimingSource : uint8_t { Unknown, ModelledCadence, QualifiedAcquisition };
enum class DynamicMotionQuality : uint8_t { Invalid, Unreliable };
enum class DynamicMotionFault : uint8_t {
  None,
  Disabled,
  Configuration,
  Reference,
  Measurement,
  Identity,
  Timing,
  Sequence,
  Discontinuity,
  Horizon,
  Uninitialized
};
struct DynamicMotionConfig {
  bool enabled = false;
  uint32_t max_step_us = 20000, max_horizon_us = 2000000;
};
struct DynamicMotionInput {
  MotionVector specific_force_mps2, angular_rate_rad_s;
  bool measurements_valid = false, discontinuity = false;
  uint64_t session_id = 0, sample_time_us = 0;
  uint32_t sensor_id = 0, config_generation = 0, mount_id = 0, calibration_id = 0, sensor_epoch = 0,
           sequence = 0;
  DynamicTimingSource timing_source = DynamicTimingSource::Unknown;
};
struct DynamicMotionReference {
  bool externally_stationary = false;
  uint32_t declaration = 0;
};
struct DynamicMotionEstimate {
  std::array<float, 4> quaternion_wxyz{{1, 0, 0, 0}};
  MotionVector gravity_body_mps2, linear_acceleration_body_mps2;
  float roll_rad = 0, pitch_rad = 0;
  uint32_t elapsed_us = 0;
  DynamicTimingSource timing_source = DynamicTimingSource::Unknown;
  DynamicMotionQuality quality = DynamicMotionQuality::Invalid;
  DynamicMotionFault fault = DynamicMotionFault::Uninitialized;
  bool numeric_available = false, angles_available = false, dynamic_lean_valid = false,
       dynamic_acceleration_valid = false;
};
// Offline experimental arithmetic only: body-to-local quaternion with relative
// yaw gauge zero at external stationary initialization. No measured error budget,
// moving correction, GNSS aiding, or dynamic validity. Owner supplies body units,
// calibrated measurements and timing provenance; snapshots have no wall-clock TTL.
class DynamicMotionEstimator {
public:
  static constexpr uint32_t kMaxHorizonUs = 2000000;
  explicit DynamicMotionEstimator(const DynamicMotionConfig &c) : config_(c) {}
  static bool configValid(const DynamicMotionConfig &);
  DynamicMotionEstimate initialize(const DynamicMotionInput &, const DynamicMotionReference &);
  DynamicMotionEstimate update(const DynamicMotionInput &);
  void reset();
  const DynamicMotionEstimate &estimate() const { return estimate_; }

private:
  DynamicMotionConfig config_;
  DynamicMotionEstimate estimate_;
  DynamicMotionInput previous_;
  std::array<double, 4> quaternion_{{1, 0, 0, 0}};
  uint64_t initialized_us_ = 0;
  uint32_t last_declaration_ = 0;
  bool initialized_ = false;
  DynamicMotionEstimate fail(DynamicMotionFault);
  DynamicMotionFault validate(const DynamicMotionInput &) const;
  DynamicMotionEstimate publish(const DynamicMotionInput &, uint32_t);
};
static_assert(sizeof(DynamicMotionEstimator) <= 256, "bounded experimental estimator state");
} // namespace ridesync

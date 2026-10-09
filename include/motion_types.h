#pragma once
#include <cstdint>
namespace ridesync {
enum class MotionCalibrationConvention : uint8_t { Unsupported, ResidualCountsOffsetThenGain };
struct MotionEstimatorConfig {
  bool mount_qualified = false, residual_calibration_qualified = false;
  uint32_t mount_id = 0, calibration_id = 0;
  MotionCalibrationConvention convention = MotionCalibrationConvention::Unsupported;
  // Qualification applies to residuals in delivered counts with exactly these
  // device compensation states (1 disabled, 2 enabled), not pre-device biases.
  uint8_t accel_compensation = 0, gyro_compensation = 0;
  // Row-major proper rotation R_BS; body X forward, Y left, Z up.
  float sensor_to_body[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};
};
struct StaticMotionReference {
  bool externally_stationary = false;
  uint64_t session_id = 0;
  uint32_t config_generation = 0, batch_sequence = 0;
  // Caller-owned strictly increasing, nonzero declaration per update. Keep
  // increasing across reset; construct a new estimator before exhaustion.
  uint32_t declaration = 0;
};
struct MotionVector {
  float x = 0, y = 0, z = 0;
};
struct MotionEstimate {
  MotionVector specific_force_mps2, angular_rate_rad_s;
  float roll_rad = 0, pitch_rad = 0;
  // Qualified numeric conversion of supplied evidence, not live freshness.
  bool measurements_valid = false, static_tilt_valid = false;
  bool dynamic_lean_valid = false, dynamic_acceleration_valid = false;
};
enum class MotionAdmission : uint8_t { Disabled = 0, Refused = 1, Enabled = 2, Revoked = 3 };
struct MotionEvidence {
  MotionAdmission state = MotionAdmission::Disabled;
  MotionEstimatorConfig config;
  uint32_t snapshot_max_age_ms = 0;
  StaticMotionReference reference;
  MotionEstimate estimate;
};
} // namespace ridesync

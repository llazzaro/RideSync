#pragma once
// Repository-authored synthetic counts/calibration, not hardware evidence. MIT.
#include "motion_estimator.h"
namespace motion_fixture {
using namespace ridesync;
inline MotionEstimatorConfig config() {
  MotionEstimatorConfig c;
  c.mount_qualified = true;
  c.mount_id = 7;
  c.calibration_id = 9;
  c.residual_calibration_qualified = true;
  c.convention = MotionCalibrationConvention::ResidualCountsOffsetThenGain;
  c.accel_compensation = c.gyro_compensation = 1;
  return c;
}
inline ImuEvidence sample(uint64_t id = 11, uint32_t raw = 123) {
  ImuEvidence e;
  e.session_id = id;
  e.config.generation = 3;
  e.config.sensor_id = 5;
  e.config.mount_id = 7;
  e.config.calibration_id = 9;
  e.config.sensor_state = e.config.mount_state = e.config.calibration_state =
      Qualification::Qualified;
  e.config.accel_scale_numerator = 1;
  e.config.accel_scale_denominator = 2048;
  e.config.gyro_scale_numerator = 125;
  e.config.gyro_scale_denominator = 2048;
  e.config.accel_offset_compensation = e.config.gyro_offset_compensation = 1;
  e.config.calibration_offsets_known = e.config.calibration_gains_known = true;
  for (unsigned i = 0; i < 3; ++i) {
    e.config.accel_gain_numerator[i] = e.config.accel_gain_denominator[i] = 1;
    e.config.gyro_gain_numerator[i] = e.config.gyro_gain_denominator[i] = 1;
  }
  e.batch_sequence = 13;
  e.receipt_known = true;
  e.receipt_millis32 = raw;
  e.accel[2] = 2048;
  return e;
}
} // namespace motion_fixture

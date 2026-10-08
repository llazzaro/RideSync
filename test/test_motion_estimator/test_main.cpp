#include "motion_estimator.h"
#include <cmath>
#include <unity.h>
using namespace ridesync;
MotionEstimatorConfig qualifiedConfig() {
  MotionEstimatorConfig c;
  c.mount_qualified = true;
  c.mount_id = 7;
  c.calibration_id = 9;
  c.residual_calibration_qualified = true;
  c.convention = MotionCalibrationConvention::ResidualCountsOffsetThenGain;
  c.accel_compensation = c.gyro_compensation = 1;
  return c;
}
ImuEvidence sample() {
  ImuEvidence e;
  e.session_id = 11;
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
  e.receipt_millis32 = 123;
  e.accel[2] = 2048;
  return e;
}
StaticMotionReference reference(const ImuEvidence &e, uint32_t declaration) {
  StaticMotionReference r;
  r.externally_stationary = true;
  r.session_id = e.session_id;
  r.config_generation = e.config.generation;
  r.batch_sequence = e.batch_sequence;
  r.declaration = declaration;
  return r;
}
void known_static_angles_use_real_counts_and_explicit_body_frame() {
  // Independently specified integer fixture poses; quantization permits 0.001rad.
  struct Pose {
    int16_t x, y, z;
    float roll, pitch;
  };
  const Pose poses[] = {{0, 0, 2048, 0, 0},
                        {0, 700, 1924, 0.349066f, 0},
                        {0, -700, 1924, -0.349066f, 0},
                        {-700, 0, 1924, 0, 0.349066f},
                        {700, 0, 1924, 0, -0.349066f},
                        {0, 1448, 1448, 0.785398f, 0},
                        {0, -1448, 1448, -0.785398f, 0}};
  MotionEstimator estimator(qualifiedConfig());
  uint32_t id = 0;
  for (const auto &pose : poses) {
    auto e = sample();
    e.accel[0] = pose.x;
    e.accel[1] = pose.y;
    e.accel[2] = pose.z;
    const auto out = estimator.update(e, reference(e, ++id));
    TEST_ASSERT_TRUE(out.measurements_valid && out.static_tilt_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, pose.roll, out.roll_rad);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, pose.pitch, out.pitch_rad);
    TEST_ASSERT_FALSE(out.dynamic_lean_valid || out.dynamic_acceleration_valid);
  }
}
void offsets_gains_rates_and_mount_are_actually_applied() {
  auto config = qualifiedConfig();
  // R_BS is a proper +90degree rotation about Z: sensor X becomes body Y.
  const float matrix[9] = {0, -1, 0, 1, 0, 0, 0, 0, 1};
  for (unsigned i = 0; i < 9; ++i)
    config.sensor_to_body[i] = matrix[i];
  MotionEstimator estimator(config);
  auto e = sample();
  e.config.accel_offset[0] = 100;
  e.config.accel_offset[1] = -50;
  e.config.accel_offset[2] = 20;
  e.accel[0] = 100;
  e.accel[1] = -50;
  e.accel[2] = 1044;
  e.config.accel_gain_numerator[2] = 2;
  e.config.gyro_offset[0] = 16;
  e.config.gyro_offset[1] = -32;
  e.config.gyro_offset[2] = 48;
  e.gyro[0] = 16;
  e.gyro[1] = -32;
  e.gyro[2] = 48;
  auto out = estimator.update(e, reference(e, 1));
  TEST_ASSERT_TRUE(out.static_tilt_valid);
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 9.80665f, out.specific_force_mps2.z);
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0, out.angular_rate_rad_s.x);
  e.gyro[0] = 2064;
  out = estimator.update(e, {});
  TEST_ASSERT_TRUE(out.measurements_valid);
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 2.1816616f, out.angular_rate_rad_s.y);
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0, out.angular_rate_rad_s.x);
  e = sample();
  e.accel[0] = 700;
  e.accel[2] = 1924;
  out = estimator.update(e, reference(e, 2));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.349066f, out.roll_rad);
}
void calibration_convention_and_compensation_cannot_be_inferred_from_ids() {
  auto e = sample();
  e.config.calibration_method = 12345;
  auto config = qualifiedConfig();
  config.residual_calibration_qualified = false;
  MotionEstimator blocked(config);
  TEST_ASSERT_FALSE(blocked.update(e, reference(e, 1)).measurements_valid);
  config = qualifiedConfig();
  config.convention = MotionCalibrationConvention::Unsupported;
  MotionEstimator unknown(config);
  TEST_ASSERT_FALSE(unknown.update(e, reference(e, 1)).measurements_valid);
  config = qualifiedConfig();
  e.config.accel_offset_compensation = 2;
  MotionEstimator wrong_device_state(config);
  TEST_ASSERT_FALSE(wrong_device_state.update(e, reference(e, 1)).measurements_valid);
  config.accel_compensation = 2; // Caller explicitly qualified residuals after device compensation.
  MotionEstimator explicit_device_state(config);
  TEST_ASSERT_TRUE(explicit_device_state.update(e, reference(e, 1)).static_tilt_valid);
  e.config.accel_gain_denominator[0] = 0;
  TEST_ASSERT_FALSE(explicit_device_state.update(e, reference(e, 2)).measurements_valid);
}
void reset_and_changed_context_require_fresh_static_declaration() {
  MotionEstimator estimator(qualifiedConfig());
  auto e = sample();
  auto r = reference(e, 1);
  TEST_ASSERT_TRUE(estimator.update(e, r).static_tilt_valid);
  estimator.reset();
  TEST_ASSERT_FALSE(estimator.estimate().static_tilt_valid);
  TEST_ASSERT_FALSE(estimator.update(e, r).static_tilt_valid);
  TEST_ASSERT_TRUE(estimator.update(e, reference(e, 2)).static_tilt_valid);
  ++e.config.generation;
  TEST_ASSERT_FALSE(estimator.update(e, r).static_tilt_valid);
  TEST_ASSERT_TRUE(estimator.update(e, reference(e, 3)).static_tilt_valid);
  ++e.batch_sequence;
  TEST_ASSERT_FALSE(estimator.update(e, reference(sample(), 4)).static_tilt_valid);
  TEST_ASSERT_TRUE(estimator.update(e, reference(e, 5)).static_tilt_valid);
  e.kind = RecordKind::ImuControl;
  e.event_code = 9;
  TEST_ASSERT_FALSE(estimator.update(e, {}).static_tilt_valid);
  e.kind = RecordKind::ImuSample;
  TEST_ASSERT_FALSE(estimator.update(e, reference(e, 5)).static_tilt_valid);
  TEST_ASSERT_TRUE(estimator.update(e, reference(e, 6)).static_tilt_valid);
}
void receipt_time_is_not_a_gyro_epoch_and_missing_time_cannot_publish_tilt() {
  MotionEstimator estimator(qualifiedConfig());
  auto e = sample();
  TEST_ASSERT_TRUE(estimator.update(e, reference(e, 1)).static_tilt_valid);
  ++e.frame_sequence; // Same FIFO batch and repeated HOST receipt are normal.
  TEST_ASSERT_TRUE(estimator.update(e, reference(e, 2)).static_tilt_valid);
  e.receipt_known = false;
  e.sensor_time_present = true;
  e.sensor_time_ticks24 = 100;
  auto out = estimator.update(e, reference(e, 3));
  TEST_ASSERT_TRUE(out.measurements_valid);
  TEST_ASSERT_FALSE(out.static_tilt_valid);
  e.receipt_known = true;
  e.timing_flags = 1;
  TEST_ASSERT_FALSE(estimator.update(e, reference(e, 4)).static_tilt_valid);
  e.timing_flags = 0;
  TEST_ASSERT_FALSE(estimator.update(e, {}).static_tilt_valid);
}
void acceleration_turn_and_vibration_do_not_establish_stationarity() {
  MotionEstimator estimator(qualifiedConfig());
  auto e = sample();
  // Unit-norm force can be gravity tilt OR sustained translational/turn acceleration.
  e.accel[1] = 1448;
  e.accel[2] = 1448;
  auto out = estimator.update(e, {});
  TEST_ASSERT_TRUE(out.measurements_valid);
  TEST_ASSERT_FALSE(out.static_tilt_valid || out.dynamic_lean_valid ||
                    out.dynamic_acceleration_valid);
  e = sample();
  e.accel[2] = 2500;
  TEST_ASSERT_FALSE(estimator.update(e, reference(e, 1)).static_tilt_valid);
  e = sample();
  e.gyro[0] = 2048;
  TEST_ASSERT_FALSE(estimator.update(e, reference(e, 2)).static_tilt_valid);
}
void saturation_invalid_metadata_and_singular_attitude_fail_closed() {
  MotionEstimator estimator(qualifiedConfig());
  auto e = sample();
  for (int16_t count : {int16_t(32767), int16_t(-32768)}) {
    e.accel[0] = count;
    const auto out = estimator.update(e, reference(e, 1));
    TEST_ASSERT_FALSE(out.measurements_valid || out.static_tilt_valid);
  }
  e = sample();
  e.timing_flags = 8;
  TEST_ASSERT_FALSE(estimator.update(e, reference(e, 2)).measurements_valid);
  e = sample();
  e.accel[0] = 2048;
  e.accel[2] = 0;
  TEST_ASSERT_FALSE(
      estimator.update(e, reference(e, 3)).static_tilt_valid); // Roll undefined at pitch90.
  auto config = qualifiedConfig();
  config.sensor_to_body[8] = -1;
  MotionEstimator reflected(config);
  e = sample();
  TEST_ASSERT_FALSE(reflected.update(e, reference(e, 1)).measurements_valid);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(known_static_angles_use_real_counts_and_explicit_body_frame);
  RUN_TEST(offsets_gains_rates_and_mount_are_actually_applied);
  RUN_TEST(calibration_convention_and_compensation_cannot_be_inferred_from_ids);
  RUN_TEST(reset_and_changed_context_require_fresh_static_declaration);
  RUN_TEST(receipt_time_is_not_a_gyro_epoch_and_missing_time_cannot_publish_tilt);
  RUN_TEST(acceleration_turn_and_vibration_do_not_establish_stationarity);
  RUN_TEST(saturation_invalid_metadata_and_singular_attitude_fail_closed);
  return UNITY_END();
}

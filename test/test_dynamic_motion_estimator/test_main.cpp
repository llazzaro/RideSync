#include "dynamic_motion_estimator.h"
#include <cmath>
#include <limits>
#include <unity.h>
using namespace ridesync;
constexpr float g = 9.80665f;
MotionVector vector(float x, float y, float z) {
  MotionVector v;
  v.x = x;
  v.y = y;
  v.z = z;
  return v;
}
DynamicMotionConfig config() {
  DynamicMotionConfig c;
  c.enabled = true;
  return c;
}
DynamicMotionInput input() {
  DynamicMotionInput i;
  i.session_id = 1;
  i.sample_time_us = 1000000;
  i.sensor_id = i.config_generation = i.mount_id = i.calibration_id = i.sensor_epoch = 1;
  i.measurements_valid = true;
  i.specific_force_mps2.z = g;
  i.timing_source = DynamicTimingSource::QualifiedAcquisition;
  return i;
}
DynamicMotionReference reference(uint32_t n = 1) {
  DynamicMotionReference r;
  r.externally_stationary = true;
  r.declaration = n;
  return r;
}
void unreliable(const DynamicMotionEstimate &e) {
  TEST_ASSERT_TRUE(e.numeric_available);
  TEST_ASSERT_EQUAL(static_cast<int>(DynamicMotionQuality::Unreliable),
                    static_cast<int>(e.quality));
  TEST_ASSERT_FALSE(e.dynamic_lean_valid);
  TEST_ASSERT_FALSE(e.dynamic_acceleration_valid);
}
void invalid(const DynamicMotionEstimate &e) {
  TEST_ASSERT_FALSE(e.numeric_available);
  TEST_ASSERT_FALSE(e.angles_available);
  TEST_ASSERT_EQUAL(static_cast<int>(DynamicMotionQuality::Invalid), static_cast<int>(e.quality));
  TEST_ASSERT_FALSE(e.dynamic_lean_valid);
  TEST_ASSERT_FALSE(e.dynamic_acceleration_valid);
}
void tilted_stationary_anchor_and_disabled_refusal() {
  DynamicMotionEstimator disabled({});
  invalid(disabled.initialize(input(), reference()));
  auto i = input();
  i.specific_force_mps2 = vector(-g * .5f, g * .4330127019f, g * .75f);
  DynamicMotionEstimator e(config());
  auto out = e.initialize(i, reference());
  unreliable(out);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .5235987756f, out.roll_rad);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .5235987756f, out.pitch_rad);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, 0, out.linear_acceleration_body_mps2.x);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, 0, out.linear_acceleration_body_mps2.y);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, 0, out.linear_acceleration_body_mps2.z);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .9330127019f, out.quaternion_wxyz[0]);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .25f, out.quaternion_wxyz[1]);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .25f, out.quaternion_wxyz[2]);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, -.0669872981f, out.quaternion_wxyz[3]);
}
void signed_axes_and_noncommuting_body_rotation() {
  for (unsigned axis = 0; axis < 3; ++axis)
    for (int sign : {-1, 1}) {
      DynamicMotionEstimator e(config());
      auto i = input();
      unreliable(e.initialize(i, reference()));
      i.sample_time_us += 20000;
      ++i.sequence;
      if (axis == 0)
        i.angular_rate_rad_s.x = sign * 25;
      if (axis == 1)
        i.angular_rate_rad_s.y = sign * 25;
      if (axis == 2)
        i.angular_rate_rad_s.z = sign * 25;
      auto out = e.update(i);
      unreliable(out);
      TEST_ASSERT_FLOAT_WITHIN(.00001f, .9689124217f, out.quaternion_wxyz[0]);
      TEST_ASSERT_FLOAT_WITHIN(.00001f, sign * .2474039593f, out.quaternion_wxyz[axis + 1]);
      if (axis == 0)
        TEST_ASSERT_FLOAT_WITHIN(.00001f, sign * g * .4794255386f, out.gravity_body_mps2.y);
      if (axis == 1)
        TEST_ASSERT_FLOAT_WITHIN(.00001f, -sign * g * .4794255386f, out.gravity_body_mps2.x);
      if (axis == 2)
        TEST_ASSERT_FLOAT_WITHIN(.00001f, g, out.gravity_body_mps2.z);
    }
  DynamicMotionEstimator e(config());
  auto i = input();
  e.initialize(i, reference());
  ++i.sequence;
  i.sample_time_us += 20000;
  i.angular_rate_rad_s = vector(25, 0, 0);
  e.update(i);
  ++i.sequence;
  i.sample_time_us += 20000;
  i.angular_rate_rad_s = vector(0, 25, 0);
  auto out = e.update(i);
  unreliable(out);
  // Analytic q_x(.5) q_y(.5): right multiplication yields positive z term.
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .9387912809f, out.quaternion_wxyz[0]);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .2397127693f, out.quaternion_wxyz[1]);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .2397127693f, out.quaternion_wxyz[2]);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .0612087191f, out.quaternion_wxyz[3]);
}
void translation_turn_ambiguity_and_euler_singularity() {
  DynamicMotionEstimator e(config());
  auto i = input();
  e.initialize(i, reference());
  i.sample_time_us += 10000;
  ++i.sequence;
  i.specific_force_mps2 = vector(2, 3, g);
  auto out = e.update(i);
  unreliable(out);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, 2, out.linear_acceleration_body_mps2.x);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, 3, out.linear_acceleration_body_mps2.y);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, 0, out.roll_rad);
  // A coordinated-turn-looking force never corrects attitude or becomes trusted.
  i.sample_time_us += 10000;
  ++i.sequence;
  i.specific_force_mps2 = vector(0, g * .5f, g * .8660254038f);
  out = e.update(i);
  unreliable(out);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, 0, out.roll_rad);
  i.sample_time_us += 20000;
  ++i.sequence;
  i.angular_rate_rad_s = vector(0, 78.53981634f, 0);
  out = e.update(i);
  unreliable(out);
  TEST_ASSERT_FALSE(out.angles_available);
  TEST_ASSERT_FLOAT_WITHIN(.00002f, -g, out.gravity_body_mps2.x);
}
void invalid_inputs_reset_and_nonce_cannot_be_reused() {
  for (unsigned mode = 0; mode < 16; ++mode) {
    DynamicMotionEstimator e(config());
    auto i = input();
    e.initialize(i, reference());
    ++i.sequence;
    i.sample_time_us += 10000;
    if (mode == 0)
      i.session_id = 2;
    if (mode == 1)
      i.sensor_id = 2;
    if (mode == 2)
      i.config_generation = 2;
    if (mode == 3)
      i.mount_id = 2;
    if (mode == 4)
      i.calibration_id = 2;
    if (mode == 5)
      i.sensor_epoch = 2;
    if (mode == 6)
      i.timing_source = DynamicTimingSource::ModelledCadence;
    if (mode == 7)
      i.measurements_valid = false; // Caller marks saturated/unqualified samples.
    if (mode == 8)
      i.discontinuity = true;
    if (mode == 9)
      i.sample_time_us = 1000000;
    if (mode == 10)
      i.sample_time_us = 999999;
    if (mode == 11)
      i.sample_time_us = 1020001;
    if (mode == 12)
      i.sequence = 0;
    if (mode == 13)
      i.sequence = 2;
    if (mode == 14)
      i.specific_force_mps2.x = std::numeric_limits<float>::infinity();
    if (mode == 15)
      i.angular_rate_rad_s.z = std::numeric_limits<float>::quiet_NaN();
    invalid(e.update(i));
    auto fresh = input();
    fresh.sample_time_us = 1030000;
    fresh.sequence = 2;
    invalid(e.update(fresh));
    invalid(e.initialize(input(), reference()));
    e.reset();
    invalid(e.initialize(input(), reference()));
    unreliable(e.initialize(input(), reference(2)));
  }
}
void initialization_vetoes_and_required_identities() {
  for (unsigned mode = 0; mode < 13; ++mode) {
    DynamicMotionEstimator e(config());
    auto i = input();
    auto r = reference();
    if (mode == 0)
      r.externally_stationary = false;
    if (mode == 1)
      r.declaration = 0;
    if (mode == 2)
      i.specific_force_mps2.z = g * 1.11f;
    if (mode == 3)
      i.angular_rate_rad_s.x = .051f;
    if (mode == 4)
      i.specific_force_mps2 = vector(g, 0, 0);
    if (mode == 5)
      i.session_id = 0;
    if (mode == 6)
      i.sensor_id = 0;
    if (mode == 7)
      i.config_generation = 0;
    if (mode == 8)
      i.mount_id = 0;
    if (mode == 9)
      i.calibration_id = 0;
    if (mode == 10)
      i.sensor_epoch = 0;
    if (mode == 11)
      i.timing_source = DynamicTimingSource::Unknown;
    if (mode == 12)
      i.discontinuity = true;
    invalid(e.initialize(i, r));
    invalid(e.initialize(input(), reference(mode == 1 ? 0 : 1)));
    unreliable(e.initialize(input(), reference(2)));
  }
}
void large_times_horizon_sequence_wrap_and_config() {
  auto c = config();
  c.max_horizon_us = 2000001;
  TEST_ASSERT_FALSE(DynamicMotionEstimator::configValid(c));
  c = config();
  c.max_step_us = 0;
  TEST_ASSERT_FALSE(DynamicMotionEstimator::configValid(c));
  c = config();
  c.max_horizon_us = 0;
  TEST_ASSERT_FALSE(DynamicMotionEstimator::configValid(c));
  c = config();
  c.max_step_us = 2000001;
  TEST_ASSERT_FALSE(DynamicMotionEstimator::configValid(c));
  c = config();
  auto i = input();
  i.sample_time_us = std::numeric_limits<uint64_t>::max() - 2000000;
  i.timing_source = DynamicTimingSource::ModelledCadence;
  DynamicMotionEstimator e(c);
  unreliable(e.initialize(i, reference()));
  for (unsigned n = 0; n < 100; ++n) {
    i.sample_time_us += 20000;
    ++i.sequence;
    unreliable(e.update(i));
  }
  TEST_ASSERT_EQUAL_UINT32(2000000, e.estimate().elapsed_us);
  i.sample_time_us = 0;
  ++i.sequence;
  invalid(e.update(i));
  auto h = input();
  DynamicMotionEstimator horizon(c);
  horizon.initialize(h, reference());
  for (unsigned n = 0; n < 100; ++n) {
    h.sample_time_us += 20000;
    ++h.sequence;
    horizon.update(h);
  }
  ++h.sequence;
  ++h.sample_time_us;
  invalid(horizon.update(h));
  auto w = input();
  w.sequence = std::numeric_limits<uint32_t>::max();
  DynamicMotionEstimator wrap(c);
  wrap.initialize(w, reference());
  w.sequence = 0;
  w.sample_time_us += 10000;
  invalid(wrap.update(w));
}
void finite_window_rotation_preserves_norm_and_nonce_exhaustion() {
  DynamicMotionEstimator e(config());
  auto i = input();
  i.timing_source = DynamicTimingSource::ModelledCadence;
  unreliable(e.initialize(i, reference(std::numeric_limits<uint32_t>::max())));
  for (unsigned n = 0; n < 100; ++n) {
    i.sample_time_us += 20000;
    ++i.sequence;
    i.angular_rate_rad_s = vector(0, 0, 1);
    unreliable(e.update(i));
  }
  const auto out = e.estimate();
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .5403023059f, out.quaternion_wxyz[0]);
  TEST_ASSERT_FLOAT_WITHIN(.00001f, .8414709848f, out.quaternion_wxyz[3]);
  TEST_ASSERT_EQUAL(static_cast<int>(DynamicTimingSource::ModelledCadence),
                    static_cast<int>(out.timing_source));
  TEST_ASSERT_FLOAT_WITHIN(.00001f, g, out.gravity_body_mps2.z);
  e.reset();
  invalid(e.initialize(input(), reference(1)));
  invalid(e.initialize(input(), reference(std::numeric_limits<uint32_t>::max())));
  DynamicMotionEstimator untouched(config());
  invalid(untouched.update(input()));
  auto c = config();
  c.max_step_us = 0;
  DynamicMotionEstimator bad(c);
  invalid(bad.initialize(input(), reference()));
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(finite_window_rotation_preserves_norm_and_nonce_exhaustion);
  RUN_TEST(tilted_stationary_anchor_and_disabled_refusal);
  RUN_TEST(signed_axes_and_noncommuting_body_rotation);
  RUN_TEST(translation_turn_ambiguity_and_euler_singularity);
  RUN_TEST(invalid_inputs_reset_and_nonce_cannot_be_reused);
  RUN_TEST(initialization_vetoes_and_required_identities);
  RUN_TEST(large_times_horizon_sequence_wrap_and_config);
  return UNITY_END();
}

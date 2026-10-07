#include "camera_manager.h"
#include "session_clock.h"
#include <unity.h>
using namespace ridesync;
struct FakeClock : Clock {
  uint32_t raw = 0;
  uint32_t now() const override { return raw; }
};
UtcDateTime epoch() { return {2000, 1, 1, 0, 0, 0, 0}; }
void startup_and_reset() {
  FakeClock c;
  c.raw = 200;
  SessionClock s(c, 42, 100);
  c.raw = 223;
  auto r = s.snapshot();
  TEST_ASSERT_EQUAL_UINT64(42, r.session_id);
  TEST_ASSERT_EQUAL_UINT64(23, r.monotonic_ms);
  TEST_ASSERT_EQUAL_INT((int)AnchorQuality::Missing, (int)r.anchor_quality);
  TEST_ASSERT_FALSE(r.has_utc_estimate);
  TEST_ASSERT_FALSE(s.reset(42));
  TEST_ASSERT_FALSE(s.reset(0));
  TEST_ASSERT_TRUE(s.reset(43));
  r = s.snapshot();
  TEST_ASSERT_EQUAL_UINT64(43, r.session_id);
  TEST_ASSERT_EQUAL_UINT64(0, r.monotonic_ms);
  SessionClock invalid(c, 0, 100);
  TEST_ASSERT_EQUAL_INT((int)MonotonicQuality::InvalidSession,
                        (int)invalid.snapshot().monotonic_quality);
  TEST_ASSERT_FALSE(invalid.anchor(epoch()));
  TEST_ASSERT_TRUE(invalid.reset(44));
}
void anchors_expire_and_old_records_keep_their_mapping() {
  FakeClock c;
  SessionClock s(c, 1, 100);
  c.raw = 10;
  TEST_ASSERT_TRUE(s.anchor(epoch(), true, 50));
  c.raw = 110;
  const auto saved = s.snapshot();
  TEST_ASSERT_EQUAL_UINT64(100, saved.anchor_age_ms);
  TEST_ASSERT_EQUAL_UINT64(10, saved.anchor.receipt_ms);
  TEST_ASSERT_EQUAL_INT64(946684800100LL, saved.utc_estimate_ms);
  TEST_ASSERT_TRUE(saved.has_utc_estimate);
  TEST_ASSERT_TRUE(saved.anchor.uncertainty_known);
  TEST_ASSERT_EQUAL_UINT32(50, saved.anchor.uncertainty_ms);
  c.raw = 111;
  auto expired = s.snapshot();
  TEST_ASSERT_EQUAL_INT((int)AnchorQuality::Expired, (int)expired.anchor_quality);
  TEST_ASSERT_FALSE(expired.has_utc_estimate);
  TEST_ASSERT_EQUAL_UINT64(101, expired.anchor_age_ms);
  auto later = epoch();
  later.minute = 1;
  TEST_ASSERT_TRUE(s.anchor(later));
  auto forward = s.snapshot();
  TEST_ASSERT_EQUAL_INT64(946684860000LL, forward.utc_estimate_ms);
  TEST_ASSERT_EQUAL_UINT32(2, forward.anchor.sequence);
  TEST_ASSERT_TRUE(s.anchor(epoch()));
  auto backward = s.snapshot();
  TEST_ASSERT_EQUAL_INT64(946684800000LL, backward.utc_estimate_ms);
  TEST_ASSERT_EQUAL_UINT32(3, backward.anchor.sequence);
  TEST_ASSERT_EQUAL_UINT64(111, backward.monotonic_ms);
  TEST_ASSERT_EQUAL_UINT32(1, saved.anchor.sequence);
  TEST_ASSERT_EQUAL_INT64(946684800100LL, saved.utc_estimate_ms);
  TEST_ASSERT_FALSE(backward.anchor.uncertainty_known);
  TEST_ASSERT_TRUE(s.reset(2));
  TEST_ASSERT_EQUAL_INT((int)AnchorQuality::Missing, (int)s.snapshot().anchor_quality);
}
void calendar_validation_preserves_last_anchor() {
  FakeClock c;
  SessionClock s(c, 1, 100);
  TEST_ASSERT_TRUE(s.anchor({2000, 2, 29, 12, 34, 56, 789}));
  TEST_ASSERT_EQUAL_INT64(951827696789LL, s.snapshot().anchor.utc_ms);
  const UtcDateTime bad[] = {
      {1999, 1, 1, 0, 0, 0, 0},  {2100, 1, 1, 0, 0, 0, 0},   {2001, 2, 29, 0, 0, 0, 0},
      {2000, 0, 1, 0, 0, 0, 0},  {2000, 13, 1, 0, 0, 0, 0},  {2000, 1, 0, 0, 0, 0, 0},
      {2000, 4, 31, 0, 0, 0, 0}, {2000, 1, 1, 24, 0, 0, 0},  {2000, 1, 1, 0, 60, 0, 0},
      {2000, 1, 1, 0, 0, 60, 0}, {2000, 1, 1, 0, 0, 0, 1000}};
  for (const auto &date : bad) {
    TEST_ASSERT_FALSE(s.anchor(date));
    TEST_ASSERT_EQUAL_UINT32(1, s.snapshot().anchor.sequence);
    TEST_ASSERT_EQUAL_INT64(951827696789LL, s.snapshot().anchor.utc_ms);
  }
  TEST_ASSERT_TRUE(s.anchor({2099, 12, 31, 23, 59, 59, 999}));
  TEST_ASSERT_EQUAL_INT64(4102444799999LL, s.snapshot().anchor.utc_ms);
}
void wraps_extend_monotonic_and_duration_exhaustion_is_explicit() {
  FakeClock c;
  c.raw = 0xfffffff0U;
  SessionClock s(c, 1, 100);
  TEST_ASSERT_TRUE(s.anchor(epoch()));
  c.raw = 0x10;
  auto r = s.snapshot();
  TEST_ASSERT_EQUAL_UINT64(32, r.monotonic_ms);
  TEST_ASSERT_EQUAL_UINT64(32, r.anchor_age_ms);
  // Each sampled increment is less than one full wrap period.
  for (int i = 0; i < 14; ++i) {
    c.raw += 2000000000U;
    s.snapshot();
  }
  c.raw += 3535999968U;
  r = s.snapshot();
  TEST_ASSERT_EQUAL_UINT64(31536000000ULL, r.monotonic_ms);
  TEST_ASSERT_EQUAL_INT((int)MonotonicQuality::Valid, (int)r.monotonic_quality);
  ++c.raw;
  r = s.snapshot();
  TEST_ASSERT_EQUAL_INT((int)MonotonicQuality::DurationExceeded, (int)r.monotonic_quality);
  TEST_ASSERT_FALSE(r.has_utc_estimate);
  TEST_ASSERT_FALSE(s.anchor(epoch()));
  c.raw += 200;
  TEST_ASSERT_EQUAL_UINT64(31536000000ULL, s.snapshot().monotonic_ms);
  TEST_ASSERT_TRUE(s.reset(2));
  TEST_ASSERT_EQUAL_UINT64(0, s.snapshot().monotonic_ms);
}
void zero_age_and_unknown_uncertainty_are_explicit() {
  FakeClock c;
  SessionClock s(c, 1, 0);
  TEST_ASSERT_TRUE(s.anchor(epoch(), false, 999));
  auto r = s.snapshot();
  TEST_ASSERT_TRUE(r.has_utc_estimate);
  TEST_ASSERT_FALSE(r.anchor.uncertainty_known);
  TEST_ASSERT_EQUAL_UINT32(0, r.anchor.uncertainty_ms);
  c.raw = 1;
  r = s.snapshot();
  TEST_ASSERT_FALSE(r.has_utc_estimate);
  TEST_ASSERT_EQUAL_INT((int)AnchorQuality::Expired, (int)r.anchor_quality);
}
void uninitialized_timestamp_is_invalid() {
  RecordTimestamp r;
  TEST_ASSERT_EQUAL_INT((int)MonotonicQuality::InvalidSession, (int)r.monotonic_quality);
  TEST_ASSERT_FALSE(r.has_utc_estimate);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(uninitialized_timestamp_is_invalid);
  RUN_TEST(startup_and_reset);
  RUN_TEST(zero_age_and_unknown_uncertainty_are_explicit);
  RUN_TEST(anchors_expire_and_old_records_keep_their_mapping);
  RUN_TEST(calendar_validation_preserves_last_anchor);
  RUN_TEST(wraps_extend_monotonic_and_duration_exhaustion_is_explicit);
  return UNITY_END();
}

void setUp() {}
void tearDown() {}

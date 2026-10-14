#include <initializer_list>
#include <unity.h>
#define HERO12_CHECK(value) TEST_ASSERT_TRUE(value)
#include "../fixtures/hero12_probe/policy_cases.h"
using namespace hero12_probe_test;
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(missing_qualification_refuses_shutter);
  RUN_TEST(explicit_start_stop_use_fresh_encoding_not_a_toggle);
  RUN_TEST(stale_status_and_expired_session_refuse_new_shutter);
  RUN_TEST(disconnect_and_reconnect_clear_prior_admission);
  RUN_TEST(monotonic_wrap_preserves_freshness_and_expiry);
  RUN_TEST(descriptor_search_stops_before_next_characteristic);
  RUN_TEST(pasted_burst_does_not_execute_its_last_record_command);
  RUN_TEST(oversized_and_blank_burst_prefixes_do_not_hide_record);
  RUN_TEST(emergency_disconnect_does_not_authorize_following_burst_record);
  return UNITY_END();
}

#include "wake_radio_policy.h"
#include <unity.h>
using namespace ridesync;
WakeOperation op(uint32_t id = 1, unsigned peer = 0) {
  WakeOperation o;
  o.id = id;
  o.peer = peer;
  o.generation = 1;
  return o;
}
void cancellation_and_deadline_deny_unadmitted_start() {
  WakeRadioPolicy p;
  TEST_ASSERT_TRUE(p.reserve(op(), 100, 0));
  p.seal(op());
  TEST_ASSERT_FALSE(p.admitStart(op(), 1));
  TEST_ASSERT_FALSE(p.reusable());
  p.terminal(op());
  p.barrierReleased(op());
  TEST_ASSERT_TRUE(p.reusable());
  TEST_ASSERT_TRUE(p.reserve(op(2), 200, 100));
  TEST_ASSERT_FALSE(p.admitStart(op(2), 200));
}
void admitted_return_after_cancel_keeps_delivery_uncertain() {
  WakeRadioPolicy p;
  TEST_ASSERT_TRUE(p.reserve(op(), 100, 0));
  TEST_ASSERT_TRUE(p.admitStart(op(), 1));
  p.seal(op());
  p.terminal(op());
  p.barrierReleased(op());
  TEST_ASSERT_FALSE(p.reusable());
  p.returned(op(), 0);
  TEST_ASSERT_TRUE(p.status().submitted);
  TEST_ASSERT_TRUE(p.reusable());
}
void callback_reference_and_barrier_are_independent_release_conditions() {
  WakeRadioPolicy p;
  TEST_ASSERT_TRUE(p.reserve(op(), 100, 0));
  p.callbackEnter();
  p.terminal(op());
  p.barrierReleased(op());
  TEST_ASSERT_FALSE(p.reusable());
  p.callbackExit();
  TEST_ASSERT_TRUE(p.reusable());
  p.callbackEnter();
  TEST_ASSERT_FALSE(p.reusable());
  p.callbackExit();
  TEST_ASSERT_FALSE(p.reusable());
  p.barrierReleased(op());
  TEST_ASSERT_TRUE(p.reusable());
}
void incoming_connection_requires_its_matching_disconnect() {
  WakeRadioPolicy p;
  TEST_ASSERT_TRUE(p.reserve(op(), 100, 0));
  TEST_ASSERT_TRUE(p.incoming(op(), 19));
  p.terminal(op());
  p.barrierReleased(op());
  TEST_ASSERT_FALSE(p.reusable());
  p.disconnected(op(), 20);
  TEST_ASSERT_FALSE(p.reusable());
  p.disconnected(op(), 19);
  TEST_ASSERT_FALSE(p.reusable());
  p.barrierReleased(op());
  TEST_ASSERT_TRUE(p.reusable());
}
void conflicting_owned_handles_quarantine_instead_of_losing_reference() {
  WakeRadioPolicy p;
  TEST_ASSERT_TRUE(p.reserve(op(), 100, 0));
  TEST_ASSERT_TRUE(p.incoming(op(), 19));
  TEST_ASSERT_FALSE(p.incoming(op(), 20));
  p.terminal(op());
  p.disconnected(op(), 19);
  p.barrierReleased(op());
  TEST_ASSERT_FALSE(p.reusable());
  TEST_ASSERT_FALSE(p.reserve(op(2), 100, 0));
}
void old_operations_and_invalid_inputs_never_mutate_new_lease() {
  WakeRadioPolicy p;
  TEST_ASSERT_FALSE(p.reserve(op(0), 100, 0));
  TEST_ASSERT_FALSE(p.reserve(op(UINT32_MAX), 100, 0));
  TEST_ASSERT_FALSE(p.reserve(op(1, 4), 100, 0));
  TEST_ASSERT_FALSE(p.reserve(op(), 0, 0));
  TEST_ASSERT_FALSE(p.reserve(op(), 0x80000000u, 0));
  TEST_ASSERT_TRUE(p.reserve(op(), 100, 0));
  p.terminal(op());
  p.barrierReleased(op());
  TEST_ASSERT_FALSE(p.reserve(op(), 200, 0));
  TEST_ASSERT_TRUE(p.reserve(op(2), 200, 0));
  p.seal(op());
  p.returned(op(), 44);
  p.terminal(op());
  p.barrierReleased(op());
  TEST_ASSERT_TRUE(p.admitStart(op(2), 1));
  TEST_ASSERT_FALSE(p.reusable());
}
void one_start_only_and_rollover_deadline() {
  WakeRadioPolicy p;
  const uint32_t now = UINT32_MAX - 10;
  TEST_ASSERT_TRUE(p.reserve(op(), now + 30, now));
  TEST_ASSERT_TRUE(p.admitStart(op(), now + 29));
  TEST_ASSERT_FALSE(p.admitStart(op(), now + 29));
  p.returned(op(), 3);
  p.terminal(op());
  p.barrierReleased(op());
  TEST_ASSERT_TRUE(p.reusable());
  TEST_ASSERT_EQUAL(3, p.status().sdk_error);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(cancellation_and_deadline_deny_unadmitted_start);
  RUN_TEST(admitted_return_after_cancel_keeps_delivery_uncertain);
  RUN_TEST(callback_reference_and_barrier_are_independent_release_conditions);
  RUN_TEST(incoming_connection_requires_its_matching_disconnect);
  RUN_TEST(conflicting_owned_handles_quarantine_instead_of_losing_reference);
  RUN_TEST(old_operations_and_invalid_inputs_never_mutate_new_lease);
  RUN_TEST(one_start_only_and_rollover_deadline);
  return UNITY_END();
}

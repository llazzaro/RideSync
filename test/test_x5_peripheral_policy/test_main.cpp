#include "x5_peripheral_policy.h"
#include <unity.h>
using namespace ridesync;
Token token(uint32_t connection = 1, uint32_t operation = 1) {
  Token t;
  t.connection = connection;
  t.operation = operation;
  return t;
}
X5ShutterRequest request(uint32_t generation = 1, uint32_t operation = 2) {
  X5ShutterRequest r;
  r.token = token(generation, operation);
  r.handle = 7;
  r.deadline_ms = 5100;
  return r;
}
void open(X5PeripheralPolicy &p) {
  TEST_ASSERT_TRUE(p.begin(token(), 15100, 100));
  TEST_ASSERT_TRUE(p.connected(7, 200));
  p.subscription(true);
}
void lost_photo_transition_survives_full_queue() {
  PeripheralMailbox m;
  TEST_ASSERT_TRUE(m.begin(7));
  X5Input in;
  in.connection = 7;
  in.kind = X5InputKind::Display;
  in.size = 256;
  in.bytes.fill(0xa5);
  for (unsigned n = 0; n < 32; ++n) {
    in.sequence = n + 1;
    TEST_ASSERT_TRUE(m.push(in));
  }
  TEST_ASSERT_FALSE(m.push(in));
  TEST_ASSERT_FALSE(m.takeLoss(8));
  TEST_ASSERT_TRUE(m.takeLoss(7));
  TEST_ASSERT_FALSE(m.takeLoss(7));
  X5Input out;
  for (unsigned n = 0; n < 32; ++n) {
    TEST_ASSERT_TRUE(m.poll(out));
    TEST_ASSERT_EQUAL_UINT32(n + 1, out.sequence);
    TEST_ASSERT_EQUAL_UINT8(0xa5, out.bytes[255]);
  }
  TEST_ASSERT_FALSE(m.poll(out));
}
void mailbox_never_accepts_old_generation() {
  PeripheralMailbox m;
  TEST_ASSERT_TRUE(m.begin(7));
  X5Input input;
  input.connection = 6;
  TEST_ASSERT_FALSE(m.push(input));
  TEST_ASSERT_FALSE(m.takeLoss(7));
  TEST_ASSERT_FALSE(m.begin(7));
  TEST_ASSERT_FALSE(m.begin(6));
  TEST_ASSERT_TRUE(m.begin(8));
  input.connection = 7;
  TEST_ASSERT_FALSE(m.push(input));
}
void unsubscription_after_preparation_prevents_notify() {
  X5PeripheralPolicy p;
  open(p);
  TEST_ASSERT_TRUE(p.enqueue(request()));
  X5ShutterRequest copied;
  TEST_ASSERT_TRUE(p.pending(copied));
  p.subscription(false);
  TEST_ASSERT_FALSE(p.admit(300));
  p.subscription(true);
  TEST_ASSERT_FALSE(p.admit(300));
}
void consumed_notification_never_reenters_sdk() {
  X5PeripheralPolicy p;
  open(p);
  TEST_ASSERT_TRUE(p.enqueue(request()));
  TEST_ASSERT_TRUE(p.admit(300));
  TEST_ASSERT_FALSE(p.admit(300));
  TEST_ASSERT_FALSE(p.enqueue(request()));
  TEST_ASSERT_TRUE(p.enqueue(request(1, 3)));
  TEST_ASSERT_TRUE(p.admit(301));
}
void cancellation_before_submit_discards_request() {
  X5PeripheralPolicy p;
  open(p);
  TEST_ASSERT_TRUE(p.enqueue(request()));
  p.cancel(token(1, 2));
  TEST_ASSERT_FALSE(p.admit(300));
  TEST_ASSERT_FALSE(p.enqueue(request()));
}
void generation_and_handle_must_match_at_submission() {
  X5PeripheralPolicy p;
  open(p);
  TEST_ASSERT_FALSE(p.enqueue(request(2, 2)));
  auto wrong = request();
  wrong.handle = 8;
  TEST_ASSERT_FALSE(p.enqueue(wrong));
  TEST_ASSERT_TRUE(p.enqueue(request()));
  p.terminal();
  TEST_ASSERT_FALSE(p.admit(300));
}
void loss_revokes_all_control_until_new_connection() {
  X5PeripheralPolicy p;
  open(p);
  TEST_ASSERT_TRUE(p.enqueue(request()));
  p.loss();
  TEST_ASSERT_FALSE(p.admit(300));
  TEST_ASSERT_FALSE(p.subscribed());
  p.subscription(true);
  TEST_ASSERT_FALSE(p.enqueue(request(1, 3)));
}
void deadline_and_wrap_are_checked_at_actual_entry() {
  X5PeripheralPolicy p;
  open(p);
  TEST_ASSERT_TRUE(p.enqueue(request()));
  TEST_ASSERT_FALSE(p.admit(5100));
  TEST_ASSERT_FALSE(p.admit(5099));
  X5PeripheralPolicy q;
  auto t = token();
  TEST_ASSERT_TRUE(q.begin(t, 12000, UINT32_MAX - 1000));
  TEST_ASSERT_TRUE(q.connected(7, UINT32_MAX - 900));
  q.subscription(true);
  auto r = request();
  r.deadline_ms = 4000;
  TEST_ASSERT_TRUE(q.enqueue(r));
  TEST_ASSERT_TRUE(q.admit(3999));
  TEST_ASSERT_FALSE(q.admit(4000));
}
void terminal_requires_barrier_callbacks_and_sdk_return() {
  X5PeripheralPolicy p;
  open(p);
  p.sdkEnter();
  p.callbackEnter();
  p.terminal();
  TEST_ASSERT_FALSE(p.released());
  p.barrier();
  TEST_ASSERT_FALSE(p.released());
  p.callbackExit();
  TEST_ASSERT_FALSE(p.released());
  p.sdkExit();
  TEST_ASSERT_TRUE(p.released());
  TEST_ASSERT_FALSE(p.begin(token(1, 4), 15100, 100));
  TEST_ASSERT_TRUE(p.begin(token(2, 4), 15100, 100));
}
void seal_is_not_proof_of_sdk_terminal() {
  X5PeripheralPolicy p;
  open(p);
  p.seal();
  TEST_ASSERT_FALSE(p.alive());
  TEST_ASSERT_FALSE(p.released());
  TEST_ASSERT_FALSE(p.begin(token(2, 2), 15100, 100));
  p.terminal();
  TEST_ASSERT_FALSE(p.released());
  p.barrier();
  TEST_ASSERT_TRUE(p.released());
}
void delayed_return_cannot_replay_consumed_command() {
  X5PeripheralPolicy p;
  open(p);
  TEST_ASSERT_TRUE(p.enqueue(request()));
  TEST_ASSERT_TRUE(p.admit(300));
  p.sdkEnter();
  p.seal();
  p.terminal();
  p.barrier();
  TEST_ASSERT_FALSE(p.released());
  p.sdkExit();
  TEST_ASSERT_TRUE(p.released());
  TEST_ASSERT_FALSE(p.enqueue(request()));
  TEST_ASSERT_TRUE(p.begin(token(2, 9), 15100, 100));
  TEST_ASSERT_FALSE(p.admit(100));
}
void invalid_start_and_expired_connection_fail_closed() {
  X5PeripheralPolicy p;
  TEST_ASSERT_FALSE(p.begin(token(0, 1), 15000, 0));
  TEST_ASSERT_FALSE(p.begin(token(1, UINT32_MAX), 15000, 0));
  TEST_ASSERT_FALSE(p.begin(token(), 15001, 0));
  TEST_ASSERT_TRUE(p.begin(token(), 15000, 0));
  TEST_ASSERT_FALSE(p.connected(7, 15000));
  TEST_ASSERT_FALSE(p.alive());
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(lost_photo_transition_survives_full_queue);
  RUN_TEST(mailbox_never_accepts_old_generation);
  RUN_TEST(unsubscription_after_preparation_prevents_notify);
  RUN_TEST(consumed_notification_never_reenters_sdk);
  RUN_TEST(cancellation_before_submit_discards_request);
  RUN_TEST(generation_and_handle_must_match_at_submission);
  RUN_TEST(loss_revokes_all_control_until_new_connection);
  RUN_TEST(deadline_and_wrap_are_checked_at_actual_entry);
  RUN_TEST(terminal_requires_barrier_callbacks_and_sdk_return);
  RUN_TEST(seal_is_not_proof_of_sdk_terminal);
  RUN_TEST(delayed_return_cannot_replay_consumed_command);
  RUN_TEST(invalid_start_and_expired_connection_fail_closed);
  return UNITY_END();
}

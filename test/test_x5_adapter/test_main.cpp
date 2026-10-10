#include "../fixtures/insta360/ce80_display.h"
#include "profiles/insta360_x5.h"
#include <cstring>
#include <deque>
#include <unity.h>
using namespace ridesync;
using namespace insta360_fixture;
struct FakeClock : Clock {
  uint32_t time = 1000;
  uint32_t now() const override { return time; }
};
struct FakePeripheral : X5PeripheralPort {
  std::deque<X5Input> events;
  X5Qualification q;
  Token connect_token;
  X5ShutterRequest request;
  uint32_t notifications = 0, connects = 0, closes = 0;
  uint32_t seq = 0, cutoff = 0;
  bool loss = false, accept = true, release = true;
  bool configure(const X5Qualification &value) override {
    q = value;
    return accept;
  }
  bool connect(Token t, uint32_t) override {
    connect_token = t;
    ++connects;
    return accept;
  }
  bool notify(const X5ShutterRequest &value) override {
    request = value;
    cutoff = seq;
    ++notifications;
    return accept;
  }
  void cancel(Token) override {}
  void close(uint32_t) override { ++closes; }
  bool poll(X5Input &input) override {
    if (events.empty())
      return false;
    input = events.front();
    events.pop_front();
    return true;
  }
  bool takeLoss(uint32_t) override {
    bool out = loss;
    loss = false;
    return out;
  }
  bool released(uint32_t) const override { return release; }
  void service(uint32_t) override {}
};
struct Harness {
  FakeClock clock;
  FakePeripheral port;
  X5Adapter adapter;
  CameraManager manager;
  X5Qualification q;
  SourceConfig config;
  Harness() : adapter(port, clock), manager(clock, adapter, X5Adapter::managerPolicy()) {
    q.enabled = true;
    q.identity.verified = true;
    q.identity.type = IdentityType::Public;
    q.identity.address = {{1, 2, 3, 4, 5, 6}};
    q.store.qualification_record = 1;
    q.display.profile = insta360::Ce80DisplayProfile::X5CapturedDisplayV1;
    std::memcpy(q.firmware.data(), "1.11.10", 7);
    q.firmware_size = 7;
    config.count = 1;
    config.cameras[0].name = "Synthetic X5";
    config.cameras[0].family = CameraFamily::Insta360;
    config.cameras[0].model = CameraModel::X5;
    config.cameras[0].identifier = "06:05:04:03:02:01";
    config.cameras[0].address_type = AddressType::Public;
    TEST_ASSERT_TRUE(adapter.attach(manager));
    TEST_ASSERT_TRUE(adapter.configure(q));
    TEST_ASSERT_TRUE(manager.configure(config).ok());
  }
  void service() {
    adapter.service();
    manager.tick();
  }
  void event(X5InputKind kind, int status = 0) {
    X5Input input;
    input.kind = kind;
    input.connection = manager.state(0)->token.connection;
    input.operation = manager.state(0)->token.operation;
    input.sequence = ++port.seq;
    input.received_ms = clock.time;
    input.handle = 12;
    input.identity = q.identity;
    input.status = status;
    port.events.push_back(input);
  }
  template <size_t N> void frame(const uint8_t (&bytes)[N]) {
    event(X5InputKind::Display);
    auto &input = port.events.back();
    input.size = N;
    std::memcpy(input.bytes.data(), bytes, N);
  }
  void connected(bool video = true) {
    TEST_ASSERT_EQUAL_INT(int(CameraError::None), int(manager.request(0, Operation::Connect)));
    event(X5InputKind::Connected);
    event(X5InputKind::Subscribed);
    service();
    TEST_ASSERT_EQUAL_INT(int(Lifecycle::Ready), int(manager.state(0)->lifecycle));
    if (video) {
      frame(kCe80Video);
      service();
    }
  }
  void returned(int status = 0) {
    event(X5InputKind::SendReturned, status);
    port.events.back().sequence = port.cutoff;
    service();
  }
  void request(Operation op) {
    TEST_ASSERT_TRUE(adapter.ready(op));
    TEST_ASSERT_EQUAL_INT(int(CameraError::None), int(manager.request(0, op)));
    service();
  }
};
void connect_does_not_prove_recording() {
  Harness h;
  h.connected(false);
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Unknown), int(h.manager.state(0)->observed));
  TEST_ASSERT_FALSE(h.adapter.ready(Operation::Start));
}
void send_acceptance_does_not_prove_recording() {
  Harness h;
  h.connected();
  h.request(Operation::Start);
  h.returned();
  TEST_ASSERT_EQUAL_UINT32(1, h.port.notifications);
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Operating), int(h.manager.state(0)->lifecycle));
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Unknown), int(h.adapter.observed()));
  h.frame(kCe80Timer);
  h.service();
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Recording), int(h.manager.state(0)->observed));
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Ready), int(h.manager.state(0)->lifecycle));
  h.request(Operation::Stop);
  h.returned();
  h.frame(kCe80VideoUpdate);
  h.service();
  TEST_ASSERT_EQUAL_UINT32(2, h.port.notifications);
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Stopped), int(h.manager.state(0)->observed));
}
void ambiguous_send_never_retries() {
  Harness h;
  h.connected();
  h.request(Operation::Start);
  h.returned(-4);
  h.clock.time += 20000;
  h.service();
  TEST_ASSERT_EQUAL_UINT32(1, h.port.notifications);
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Failed), int(h.manager.state(0)->lifecycle));
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Unknown), int(h.adapter.observed()));
}
void missing_response_expires_without_retry() {
  Harness h;
  h.connected();
  h.request(Operation::Start);
  h.returned();
  h.clock.time += 5000;
  h.service();
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Failed), int(h.manager.state(0)->lifecycle));
  h.clock.time += 20000;
  h.service();
  TEST_ASSERT_EQUAL_UINT32(1, h.port.notifications);
  TEST_ASSERT_EQUAL_INT(int(X5Failure::Timeout), int(h.adapter.failure()));
}
void freshness_boundary_and_remaining_is_not_stop() {
  Harness h;
  h.connected();
  h.clock.time += 5000;
  h.frame(kCe80Runtime);
  h.service();
  TEST_ASSERT_TRUE(h.adapter.ready(Operation::Start));
  h.clock.time += 1;
  h.frame(kCe80Count);
  h.service();
  TEST_ASSERT_FALSE(h.adapter.ready(Operation::Start));
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Unknown), int(h.manager.state(0)->observed));
}
void photo_and_unqualified_timer_refuse_toggle() {
  Harness h;
  h.connected();
  h.frame(kCe80Photo);
  h.service();
  TEST_ASSERT_FALSE(h.adapter.ready(Operation::Start));
  h.frame(kCe80Timer);
  h.service();
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Recording), int(h.adapter.observed()));
  TEST_ASSERT_FALSE(h.adapter.ready(Operation::Stop));
  TEST_ASSERT_EQUAL_UINT32(0, h.port.notifications);
}
void already_desired_is_noop() {
  Harness h;
  h.connected();
  h.request(Operation::Stop);
  TEST_ASSERT_EQUAL_UINT32(0, h.port.notifications);
  h.frame(kCe80Timer);
  h.service();
  h.request(Operation::Start);
  TEST_ASSERT_EQUAL_UINT32(0, h.port.notifications);
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Ready), int(h.manager.state(0)->lifecycle));
}
void pre_submission_frame_cannot_complete_new_toggle() {
  Harness h;
  h.connected();
  h.request(Operation::Start);
  // A physically queued receipt at submission is not a command observation.
  h.frame(kCe80Timer);
  h.port.events.back().sequence = h.port.cutoff;
  h.returned();
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Operating), int(h.manager.state(0)->lifecycle));
  h.frame(kCe80Timer);
  h.service();
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Ready), int(h.manager.state(0)->lifecycle));
}
void observation_during_notify_waits_for_cutoff() {
  Harness h;
  h.connected();
  h.request(Operation::Start);
  h.frame(kCe80Timer);
  h.service();
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Operating), int(h.manager.state(0)->lifecycle));
  h.returned();
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Ready), int(h.manager.state(0)->lifecycle));
}
void input_loss_clears_mode_and_fails_pending_command() {
  Harness h;
  h.connected();
  h.request(Operation::Start);
  h.port.loss = true;
  h.service();
  h.frame(kCe80Timer);
  h.service();
  TEST_ASSERT_FALSE(h.adapter.ready(Operation::Stop));
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Failed), int(h.manager.state(0)->lifecycle));
  TEST_ASSERT_EQUAL_UINT32(1, h.port.notifications);
}
void query_is_passive_and_bounded() {
  Harness h;
  h.connected(false);
  h.request(Operation::Query);
  TEST_ASSERT_EQUAL_UINT32(0, h.port.notifications);
  h.clock.time += 4999;
  h.service();
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Operating), int(h.manager.state(0)->lifecycle));
  h.clock.time += 1;
  h.service();
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Failed), int(h.manager.state(0)->lifecycle));
}
void query_accepts_photo_observation_without_toggle() {
  Harness h;
  h.connected(false);
  h.request(Operation::Query);
  h.frame(kCe80Photo);
  h.service();
  TEST_ASSERT_EQUAL_UINT32(0, h.port.notifications);
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Ready), int(h.manager.state(0)->lifecycle));
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Stopped), int(h.manager.state(0)->observed));
}
void disconnect_and_old_generation_cannot_replay() {
  Harness h;
  h.connected();
  h.request(Operation::Start);
  auto old = h.manager.state(0)->token;
  h.event(X5InputKind::Disconnected);
  h.service();
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Idle), int(h.manager.state(0)->lifecycle));
  h.connected(false);
  h.frame(kCe80Video);
  h.port.events.back().connection = old.connection;
  h.service();
  TEST_ASSERT_FALSE(h.adapter.ready(Operation::Start));
  TEST_ASSERT_EQUAL_UINT32(1, h.port.notifications);
}
void subscription_loss_before_submission_discards_toggle() {
  Harness h;
  h.connected();
  h.manager.request(0, Operation::Start);
  h.event(X5InputKind::Unsubscribed);
  h.service();
  TEST_ASSERT_EQUAL_UINT32(0, h.port.notifications);
  TEST_ASSERT_FALSE(h.adapter.ready(Operation::Start));
}
void malformed_or_oversize_display_invalidates_state() {
  Harness h;
  h.connected();
  h.frame(kCe80Video);
  h.port.events.back().size = 257;
  h.service();
  TEST_ASSERT_FALSE(h.adapter.ready(Operation::Start));
  h.frame(kCe80Video);
  h.service();
  h.frame(kCe80Timer);
  h.port.events.back().bytes[0] = 0;
  h.service();
  TEST_ASSERT_FALSE(h.adapter.ready(Operation::Stop));
}
void bad_peer_and_connect_timeout_do_not_become_ready() {
  Harness h;
  h.manager.request(0, Operation::Connect);
  h.event(X5InputKind::Connected);
  h.port.events.back().identity.address[0] ^= 1;
  h.event(X5InputKind::Subscribed);
  h.service();
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Failed), int(h.manager.state(0)->lifecycle));
  TEST_ASSERT_EQUAL_UINT32(0, h.port.notifications);
}
void missing_subscription_hits_original_deadline() {
  Harness h;
  h.manager.request(0, Operation::Connect);
  h.event(X5InputKind::Connected);
  h.service();
  h.clock.time += 15000;
  h.service();
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Failed), int(h.manager.state(0)->lifecycle));
  TEST_ASSERT_EQUAL_UINT32(1, h.port.connects);
}
void cancellation_and_reset_leave_no_recording_intent() {
  Harness h;
  h.connected();
  h.manager.request(0, Operation::Start);
  h.manager.cancel(0);
  h.service();
  TEST_ASSERT_EQUAL_UINT32(0, h.port.notifications);
  h.manager.reset();
  h.service();
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Unknown), int(h.adapter.observed()));
}
void wrap_safe_deadline_and_generation_exhaustion() {
  Harness h;
  h.clock.time = UINT32_MAX - 2000;
  h.connected();
  h.request(Operation::Start);
  h.returned();
  h.clock.time += 5000;
  h.service();
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Failed), int(h.manager.state(0)->lifecycle));
  CameraConfig c = h.config.cameras[0];
  Token t;
  t.connection = UINT32_MAX;
  TEST_ASSERT_FALSE(h.adapter.begin(0, c, Operation::Connect, t));
}
void invalid_qualification_and_wrong_model_are_refused() {
  Harness h;
  auto bad = h.q;
  bad.identity.verified = false;
  TEST_ASSERT_FALSE(h.adapter.configure(bad));
  TEST_ASSERT_FALSE(h.adapter.ready(Operation::Connect));
  TEST_ASSERT_TRUE(h.adapter.configure(h.q));
  CameraConfig c = h.config.cameras[0];
  c.model = CameraModel::GO3S;
  Token t;
  t.connection = t.operation = 1;
  TEST_ASSERT_FALSE(h.adapter.begin(0, c, Operation::Connect, t));
  bad = h.q;
  bad.firmware_size = 16;
  TEST_ASSERT_FALSE(h.adapter.configure(bad));
}

struct Audit : CameraAudit {
  unsigned accepted_count = 0;
  void request(size_t, Operation, CameraError, bool, uint32_t) override {}
  void attempt(size_t, Operation, Token, uint32_t, bool) override {}
  void accepted(const Event &, Operation, uint32_t) override { ++accepted_count; }
  void cancelled(size_t, Operation, Token, uint32_t) override {}
  void failure(size_t, Operation, Token, uint32_t, CameraError) override {}
  void wireAck(size_t, Operation, Token, uint32_t, CameraAckDomain, CameraAckAction) override {}
};
void transport_entrypoints_never_deliver_inline_events() {
  Harness h;
  Audit audit;
  h.manager.attachAudit(audit);
  h.connected();
  h.manager.request(0, Operation::Start);
  unsigned before = audit.accepted_count;
  h.manager.cancel(0);
  TEST_ASSERT_EQUAL_UINT32(before, audit.accepted_count);
  h.manager.detachAudit(&audit);
}
void wrong_handle_send_return_cannot_complete_operation() {
  Harness h;
  h.connected();
  h.request(Operation::Start);
  h.event(X5InputKind::SendReturned);
  h.port.events.back().sequence = h.port.cutoff;
  h.port.events.back().handle = 99;
  h.frame(kCe80Timer);
  h.service();
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Operating), int(h.manager.state(0)->lifecycle));
}
void queued_state_change_revokes_pending_toggle() {
  Harness h;
  h.connected();
  h.manager.request(0, Operation::Start);
  h.frame(kCe80Timer);
  h.service();
  TEST_ASSERT_EQUAL_UINT32(0, h.port.notifications);
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Failed), int(h.manager.state(0)->lifecycle));
}
void old_or_future_receipts_cannot_refresh_state() {
  Harness h;
  h.connected();
  h.frame(kCe80Timer);
  h.port.events.back().received_ms = h.clock.time + 1;
  h.service();
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Unknown), int(h.adapter.observed()));
  h.frame(kCe80Video);
  h.port.events.back().received_ms = h.clock.time - 5001;
  h.service();
  TEST_ASSERT_FALSE(h.adapter.ready(Operation::Start));
}
void terminal_barrier_blocks_reconnect_until_released() {
  Harness h;
  h.connected();
  h.event(X5InputKind::Disconnected);
  h.port.release = false;
  h.service();
  TEST_ASSERT_FALSE(h.adapter.ready(Operation::Connect));
  h.port.release = true;
  TEST_ASSERT_TRUE(h.adapter.ready(Operation::Connect));
}
void configuration_validation_is_fail_closed() {
  Harness h;
  auto q = h.q;
  q.enabled = false;
  TEST_ASSERT_FALSE(h.adapter.configure(q));
  q = h.q;
  q.store.qualification_record = 0;
  TEST_ASSERT_FALSE(h.adapter.configure(q));
  q = h.q;
  q.identity.type = IdentityType::UnresolvedPrivate;
  TEST_ASSERT_FALSE(h.adapter.configure(q));
  q = h.q;
  q.display.profile = static_cast<insta360::Ce80DisplayProfile>(255);
  TEST_ASSERT_FALSE(h.adapter.configure(q));
  q = h.q;
  q.firmware[9] = 'x';
  TEST_ASSERT_FALSE(h.adapter.configure(q));
  TEST_ASSERT_FALSE(h.adapter.ready(Operation::Connect));
  TEST_ASSERT_FALSE(h.adapter.attach(h.manager));
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(connect_does_not_prove_recording);
  RUN_TEST(send_acceptance_does_not_prove_recording);
  RUN_TEST(ambiguous_send_never_retries);
  RUN_TEST(missing_response_expires_without_retry);
  RUN_TEST(freshness_boundary_and_remaining_is_not_stop);
  RUN_TEST(photo_and_unqualified_timer_refuse_toggle);
  RUN_TEST(already_desired_is_noop);
  RUN_TEST(pre_submission_frame_cannot_complete_new_toggle);
  RUN_TEST(observation_during_notify_waits_for_cutoff);
  RUN_TEST(input_loss_clears_mode_and_fails_pending_command);
  RUN_TEST(query_is_passive_and_bounded);
  RUN_TEST(query_accepts_photo_observation_without_toggle);
  RUN_TEST(disconnect_and_old_generation_cannot_replay);
  RUN_TEST(subscription_loss_before_submission_discards_toggle);
  RUN_TEST(malformed_or_oversize_display_invalidates_state);
  RUN_TEST(bad_peer_and_connect_timeout_do_not_become_ready);
  RUN_TEST(missing_subscription_hits_original_deadline);
  RUN_TEST(cancellation_and_reset_leave_no_recording_intent);
  RUN_TEST(wrap_safe_deadline_and_generation_exhaustion);
  RUN_TEST(invalid_qualification_and_wrong_model_are_refused);
  RUN_TEST(transport_entrypoints_never_deliver_inline_events);
  RUN_TEST(wrong_handle_send_return_cannot_complete_operation);
  RUN_TEST(queued_state_change_revokes_pending_toggle);
  RUN_TEST(old_or_future_receipts_cannot_refresh_state);
  RUN_TEST(terminal_barrier_blocks_reconnect_until_released);
  RUN_TEST(configuration_validation_is_fail_closed);
  return UNITY_END();
}

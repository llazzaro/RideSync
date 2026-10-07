#include "camera_manager.h"
#include <unity.h>
#include <vector>
using namespace ridesync;
struct FakeClock : Clock {
  uint32_t time = 0;
  uint32_t now() const override { return time; }
};
struct FakeTransport : CameraTransport {
  struct Call {
    size_t peer;
    Operation op;
    Token token;
  };
  std::vector<Call> calls;
  std::vector<Token> cancelled;
  std::vector<size_t> closed;
  bool accept = true;
  bool begin(size_t p, const CameraConfig &, Operation op, Token t) override {
    calls.push_back({p, op, t});
    return accept;
  }
  void cancel(size_t, Token t) override { cancelled.push_back(t); }
  void close(size_t p, Token) override { closed.push_back(p); }
};
SourceConfig config() {
  SourceConfig c;
  c.count = 4;
  for (size_t i = 0; i < c.count; ++i) {
    auto &p = c.cameras[i];
    p.name = "Camera";
    p.family = CameraFamily::Insta360;
    p.model = CameraModel::X5;
    p.identifier = "01:23:45:67:89:A0";
    p.identifier.back() = "0123"[i];
    p.address_type = AddressType::Random;
  }
  c.cameras[3].enabled = false;
  return c;
}
Event completion(size_t p, Token t) {
  Event e{p, t, EventKind::Completed};
  e.capabilities.start = CapabilityState::Supported;
  e.capabilities.stop = CapabilityState::Supported;
  e.capabilities.query = CapabilityState::Supported;
  return e;
}
void connect(CameraManager &m, size_t p) {
  TEST_ASSERT_EQUAL((int)CameraError::None, (int)m.request(p, Operation::Connect));
  TEST_ASSERT_TRUE(m.event(completion(p, m.state(p)->token)));
}
void requests_do_not_imply_observation() {
  FakeClock c;
  FakeTransport t;
  CameraManager m(c, t);
  m.configure(config());
  connect(m, 0);
  TEST_ASSERT_EQUAL((int)RecordingState::Unknown, (int)m.state(0)->observed);
  TEST_ASSERT_EQUAL((int)CameraError::None, (int)m.request(0, Operation::Start));
  TEST_ASSERT_EQUAL((int)RecordingState::Recording, (int)m.state(0)->desired);
  TEST_ASSERT_TRUE(m.event(completion(0, m.state(0)->token)));
  TEST_ASSERT_EQUAL((int)RecordingState::Unknown, (int)m.state(0)->observed);
  c.time = 17;
  Event e{0, m.state(0)->token, EventKind::RecordingObserved};
  e.recording = RecordingState::Recording;
  TEST_ASSERT_TRUE(m.event(e));
  TEST_ASSERT_EQUAL((int)RecordingState::Recording, (int)m.state(0)->observed);
  TEST_ASSERT_TRUE(m.state(0)->has_observation);
  TEST_ASSERT_EQUAL(17, m.state(0)->last_observed_ms);
  TEST_ASSERT_EQUAL((int)CameraError::Disabled, (int)m.request(3, Operation::Connect));
  TEST_ASSERT_EQUAL((int)CameraError::InvalidPeer, (int)m.request(4, Operation::Connect));
}
void deadlines_backoff_and_peer_isolation() {
  FakeClock c;
  FakeTransport t;
  RetryPolicy p;
  p.timeout_ms = 10;
  p.backoff_ms = 5;
  p.max_attempts = 2;
  CameraManager m(c, t, p);
  m.configure(config());
  m.request(0, Operation::Connect);
  c.time = 4;
  m.request(1, Operation::Connect);
  c.time = 10;
  m.tick();
  TEST_ASSERT_EQUAL((int)Lifecycle::Backoff, (int)m.state(0)->lifecycle);
  TEST_ASSERT_EQUAL((int)Lifecycle::Connecting, (int)m.state(1)->lifecycle);
  TEST_ASSERT_TRUE(m.event(completion(1, m.state(1)->token)));
  c.time = 14;
  m.tick();
  TEST_ASSERT_EQUAL(1, m.state(0)->attempts);
  c.time = 15;
  m.tick();
  TEST_ASSERT_EQUAL(2, m.state(0)->attempts);
  c.time = 25;
  m.tick();
  TEST_ASSERT_EQUAL((int)Lifecycle::Failed, (int)m.state(0)->lifecycle);
  TEST_ASSERT_EQUAL((int)CameraError::Timeout, (int)m.state(0)->error);
  TEST_ASSERT_EQUAL((int)Lifecycle::Ready, (int)m.state(1)->lifecycle);
  c.time = 1000;
  m.tick();
  TEST_ASSERT_EQUAL(2, m.state(0)->attempts);
}
void rollover_deadline_and_backoff() {
  FakeClock c;
  c.time = UINT32_MAX - 4;
  FakeTransport t;
  RetryPolicy p;
  p.timeout_ms = 10;
  p.backoff_ms = 5;
  p.max_attempts = 2;
  CameraManager m(c, t, p);
  m.configure(config());
  m.request(0, Operation::Connect);
  c.time = 4;
  m.tick();
  TEST_ASSERT_EQUAL((int)Lifecycle::Connecting, (int)m.state(0)->lifecycle);
  c.time = 5;
  m.tick();
  TEST_ASSERT_EQUAL((int)Lifecycle::Backoff, (int)m.state(0)->lifecycle);
  c.time = 9;
  m.tick();
  TEST_ASSERT_EQUAL(1, m.state(0)->attempts);
  c.time = 10;
  m.tick();
  TEST_ASSERT_EQUAL(2, m.state(0)->attempts);
}
void generations_cancel_reset_and_queue() {
  FakeClock c;
  FakeTransport t;
  CameraManager m(c, t);
  m.configure(config());
  connect(m, 0);
  m.request(0, Operation::Start);
  Token old = m.state(0)->token;
  for (size_t i = 0; i < CameraManager::kQueueDepth; ++i)
    TEST_ASSERT_EQUAL((int)CameraError::None, (int)m.request(0, Operation::Query));
  TEST_ASSERT_EQUAL((int)CameraError::QueueFull, (int)m.request(0, Operation::Stop));
  TEST_ASSERT_EQUAL((int)RecordingState::Recording, (int)m.state(0)->desired);
  m.cancel(0);
  TEST_ASSERT_FALSE(m.event(completion(0, old)));
  TEST_ASSERT_EQUAL((int)CameraError::Cancelled, (int)m.state(0)->error);
  TEST_ASSERT_EQUAL((int)RecordingState::Unknown, (int)m.state(0)->observed);
  m.request(0, Operation::Query);
  Token query = m.state(0)->token;
  TEST_ASSERT_TRUE(m.event(completion(0, query)));
  TEST_ASSERT_EQUAL(3, t.calls.size()); // connect, start, query; cancelled queue never sent
  m.reset();
  TEST_ASSERT_FALSE(m.event(completion(0, query)));
  TEST_ASSERT_EQUAL((int)RecordingState::Unknown, (int)m.state(0)->observed);
  connect(m, 0);
  TEST_ASSERT_FALSE(m.event(completion(0, query)));
  TEST_ASSERT_TRUE(m.state(0)->token.connection != query.connection);
}
void stale_retry_and_serialized_operations() {
  FakeClock c;
  FakeTransport t;
  RetryPolicy p;
  p.timeout_ms = 10;
  p.backoff_ms = 5;
  CameraManager m(c, t, p);
  m.configure(config());
  m.request(0, Operation::Connect);
  Token old = m.state(0)->token;
  c.time = 10;
  m.tick();
  TEST_ASSERT_FALSE(m.event(completion(0, old)));
  c.time = 15;
  m.tick();
  TEST_ASSERT_FALSE(m.event(completion(0, old)));
  TEST_ASSERT_TRUE(m.event(completion(0, m.state(0)->token)));
  m.request(0, Operation::Start);
  Token start = m.state(0)->token;
  m.request(0, Operation::Stop);
  TEST_ASSERT_EQUAL((int)RecordingState::Stopped, (int)m.state(0)->desired);
  TEST_ASSERT_EQUAL(3, t.calls.size());
  TEST_ASSERT_TRUE(m.event(completion(0, start)));
  TEST_ASSERT_EQUAL(4, t.calls.size());
  TEST_ASSERT_EQUAL((int)Operation::Stop, (int)t.calls.back().op);
  TEST_ASSERT_FALSE(m.event(completion(0, start)));
}
void failure_disconnect_and_invalid_configuration() {
  FakeClock c;
  FakeTransport t;
  CameraManager m(c, t);
  m.configure(config());
  TEST_ASSERT_EQUAL((int)CameraError::NotConnected, (int)m.request(0, Operation::Start));
  connect(m, 0);
  TEST_ASSERT_EQUAL((int)CameraError::Unsupported, (int)m.request(0, Operation::Wake));
  Event e{0, m.state(0)->token, EventKind::RecordingObserved};
  e.recording = RecordingState::Recording;
  m.event(e);
  e.kind = EventKind::Disconnected;
  TEST_ASSERT_TRUE(m.event(e));
  TEST_ASSERT_EQUAL((int)RecordingState::Unknown, (int)m.state(0)->observed);
  TEST_ASSERT_FALSE(m.event(e));
  auto invalid = config();
  invalid.count = kMaxCameras + 1;
  TEST_ASSERT_FALSE(m.configure(invalid).ok());
  TEST_ASSERT_EQUAL(4, m.size());
  RetryPolicy bad;
  bad.timeout_ms = 0;
  CameraManager broken(c, t, bad);
  broken.configure(config());
  TEST_ASSERT_EQUAL((int)CameraError::InvalidPolicy, (int)broken.request(0, Operation::Connect));
}
void transport_rejection_is_bounded() {
  FakeClock c;
  FakeTransport t;
  t.accept = false;
  RetryPolicy p;
  p.max_attempts = 2;
  p.backoff_ms = 5;
  CameraManager m(c, t, p);
  m.configure(config());
  m.request(0, Operation::Connect);
  TEST_ASSERT_EQUAL((int)Lifecycle::Backoff, (int)m.state(0)->lifecycle);
  c.time = 5;
  m.tick();
  TEST_ASSERT_EQUAL((int)Lifecycle::Failed, (int)m.state(0)->lifecycle);
  TEST_ASSERT_EQUAL((int)CameraError::Transport, (int)m.state(0)->error);
  TEST_ASSERT_EQUAL(2, m.state(0)->attempts);
}
void expired_callbacks_cannot_complete_operations() {
  FakeClock c;
  FakeTransport t;
  RetryPolicy p;
  p.timeout_ms = 10;
  CameraManager m(c, t, p);
  m.configure(config());
  m.request(0, Operation::Connect);
  const auto token = m.state(0)->token;
  c.time = 10;
  TEST_ASSERT_FALSE(m.event(completion(0, token)));
  TEST_ASSERT_EQUAL((int)Lifecycle::Backoff, (int)m.state(0)->lifecycle);
}
void resets_release_ready_links_and_reconfiguration_is_atomic() {
  FakeClock c;
  FakeTransport t;
  CameraManager m(c, t);
  m.configure(config());
  connect(m, 0);
  connect(m, 1);
  const auto token = m.state(0)->token;
  auto bad = config();
  bad.cameras[0].identifier = "invalid";
  TEST_ASSERT_FALSE(m.configure(bad).ok());
  TEST_ASSERT_EQUAL(0, t.closed.size());
  TEST_ASSERT_EQUAL((int)Lifecycle::Ready, (int)m.state(0)->lifecycle);
  m.reset();
  TEST_ASSERT_EQUAL(2, t.closed.size());
  TEST_ASSERT_FALSE(m.event(completion(0, token)));
  connect(m, 0);
  const auto old = m.state(0)->token;
  SourceConfig empty;
  TEST_ASSERT_TRUE(m.configure(empty).ok());
  TEST_ASSERT_EQUAL(0, m.size());
  TEST_ASSERT_NULL(m.state(0));
  TEST_ASSERT_FALSE(m.event(completion(0, old)));
  TEST_ASSERT_EQUAL(3, t.closed.size());
}
void backoff_wrap_and_attempt_policy_bounds() {
  FakeClock c;
  c.time = UINT32_MAX - 9;
  FakeTransport t;
  RetryPolicy p;
  p.timeout_ms = 5;
  p.backoff_ms = 10;
  p.max_attempts = 1;
  CameraManager m(c, t, p);
  m.configure(config());
  m.request(0, Operation::Connect);
  c.time = UINT32_MAX - 4;
  m.tick();
  TEST_ASSERT_EQUAL((int)Lifecycle::Failed, (int)m.state(0)->lifecycle);
  p.max_attempts = 2;
  c.time = UINT32_MAX - 14;
  CameraManager retry(c, t, p);
  retry.configure(config());
  retry.request(0, Operation::Connect);
  c.time = UINT32_MAX - 9;
  retry.tick();
  TEST_ASSERT_EQUAL((int)Lifecycle::Backoff, (int)retry.state(0)->lifecycle);
  c.time = UINT32_MAX;
  retry.tick();
  TEST_ASSERT_EQUAL(1, retry.state(0)->attempts);
  c.time = 0;
  retry.tick();
  TEST_ASSERT_EQUAL(2, retry.state(0)->attempts);
  p.max_attempts = 6;
  CameraManager bad(c, t, p);
  bad.configure(config());
  TEST_ASSERT_EQUAL((int)CameraError::InvalidPolicy, (int)bad.request(0, Operation::Connect));
  p.max_attempts = 2;
  p.backoff_ms = UINT32_MAX;
  CameraManager badTimer(c, t, p);
  badTimer.configure(config());
  TEST_ASSERT_EQUAL((int)CameraError::InvalidPolicy, (int)badTimer.request(0, Operation::Connect));
}
void asynchronous_failure_and_disabled_peer_are_isolated() {
  FakeClock c;
  FakeTransport t;
  RetryPolicy p;
  p.max_attempts = 1;
  CameraManager m(c, t, p);
  m.configure(config());
  connect(m, 0);
  connect(m, 1);
  m.request(0, Operation::Start);
  Event failed{0, m.state(0)->token, EventKind::Failed};
  TEST_ASSERT_TRUE(m.event(failed));
  TEST_ASSERT_EQUAL((int)Lifecycle::Failed, (int)m.state(0)->lifecycle);
  TEST_ASSERT_EQUAL((int)Lifecycle::Ready, (int)m.state(1)->lifecycle);
  TEST_ASSERT_EQUAL((int)CameraError::None, (int)m.request(1, Operation::Query));
  TEST_ASSERT_EQUAL((int)CameraError::Disabled, (int)m.cancel(3));
  TEST_ASSERT_EQUAL((int)Lifecycle::Disabled, (int)m.state(3)->lifecycle);
  TEST_ASSERT_EQUAL((int)CameraError::InvalidPeer, (int)m.cancel(4));
}
void cancelling_ready_link_preserves_connection() {
  FakeClock c;
  FakeTransport t;
  CameraManager m(c, t);
  m.configure(config());
  connect(m, 0);
  TEST_ASSERT_EQUAL((int)CameraError::None, (int)m.cancel(0));
  TEST_ASSERT_EQUAL((int)Lifecycle::Ready, (int)m.state(0)->lifecycle);
  TEST_ASSERT_EQUAL(0, t.closed.size());
  TEST_ASSERT_EQUAL((int)CameraError::None, (int)m.request(0, Operation::Query));
}
void disconnect_during_command_backoff_prevents_retry() {
  FakeClock c;
  FakeTransport t;
  RetryPolicy p;
  p.timeout_ms = 10;
  p.backoff_ms = 5;
  CameraManager m(c, t, p);
  m.configure(config());
  connect(m, 0);
  m.request(0, Operation::Start);
  c.time = 10;
  m.tick();
  Event disconnected{0, m.state(0)->token, EventKind::Disconnected};
  TEST_ASSERT_TRUE(m.event(disconnected));
  TEST_ASSERT_EQUAL((int)Lifecycle::Idle, (int)m.state(0)->lifecycle);
  c.time = 15;
  m.tick();
  TEST_ASSERT_EQUAL(2, t.calls.size());
  TEST_ASSERT_EQUAL((int)CameraError::NotConnected, (int)m.request(0, Operation::Stop));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(cancelling_ready_link_preserves_connection);
  RUN_TEST(disconnect_during_command_backoff_prevents_retry);
  RUN_TEST(expired_callbacks_cannot_complete_operations);
  RUN_TEST(resets_release_ready_links_and_reconfiguration_is_atomic);
  RUN_TEST(backoff_wrap_and_attempt_policy_bounds);
  RUN_TEST(asynchronous_failure_and_disabled_peer_are_isolated);
  RUN_TEST(requests_do_not_imply_observation);
  RUN_TEST(deadlines_backoff_and_peer_isolation);
  RUN_TEST(rollover_deadline_and_backoff);
  RUN_TEST(generations_cancel_reset_and_queue);
  RUN_TEST(stale_retry_and_serialized_operations);
  RUN_TEST(failure_disconnect_and_invalid_configuration);
  RUN_TEST(transport_rejection_is_bounded);
  return UNITY_END();
}
void setUp() {}
void tearDown() {}

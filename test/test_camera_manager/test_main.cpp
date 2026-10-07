#include "camera_manager.h"
#include <cstdlib>
#include <new>
#include <unity.h>
#include <vector>

namespace {
bool trackAllocations = false;
size_t largestAllocation = 0;
} // namespace
void *operator new(size_t size) {
  if (trackAllocations && size > largestAllocation)
    largestAllocation = size;
  if (void *memory = std::malloc(size))
    return memory;
  throw std::bad_alloc();
}
void operator delete(void *memory) noexcept { std::free(memory); }
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
  const Token captured = t.calls.back().token;
  c.time = 10;
  m.tick();
  Event disconnected{0, captured.connection, EventKind::Disconnected};
  TEST_ASSERT_TRUE(m.event(disconnected));
  TEST_ASSERT_EQUAL((int)Lifecycle::Idle, (int)m.state(0)->lifecycle);
  c.time = 15;
  m.tick();
  TEST_ASSERT_EQUAL(2, t.calls.size());
  TEST_ASSERT_EQUAL((int)CameraError::NotConnected, (int)m.request(0, Operation::Stop));
}
void captured_connection_events_survive_ready_cancel() {
  FakeClock c;
  FakeTransport t;
  CameraManager m(c, t);
  m.configure(config());
  m.request(0, Operation::Connect);
  const Token subscription = t.calls.back().token;
  TEST_ASSERT_TRUE(m.event(completion(0, subscription)));
  m.cancel(0);
  Event observed{0, subscription.connection, EventKind::RecordingObserved};
  observed.recording = RecordingState::Recording;
  TEST_ASSERT_TRUE(m.event(observed));
  TEST_ASSERT_EQUAL((int)RecordingState::Recording, (int)m.state(0)->observed);
  Event disconnect{0, subscription.connection, EventKind::Disconnected};
  TEST_ASSERT_TRUE(m.event(disconnect));
  TEST_ASSERT_EQUAL((int)Lifecycle::Idle, (int)m.state(0)->lifecycle);
}
void captured_disconnect_during_timeout_prevents_lost_link_retry() {
  FakeClock c;
  FakeTransport t;
  RetryPolicy p;
  p.timeout_ms = 10;
  p.backoff_ms = 5;
  CameraManager m(c, t, p);
  m.configure(config());
  m.request(0, Operation::Connect);
  const Token subscription = t.calls.back().token;
  TEST_ASSERT_TRUE(m.event(completion(0, subscription)));
  m.request(0, Operation::Start);
  const Token command = t.calls.back().token;
  c.time = 10;
  m.tick();
  Event observed{0, subscription.connection, EventKind::RecordingObserved};
  observed.recording = RecordingState::Recording;
  TEST_ASSERT_TRUE(m.event(observed));
  TEST_ASSERT_FALSE(m.event(completion(0, command)));
  Event disconnect{0, command.connection, EventKind::Disconnected};
  TEST_ASSERT_TRUE(m.event(disconnect));
  c.time = 15;
  m.tick();
  TEST_ASSERT_EQUAL(2, t.calls.size());
  TEST_ASSERT_EQUAL((int)Lifecycle::Idle, (int)m.state(0)->lifecycle);
}
void queued_connection_events_survive_command_transition_but_not_reconnect() {
  FakeClock c;
  FakeTransport t;
  CameraManager m(c, t);
  m.configure(config());
  m.request(0, Operation::Connect);
  const Token subscription = t.calls.back().token;
  TEST_ASSERT_TRUE(m.event(completion(0, subscription)));
  m.request(0, Operation::Start);
  const Token start = t.calls.back().token;
  m.request(0, Operation::Stop);
  TEST_ASSERT_TRUE(m.event(completion(0, start)));
  Event observed{0, subscription.connection, EventKind::RecordingObserved};
  observed.recording = RecordingState::Recording;
  TEST_ASSERT_TRUE(m.event(observed));
  Event disconnect{0, start.connection, EventKind::Disconnected};
  TEST_ASSERT_TRUE(m.event(disconnect));
  TEST_ASSERT_FALSE(m.event(completion(0, start)));
  m.request(0, Operation::Connect);
  const Token fresh = t.calls.back().token;
  TEST_ASSERT_TRUE(m.event(completion(0, fresh)));
  TEST_ASSERT_FALSE(m.event(observed));
  TEST_ASSERT_FALSE(m.event(disconnect));
  TEST_ASSERT_EQUAL((int)RecordingState::Unknown, (int)m.state(0)->observed);
  TEST_ASSERT_EQUAL((int)Lifecycle::Ready, (int)m.state(0)->lifecycle);
}
void unused_source_slots_are_not_allocated_into_manager() {
  FakeClock c;
  FakeTransport t;
  CameraManager m(c, t);
  SourceConfig unused;
  unused.cameras[7].name = std::string(1024 * 1024, 'x');
  TEST_ASSERT_TRUE(validate(unused).ok());
  largestAllocation = 0;
  trackAllocations = true;
  const auto result = m.configure(unused);
  trackAllocations = false;
  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL(0, m.size());
  TEST_ASSERT_TRUE_MESSAGE(largestAllocation < 1024, "configure copied unvalidated unused storage");
  unused = config();
  unused.count = 1;
  unused.cameras[7].identifier = std::string(1024 * 1024, 'y');
  largestAllocation = 0;
  trackAllocations = true;
  const auto usedResult = m.configure(unused);
  trackAllocations = false;
  TEST_ASSERT_TRUE(usedResult.ok());
  TEST_ASSERT_TRUE_MESSAGE(largestAllocation < 1024,
                           "configure retained unused slots with a valid peer");
  m.request(0, Operation::Connect);
  TEST_ASSERT_EQUAL(1, t.calls.size());
}
void command_observations_reject_retired_operation_generations() {
  FakeClock c;
  FakeTransport t;
  CameraManager m(c, t);
  m.configure(config());
  m.request(0, Operation::Connect);
  const Token subscription = t.calls.back().token;
  TEST_ASSERT_TRUE(m.event(completion(0, subscription)));
  m.request(0, Operation::Query);
  const Token query = t.calls.back().token;
  Event commandObservation{0, query, EventKind::CommandRecordingObserved};
  commandObservation.recording = RecordingState::Stopped;
  TEST_ASSERT_TRUE(m.event(commandObservation));
  TEST_ASSERT_EQUAL((int)RecordingState::Stopped, (int)m.state(0)->observed);
  m.cancel(0);
  TEST_ASSERT_FALSE(m.event(commandObservation));
  TEST_ASSERT_FALSE(m.event(completion(0, query)));
  Event persistent{0, subscription.connection, EventKind::RecordingObserved};
  persistent.recording = RecordingState::Recording;
  TEST_ASSERT_TRUE(m.event(persistent));
  m.reset();
  TEST_ASSERT_FALSE(m.event(persistent));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(command_observations_reject_retired_operation_generations);
  RUN_TEST(captured_connection_events_survive_ready_cancel);
  RUN_TEST(captured_disconnect_during_timeout_prevents_lost_link_retry);
  RUN_TEST(queued_connection_events_survive_command_transition_but_not_reconnect);
  RUN_TEST(unused_source_slots_are_not_allocated_into_manager);
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

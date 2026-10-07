#include "recording_manager.h"
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
  bool begin(size_t p, const CameraConfig &, Operation op, Token t) override {
    calls.push_back({p, op, t});
    return true;
  }
  void cancel(size_t, Token) override {}
  void close(size_t, Token) override {}
  Call last(size_t peer) const {
    for (auto i = calls.rbegin(); i != calls.rend(); ++i)
      if (i->peer == peer)
        return *i;
    TEST_FAIL_MESSAGE("missing transport request");
    return {};
  }
};
SourceConfig config(size_t n) {
  SourceConfig c;
  c.count = n;
  for (size_t i = 0; i < n; ++i) {
    auto &p = c.cameras[i];
    p.name = "Camera";
    p.family = CameraFamily::Insta360;
    p.model = CameraModel::X5;
    p.identifier = "01:23:45:67:89:A0";
    p.identifier.back() = "01234567"[i];
    p.address_type = AddressType::Random;
  }
  return c;
}
Event complete(FakeTransport::Call c, bool supported = true) {
  Event e{c.peer, c.token, EventKind::Completed};
  e.capabilities.start = e.capabilities.stop = e.capabilities.query =
      supported ? CapabilityState::Supported : CapabilityState::Unsupported;
  return e;
}
void observe(RecordingManager &g, FakeTransport::Call c, RecordingState state,
             bool command = false) {
  Event e{c.peer, c.token,
          command ? EventKind::CommandRecordingObserved : EventKind::RecordingObserved};
  e.recording = state;
  TEST_ASSERT_TRUE(g.event(e));
}
RetryPolicy policy() {
  RetryPolicy p;
  p.timeout_ms = 10;
  p.backoff_ms = 5;
  p.max_attempts = 1;
  return p;
}
struct Fixture {
  FakeClock clock;
  FakeTransport transport;
  CameraManager cameras;
  RecordingManager group;
  Fixture(size_t n) : cameras(clock, transport, policy()), group(cameras, clock) {
    TEST_ASSERT_TRUE(cameras.configure(config(n)).ok());
  }
  void ready(size_t p, RecordingState s = RecordingState::Stopped, bool supported = true) {
    cameras.request(p, Operation::Connect);
    auto call = transport.last(p);
    TEST_ASSERT_TRUE(group.event(complete(call, supported)));
    observe(group, call, s);
  }
};
void counts_and_pending_are_confirmed_not_acknowledged() {
  Fixture f(3);
  f.ready(0);
  f.ready(1);
  TEST_ASSERT_EQUAL(0, (int)f.group.request(RecordingState::Recording));
  auto s = f.group.status();
  TEST_ASSERT_EQUAL(3, s.enabled);
  TEST_ASSERT_EQUAL(2, s.ready);
  TEST_ASSERT_EQUAL(0, s.recording);
  TEST_ASSERT_EQUAL(3, s.pending);
  auto a = f.transport.last(0);
  TEST_ASSERT_TRUE(f.group.event(complete(a)));
  TEST_ASSERT_TRUE(f.group.status().peers[0].acknowledged);
  TEST_ASSERT_EQUAL(0, f.group.status().recording);
  observe(f.group, a, RecordingState::Recording, true);
  observe(f.group, f.transport.last(1), RecordingState::Recording, true);
  TEST_ASSERT_TRUE(f.group.event(complete(f.transport.last(1))));
  f.clock.time = 10;
  f.group.tick();
  s = f.group.status();
  TEST_ASSERT_EQUAL(2, s.recording);
  TEST_ASSERT_EQUAL(2, s.ready);
  TEST_ASSERT_EQUAL(0, s.pending);
  TEST_ASSERT_EQUAL(1, s.errors);
  TEST_ASSERT_EQUAL((int)CameraError::Timeout, (int)s.peers[2].error);
}
void zero_one_four_disabled_and_unsupported() {
  Fixture empty(0);
  TEST_ASSERT_EQUAL((int)GroupError::NoCameras, (int)empty.group.shortPress());
  Fixture one(1);
  one.ready(0);
  TEST_ASSERT_EQUAL(0, (int)one.group.shortPress());
  TEST_ASSERT_EQUAL((int)Operation::Start, (int)one.transport.last(0).op);
  Fixture four(4);
  for (size_t i = 0; i < 4; ++i)
    four.ready(i, RecordingState::Stopped, i != 3);
  four.group.request(RecordingState::Recording);
  for (size_t i = 0; i < 3; ++i) {
    auto c = four.transport.last(i);
    observe(four.group, c, RecordingState::Recording, true);
    four.group.event(complete(c));
  }
  auto s = four.group.status();
  TEST_ASSERT_EQUAL(4, s.enabled);
  TEST_ASSERT_EQUAL(4, s.ready);
  TEST_ASSERT_EQUAL(3, s.recording);
  TEST_ASSERT_EQUAL(1, s.errors);
  TEST_ASSERT_EQUAL((int)CameraError::Unsupported, (int)s.peers[3].error);
  auto cfg = config(4);
  cfg.cameras[3].enabled = false;
  four.group.cancel();
  four.cameras.configure(cfg);
  TEST_ASSERT_EQUAL(3, four.group.status().enabled);
}
void short_press_policy_unknown_and_resync() {
  Fixture f(2);
  TEST_ASSERT_EQUAL(0, (int)f.group.shortPress());
  TEST_ASSERT_EQUAL(2, f.transport.calls.size());
  TEST_ASSERT_EQUAL((int)GroupError::ResyncPending, (int)f.group.shortPress());
  for (size_t i = 0; i < 2; ++i) {
    f.group.event(complete(f.transport.last(i)));
    TEST_ASSERT_EQUAL((int)Operation::Query, (int)f.transport.last(i).op);
    auto q = f.transport.last(i);
    observe(f.group, q, i == 0 ? RecordingState::Recording : RecordingState::Stopped, true);
    f.group.event(complete(q));
  }
  TEST_ASSERT_EQUAL((int)RecordingState::Recording, (int)f.group.status().intent);
  TEST_ASSERT_EQUAL(0, (int)f.group.shortPress());
  TEST_ASSERT_EQUAL((int)RecordingState::Stopped, (int)f.group.status().intent);
  Fixture all(1);
  all.ready(0, RecordingState::Recording);
  all.group.shortPress();
  TEST_ASSERT_EQUAL((int)Operation::Stop, (int)all.transport.last(0).op);
}
void latest_intent_rejects_delayed_transport_tokens_and_cancel() {
  Fixture f(1);
  f.ready(0);
  f.group.request(RecordingState::Recording);
  auto old = f.transport.last(0);
  f.group.request(RecordingState::Stopped);
  auto latest = f.transport.last(0);
  TEST_ASSERT_EQUAL((int)Operation::Stop, (int)latest.op);
  TEST_ASSERT_FALSE(f.group.event(complete(old)));
  Event stale{0, old.token, EventKind::CommandRecordingObserved};
  stale.recording = RecordingState::Recording;
  TEST_ASSERT_FALSE(f.group.event(stale));
  observe(f.group, latest, RecordingState::Stopped, true);
  f.group.event(complete(latest));
  TEST_ASSERT_EQUAL((int)RecordingState::Stopped, (int)f.group.status().intent);
  TEST_ASSERT_EQUAL(0, f.group.status().pending);
  f.group.request(RecordingState::Recording);
  latest = f.transport.last(0);
  f.group.cancel();
  TEST_ASSERT_FALSE(f.group.event(complete(latest)));
  TEST_ASSERT_EQUAL(0, f.group.status().pending);
  TEST_ASSERT_EQUAL((int)RecordingState::Unknown, (int)f.group.status().intent);
}
void acknowledged_without_observation_has_bounded_confirmation_wait() {
  Fixture f(1);
  f.ready(0);
  f.group.request(RecordingState::Recording);
  f.group.event(complete(f.transport.last(0)));
  f.clock.time = 1000;
  f.group.tick();
  TEST_ASSERT_EQUAL(0, f.group.status().pending);
  TEST_ASSERT_EQUAL((int)CameraError::Timeout, (int)f.group.status().peers[0].error);
}
void unknown_startup_progresses_available_peer_and_reports_partial() {
  Fixture f(2);
  f.group.shortPress();
  f.group.event(complete(f.transport.last(0)));
  auto q = f.transport.last(0);
  observe(f.group, q, RecordingState::Stopped, true);
  f.group.event(complete(q));
  f.clock.time = 10;
  f.group.tick();
  TEST_ASSERT_EQUAL((int)Operation::Start, (int)f.transport.last(0).op);
  auto start = f.transport.last(0);
  observe(f.group, start, RecordingState::Recording, true);
  f.group.event(complete(start));
  auto s = f.group.status();
  TEST_ASSERT_EQUAL(1, s.recording);
  TEST_ASSERT_EQUAL(1, s.errors);
  TEST_ASSERT_EQUAL(0, s.pending);
  Fixture absent(1);
  absent.group.shortPress();
  absent.clock.time = 10;
  absent.group.tick();
  TEST_ASSERT_EQUAL((int)GroupError::UnknownState, (int)absent.group.status().error);
  TEST_ASSERT_EQUAL((int)RecordingState::Unknown, (int)absent.group.status().intent);
}
void resync_is_superseded_by_explicit_request_and_unknown_response_expires() {
  Fixture f(1);
  f.ready(0);
  f.group.resync();
  auto query = f.transport.last(0);
  f.group.request(RecordingState::Recording);
  Event old{0, query.token, EventKind::CommandRecordingObserved};
  old.recording = RecordingState::Stopped;
  TEST_ASSERT_FALSE(f.group.event(old));
  TEST_ASSERT_FALSE(f.group.event(complete(query)));
  TEST_ASSERT_EQUAL((int)RecordingState::Recording, (int)f.group.status().intent);
  Fixture unknown(1);
  unknown.ready(0, RecordingState::Unknown);
  unknown.group.shortPress();
  auto q = unknown.transport.last(0);
  observe(unknown.group, q, RecordingState::Unknown, true);
  unknown.group.event(complete(q));
  unknown.clock.time = 1000;
  unknown.group.tick();
  TEST_ASSERT_EQUAL((int)GroupError::UnknownState, (int)unknown.group.status().error);
}
void cancelled_completed_command_cannot_relabel_state() {
  Fixture f(1);
  f.ready(0);
  f.group.request(RecordingState::Recording);
  auto old = f.transport.last(0);
  observe(f.group, old, RecordingState::Recording, true);
  f.group.event(complete(old));
  f.group.cancel();
  Event stale{0, old.token, EventKind::CommandRecordingObserved};
  stale.recording = RecordingState::Stopped;
  TEST_ASSERT_FALSE(f.group.event(stale));
}
void confirmation_rollover_and_ui_independent_callback() {
  Fixture f(1);
  f.ready(0);
  struct Listener {
    size_t count = 0;
    RecordingStatus last;
  } listener;
  RecordingManager g(
      f.cameras, f.clock,
      [](void *ctx, const RecordingStatus &s) {
        auto &l = *static_cast<Listener *>(ctx);
        ++l.count;
        l.last = s;
      },
      &listener);
  f.clock.time = UINT32_MAX - 499;
  g.request(RecordingState::Recording);
  g.event(complete(f.transport.last(0)));
  f.clock.time = 499;
  g.tick();
  TEST_ASSERT_EQUAL(1, g.status().pending);
  f.clock.time = 500;
  g.tick();
  TEST_ASSERT_EQUAL(0, listener.last.pending);
  TEST_ASSERT_EQUAL(1, listener.last.errors);
  TEST_ASSERT_TRUE(listener.count >= 3);
  TEST_ASSERT_EQUAL((int)GroupError::InvalidIntent, (int)g.request(RecordingState::Unknown));
}
void disconnect_after_confirmation_remains_visible_in_summary() {
  Fixture f(1);
  f.ready(0);
  f.group.request(RecordingState::Recording);
  auto call = f.transport.last(0);
  observe(f.group, call, RecordingState::Recording, true);
  f.group.event(complete(call));
  Event disconnected{0, call.token.connection, EventKind::Disconnected};
  TEST_ASSERT_TRUE(f.group.event(disconnected));
  TEST_ASSERT_EQUAL(1, f.group.status().errors);
  TEST_ASSERT_EQUAL(1, f.group.status().unknown);
  TEST_ASSERT_EQUAL(0, f.group.status().recording);
}
void resync_confirmation_lost_before_group_decision_is_not_usable() {
  Fixture f(2);
  f.group.shortPress();
  f.group.event(complete(f.transport.last(0)));
  auto query = f.transport.last(0);
  observe(f.group, query, RecordingState::Stopped, true);
  f.group.event(complete(query));
  Event disconnected{0, query.token.connection, EventKind::Disconnected};
  f.group.event(disconnected);
  f.clock.time = 10;
  f.group.tick();
  TEST_ASSERT_EQUAL((int)GroupError::UnknownState, (int)f.group.status().error);
  TEST_ASSERT_EQUAL((int)RecordingState::Unknown, (int)f.group.status().intent);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(resync_confirmation_lost_before_group_decision_is_not_usable);
  RUN_TEST(disconnect_after_confirmation_remains_visible_in_summary);
  RUN_TEST(unknown_startup_progresses_available_peer_and_reports_partial);
  RUN_TEST(resync_is_superseded_by_explicit_request_and_unknown_response_expires);
  RUN_TEST(cancelled_completed_command_cannot_relabel_state);
  RUN_TEST(confirmation_rollover_and_ui_independent_callback);
  RUN_TEST(counts_and_pending_are_confirmed_not_acknowledged);
  RUN_TEST(zero_one_four_disabled_and_unsupported);
  RUN_TEST(short_press_policy_unknown_and_resync);
  RUN_TEST(latest_intent_rejects_delayed_transport_tokens_and_cancel);
  RUN_TEST(acknowledged_without_observation_has_bounded_confirmation_wait);
  return UNITY_END();
}
void setUp() {}
void tearDown() {}

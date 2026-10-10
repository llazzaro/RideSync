#include "../fixtures/insta360/ce80_display.h"
#include "wake_preparation.h"
#include "x5_wake.h"
#include <cstring>
#include <deque>
#include <unity.h>
using namespace ridesync;
struct FakeClock : Clock {
  uint32_t time = 1000;
  uint32_t now() const override { return time; }
};
struct FakePort : X5PeripheralPort {
  unsigned connects = 0, notifications = 0, closes = 0, configs = 0;
  std::deque<X5Input> events;
  X5Qualification q;
  Token token;
  uint32_t seq = 0;
  X5ShutterRequest send;
  bool released_ = true;
  bool configure(const X5Qualification &value) override {
    ++configs;
    q = value;
    return true;
  }
  bool connect(Token value, uint32_t) override {
    token = value;
    ++connects;
    released_ = false;
    return true;
  }
  bool notify(const X5ShutterRequest &r) override {
    send = r;
    ++notifications;
    return true;
  }
  void cancel(Token) override {}
  void close(uint32_t) override { ++closes; }
  bool poll(X5Input &out) override {
    if (events.empty())
      return false;
    out = events.front();
    events.pop_front();
    return true;
  }
  bool takeLoss(uint32_t) override { return false; }
  bool released(uint32_t) const override { return released_; }
  void service(uint32_t) override {}
};
struct Harness {
  FakeClock clock;
  FakePort port;
  X5Runtime runtime;
  X5Qualification q;
  SourceConfig source;
  Harness() : runtime(port, clock) {
    q.enabled = true;
    q.identity.verified = true;
    q.identity.type = IdentityType::Public;
    q.identity.address = {{1, 2, 3, 4, 5, 6}};
    q.store.qualification_record = 1;
    q.display.profile = insta360::Ce80DisplayProfile::X5CapturedDisplayV1;
    std::memcpy(q.firmware.data(), "1.11.10", 7);
    q.firmware_size = 7;
    source.count = 1;
    auto &c = source.cameras[0];
    c.name = "Synthetic X5";
    c.model = CameraModel::X5;
    c.family = CameraFamily::Insta360;
    c.identifier = "06:05:04:03:02:01";
    c.address_type = AddressType::Public;
  }
  void configure() { TEST_ASSERT_TRUE(runtime.configure(q, source)); }
  void event(X5InputKind kind) {
    X5Input e;
    e.kind = kind;
    e.connection = port.token.connection;
    e.operation = port.token.operation;
    e.handle = 7;
    e.sequence = ++port.seq;
    e.received_ms = clock.time;
    e.identity = q.identity;
    port.events.push_back(e);
  }
  template <size_t N> void frame(const uint8_t (&b)[N]) {
    event(X5InputKind::Display);
    auto &e = port.events.back();
    e.size = N;
    std::memcpy(e.bytes.data(), b, N);
  }
  void connect() {
    TEST_ASSERT_EQUAL_INT(int(CameraError::None), int(runtime.request(Operation::Connect)));
    event(X5InputKind::Connected);
    event(X5InputKind::Subscribed);
    frame(insta360_fixture::kCe80Video);
    runtime.service();
  }
};

struct Radio : WakeRadio {
  WakeRadioResult result;
  unsigned calls = 0;
  WakeSubmit begin(const WakeOperation &op, const insta360::WakeEncoding &, uint32_t,
                   uint32_t) override {
    ++calls;
    result = {};
    result.operation = op;
    return WakeSubmit::Accepted;
  }
  void cancel(const WakeOperation &) override {}
  WakeRadioResult poll(const WakeOperation &, uint32_t) override { return result; }
  void finish() { result.submitted = result.terminal = result.released = true; }
};
WakePeerConfig wakeConfig() {
  WakePeerConfig c;
  c.enabled = c.source_qualified = true;
  c.profile = insta360::WakeProfile::M5WakeV1;
  c.identifier = {{'A', 'B', 'C', '1', '2', '3'}};
  return c;
}
struct WakeHarness : Harness {
  Radio radio;
  X5WakeControl wake{runtime, clock, radio};
  WakeHarness() {
    configure();
    TEST_ASSERT_TRUE(wake.configure(wakeConfig()));
  }
  void pass() {
    runtime.service();
    wake.service();
  }
  void begin() {
    TEST_ASSERT_EQUAL_INT(0, int(wake.request()));
    pass();
    radio.finish();
    pass();
  }
  void ready() {
    event(X5InputKind::Connected);
    event(X5InputKind::Subscribed);
    frame(insta360_fixture::kCe80Video);
    pass();
  }
};
void wake_connect_observe_then_explicit_start() {
  WakeHarness h;
  h.pass();
  TEST_ASSERT_EQUAL_UINT(0, h.radio.calls);
  h.begin();
  TEST_ASSERT_EQUAL_UINT(1, h.port.connects);
  TEST_ASSERT_EQUAL_INT(int(CameraError::Busy), int(h.wake.command(Operation::Start)));
  h.ready();
  TEST_ASSERT_EQUAL_INT(int(WakePhase::Ready), int(h.wake.status().phase));
  TEST_ASSERT_EQUAL_UINT(0, h.port.notifications);
  TEST_ASSERT_EQUAL_INT(0, int(h.wake.command(Operation::Start)));
  h.pass();
  TEST_ASSERT_EQUAL_UINT(1, h.port.notifications);
}
void subscription_without_video_never_readies() {
  WakeHarness h;
  h.begin();
  h.event(X5InputKind::Connected);
  h.event(X5InputKind::Subscribed);
  h.pass();
  TEST_ASSERT_EQUAL_INT(int(WakePhase::Recovering), int(h.wake.status().phase));
  h.frame(insta360_fixture::kCe80Photo);
  h.pass();
  TEST_ASSERT_EQUAL_INT(int(WakePhase::Recovering), int(h.wake.status().phase));
  h.clock.time = 16000;
  h.pass();
  TEST_ASSERT_EQUAL_INT(int(WakePhase::Timeout), int(h.wake.status().phase));
  TEST_ASSERT_FALSE(h.wake.status().released);
  TEST_ASSERT_EQUAL_UINT(0, h.port.notifications);
  h.port.released_ = true;
  h.pass();
  TEST_ASSERT_TRUE(h.wake.status().released);
}
void cancellation_and_late_display_cannot_replay() {
  WakeHarness h;
  h.begin();
  h.wake.cancel();
  h.ready();
  TEST_ASSERT_EQUAL_INT(int(WakePhase::Cancelled), int(h.wake.status().phase));
  TEST_ASSERT_FALSE(h.wake.status().released);
  TEST_ASSERT_EQUAL_INT(int(CameraError::Busy), int(h.wake.request()));
  h.port.released_ = true;
  h.pass();
  TEST_ASSERT_TRUE(h.wake.status().released);
  h.pass();
  TEST_ASSERT_EQUAL_UINT(1, h.port.connects);
  TEST_ASSERT_EQUAL_UINT(0, h.port.notifications);
}
void live_link_and_unqualified_wake_are_refused() {
  WakeHarness h;
  h.connect();
  TEST_ASSERT_EQUAL_INT(int(CameraError::Busy), int(h.wake.request()));
  TEST_ASSERT_EQUAL_UINT(0, h.radio.calls);
  TEST_ASSERT_EQUAL_UINT(0, h.port.closes);
  Harness bare;
  bare.configure();
  Radio r;
  X5WakeControl control{bare.runtime, bare.clock, r};
  auto c = wakeConfig();
  c.source_qualified = false;
  TEST_ASSERT_FALSE(control.configure(c));
  TEST_ASSERT_EQUAL_INT(int(CameraError::Disabled), int(control.request()));
  TEST_ASSERT_EQUAL_UINT(0, r.calls);
}
void deadline_wrap_and_revocation_are_terminal() {
  WakeHarness h;
  h.clock.time = UINT32_MAX - 100;
  h.begin();
  h.clock.time += 15000;
  h.pass();
  TEST_ASSERT_EQUAL_INT(int(WakePhase::Timeout), int(h.wake.status().phase));
  WakeHarness second;
  second.begin();
  second.runtime.revoke();
  second.pass();
  TEST_ASSERT_EQUAL_INT(int(WakePhase::Cancelled), int(second.wake.status().phase));
  TEST_ASSERT_EQUAL_UINT(0, second.port.notifications);
}
void ready_snapshot_does_not_authorize_stale_start() {
  WakeHarness h;
  h.begin();
  h.ready();
  h.clock.time += 5001;
  h.pass();
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Unknown), int(h.wake.status().observed));
  TEST_ASSERT_EQUAL_INT(int(CameraError::Unsupported), int(h.wake.command(Operation::Start)));
  TEST_ASSERT_EQUAL_UINT(0, h.port.notifications);
}
void actual_group_preparation_admits_and_confirms_one_start() {
  WakeHarness h;
  X5WakeRecovery recovery(h.runtime, h.clock);
  WakeManager manager(h.radio, &recovery);
  std::array<WakePeerConfig, kWakePeers> configs{};
  configs[0] = wakeConfig();
  WakePreparation preparation(manager, h.clock, configs, 1);
  RecordingManager group(h.runtime.manager(), h.clock);
  TEST_ASSERT_TRUE(h.runtime.adapter().attachRecording(group));
  TEST_ASSERT_TRUE(group.attachPreparation(preparation));
  TEST_ASSERT_EQUAL_INT(0, int(group.request(RecordingState::Recording)));
  preparation.service();
  h.radio.finish();
  preparation.service();
  h.event(X5InputKind::Connected);
  h.event(X5InputKind::Subscribed);
  h.frame(insta360_fixture::kCe80Video);
  h.runtime.service();
  preparation.service();
  group.tick();
  h.runtime.service();
  TEST_ASSERT_EQUAL_UINT(1, h.port.notifications);
  h.event(X5InputKind::SendReturned);
  h.port.events.back().sequence = h.port.send.observation_sequence;
  h.port.events.back().operation = h.port.send.token.operation;
  h.frame(insta360_fixture::kCe80Timer);
  h.runtime.service();
  group.tick();
  TEST_ASSERT_EQUAL_UINT(0, group.status().pending);
  TEST_ASSERT_EQUAL_UINT(1, group.status().recording);
  TEST_ASSERT_EQUAL_UINT(1, h.port.notifications);
  TEST_ASSERT_EQUAL_UINT(0, group.status().errors);
  TEST_ASSERT_EQUAL_INT(0, int(group.request(RecordingState::Stopped)));
  h.runtime.service();
  TEST_ASSERT_EQUAL_UINT(2, h.port.notifications);
  h.event(X5InputKind::SendReturned);
  h.port.events.back().sequence = h.port.send.observation_sequence;
  h.port.events.back().operation = h.port.send.token.operation;
  h.frame(insta360_fixture::kCe80Video);
  h.runtime.service();
  group.tick();
  TEST_ASSERT_EQUAL_UINT(0, group.status().pending);
  TEST_ASSERT_EQUAL_UINT(1, group.status().stopped);
  TEST_ASSERT_EQUAL_INT(0, int(group.request(RecordingState::Recording)));
  preparation.service();
  group.tick();
  h.runtime.service();
  TEST_ASSERT_EQUAL_UINT(1, h.radio.calls);
  TEST_ASSERT_EQUAL_UINT(1, h.port.connects);
  TEST_ASSERT_EQUAL_UINT(3, h.port.notifications);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(actual_group_preparation_admits_and_confirms_one_start);
  RUN_TEST(wake_connect_observe_then_explicit_start);
  RUN_TEST(subscription_without_video_never_readies);
  RUN_TEST(cancellation_and_late_display_cannot_replay);
  RUN_TEST(live_link_and_unqualified_wake_are_refused);
  RUN_TEST(deadline_wrap_and_revocation_are_terminal);
  RUN_TEST(ready_snapshot_does_not_authorize_stale_start);
  return UNITY_END();
}

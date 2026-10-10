#include "../fixtures/insta360/ce80_display.h"
#include "x5_runtime.h"
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
void boot_and_status_are_inert() {
  Harness h;
  TEST_ASSERT_EQUAL_UINT(0, h.port.configs);
  h.configure();
  for (unsigned i = 0; i < 10; ++i) {
    h.runtime.service();
    h.runtime.status();
  }
  TEST_ASSERT_EQUAL_UINT(0, h.port.connects);
  TEST_ASSERT_EQUAL_UINT(0, h.port.notifications);
  TEST_ASSERT_TRUE(h.runtime.status().configured);
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Unknown), int(h.runtime.status().observed));
}
void real_runtime_refuses_delayed_or_duplicate_commands() {
  Harness h;
  h.configure();
  h.connect();
  auto s = h.runtime.status();
  TEST_ASSERT_TRUE(s.connected && s.subscribed && s.age_known);
  TEST_ASSERT_EQUAL_UINT32(0, s.age_ms);
  TEST_ASSERT_EQUAL_INT(int(CameraError::None), int(h.runtime.request(Operation::Start)));
  TEST_ASSERT_EQUAL_INT(int(CameraError::Busy), int(h.runtime.request(Operation::Start)));
  h.runtime.service();
  TEST_ASSERT_EQUAL_UINT(1, h.port.notifications);
  h.clock.time += 5000;
  h.runtime.service();
  TEST_ASSERT_EQUAL_INT(int(Lifecycle::Failed), int(h.runtime.status().lifecycle));
  TEST_ASSERT_EQUAL_INT(int(CameraError::Timeout), int(h.runtime.status().error));
  TEST_ASSERT_EQUAL_INT(int(X5Failure::Timeout), int(h.runtime.status().failure));
  TEST_ASSERT_EQUAL_UINT32(2, h.runtime.status().request_id);
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Unknown), int(h.runtime.status().observed));
  for (unsigned i = 0; i < 5; ++i)
    h.runtime.service();
  TEST_ASSERT_EQUAL_UINT(1, h.port.notifications);
}
void busy_duplicate_preserves_successful_operation_identity() {
  Harness h;
  h.configure();
  h.connect();
  TEST_ASSERT_EQUAL_INT(int(CameraError::None), int(h.runtime.request(Operation::Start)));
  TEST_ASSERT_EQUAL_INT(int(CameraError::Busy), int(h.runtime.request(Operation::Start)));
  h.runtime.service();
  h.event(X5InputKind::SendReturned);
  h.port.events.back().sequence = h.port.send.observation_sequence;
  h.frame(insta360_fixture::kCe80Timer);
  h.runtime.service();
  auto s = h.runtime.status();
  TEST_ASSERT_EQUAL_UINT32(2, s.request_id);
  TEST_ASSERT_EQUAL_UINT32(3, s.last_request_id);
  TEST_ASSERT_EQUAL_INT(int(CameraError::Busy), int(s.last_request_error));
  TEST_ASSERT_EQUAL_INT(int(CameraError::None), int(s.error));
  TEST_ASSERT_EQUAL_INT(int(X5Failure::None), int(s.failure));
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Recording), int(s.observed));
}
void reset_disconnect_revoke_do_not_replay() {
  Harness h;
  h.configure();
  h.connect();
  h.runtime.request(Operation::Start);
  h.runtime.disconnect();
  h.runtime.service();
  TEST_ASSERT_EQUAL_UINT(0, h.port.notifications);
  TEST_ASSERT_EQUAL_INT(int(RecordingState::Unknown), int(h.runtime.status().observed));
  TEST_ASSERT_EQUAL_INT(int(CameraError::Busy), int(h.runtime.request(Operation::Connect)));
  h.port.released_ = true;
  h.connect();
  TEST_ASSERT_EQUAL_UINT(0, h.port.notifications);
  h.runtime.revoke();
  h.runtime.service();
  TEST_ASSERT_EQUAL_INT(int(CameraError::Disabled), int(h.runtime.request(Operation::Start)));
  TEST_ASSERT_FALSE(h.runtime.configure(h.q, h.source));
  TEST_ASSERT_TRUE(h.runtime.status().revoked);
}
void wrong_source_cannot_commission_the_port() {
  Harness h;
  h.source.cameras[0].identifier = "06:05:04:03:02:02";
  TEST_ASSERT_FALSE(h.runtime.configure(h.q, h.source));
  TEST_ASSERT_EQUAL_UINT(0, h.port.configs);
  TEST_ASSERT_EQUAL_INT(int(CameraError::Disabled), int(h.runtime.request(Operation::Connect)));
  Harness second;
  second.source.cameras[0].gps_telemetry = true;
  TEST_ASSERT_FALSE(second.runtime.configure(second.q, second.source));
  TEST_ASSERT_EQUAL_UINT(0, second.port.configs);
}
void stale_and_photo_are_explicit_local_refusals() {
  Harness h;
  h.configure();
  h.connect();
  h.clock.time += 5001;
  h.runtime.service();
  TEST_ASSERT_EQUAL_INT(int(CameraError::Unsupported), int(h.runtime.request(Operation::Start)));
  TEST_ASSERT_EQUAL_UINT(0, h.port.notifications);
  h.frame(insta360_fixture::kCe80Photo);
  h.runtime.service();
  TEST_ASSERT_EQUAL_INT(int(CameraError::Unsupported), int(h.runtime.request(Operation::Start)));
  TEST_ASSERT_EQUAL_UINT(0, h.port.notifications);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(boot_and_status_are_inert);
  RUN_TEST(real_runtime_refuses_delayed_or_duplicate_commands);
  RUN_TEST(busy_duplicate_preserves_successful_operation_identity);
  RUN_TEST(reset_disconnect_revoke_do_not_replay);
  RUN_TEST(wrong_source_cannot_commission_the_port);
  RUN_TEST(stale_and_photo_are_explicit_local_refusals);
  return UNITY_END();
}

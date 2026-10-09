#include "wake_preparation.h"
#include <unity.h>
#include <vector>
using namespace ridesync;
struct FakeClock : Clock {
  uint32_t time = 0;
  uint32_t now() const override { return time; }
};
struct Radio : WakeRadio {
  WakeRadioResult result;
  unsigned calls = 0, cancels = 0;
  WakeSubmit begin(const WakeOperation &op, const insta360::WakeEncoding &, uint32_t,
                   uint32_t) override {
    ++calls;
    result = {};
    result.operation = op;
    return WakeSubmit::Accepted;
  }
  void cancel(const WakeOperation &) override { ++cancels; }
  WakeRadioResult poll(const WakeOperation &, uint32_t) override { return result; }
  void finish() { result.submitted = result.terminal = result.released = true; }
};
struct Recovery : WakeRecovery {
  WakeRecoveryResult result;
  CameraError submission = CameraError::None;
  CameraError begin(const WakeOperation &op, uint32_t) override {
    result = {};
    result.operation = op;
    return submission;
  }
  void cancel(const WakeOperation &) override {}
  WakeRecoveryResult poll(const WakeOperation &, uint32_t) override { return result; }
  void finish(RecordingState state = RecordingState::Stopped, bool fresh = true) {
    result.terminal = result.released = true;
    result.fresh = fresh;
    result.observed = state;
  }
};
struct Transport : CameraTransport {
  struct Call {
    size_t peer;
    Operation op;
    Token token;
  };
  std::vector<Call> calls;
  bool begin(size_t p, const CameraConfig &, Operation op, Token token) override {
    calls.push_back({p, op, token});
    return true;
  }
  void cancel(size_t, Token) override {}
  void close(size_t, Token) override {}
  unsigned count(Operation op) const {
    unsigned n = 0;
    for (auto c : calls)
      n += c.op == op;
    return n;
  }
};
std::array<WakePeerConfig, 4> configs() {
  std::array<WakePeerConfig, 4> cs{};
  for (auto &c : cs) {
    c.enabled = c.source_qualified = true;
    c.profile = insta360::WakeProfile::M5WakeV1;
    c.identifier = {{'A', 'B', 'C', '1', '2', '3'}};
  }
  return cs;
}
struct Fixture {
  FakeClock clock;
  Radio radio;
  Recovery recovery;
  WakeManager wakes{radio, &recovery};
  WakePreparation bridge{wakes, clock, configs(), 1};
  Transport transport;
  CameraManager cameras{clock, transport};
  RecordingManager group{cameras, clock};
  Fixture() {
    SourceConfig config;
    config.count = 1;
    auto &c = config.cameras[0];
    c.name = "X5";
    c.family = CameraFamily::Insta360;
    c.model = CameraModel::X5;
    c.identifier = "01:23:45:67:89:A0";
    c.address_type = AddressType::Random;
    TEST_ASSERT_TRUE(cameras.configure(config).ok());
    TEST_ASSERT_TRUE(group.attachPreparation(bridge));
  }
  void pass() {
    bridge.service();
    cameras.tick();
    group.tick();
  }
  void ready(RecordingState state) {
    TEST_ASSERT_EQUAL(int(CameraError::None), int(cameras.request(0, Operation::Connect)));
    auto call = transport.calls.back();
    Event e{0, call.token, EventKind::Completed};
    e.capabilities.start = e.capabilities.stop = e.capabilities.query = CapabilityState::Supported;
    TEST_ASSERT_TRUE(group.event(e));
    Event observation{0, call.token, EventKind::RecordingObserved};
    observation.recording = state;
    TEST_ASSERT_TRUE(group.event(observation));
  }
  void wake() {
    bridge.service();
    radio.finish();
    bridge.service();
  }
};
void fresh_stopped_group_alone_starts_and_stop_seals_old_ready() {
  Fixture f;
  TEST_ASSERT_EQUAL(int(GroupError::None), int(f.group.request(RecordingState::Recording)));
  f.wake();
  f.ready(RecordingState::Stopped);
  f.recovery.finish();
  f.pass();
  TEST_ASSERT_EQUAL(1, f.transport.count(Operation::Start));
  auto start = f.transport.calls.back();
  Event ack{0, start.token, EventKind::Completed};
  TEST_ASSERT_TRUE(f.group.event(ack));
  Event seen{0, start.token, EventKind::CommandRecordingObserved};
  seen.recording = RecordingState::Recording;
  TEST_ASSERT_TRUE(f.group.event(seen));
  TEST_ASSERT_EQUAL(int(GroupError::None), int(f.group.request(RecordingState::Stopped)));
  f.pass();
  TEST_ASSERT_EQUAL(1, f.transport.count(Operation::Stop));
  f.recovery.finish();
  f.pass();
  TEST_ASSERT_EQUAL(1, f.transport.count(Operation::Start));
}
void fresh_recording_avoids_duplicate_start() {
  Fixture f;
  f.group.request(RecordingState::Recording);
  f.wake();
  f.ready(RecordingState::Recording);
  f.recovery.finish(RecordingState::Recording);
  f.pass();
  TEST_ASSERT_EQUAL(0, f.transport.count(Operation::Start));
  TEST_ASSERT_EQUAL(0, f.group.status().pending);
}
void stale_unknown_and_wrong_generation_never_start() {
  for (unsigned mode = 0; mode < 3; ++mode) {
    Fixture f;
    f.group.request(RecordingState::Recording);
    f.wake();
    f.ready(RecordingState::Stopped);
    f.recovery.finish(mode == 0 ? RecordingState::Unknown : RecordingState::Stopped, mode != 1);
    if (mode == 2)
      ++f.recovery.result.operation.generation;
    f.pass();
    TEST_ASSERT_FALSE(f.bridge.commandReady(0));
    TEST_ASSERT_EQUAL(0, f.transport.count(Operation::Start));
  }
}
void unsupported_is_per_peer_and_second_attachment_rejected() {
  Fixture f;
  f.recovery.submission = CameraError::Unsupported;
  f.group.request(RecordingState::Recording);
  f.wake();
  f.pass();
  TEST_ASSERT_EQUAL(int(CameraError::Unsupported), int(f.group.status().peers[0].error));
  WakePreparation other{f.wakes, f.clock, configs(), 1};
  TEST_ASSERT_FALSE(f.group.attachPreparation(other));
  TEST_ASSERT_EQUAL(int(CameraError::None), int(f.bridge.prepare(1)));
  f.bridge.service();
  TEST_ASSERT_EQUAL(2, f.radio.calls);
}
void retirement_waits_for_release_and_allows_replacement() {
  Fixture f;
  TEST_ASSERT_EQUAL(0, int(f.bridge.prepare(0)));
  f.bridge.service();
  f.bridge.retire(0);
  TEST_ASSERT_TRUE(f.bridge.retiring(0));
  TEST_ASSERT_FALSE(f.bridge.released(0));
  TEST_ASSERT_EQUAL(int(CameraError::Busy), int(f.bridge.prepare(0)));
  auto old = f.radio.result.operation;
  f.radio.finish();
  f.bridge.service();
  TEST_ASSERT_TRUE(f.bridge.released(0));
  TEST_ASSERT_FALSE(f.bridge.retiring(0));
  TEST_ASSERT_EQUAL(0, int(f.bridge.prepare(0)));
  f.bridge.service();
  TEST_ASSERT_TRUE(f.radio.result.operation.id > old.id);
}
void invalidate_and_delayed_completion_cannot_revive() {
  Fixture f;
  f.group.request(RecordingState::Recording);
  f.wake();
  f.bridge.invalidate(2);
  f.recovery.finish();
  f.pass();
  TEST_ASSERT_FALSE(f.bridge.commandReady(0));
  TEST_ASSERT_EQUAL(0, f.transport.count(Operation::Start));
  TEST_ASSERT_EQUAL(0, int(f.bridge.prepare(0)));
  f.bridge.service();
  TEST_ASSERT_EQUAL(2, f.radio.result.operation.generation);
}
void invalid_peers_and_policy_fail_honestly() {
  Fixture f;
  for (auto p : {size_t(4), size_t(255), size_t(256)}) {
    TEST_ASSERT_EQUAL(int(CameraError::InvalidPeer), int(f.bridge.prepare(p)));
    TEST_ASSERT_EQUAL(int(CameraError::InvalidPeer), int(f.bridge.prepared(p).error));
    TEST_ASSERT_FALSE(f.bridge.commandReady(p));
    f.bridge.retire(p);
  }
  f.bridge.invalidate(0);
  TEST_ASSERT_EQUAL(int(CameraError::InvalidPolicy), int(f.bridge.prepare(0)));
}
void cancelled_pending_wake_never_admits_rec() {
  Fixture f;
  f.group.request(RecordingState::Recording);
  f.bridge.service();
  f.group.request(RecordingState::Stopped);
  f.radio.finish();
  f.pass();
  f.recovery.finish();
  f.pass();
  TEST_ASSERT_EQUAL(0, f.transport.count(Operation::Start));
}
void setup() {}
void teardown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(fresh_stopped_group_alone_starts_and_stop_seals_old_ready);
  RUN_TEST(fresh_recording_avoids_duplicate_start);
  RUN_TEST(stale_unknown_and_wrong_generation_never_start);
  RUN_TEST(unsupported_is_per_peer_and_second_attachment_rejected);
  RUN_TEST(retirement_waits_for_release_and_allows_replacement);
  RUN_TEST(invalidate_and_delayed_completion_cannot_revive);
  RUN_TEST(invalid_peers_and_policy_fail_honestly);
  RUN_TEST(cancelled_pending_wake_never_admits_rec);
  return UNITY_END();
}

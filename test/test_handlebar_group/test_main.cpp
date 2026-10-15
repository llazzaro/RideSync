#include "handlebar_control.h"
#include <unity.h>
#include <vector>
using namespace ridesync;
namespace {
struct Time : Clock {
  uint32_t value = 0;
  uint32_t now() const override { return value; }
};
struct Input : ButtonInput {
  bool down = false;
  bool pressed() override { return down; }
};
struct Led : LedSink {
  int write(LedFrame) override { return 0; }
};
// Synthetic camera boundary: the real button, control, camera manager and group
// run together. Profile wire protocols remain covered by their adapter suites.
struct Runtime : CameraRuntimePort, CameraTransport {
  struct Command {
    size_t peer;
    Operation operation;
    Token token;
  };
  Time &clock;
  CameraManager *manager = nullptr;
  RecordingManager *group = nullptr;
  std::vector<Command> pending, sent;
  std::array<bool, 4> preparing{}, holding{};
  std::array<RecordingState, 4> observed{{RecordingState::Stopped, RecordingState::Stopped,
                                          RecordingState::Stopped, RecordingState::Stopped}};
  int unavailable = -1;
  explicit Runtime(Time &t) : clock(t) {}
  void attachGroup(RecordingManager &g) override { group = &g; }
  void detachGroup(const RecordingManager *) override { group = nullptr; }
  bool begin(size_t i, const CameraConfig &, Operation op, Token token) override {
    pending.push_back({i, op, token});
    sent.push_back({i, op, token});
    return true;
  }
  void cancel(size_t i, Token) override {
    for (auto it = pending.begin(); it != pending.end();)
      if (it->peer == i)
        it = pending.erase(it);
      else
        ++it;
  }
  void close(size_t i, Token token) override { cancel(i, token); }
  void service(CameraServiceAction *action = nullptr) override {
    group->beginServicePass();
    const auto replies = pending;
    pending.clear();
    for (const auto &c : replies) {
      if (holding[c.peer]) {
        pending.push_back(c);
        continue;
      }
      Event done(c.peer, c.token, EventKind::Completed);
      done.capabilities.start = done.capabilities.stop = done.capabilities.query =
          CapabilityState::Supported;
      group->event(done);
      if (c.operation == Operation::Start)
        observed[c.peer] = RecordingState::Recording;
      else if (c.operation == Operation::Stop)
        observed[c.peer] = RecordingState::Stopped;
      Event state(c.peer, c.token,
                  c.operation == Operation::Connect ? EventKind::RecordingObserved
                                                    : EventKind::CommandRecordingObserved);
      state.recording = observed[c.peer];
      group->event(state);
    }
    if (action)
      action->beforeAdvance();
    group->tick();
    ++clock.value;
  }
  void stop() override {}
  bool canDestroy() const override { return true; }
  Hero12Fault fault(uint8_t) const override { return Hero12Fault::None; }
  CameraError requestRecovery(uint8_t i, bool recording, Hero12PowerCondition) override {
    TEST_ASSERT_FALSE(recording); // Only the group may send REC.
    preparing[i] = true;
    if (i == unavailable)
      return CameraError::Timeout;
    return commandReady(i) ? CameraError::None : manager->request(i, Operation::Connect);
  }
  CameraError cancelRecovery(uint8_t i) override {
    preparing[i] = false;
    return CameraError::None;
  }
  Hero12RecoveryState recoveryState(uint8_t i) const override {
    Hero12RecoveryState s;
    s.phase = !preparing[i]     ? Hero12RecoveryPhase::Idle
              : commandReady(i) ? Hero12RecoveryPhase::Ready
                                : Hero12RecoveryPhase::Connecting;
    return s;
  }
  bool recoveryReady(uint8_t i) const override { return commandReady(i); }
  bool commandReady(uint8_t i) const override {
    const auto l = manager->state(i)->lifecycle;
    return l == Lifecycle::Ready || l == Lifecycle::Operating;
  }
  bool wakeSupported(uint8_t i) const override { return i == 0; }
  bool linkRetiring(uint8_t) const override { return false; }
  bool linkReleased(uint8_t) const override { return true; }
  unsigned commands(size_t peer, Operation op) const {
    unsigned n = 0;
    for (const auto &c : sent)
      n += c.peer == peer && c.operation == op;
    return n;
  }
};
struct Rig {
  Time clock;
  Runtime runtime{clock};
  CameraManager cameras{clock, runtime};
  RecordingManager group{cameras, clock};
  Input input;
  Led led;
  HandlebarControl control{clock, runtime, cameras, group, input, led};
  LocalTelemetryStatus local;
  Rig() {
    SourceConfig config;
    config.count = 4;
    const CameraModel models[] = {CameraModel::X5, CameraModel::GO3S, CameraModel::HERO12_BLACK,
                                  CameraModel::ONE_RS};
    const char *addresses[] = {"01:02:03:04:05:06", "01:02:03:04:05:07", "01:02:03:04:05:08",
                               "01:02:03:04:05:09"};
    for (unsigned i = 0; i < 4; ++i) {
      auto &c = config.cameras[i];
      c.name = addresses[i];
      c.identifier = addresses[i];
      c.address_type = AddressType::Public;
      c.model = models[i];
      c.family = i == 2 ? CameraFamily::GoPro : CameraFamily::Insta360;
    }
    TEST_ASSERT_TRUE(cameras.configure(config).ok());
    runtime.manager = &cameras;
    runtime.attachGroup(group);
    TEST_ASSERT_TRUE(control.begin({}));
    local.phase = TelemetryPhase::Running;
    local.camera = CameraAdmission::Admitted;
    control.observe(local);
  }
  void pass(unsigned count = 1) {
    while (count--) {
      runtime.service(&control);
      control.observe(local);
    }
  }
  void press() {
    pass();
    clock.value += 21;
    pass();
    input.down = true;
    pass();
    clock.value += 21;
    pass();
    input.down = false;
    pass();
    clock.value += 21;
    pass();
  }
};
void one_button_starts_and_stops_every_linked_model() {
  Rig r;
  r.press();
  r.pass(10);
  TEST_ASSERT_EQUAL_UINT(4, r.group.status().recording);
  for (unsigned i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL_UINT(1, r.runtime.commands(i, Operation::Start));
    TEST_ASSERT_EQUAL_UINT(1, r.runtime.commands(i, Operation::Connect));
  }
  r.press();
  r.pass(10);
  TEST_ASSERT_EQUAL_UINT(4, r.group.status().stopped);
  TEST_ASSERT_EQUAL_UINT(0, r.group.status().errors);
  for (unsigned i = 0; i < 4; ++i)
    TEST_ASSERT_EQUAL_UINT(1, r.runtime.commands(i, Operation::Stop));
}
void unavailable_camera_does_not_block_other_models() {
  Rig r;
  r.runtime.unavailable = 1;
  r.press();
  r.pass(10);
  TEST_ASSERT_EQUAL_UINT(3, r.group.status().recording);
  TEST_ASSERT_EQUAL_INT(CameraError::Timeout, r.group.status().peers[1].error);
  TEST_ASSERT_EQUAL_UINT(0, r.runtime.commands(1, Operation::Start));
  r.press();
  r.pass(10);
  for (unsigned i : {0u, 2u, 3u})
    TEST_ASSERT_EQUAL_UINT(1, r.runtime.commands(i, Operation::Stop));
}
void stop_during_preparation_never_starts_a_late_camera() {
  Rig r;
  r.runtime.holding[1] = true;
  r.press();
  r.pass(10);
  TEST_ASSERT_EQUAL_UINT(3, r.group.status().recording);
  r.press();
  r.runtime.holding[1] = false;
  r.pass(10);
  TEST_ASSERT_EQUAL_UINT(0, r.runtime.commands(1, Operation::Start));
  TEST_ASSERT_EQUAL_UINT(4, r.group.status().stopped);
  TEST_ASSERT_EQUAL_UINT(0, r.group.status().pending);
}
void button_tracks_explicit_group_commands_from_the_console() {
  for (unsigned sync = 0; sync < 3; ++sync) {
    Rig r;
    r.group.request(RecordingState::Recording);
    r.pass(10);
    if (sync == 1)
      r.control.submit(ButtonAction::Resync);
    else if (sync == 2)
      r.group.resync();
    r.pass(10);
    r.press();
    r.pass(10);
    TEST_ASSERT_EQUAL_UINT(4, r.group.status().stopped);
    r.press();
    r.pass(10);
    r.group.request(RecordingState::Stopped);
    r.pass(10);
    if (sync == 1)
      r.control.submit(ButtonAction::WakeReconnect);
    else if (sync == 2)
      r.group.resync();
    r.pass(10);
    r.press();
    r.pass(10);
    TEST_ASSERT_EQUAL_UINT(4, r.group.status().recording);
  }
}
void resync_preserves_stop_for_all_models() {
  Rig r;
  r.press();
  r.pass(10);
  r.control.submit(ButtonAction::Resync);
  r.pass(10);
  r.press();
  r.pass(10);
  TEST_ASSERT_EQUAL_UINT(4, r.group.status().stopped);
  for (unsigned i = 0; i < 4; ++i)
    TEST_ASSERT_EQUAL_UINT(1, r.runtime.commands(i, Operation::Stop));
}
} // namespace
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(one_button_starts_and_stops_every_linked_model);
  RUN_TEST(unavailable_camera_does_not_block_other_models);
  RUN_TEST(stop_during_preparation_never_starts_a_late_camera);
  RUN_TEST(resync_preserves_stop_for_all_models);
  RUN_TEST(button_tracks_explicit_group_commands_from_the_console);
  return UNITY_END();
}

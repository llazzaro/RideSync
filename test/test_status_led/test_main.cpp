#include "status_led.h"
#include <unity.h>
#include <vector>
using namespace ridesync;
struct Sink : LedSink {
  std::vector<LedFrame> frames;
  int write(LedFrame f) override {
    frames.push_back(f);
    return 0;
  }
};
void ready_and_recording_require_observation() {
  RecordingStatus s;
  s.enabled = s.ready = s.stopped = 1;
  s.peers[0].enabled = s.peers[0].ready = true;
  s.peers[0].observed = RecordingState::Stopped;
  std::array<Lifecycle, kMaxCameras> lifecycle{};
  lifecycle[0] = Lifecycle::Ready;
  TEST_ASSERT_EQUAL((int)LedState::Ready, (int)selectLedState(&s, lifecycle));
  s.stopped = 0;
  s.unknown = 1;
  s.intent = RecordingState::Recording;
  s.peers[0].acknowledged = true;
  s.peers[0].observed = RecordingState::Unknown;
  TEST_ASSERT_EQUAL((int)LedState::Partial, (int)selectLedState(&s, lifecycle));
}
void recording_half_open_edges() {
  Sink sink;
  StatusLed led(sink);
  led.service(LedState::Recording, 0);
  TEST_ASSERT_EQUAL(1, sink.frames.size());
  TEST_ASSERT_TRUE(sink.frames.back() == LedFrame(true));
  led.service(LedState::Recording, 499);
  TEST_ASSERT_EQUAL(1, sink.frames.size());
  led.service(LedState::Recording, 500);
  TEST_ASSERT_FALSE(sink.frames.back().lit());
  led.service(LedState::Recording, 1000);
  TEST_ASSERT_TRUE(sink.frames.back().red);
}

struct Port : LedGpioPort {
  struct Level {
    int pin;
    bool level;
  };
  std::vector<Level> levels;
  uint64_t configured = 0;
  int configs = 0, config_error = 0, fail_write = -1, writes = 0;
  int configureOutputs(uint64_t mask) override {
    ++configs;
    configured = mask;
    return config_error;
  }
  int setLevel(int pin, bool level) override {
    levels.push_back({pin, level});
    return writes++ == fail_write ? 77 : 0;
  }
};
LedWiring wiring(LedMode mode = LedMode::Mono) {
  LedWiring c;
  c.mode = mode;
  c.board_qualified = c.reservations_complete = true;
  c.pins = {{18, 19, 23}};
  c.polarity = {{LedPolarity::ActiveHigh, LedPolarity::ActiveHigh, LedPolarity::ActiveHigh}};
  if (mode == LedMode::Mono) {
    c.pins[1] = c.pins[2] = -1;
    c.polarity[1] = c.polarity[2] = LedPolarity::Unspecified;
  }
  return c;
}
void backend_refuses_without_any_gpio() {
  for (int scenario = 0; scenario < 17; ++scenario) {
    Port port;
    GpioLedSink sink(port);
    auto c = wiring(LedMode::Rgb);
    bool ack = true;
    ButtonGpioConfig button;
    switch (scenario) {
    case 0:
      c.mode = LedMode::Disabled;
      break;
    case 1:
      c.board_qualified = false;
      break;
    case 2:
      ack = false;
      break;
    case 3:
      c.reservations_complete = false;
      break;
    case 4:
      c.pins[1] = 18;
      break;
    case 5:
      c.polarity[1] = LedPolarity::Unspecified;
      break;
    case 6:
      c.reserved_pins = uint64_t(1) << 19;
      break;
    case 7:
      button.enabled = true;
      button.pin = 19;
      break;
    case 8:
      c.pins[0] = -1;
      break;
    case 9:
      c.pins[0] = 34;
      break;
    case 10:
      c.pins[0] = 6;
      break;
    case 11:
      c.pins[0] = 16;
      break;
    case 12:
      c.pins[0] = 26;
      break;
    case 13:
      c.mode = static_cast<LedMode>(99);
      break;
    case 14:
      c.polarity[0] = static_cast<LedPolarity>(99);
      break;
    case 15:
      c.mode = LedMode::Mono; // Accidental remaining RGB pins cannot be ignored.
      break;
    case 16:
      button.enabled = true;
      button.pin = -1;
      break;
    }
    auto result = sink.begin(c, ack, button);
    TEST_ASSERT_EQUAL((int)(scenario == 0 ? LedBackendState::Disabled : LedBackendState::Refused),
                      (int)result);
    sink.write({true, true, true});
    TEST_ASSERT_EQUAL(0, port.configs);
    TEST_ASSERT_EQUAL(0, port.writes);
  }
  // Enumerate every ESP32 pin outside the conservative output candidate list.
  for (int pin = -2; pin <= 40; ++pin) {
    if (pin == 18 || pin == 19 || pin == 21 || pin == 22 || pin == 23 || pin == 32)
      continue;
    Port port;
    GpioLedSink sink(port);
    auto c = wiring();
    c.pins[0] = pin;
    TEST_ASSERT_EQUAL((int)LedBackendState::Refused, (int)sink.begin(c, true));
    TEST_ASSERT_EQUAL(0, port.configs);
    TEST_ASSERT_EQUAL(0, port.writes);
  }
}
void backend_polarities_and_failures() {
  for (unsigned bits = 0; bits < 8; ++bits) {
    Port port;
    GpioLedSink sink(port);
    auto c = wiring(LedMode::Rgb);
    for (int i = 0; i < 3; ++i)
      c.polarity[i] = (bits & (1 << i)) ? LedPolarity::ActiveLow : LedPolarity::ActiveHigh;
    TEST_ASSERT_EQUAL((int)LedBackendState::Ready, (int)sink.begin(c, true));
    TEST_ASSERT_EQUAL((uint64_t(1) << 18) | (uint64_t(1) << 19) | (uint64_t(1) << 23),
                      port.configured);
    for (int i = 0; i < 3; ++i)
      TEST_ASSERT_EQUAL(bool(bits & (1 << i)), port.levels[i].level);
    TEST_ASSERT_EQUAL(0, sink.write({true, true, false}));
    for (int i = 0; i < 3; ++i) {
      TEST_ASSERT_EQUAL(c.pins[i], port.levels[3 + i].pin);
      TEST_ASSERT_EQUAL((i < 2) != bool(bits & (1 << i)), port.levels[3 + i].level);
    }
    int count = port.writes;
    sink.begin(c, true);
    TEST_ASSERT_EQUAL(count, port.writes);
  }
  for (bool low : {false, true}) {
    Port port;
    GpioLedSink sink(port);
    auto c = wiring();
    c.polarity[0] = low ? LedPolarity::ActiveLow : LedPolarity::ActiveHigh;
    sink.begin(c, true);
    sink.write({false, false, true});
    TEST_ASSERT_EQUAL(!low, port.levels.back().level);
    sink.write({});
    TEST_ASSERT_EQUAL(low, port.levels.back().level);
  }
  for (int failure : {-2, 0, 1, 2, 3, 4, 5}) {
    Port port;
    GpioLedSink sink(port);
    auto c = wiring(LedMode::Rgb);
    if (failure == -2)
      port.config_error = 88;
    else
      port.fail_write = failure;
    sink.begin(c, true);
    sink.write({true, true, true});
    TEST_ASSERT_EQUAL(
        (int)(failure == -2 ? LedBackendState::ConfigFailed : LedBackendState::WriteFailed),
        (int)sink.state());
    TEST_ASSERT_EQUAL(failure == -2 ? 88 : 77, sink.error());
    const int calls = port.writes;
    TEST_ASSERT_EQUAL(sink.error(), sink.write({true}));
    TEST_ASSERT_EQUAL(calls, port.writes);
    // Best effort off on ALL qualified channels, preserving the first SDK error.
    TEST_ASSERT_TRUE(port.levels.size() >= 3);
    for (size_t i = port.levels.size() - 3; i < port.levels.size(); ++i)
      TEST_ASSERT_FALSE(port.levels[i].level);
  }
}
void all_pattern_edges_and_rollover() {
  struct Edge {
    uint32_t t;
    bool on;
  };
  const std::vector<Edge> recording{{0, true},    {499, true},  {500, false}, {501, false},
                                    {999, false}, {1000, true}, {1001, true}};
  const std::vector<Edge> partial{{0, true},    {99, true},   {100, false},  {101, false},
                                  {199, false}, {200, true},  {201, true},   {299, true},
                                  {300, false}, {301, false}, {1999, false}, {2000, true},
                                  {2001, true}};
  const std::vector<Edge> recovery{{0, true},    {99, true},  {100, false}, {101, false},
                                   {199, false}, {200, true}, {201, true}};
  const std::vector<Edge> error{{0, true},    {99, true},   {100, false}, {101, false},
                                {199, false}, {200, true},  {201, true},  {299, true},
                                {300, false}, {301, false}, {399, false}, {400, true},
                                {401, true},  {499, true},  {500, false}, {501, false},
                                {999, false}, {1000, true}, {1001, true}};
  for (uint32_t origin : {0U, UINT32_MAX - 50})
    for (LedState state :
         {LedState::Recording, LedState::Partial, LedState::Recovery, LedState::Error}) {
      Sink sink;
      StatusLed led(sink);
      const auto &edges = state == LedState::Recording  ? recording
                          : state == LedState::Partial  ? partial
                          : state == LedState::Recovery ? recovery
                                                        : error;
      for (const auto &edge : edges) {
        led.service(state, origin + edge.t);
        const LedFrame expected(edge.on && state != LedState::Recovery,
                                edge.on && state == LedState::Partial,
                                edge.on && state == LedState::Recovery);
        TEST_ASSERT_TRUE(expected == sink.frames.back());
        auto calls = sink.frames.size();
        led.service(state, origin + edge.t);
        TEST_ASSERT_EQUAL(calls, sink.frames.size());
      }
    }
}
void phase_transitions_generation_and_gaps() {
  Sink sink;
  StatusLed led(sink);
  led.service(LedState::Recording, 0);
  led.service(LedState::Recording, 450);
  led.service(LedState::Error, 450); // Same red frame but must restart.
  led.service(LedState::Error, 549);
  TEST_ASSERT_TRUE(sink.frames.back().red);
  led.service(LedState::Error, 550);
  TEST_ASSERT_FALSE(sink.frames.back().lit());
  led.service(LedState::Off, 550);
  TEST_ASSERT_FALSE(sink.frames.back().lit());
  led.service(LedState::Error, 551);
  TEST_ASSERT_TRUE(sink.frames.back().red);
  const auto calls = sink.frames.size();
  led.service(LedState::Error, 1000551);
  TEST_ASSERT_EQUAL(calls, sink.frames.size());
  led.service(LedState::Error, 1000651);
  TEST_ASSERT_FALSE(sink.frames.back().lit());
  led.service(LedState::Error, 1000651 + 0x80000000U);
  TEST_ASSERT_TRUE(sink.frames.back().red);
  // Several clock wraps remain continuous if individual gaps respect the contract.
  Sink long_sink;
  StatusLed long_led(long_sink);
  uint32_t t = 0;
  long_led.service(LedState::Recording, t);
  for (int i = 0; i < 10; ++i) {
    t += 1000000500U;
    long_led.service(LedState::Recording, t);
    TEST_ASSERT_EQUAL(i % 2 == 1, long_sink.frames.back().red);
  }
  for (uint32_t boundary : {100U, 200U, 300U, 400U, 500U, 1000U, 2000U})
    for (LedState old : {LedState::Ready, LedState::Recording, LedState::Partial,
                         LedState::Recovery, LedState::Error, LedState::Off})
      for (LedState next : {LedState::Ready, LedState::Recording, LedState::Partial,
                            LedState::Recovery, LedState::Error, LedState::Off}) {
        if (old == next)
          continue;
        Sink frames;
        StatusLed renderer(frames);
        renderer.service(old, 0);
        renderer.service(old, boundary);
        renderer.service(next, boundary);
        const LedFrame expected(
            next == LedState::Recording || next == LedState::Partial || next == LedState::Error,
            next == LedState::Ready || next == LedState::Partial, next == LedState::Recovery);
        TEST_ASSERT_TRUE(expected == frames.frames.back());
      }
}
struct ClockStub : Clock {
  uint32_t now() const override { return 0; }
};
struct TransportStub : CameraTransport {
  Token token;
  Operation operation = Operation::Connect;
  int calls = 0;
  bool begin(size_t, const CameraConfig &, Operation op, Token t) override {
    token = t;
    operation = op;
    ++calls;
    return true;
  }
  void cancel(size_t, Token) override {}
  void close(size_t, Token) override {}
};
void actual_group_observations_and_priority() {
  ClockStub clock;
  TransportStub transport;
  CameraManager cameras(clock, transport);
  RecordingManager group(cameras, clock);
  SourceConfig config;
  config.count = 1;
  auto &camera = config.cameras[0];
  camera.name = "Camera";
  camera.family = CameraFamily::Insta360;
  camera.model = CameraModel::X5;
  camera.identifier = "01:23:45:67:89:A0";
  camera.address_type = AddressType::Random;
  TEST_ASSERT_TRUE(cameras.configure(config).ok());
  std::array<Lifecycle, kMaxCameras> lives{};
  auto selected = [&](LedHealth health = LedHealth{}) {
    auto status = group.status();
    lives[0] = cameras.state(0)->lifecycle;
    const auto calls = transport.calls;
    auto result = selectLedState(&status, lives, health);
    TEST_ASSERT_EQUAL(calls, transport.calls);
    return result;
  };
  TEST_ASSERT_EQUAL((int)LedState::Partial, (int)selected());
  cameras.request(0, Operation::Connect);
  TEST_ASSERT_EQUAL((int)LedState::Recovery, (int)selected());
  Event connected{0, transport.token, EventKind::Completed};
  connected.capabilities.start = connected.capabilities.stop = connected.capabilities.query =
      CapabilityState::Supported;
  group.event(connected);
  TEST_ASSERT_EQUAL((int)LedState::Partial, (int)selected());
  Event observed{0, transport.token.connection, EventKind::RecordingObserved};
  observed.recording = RecordingState::Stopped;
  group.event(observed);
  TEST_ASSERT_EQUAL((int)LedState::Ready, (int)selected());
  group.request(RecordingState::Recording);
  TEST_ASSERT_EQUAL((int)LedState::Partial, (int)selected()); // pending shutter is NOT recovery
  group.event(Event{0, transport.token, EventKind::Completed});
  TEST_ASSERT_EQUAL((int)LedState::Partial, (int)selected());
  observed.recording = RecordingState::Recording;
  group.event(observed);
  TEST_ASSERT_EQUAL((int)LedState::Recording, (int)selected());
  Sink sink;
  StatusLed renderer(sink);
  auto status = group.status();
  renderer.service(selectLedState(&status, lives), 0);
  ++status.generation;
  renderer.service(selectLedState(&status, lives), 499);
  TEST_ASSERT_TRUE(sink.frames.back().red);
  ++status.generation;
  renderer.service(selectLedState(&status, lives), 500);
  TEST_ASSERT_FALSE(sink.frames.back().lit());
  group.cancel();
  TEST_ASSERT_EQUAL((int)LedState::Partial, (int)selected());
  group.event(observed);
  TEST_ASSERT_EQUAL((int)LedState::Recording, (int)selected());
  // All pairwise higher priorities mask recording/ready/partial; terminal vs retries.
  for (bool fault : {false, true})
    for (bool retry : {false, true})
      for (bool degraded : {false, true}) {
        LedHealth health;
        health.application_fault = fault;
        health.adapter_recovery = retry;
        health.devices[0].enabled = health.devices[0].qualified = health.devices[0].current = true;
        health.devices[0].outcome = DeviceHealth::NoFix;
        health.devices[0].severity = degraded ? LedSeverity::Partial : LedSeverity::Ignore;
        TEST_ASSERT_EQUAL((int)(fault      ? LedState::Error
                                : retry    ? LedState::Recovery
                                : degraded ? LedState::Partial
                                           : LedState::Recording),
                          (int)selected(health));
      }
  status = group.status();
  status.peers[0].error = CameraError::Timeout;
  lives[0] = Lifecycle::Backoff;
  TEST_ASSERT_EQUAL((int)LedState::Recovery, (int)selectLedState(&status, lives));
  lives[0] = Lifecycle::Failed;
  TEST_ASSERT_EQUAL((int)LedState::Error, (int)selectLedState(&status, lives));
  status.peers[0].error = CameraError::Cancelled;
  lives[0] = Lifecycle::Ready;
  TEST_ASSERT_EQUAL((int)LedState::Recording, (int)selectLedState(&status, lives));
  status.peers[0].enabled = false;
  status.enabled = 0;
  status.ready = status.recording = 0;
  lives[0] = Lifecycle::Failed;
  TEST_ASSERT_EQUAL((int)LedState::Off, (int)selectLedState(&status, lives));
}
void availability_is_not_worker_liveness() {
  std::array<Lifecycle, kMaxCameras> lives{};
  LedHealth h;
  TEST_ASSERT_EQUAL((int)LedState::Off, (int)selectLedState(nullptr, lives, h));
  for (DeviceHealth outcome :
       {DeviceHealth::Missing, DeviceHealth::NoFix, DeviceHealth::Desynchronized,
        DeviceHealth::RetryExhausted, DeviceHealth::DisconnectStorm, DeviceHealth::IoError}) {
    auto &d = h.devices[0];
    d.outcome = outcome;
    d.severity = LedSeverity::Error;
    TEST_ASSERT_EQUAL((int)LedState::Off, (int)selectLedState(nullptr, lives, h));
    d.enabled = d.qualified = d.current = true;
    d.severity = LedSeverity::Partial;
    TEST_ASSERT_EQUAL((int)LedState::Partial, (int)selectLedState(nullptr, lives, h));
    d.severity = LedSeverity::Error;
    TEST_ASSERT_EQUAL((int)LedState::Error, (int)selectLedState(nullptr, lives, h));
    d.current = false;
    TEST_ASSERT_EQUAL((int)LedState::Off, (int)selectLedState(nullptr, lives, h));
    d = {};
  }
  h.required_worker_stall = true;
  TEST_ASSERT_EQUAL((int)LedState::Error, (int)selectLedState(nullptr, lives, h));
  h.required_worker_stall = false;
  h.safe_mode = true;
  TEST_ASSERT_EQUAL((int)LedState::Error, (int)selectLedState(nullptr, lives, h));
}
void sink_failure_cannot_hide_behind_cached_frame() {
  struct FaultSink : LedSink {
    bool failed = false;
    int calls = 0;
    int write(LedFrame) override {
      ++calls;
      return failed ? 91 : 0;
    }
  } sink;
  StatusLed led(sink);
  TEST_ASSERT_EQUAL(0, led.service(LedState::Recording, 0));
  sink.failed = true;
  TEST_ASSERT_EQUAL(91, led.service(LedState::Recording, 500));
  TEST_ASSERT_EQUAL(91, led.service(LedState::Recording, 1000));
  sink.failed = false;
  TEST_ASSERT_EQUAL(0, led.service(LedState::Recording, 1001));
  TEST_ASSERT_EQUAL(4, sink.calls);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(ready_and_recording_require_observation);
  RUN_TEST(recording_half_open_edges);
  RUN_TEST(sink_failure_cannot_hide_behind_cached_frame);
  RUN_TEST(all_pattern_edges_and_rollover);
  RUN_TEST(phase_transitions_generation_and_gaps);
  RUN_TEST(actual_group_observations_and_priority);
  RUN_TEST(availability_is_not_worker_liveness);
  RUN_TEST(backend_refuses_without_any_gpio);
  RUN_TEST(backend_polarities_and_failures);
  return UNITY_END();
}
void setUp() {}
void tearDown() {}

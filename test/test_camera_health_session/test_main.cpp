#include "camera_event_session.h"
#include <string>
#include <unity.h>
using namespace ridesync;
struct RawClock : Clock {
  uint32_t value = 100;
  uint32_t now() const override { return value; }
};
struct Disk : StorageSink {
  std::string text;
  bool mount() override { return true; }
  bool openExclusive(const char *) override { return true; }
  size_t write(const char *s, size_t n) override {
    text.append(s, n);
    return n;
  }
  bool flush() override { return true; }
  void close() override {}
};
struct Adapter : CameraTransport, CameraRuntimePort {
  CameraHealthSampleSink *health = nullptr;
  CameraManager *manager = nullptr;
  bool disconnect_pending = false;
  unsigned passes = 0, writes = 0;
  bool begin(size_t, const CameraConfig &, Operation, Token) override {
    ++writes;
    return true;
  }
  void cancel(size_t, Token) override {}
  void close(size_t, Token) override {}
  bool attachHealthSink(CameraHealthSampleSink &s) override {
    health = &s;
    return true;
  }
  void detachHealthSink(const CameraHealthSampleSink *s) override {
    if (health == s)
      health = nullptr;
  }
  void attachGroup(RecordingManager &) override {}
  void detachGroup(const RecordingManager *) override {}
  void service(CameraServiceAction *a = nullptr) override {
    ++passes;
    if (a)
      a->beforeAdvance();
    if (disconnect_pending && manager) {
      disconnect_pending = false;
      manager->event(Event(0, manager->state(0)->token.connection, EventKind::Disconnected));
    }
  }
  void stop() override { disconnect_pending = manager != nullptr; }
  bool canDestroy() const override { return true; }
  Hero12Fault fault(uint8_t) const override { return Hero12Fault::None; }
  CameraError requestRecovery(uint8_t, bool, Hero12PowerCondition) override {
    return CameraError::Unsupported;
  }
  CameraError cancelRecovery(uint8_t) override { return CameraError::None; }
  Hero12RecoveryState recoveryState(uint8_t) const override { return {}; }
  bool recoveryReady(uint8_t) const override { return false; }
  bool commandReady(uint8_t) const override { return true; }
  bool linkRetiring(uint8_t) const override { return false; }
  bool linkReleased(uint8_t) const override { return true; }
};
struct Sink : CameraHealthTransitionSink {
  unsigned count = 0;
  void transition(const CameraHealthTransition &, uint32_t id, CameraModel model, uint32_t,
                  uint32_t) override {
    TEST_ASSERT_EQUAL_UINT32(71, id);
    TEST_ASSERT_EQUAL_INT(CameraModel::X5, model);
    ++count;
  }
};
void real_session_observation_alert_log_and_retirement_share_owner() {
  RawClock raw;
  Disk disk;
  Adapter adapter;
  CameraManager manager(raw, adapter);
  RecordingManager group(manager, raw);
  SourceConfig c;
  c.count = 1;
  c.cameras[0].name = "X5";
  c.cameras[0].model = CameraModel::X5;
  c.cameras[0].family = CameraFamily::Insta360;
  c.cameras[0].identifier = "10:20:30:40:50:60";
  c.cameras[0].address_type = AddressType::Public;
  TEST_ASSERT_TRUE(manager.configure(c).ok());
  Sink sink;
  CameraEventSession session(raw, disk, adapter, manager, group, 99, "test", "synthetic",
                             MotionAdmissionConfig{}, nullptr, false, false, true, &sink);
  TEST_ASSERT_TRUE(session.configurePeer(0, 71, CameraModel::X5));
  TEST_ASSERT_TRUE(session.activate());
  TEST_ASSERT_TRUE(session.healthSummary().enabled);
  TEST_ASSERT_NOT_NULL(adapter.health);
  TEST_ASSERT_EQUAL_INT(CameraError::None, manager.request(0, Operation::Connect));
  Event connected(0, manager.state(0)->token, EventKind::Completed);
  connected.capabilities.start = connected.capabilities.stop = CapabilityState::Supported;
  TEST_ASSERT_TRUE(manager.event(connected));
  TEST_ASSERT_EQUAL_INT(CameraError::None, manager.request(0, Operation::Start));
  Event observed(0, manager.state(0)->token.connection, EventKind::RecordingObserved);
  observed.has_health = true;
  observed.recording = RecordingState::Recording;
  observed.health.field = HealthField::Recording;
  observed.health.capability = CapabilityState::Supported;
  observed.health.validity = HealthValidity::Valid;
  observed.health.profile = HealthProfile::X5Ce80;
  observed.health.unit = HealthUnit::RecordingState;
  observed.health.connection = manager.state(0)->token.connection;
  observed.health.sequence = 1;
  observed.health.received_ms = raw.value;
  observed.health.value = static_cast<uint64_t>(RecordingState::Recording);
  TEST_ASSERT_TRUE(manager.event(observed));
  session.service();
  raw.value += 10;
  observed.recording = RecordingState::Stopped;
  observed.health.value = static_cast<uint64_t>(RecordingState::Stopped);
  observed.health.sequence = 2;
  observed.health.received_ms = raw.value;
  TEST_ASSERT_TRUE(manager.event(observed));
  session.service();
  TEST_ASSERT_TRUE(session.healthSummary().critical);
  TEST_ASSERT_EQUAL_INT(HealthReason::UnexpectedStop, session.healthSummary().reason);
  const unsigned calls = adapter.writes;
  raw.value += 6000;
  session.service();
  TEST_ASSERT_EQUAL_UINT32(calls, adapter.writes);
  session.finishImu();
  session.requestStop();
  for (unsigned n = 0; n < 40 && !session.stopped(); ++n) {
    session.service();
    session.storage().workerStep();
  }
  TEST_ASSERT_TRUE(session.stopped());
  TEST_ASSERT_NULL(adapter.health);
  TEST_ASSERT_TRUE(sink.count > 0);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, disk.text.find("#ridesync_telemetry,7"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, disk.text.find("camera_health,"));
}
void shutdown_retires_resource_fault_before_final_publication() {
  struct Observer : CameraHealthTransitionSink {
    unsigned retired = 0;
    void transition(const CameraHealthTransition &t, uint32_t, CameraModel, uint32_t,
                    uint32_t) override {
      if (t.reason == HealthReason::BatteryLow && t.kind == HealthTransitionKind::Retired)
        ++retired;
    }
  } observer;
  RawClock raw;
  Disk disk;
  Adapter adapter;
  CameraManager manager(raw, adapter);
  adapter.manager = &manager;
  RecordingManager group(manager, raw);
  SourceConfig c;
  c.count = 1;
  c.cameras[0].name = "Hero";
  c.cameras[0].model = CameraModel::HERO12_BLACK;
  c.cameras[0].family = CameraFamily::GoPro;
  c.cameras[0].identifier = "10:20:30:40:50:60";
  c.cameras[0].address_type = AddressType::Public;
  TEST_ASSERT_TRUE(manager.configure(c).ok());
  CameraEventSession session(raw, disk, adapter, manager, group, 99, "test", "synthetic",
                             MotionAdmissionConfig{}, nullptr, false, false, true, &observer);
  TEST_ASSERT_TRUE(session.configurePeer(0, 71, CameraModel::HERO12_BLACK));
  TEST_ASSERT_TRUE(session.activate());
  TEST_ASSERT_EQUAL_INT(CameraError::None, manager.request(0, Operation::Connect));
  Event connected(0, manager.state(0)->token, EventKind::Completed);
  connected.capabilities.start = connected.capabilities.stop = CapabilityState::Supported;
  TEST_ASSERT_TRUE(manager.event(connected));
  session.healthPollPeriod(0, 5000, raw.value);
  QualifiedHealthSample sample;
  sample.field = HealthField::BatteryPercent;
  sample.capability = CapabilityState::Supported;
  sample.validity = HealthValidity::Valid;
  sample.profile = HealthProfile::Hero12V1;
  sample.unit = HealthUnit::Percent;
  sample.value = 1;
  sample.received_ms = raw.value;
  sample.sequence = 1;
  sample.connection = manager.state(0)->token.connection;
  session.healthSample(0, sample);
  TEST_ASSERT_TRUE(session.healthSummary().critical);
  session.service();
  session.finishImu();
  session.requestStop();
  for (unsigned n = 0; n < 40 && !session.stopped(); ++n) {
    session.service();
    session.storage().workerStep();
  }
  TEST_ASSERT_TRUE(session.stopped());
  TEST_ASSERT_EQUAL_UINT32(1, observer.retired);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(real_session_observation_alert_log_and_retirement_share_owner);
  RUN_TEST(shutdown_retires_resource_fault_before_final_publication);
  return UNITY_END();
}

#include "camera_health.h"
#include "recording_manager.h"
#include <unity.h>
using namespace ridesync;
namespace {
QualifiedHealthSample sample(HealthField field, uint64_t value, uint32_t at, uint32_t seq,
                             HealthProfile profile = HealthProfile::Hero12V1, uint32_t conn = 1) {
  QualifiedHealthSample s;
  s.field = field;
  s.value = value;
  s.received_ms = at;
  s.sequence = seq;
  s.connection = conn;
  s.profile = profile;
  s.capability = CapabilityState::Supported;
  s.validity = HealthValidity::Valid;
  switch (field) {
  case HealthField::Recording:
    s.unit = HealthUnit::RecordingState;
    break;
  case HealthField::BatteryPercent:
    s.unit = HealthUnit::Percent;
    break;
  case HealthField::RemainingVideoSeconds:
    s.unit = HealthUnit::Seconds;
    break;
  case HealthField::SdRemainingKilobytes:
    s.unit = HealthUnit::Kilobytes;
    break;
  case HealthField::Overheating:
    s.unit = HealthUnit::Boolean;
    break;
  default:
    break;
  }
  return s;
}
void start(CameraHealthEvaluator &h, size_t peer = 0, uint32_t at = 0, uint32_t deadline = 1000) {
  Token t;
  t.connection = 1;
  t.operation = 2;
  h.healthIntent(peer, Operation::Start, 1, 0, false, t, at, deadline);
}
int level(CameraHealthEvaluator &h, HealthReason reason, size_t peer = 0) {
  return static_cast<int>(h.severity(peer, reason));
}
void ack_alone_and_no_first_recording_are_unconfirmed() {
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::HERO12_BLACK, 1, true, 0, 5000);
  start(h);
  Event ack(0, Token{}, EventKind::Completed);
  ack.token.connection = 1;
  h.healthAccepted(ack, Operation::Start, 1, 100);
  h.tick(1099);
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::RecordingUnconfirmed));
  h.tick(1100);
  TEST_ASSERT_EQUAL(3, level(h, HealthReason::RecordingUnconfirmed));
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::UnexpectedStop));
  TEST_ASSERT_FALSE(h.snapshot(0)->saw_recording);
}
void fresh_recording_stop_and_expiry_have_distinct_causes() {
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::X5, 1, true, 0);
  start(h);
  TEST_ASSERT_TRUE(
      h.observe(0, sample(HealthField::Recording, 2, 10, 1, HealthProfile::X5Ce80), 10));
  h.tick(5010); // X5 equality is fresh.
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::RecordingEvidenceLost));
  h.tick(5011);
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::RecordingEvidenceLost));
  const auto &f = h.snapshot(0)->fields[0];
  TEST_ASSERT_EQUAL(0, static_cast<int>(f.validity));
  TEST_ASSERT_EQUAL_UINT64(2, f.value);
  TEST_ASSERT_TRUE(f.historical);
  h.observe(0, sample(HealthField::Recording, 1, 5020, 2, HealthProfile::X5Ce80), 5020);
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::RecordingEvidenceLost));
  TEST_ASSERT_EQUAL(3, level(h, HealthReason::UnexpectedStop));
  h.observe(0, sample(HealthField::Recording, 2, 5030, 3, HealthProfile::X5Ce80), 5030);
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::UnexpectedStop));
}
void peers_rollover_and_receipt_age_are_independent() {
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::DJI_ACTION4, 1, true, UINT32_MAX - 999);
  h.configure(1, CameraModel::X5, 1, true, UINT32_MAX - 999);
  start(h, 0, UINT32_MAX - 999, 100);
  start(h, 1, UINT32_MAX - 999, 100);
  h.observe(0, sample(HealthField::Recording, 2, UINT32_MAX - 999, 1, HealthProfile::DjiStatus), 0);
  h.observe(1, sample(HealthField::Recording, 2, UINT32_MAX - 999, 1, HealthProfile::X5Ce80), 0);
  h.tick(499);
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::RecordingEvidenceLost));
  h.tick(500); // DJI expires at equality (age 1500).
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::RecordingEvidenceLost));
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::RecordingEvidenceLost, 1));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX - 999, h.snapshot(1)->fields[0].received_ms);
  h.tick(0x80000200UL); // Declared service gap >=2^31 invalidates both.
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::RecordingEvidenceLost, 1));
  TEST_ASSERT_TRUE(h.observe(
      1, sample(HealthField::Recording, 2, 0x80000201UL, 2, HealthProfile::X5Ce80), 0x80000201UL));
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::RecordingEvidenceLost, 1));
}
void duplicates_out_of_order_and_wrong_generation_do_not_refresh() {
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::X5, 1, true, 0);
  start(h);
  auto s = sample(HealthField::Recording, 2, 10, 5, HealthProfile::X5Ce80);
  TEST_ASSERT_TRUE(h.observe(0, s, 10));
  s.received_ms = 4000;
  TEST_ASSERT_FALSE(h.observe(0, s, 4000));
  s.sequence = 4;
  TEST_ASSERT_FALSE(h.observe(0, s, 4000));
  s.sequence = 6;
  s.connection = 2;
  TEST_ASSERT_FALSE(h.observe(0, s, 4000));
  h.tick(5011);
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::RecordingEvidenceLost));
  TEST_ASSERT_EQUAL_UINT32(10, h.snapshot(0)->fields[0].received_ms);
}
void battery_thresholds_require_two_distinct_spaced_recovery_samples() {
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::HERO12_BLACK, 1, true, 0, 5000);
  h.observe(0, sample(HealthField::BatteryPercent, 5, 0, 1), 0);
  TEST_ASSERT_EQUAL(3, level(h, HealthReason::BatteryLow));
  h.observe(0, sample(HealthField::BatteryPercent, 10, 1000, 2), 1000);
  h.observe(0, sample(HealthField::BatteryPercent, 10, 5999, 3), 5999);
  TEST_ASSERT_EQUAL(3, level(h, HealthReason::BatteryLow));
  h.observe(0, sample(HealthField::BatteryPercent, 10, 6000, 4), 6000);
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::BatteryLow));
  h.observe(0, sample(HealthField::BatteryPercent, 25, 7000, 5), 7000);
  h.observe(0, sample(HealthField::BatteryPercent, 25, 12000, 6), 12000);
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::BatteryLow));
  h.observe(0, sample(HealthField::BatteryPercent, 20, 13000, 7), 13000);
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::BatteryLow));
}
void storage_and_thermal_use_qualified_units_and_hysteresis() {
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::HERO12_BLACK, 1, true, 0, 5000);
  h.observe(0, sample(HealthField::RemainingVideoSeconds, 60, 0, 1), 0);
  h.observe(0, sample(HealthField::Overheating, 1, 0, 1), 0);
  TEST_ASSERT_EQUAL(3, level(h, HealthReason::StorageLow));
  TEST_ASSERT_EQUAL(3, level(h, HealthReason::Overheating));
  h.observe(0, sample(HealthField::RemainingVideoSeconds, 120, 1, 2), 1);
  h.observe(0, sample(HealthField::RemainingVideoSeconds, 120, 5001, 3), 5001);
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::StorageLow));
  h.observe(0, sample(HealthField::RemainingVideoSeconds, 420, 5002, 4), 5002);
  h.observe(0, sample(HealthField::RemainingVideoSeconds, 420, 10002, 5), 10002);
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::StorageLow));
  h.observe(0, sample(HealthField::Overheating, 0, 5000, 2), 5000);
  auto dupe = sample(HealthField::Overheating, 0, 10000, 2);
  TEST_ASSERT_FALSE(h.observe(0, dupe, 10000));
  TEST_ASSERT_EQUAL(3, level(h, HealthReason::Overheating));
  h.observe(0, sample(HealthField::Overheating, 0, 10000, 3), 10000);
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::Overheating));
}
void absent_invalid_stale_and_unsupported_never_clear_faults() {
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::HERO12_BLACK, 1, true, 0, 5000);
  h.observe(0, sample(HealthField::BatteryPercent, 5, 0, 1), 0);
  h.observe(0, sample(HealthField::BatteryPercent, 25, 1000, 2), 1000);
  auto missing = sample(HealthField::BatteryPercent, 0, 2000, 3);
  missing.validity = HealthValidity::Unavailable;
  TEST_ASSERT_TRUE(h.observe(0, missing, 2000));
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::BatteryLow)); // Fault history now Partial.
  TEST_ASSERT_EQUAL_UINT64(25, h.snapshot(0)->fields[1].value);
  h.observe(0, sample(HealthField::BatteryPercent, 25, 6000, 4), 6000);
  TEST_ASSERT_EQUAL(3, level(h, HealthReason::BatteryLow)); // Still needs second evidence.
  h.tick(37000);                                            // Resource equality 2*15000+1000.
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::BatteryLow));
  auto bad = sample(HealthField::BatteryPercent, 101, 38000, 5);
  TEST_ASSERT_FALSE(h.observe(0, bad, 38000));
  CameraHealthEvaluator x;
  x.configure(0, CameraModel::X5, 1, true, 0);
  TEST_ASSERT_FALSE(
      x.observe(0, sample(HealthField::BatteryPercent, 1, 0, 1, HealthProfile::X5Ce80), 0));
  TEST_ASSERT_EQUAL(0, level(x, HealthReason::BatteryLow));
  h.observe(0, sample(HealthField::RemainingVideoSeconds, 60, 38000, 1), 38000);
  auto photo = sample(HealthField::RemainingVideoSeconds, 0, 39000, 2);
  photo.validity = HealthValidity::Unavailable;
  h.observe(0, photo, 39000);
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::StorageLow));
}
void unsupported_monitoring_and_invalid_poll_period_are_explicit() {
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::ONE_RS, 1, true, 0);
  start(h);
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::MonitoringUnavailable));
  h.tick(10000);
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::RecordingUnconfirmed));
  h.configure(1, CameraModel::HERO12_BLACK, 1, true, 0, 30001);
  start(h, 1);
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::MonitoringUnavailable, 1));
  h.pollPeriod(1, 30000, 0);
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::MonitoringUnavailable, 1));
}
void reset_retirement_and_reconnect_require_current_generation() {
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::X5, 1, true, 0);
  start(h);
  h.observe(0, sample(HealthField::Recording, 2, 0, 1, HealthProfile::X5Ce80), 0);
  Token t;
  t.connection = 2;
  h.healthConnection(0, CameraModel::X5, t, false, 1);
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::RecordingEvidenceLost));
  h.healthConnection(0, CameraModel::X5, t, true, 2);
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::RecordingEvidenceLost));
  TEST_ASSERT_FALSE(
      h.observe(0, sample(HealthField::Recording, 2, 3, 2, HealthProfile::X5Ce80), 3));
  h.observe(0, sample(HealthField::Recording, 2, 3, 1, HealthProfile::X5Ce80, 2), 3);
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::RecordingEvidenceLost));
  h.observe(0, sample(HealthField::Recording, 1, 4, 2, HealthProfile::X5Ce80, 2), 4);
  h.healthRetired(0, t, 5);
  TEST_ASSERT_FALSE(h.snapshot(0)->armed);
  CameraHealthTransition tr;
  bool retired = false;
  while (h.popTransition(tr))
    if (tr.kind == HealthTransitionKind::Retired)
      retired = true;
  TEST_ASSERT_TRUE(retired);
}
void hero12_reconnect_requires_a_new_generation_qualified_poll_floor() {
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::HERO12_BLACK, 1, true, 0, 5000);
  start(h);
  TEST_ASSERT_TRUE(h.observe(0, sample(HealthField::Recording, 2, 1, 1), 1));
  Token reconnect;
  reconnect.connection = 2;
  h.healthConnection(0, CameraModel::HERO12_BLACK, reconnect, false, 100);
  h.healthConnection(0, CameraModel::HERO12_BLACK, reconnect, true, 200);
  TEST_ASSERT_EQUAL_UINT32(0, h.snapshot(0)->minimum_poll_ms);
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::MonitoringUnavailable));
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::RecordingEvidenceLost));
  TEST_ASSERT_TRUE(
      h.observe(0, sample(HealthField::Recording, 2, 201, 1, HealthProfile::Hero12V1, 2), 201));
  TEST_ASSERT_EQUAL(static_cast<int>(HealthValidity::Unknown),
                    static_cast<int>(h.snapshot(0)->fields[0].validity));
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::RecordingEvidenceLost));
  h.pollPeriod(0, 30000, 202);
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::MonitoringUnavailable));
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::RecordingEvidenceLost));
  TEST_ASSERT_TRUE(
      h.observe(0, sample(HealthField::Recording, 2, 203, 2, HealthProfile::Hero12V1, 2), 203));
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::RecordingEvidenceLost));
  h.tick(61202);
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::RecordingEvidenceLost));
  h.tick(61203);
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::RecordingEvidenceLost));
}
void longer_poll_floor_cannot_revive_expired_observations_before_owner_tick() {
  for (uint32_t late = 0; late <= 1; ++late) {
    CameraHealthEvaluator h;
    h.configure(0, CameraModel::HERO12_BLACK, 1, true, 0, 5000);
    start(h);
    TEST_ASSERT_TRUE(h.observe(0, sample(HealthField::Recording, 2, 0, 1), 0));
    // Adapter metadata can arrive before the owner tick at or after the old TTL.
    const uint32_t recording_now = 11000 + late;
    h.pollPeriod(0, 30000, recording_now);
    const auto &recording = h.snapshot(0)->fields[0];
    TEST_ASSERT_EQUAL(static_cast<int>(HealthValidity::Unknown),
                      static_cast<int>(recording.validity));
    TEST_ASSERT_TRUE(recording.historical);
    TEST_ASSERT_EQUAL_UINT64(2, recording.value);
    TEST_ASSERT_EQUAL_UINT32(30000, h.snapshot(0)->minimum_poll_ms);
    TEST_ASSERT_EQUAL(2, level(h, HealthReason::RecordingEvidenceLost));
    TEST_ASSERT_TRUE(
        h.observe(0, sample(HealthField::Recording, 2, recording_now + 1, 2), recording_now + 1));
    TEST_ASSERT_EQUAL(static_cast<int>(HealthValidity::Valid),
                      static_cast<int>(recording.validity));
    TEST_ASSERT_EQUAL(0, level(h, HealthReason::RecordingEvidenceLost));

    h.configure(1, CameraModel::HERO12_BLACK, 1, true, 0, 5000);
    TEST_ASSERT_TRUE(h.observe(1, sample(HealthField::BatteryPercent, 5, 0, 1), 0));
    const uint32_t resource_now = 31000 + late;
    h.pollPeriod(1, 30000, resource_now);
    const auto &battery = h.snapshot(1)->fields[static_cast<size_t>(HealthField::BatteryPercent)];
    TEST_ASSERT_EQUAL(static_cast<int>(HealthValidity::Unknown),
                      static_cast<int>(battery.validity));
    TEST_ASSERT_TRUE(battery.historical);
    TEST_ASSERT_EQUAL_UINT64(5, battery.value);
    TEST_ASSERT_EQUAL(2, level(h, HealthReason::BatteryLow, 1));
    TEST_ASSERT_EQUAL(
        static_cast<int>(HealthSeverity::Critical),
        static_cast<int>(h.snapshot(1)->alerts[static_cast<size_t>(HealthReason::BatteryLow)]));
    TEST_ASSERT_TRUE(h.observe(1, sample(HealthField::BatteryPercent, 5, resource_now + 1, 2),
                               resource_now + 1));
    TEST_ASSERT_EQUAL(static_cast<int>(HealthValidity::Valid), static_cast<int>(battery.validity));
    TEST_ASSERT_EQUAL(3, level(h, HealthReason::BatteryLow, 1));
  }
}
void reset_retires_resource_faults_and_poll_floor_without_claiming_recovery() {
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::HERO12_BLACK, 1, true, 0, 5000);
  h.observe(0, sample(HealthField::BatteryPercent, 5, 0, 1), 0);
  CameraHealthTransition t;
  while (h.popTransition(t)) {
  }
  h.healthRetired(0, Token{}, 1); // User Stop retires recording only.
  TEST_ASSERT_EQUAL(3, level(h, HealthReason::BatteryLow));
  // Manager reset must have a separate all-health retirement contract.
  h.healthReset(0, Token{}, 2);
  TEST_ASSERT_EQUAL_UINT32(0, h.snapshot(0)->minimum_poll_ms);
  h.configure(0, CameraModel::X5, 2, false, 2);
  TEST_ASSERT_EQUAL(0, level(h, HealthReason::BatteryLow));
  bool retired = false;
  while (h.popTransition(t)) {
    if (t.reason == HealthReason::BatteryLow) {
      TEST_ASSERT_EQUAL(static_cast<int>(HealthTransitionKind::Retired), static_cast<int>(t.kind));
      retired = true;
    }
  }
  TEST_ASSERT_TRUE(retired);
}
void transition_ring_is_bounded_and_retains_current_fault() {
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::HERO12_BLACK, 1, true, 0, 5000);
  for (uint32_t i = 0; i < 30; ++i) {
    h.observe(0, sample(HealthField::BatteryPercent, (i % 2) ? 20 : 5, i, i + 1), i);
    // Force episode retirement/new critical transitions without hysteresis.
    start(h, 0, i, i + 1);
    h.healthFailure(0, Token{}, true, i + 1);
  }
  TEST_ASSERT_EQUAL(8, h.pendingTransitions());
  TEST_ASSERT_GREATER_THAN(0, h.dropped());
  TEST_ASSERT_EQUAL(3, level(h, HealthReason::RecordingUnconfirmed));
  CameraHealthTransition tr;
  uint32_t previous = 0;
  while (h.popTransition(tr)) {
    TEST_ASSERT_GREATER_THAN(previous, tr.sequence);
    previous = tr.sequence;
  }
  TEST_ASSERT_EQUAL(0, h.pendingTransitions());
}
void invalid_sample_receipt_order_and_stale_numeric_values_stay_unknown() {
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::HERO12_BLACK, 1, true, 0, 5000);
  h.observe(0, sample(HealthField::BatteryPercent, 5, 100, 1), 100);
  auto missing = sample(HealthField::BatteryPercent, 0, 1000, 2);
  missing.validity = HealthValidity::Unavailable;
  h.observe(0, missing, 1000);
  TEST_ASSERT_FALSE(h.observe(0, sample(HealthField::BatteryPercent, 100, 900, 3), 1000));
  TEST_ASSERT_EQUAL_UINT64(5, h.snapshot(0)->fields[1].value);
  h.observe(0, sample(HealthField::BatteryPercent, 100, 1001, 4), 33000);
  TEST_ASSERT_EQUAL(0, static_cast<int>(h.snapshot(0)->fields[1].validity));
  TEST_ASSERT_EQUAL(2, level(h, HealthReason::BatteryLow));
  TEST_ASSERT_EQUAL_UINT64(5, h.snapshot(0)->fields[1].value);
}
void stale_fault_transition_names_the_unavailable_resource_and_keeps_history() {
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::HERO12_BLACK, 1, true, 0, 5000);
  h.observe(0, sample(HealthField::Recording, 2, 0, 1), 0);
  h.observe(0, sample(HealthField::BatteryPercent, 5, 0, 1), 0);
  h.observe(0, sample(HealthField::RemainingVideoSeconds, 1000, 0, 1), 0);
  h.observe(0, sample(HealthField::SdRemainingKilobytes, 100000, 0, 1), 0);
  h.observe(0, sample(HealthField::Overheating, 0, 0, 1), 0);
  CameraHealthTransition tr;
  while (h.popTransition(tr)) {
  }
  h.tick(31000);
  bool stale = false;
  while (h.popTransition(tr)) {
    if (tr.reason == HealthReason::BatteryLow) {
      TEST_ASSERT_EQUAL(static_cast<int>(HealthTransitionKind::SeverityChanged),
                        static_cast<int>(tr.kind));
      TEST_ASSERT_EQUAL(static_cast<int>(HealthValidity::Unknown), static_cast<int>(tr.validity));
      TEST_ASSERT_EQUAL(2, static_cast<int>(tr.severity));
      stale = true;
    }
    if (tr.reason == HealthReason::ResourceUnavailable)
      TEST_ASSERT_EQUAL(static_cast<int>(HealthUnit::Percent), static_cast<int>(tr.unit));
  }
  TEST_ASSERT_TRUE(stale);
  h.observe(0, sample(HealthField::BatteryPercent, 25, 32000, 2), 32000);
  TEST_ASSERT_EQUAL(3, level(h, HealthReason::BatteryLow));
}
struct TestClock : Clock {
  uint32_t time = 0;
  uint32_t now() const override { return time; }
};
struct Transport : CameraTransport {
  bool begin(size_t, const CameraConfig &, Operation, Token) override { return true; }
  void cancel(size_t, Token) override {}
  void close(size_t, Token) override {}
};
SourceConfig registry() {
  SourceConfig c;
  c.count = 1;
  auto &p = c.cameras[0];
  p.name = "camera";
  p.family = CameraFamily::Insta360;
  p.model = CameraModel::X5;
  p.identifier = "01:23:45:67:89:A0";
  p.address_type = AddressType::Random;
  return c;
}
void connected(CameraManager &m) {
  m.request(0, Operation::Connect);
  Event e(0, m.state(0)->token, EventKind::Completed);
  e.capabilities.start = e.capabilities.stop = e.capabilities.query = CapabilityState::Supported;
  TEST_ASSERT_TRUE(m.event(e));
}
void manager_rejected_stop_preserves_episode_queued_stop_retires_immediately() {
  TestClock clock;
  Transport port;
  CameraManager m(clock, port);
  m.configure(registry());
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::X5, 0, false, 0);
  TEST_ASSERT_TRUE(m.attachHealthObserver(h));
  connected(m);
  TEST_ASSERT_EQUAL(0, static_cast<int>(m.request(0, Operation::Start)));
  TEST_ASSERT_TRUE(h.snapshot(0)->armed);
  TEST_ASSERT_EQUAL(static_cast<int>(CameraError::InvalidPeer),
                    static_cast<int>(m.request(1, Operation::Stop)));
  TEST_ASSERT_TRUE(h.snapshot(0)->armed);
  TEST_ASSERT_EQUAL(0, static_cast<int>(m.request(0, Operation::Stop)));
  TEST_ASSERT_FALSE(h.snapshot(0)->armed); // Stop queued behind Start is already admitted.
  m.cancel(0);
  TEST_ASSERT_FALSE(h.snapshot(0)->armed);
}
void seal_invalidates_resources_and_actual_attempt_token_is_correlated() {
  TestClock clock;
  Transport port;
  CameraManager m(clock, port);
  m.configure(registry());
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::X5, 0, false, 0);
  m.attachHealthObserver(h);
  connected(m);
  m.request(0, Operation::Start);
  TEST_ASSERT_EQUAL_UINT32(m.state(0)->token.operation, h.snapshot(0)->operation);
  m.seal(0);
  TEST_ASSERT_FALSE(h.snapshot(0)->connected);
  TEST_ASSERT_EQUAL_UINT32(m.state(0)->token.connection, h.snapshot(0)->connection);
}
void rejected_stop_and_unqualified_unknown_do_not_replace_qualified_history() {
  TestClock clock;
  Transport port;
  CameraManager m(clock, port);
  m.configure(registry());
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::X5, 0, false, 0);
  m.attachHealthObserver(h);
  m.request(0, Operation::Connect);
  Event ready(0, m.state(0)->token, EventKind::Completed);
  ready.capabilities.start = CapabilityState::Supported;
  ready.capabilities.stop = CapabilityState::Unsupported;
  ready.capabilities.query = CapabilityState::Supported;
  TEST_ASSERT_TRUE(m.event(ready));
  m.request(0, Operation::Start);
  TEST_ASSERT_EQUAL(static_cast<int>(CameraError::Unsupported),
                    static_cast<int>(m.request(0, Operation::Stop)));
  TEST_ASSERT_TRUE(h.snapshot(0)->armed);
  Event e(0, m.state(0)->token, EventKind::RecordingObserved);
  e.recording = RecordingState::Recording;
  e.has_health = true;
  e.health =
      sample(HealthField::Recording, 2, 10, 1, HealthProfile::X5Ce80, m.state(0)->token.connection);
  clock.time = 100;
  TEST_ASSERT_TRUE(m.event(e));
  Event unknown(0, m.state(0)->token, EventKind::RecordingObserved);
  clock.time = 200;
  TEST_ASSERT_TRUE(m.event(unknown));
  e.health = sample(HealthField::Recording, 2, 150, 2, HealthProfile::X5Ce80,
                    m.state(0)->token.connection);
  clock.time = 210;
  TEST_ASSERT_TRUE(m.event(e));
  TEST_ASSERT_EQUAL_UINT32(150, h.snapshot(0)->fields[0].received_ms);
}
void already_recording_group_shortcut_arms_without_fabricating_operation() {
  TestClock clock;
  Transport port;
  CameraManager m(clock, port);
  m.configure(registry());
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::X5, 0, false, 0);
  m.attachHealthObserver(h);
  connected(m);
  Event e(0, m.state(0)->token, EventKind::RecordingObserved);
  e.recording = RecordingState::Recording;
  e.has_health = true;
  e.health =
      sample(HealthField::Recording, 2, 0, 1, HealthProfile::X5Ce80, m.state(0)->token.connection);
  TEST_ASSERT_TRUE(m.event(e));
  RecordingManager group(m, clock);
  group.request(RecordingState::Recording);
  TEST_ASSERT_TRUE(h.snapshot(0)->armed);
  TEST_ASSERT_TRUE(h.snapshot(0)->saw_recording);
  TEST_ASSERT_EQUAL_UINT32(0, h.snapshot(0)->operation);
  e.recording = RecordingState::Stopped;
  e.health.value = 1;
  e.health.sequence = 2;
  e.health.received_ms = 1;
  clock.time = 1;
  TEST_ASSERT_TRUE(group.event(e));
  TEST_ASSERT_EQUAL(3, level(h, HealthReason::UnexpectedStop));
}
void stable_group_resync_preserves_recording_episode_and_detects_manual_stop() {
  TestClock clock;
  Transport port;
  CameraManager m(clock, port);
  m.configure(registry());
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::X5, 0, false, 0);
  TEST_ASSERT_TRUE(m.attachHealthObserver(h));
  connected(m);
  RecordingManager group(m, clock);
  TEST_ASSERT_EQUAL(0, static_cast<int>(group.request(RecordingState::Recording)));
  Event started(0, m.state(0)->token, EventKind::Completed);
  TEST_ASSERT_TRUE(group.event(started));
  Event observed(0, m.state(0)->token, EventKind::RecordingObserved);
  observed.recording = RecordingState::Recording;
  observed.has_health = true;
  observed.health =
      sample(HealthField::Recording, 2, 1, 1, HealthProfile::X5Ce80, m.state(0)->token.connection);
  clock.time = 1;
  TEST_ASSERT_TRUE(group.event(observed));
  TEST_ASSERT_EQUAL(0, group.status().pending);
  TEST_ASSERT_TRUE(h.snapshot(0)->armed);
  const auto episode = h.snapshot(0)->episode;
  const auto recording_group = h.snapshot(0)->group_generation;

  clock.time = 2;
  TEST_ASSERT_EQUAL(0, static_cast<int>(group.resync()));
  TEST_ASSERT_TRUE(h.snapshot(0)->armed);
  TEST_ASSERT_EQUAL_UINT32(episode, h.snapshot(0)->episode);
  TEST_ASSERT_EQUAL_UINT32(recording_group, h.snapshot(0)->group_generation);
  Event queried(0, m.state(0)->token, EventKind::Completed);
  TEST_ASSERT_TRUE(group.event(queried));
  observed.token = m.state(0)->token;
  observed.health.received_ms = 3;
  observed.health.sequence = 2;
  clock.time = 3;
  TEST_ASSERT_TRUE(group.event(observed));
  TEST_ASSERT_EQUAL(0, group.status().pending);

  observed.recording = RecordingState::Stopped;
  observed.health.value = 1;
  observed.health.received_ms = 4;
  observed.health.sequence = 3;
  clock.time = 4;
  TEST_ASSERT_TRUE(group.event(observed));
  TEST_ASSERT_EQUAL(3, level(h, HealthReason::UnexpectedStop));
  CameraHealthTransition transition;
  TEST_ASSERT_TRUE(h.popTransition(transition));
  TEST_ASSERT_EQUAL(static_cast<int>(HealthTransitionKind::Enter),
                    static_cast<int>(transition.kind));
  TEST_ASSERT_EQUAL(static_cast<int>(HealthReason::UnexpectedStop),
                    static_cast<int>(transition.reason));
  TEST_ASSERT_EQUAL_UINT32(episode, transition.episode);
  TEST_ASSERT_EQUAL_UINT32(recording_group, transition.group_generation);
}
void group_stop_during_preparation_retires_and_observation_preserves_receipt() {
  TestClock clock;
  Transport port;
  CameraManager m(clock, port);
  m.configure(registry());
  CameraHealthEvaluator h;
  h.configure(0, CameraModel::X5, 0, false, 0);
  m.attachHealthObserver(h);
  connected(m);
  RecordingManager group(m, clock);
  TEST_ASSERT_EQUAL(0, static_cast<int>(group.request(RecordingState::Recording)));
  TEST_ASSERT_TRUE(h.snapshot(0)->armed);
  Event e(0, m.state(0)->token, EventKind::RecordingObserved);
  e.recording = RecordingState::Recording;
  e.health =
      sample(HealthField::Recording, 2, 10, 1, HealthProfile::X5Ce80, m.state(0)->token.connection);
  e.has_health = true;
  clock.time = 200;
  TEST_ASSERT_TRUE(group.event(e));
  TEST_ASSERT_EQUAL_UINT32(10, m.state(0)->last_observed_ms);
  TEST_ASSERT_EQUAL_UINT32(10, h.snapshot(0)->fields[0].received_ms);
  TEST_ASSERT_EQUAL(0, static_cast<int>(group.request(RecordingState::Stopped)));
  TEST_ASSERT_FALSE(h.snapshot(0)->armed);
  m.seal(0);
  TEST_ASSERT_FALSE(h.snapshot(0)->armed);
  m.reset();
  TEST_ASSERT_FALSE(h.snapshot(0)->armed);
}
} // namespace
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(ack_alone_and_no_first_recording_are_unconfirmed);
  RUN_TEST(fresh_recording_stop_and_expiry_have_distinct_causes);
  RUN_TEST(peers_rollover_and_receipt_age_are_independent);
  RUN_TEST(duplicates_out_of_order_and_wrong_generation_do_not_refresh);
  RUN_TEST(battery_thresholds_require_two_distinct_spaced_recovery_samples);
  RUN_TEST(storage_and_thermal_use_qualified_units_and_hysteresis);
  RUN_TEST(absent_invalid_stale_and_unsupported_never_clear_faults);
  RUN_TEST(unsupported_monitoring_and_invalid_poll_period_are_explicit);
  RUN_TEST(reset_retirement_and_reconnect_require_current_generation);
  RUN_TEST(hero12_reconnect_requires_a_new_generation_qualified_poll_floor);
  RUN_TEST(longer_poll_floor_cannot_revive_expired_observations_before_owner_tick);
  RUN_TEST(reset_retires_resource_faults_and_poll_floor_without_claiming_recovery);
  RUN_TEST(transition_ring_is_bounded_and_retains_current_fault);
  RUN_TEST(stale_fault_transition_names_the_unavailable_resource_and_keeps_history);
  RUN_TEST(invalid_sample_receipt_order_and_stale_numeric_values_stay_unknown);
  RUN_TEST(seal_invalidates_resources_and_actual_attempt_token_is_correlated);
  RUN_TEST(manager_rejected_stop_preserves_episode_queued_stop_retires_immediately);
  RUN_TEST(rejected_stop_and_unqualified_unknown_do_not_replace_qualified_history);
  RUN_TEST(already_recording_group_shortcut_arms_without_fabricating_operation);
  RUN_TEST(stable_group_resync_preserves_recording_episode_and_detects_manual_stop);
  RUN_TEST(group_stop_during_preparation_retires_and_observation_preserves_receipt);
  return UNITY_END();
}

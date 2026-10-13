#include "camera_health_output.h"
#include <string>
#include <unity.h>
using namespace ridesync;
struct Port : CameraHealthSerialPort {
  size_t room = 512, writes = 0;
  std::string text;
  size_t writable() const override { return room; }
  size_t write(const char *s, size_t n) override {
    ++writes;
    text.append(s, n);
    return n;
  }
};
void serial_transitions_are_bounded_and_explain_reason() {
  Port port;
  CameraHealthSerialSink sink(port);
  CameraHealthTransition t;
  t.peer = 2;
  t.reason = HealthReason::UnexpectedStop;
  t.severity = HealthSeverity::Critical;
  t.sequence = 19;
  sink.transition(t, 77, CameraModel::HERO12_BLACK, 3, 4);
  TEST_ASSERT_EQUAL(1, port.writes);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, port.text.find("UnexpectedStop"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, port.text.find("id=77"));
  TEST_ASSERT_TRUE(port.text.size() < 128);
  port.room = 0;
  sink.transition(t, 77, CameraModel::HERO12_BLACK, 3, 4);
  TEST_ASSERT_EQUAL(1, port.writes);
  TEST_ASSERT_EQUAL(1, sink.dropped());
}
void saturated_serial_fields_fit_with_complete_newline() {
  Port port;
  CameraHealthSerialSink sink(port);
  CameraHealthTransition t;
  t.peer = 7;
  t.reason = HealthReason::RecordingEvidenceLost;
  t.severity = HealthSeverity::Warning;
  t.validity = HealthValidity::Unavailable;
  t.kind = HealthTransitionKind::Enter;
  t.episode = t.sequence = UINT32_MAX;
  t.age_ms = INT32_MAX;
  sink.transition(t, UINT32_MAX, CameraModel::DJI_OSMO360, UINT32_MAX, UINT32_MAX);
  TEST_ASSERT_EQUAL(1, port.writes);
  TEST_ASSERT_EQUAL(0, sink.dropped());
  TEST_ASSERT_EQUAL(126, port.text.size());
  TEST_ASSERT_EQUAL_CHAR('\n', port.text.back());
  TEST_ASSERT_NOT_EQUAL(std::string::npos, port.text.find("d=4294967295 c=4294967295\n"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, port.text.find("a=2147483647"));
}
void saturated_serial_transition_fits_default_uart_fifo() {
  Port port;
  port.room = 127; // Default ESP32 UART FIFO availability, without a TX ring.
  CameraHealthSerialSink sink(port);
  CameraHealthTransition t;
  t.peer = 7;
  t.reason = HealthReason::MonitoringUnavailable;
  t.severity = HealthSeverity::Warning;
  t.validity = HealthValidity::Unavailable;
  t.kind = HealthTransitionKind::Enter;
  t.episode = t.sequence = UINT32_MAX;
  t.age_ms = INT32_MAX;
  sink.transition(t, UINT32_MAX, CameraModel::DJI_OSMO360, UINT32_MAX, UINT32_MAX);
  TEST_ASSERT_EQUAL(1, port.writes);
  TEST_ASSERT_EQUAL(0, sink.dropped());
  TEST_ASSERT_TRUE(port.text.size() <= port.room);
  TEST_ASSERT_EQUAL_CHAR('\n', port.text.back());
  TEST_ASSERT_EQUAL_STRING("HEALTH p=7 id=4294967295 m=8 e=4294967295 s=4294967295 k=0 "
                           "MonitoringUnavailable/2 v=2 a=2147483647 d=4294967295 c=4294967295\n",
                           port.text.c_str());
  port.room = 125; // Busy FIFO still rejects the entire line without a partial write.
  sink.transition(t, UINT32_MAX, CameraModel::DJI_OSMO360, UINT32_MAX, UINT32_MAX);
  TEST_ASSERT_EQUAL(1, port.writes);
  TEST_ASSERT_EQUAL(1, sink.dropped());
}
void summary_reports_current_severity_and_disables_cleanly() {
  CameraHealthEvaluator e;
  e.configure(0, CameraModel::GO3S, 1, true, 100);
  Token token;
  token.connection = 1;
  e.healthGroupIntent(0, RecordingState::Recording, 5, token, 100);
  const auto active = summarizeCameraHealth(e, true);
  TEST_ASSERT_TRUE(active.enabled);
  TEST_ASSERT_TRUE(active.warning);
  TEST_ASSERT_FALSE(active.critical);
  TEST_ASSERT_EQUAL_INT(HealthReason::MonitoringUnavailable, active.reason);
  TEST_ASSERT_FALSE(summarizeCameraHealth(e, false).enabled);
}
void stale_resource_fault_is_current_warning_with_historical_critical() {
  CameraHealthEvaluator e;
  e.configure(0, CameraModel::HERO12_BLACK, 1, true, 100, 5000);
  QualifiedHealthSample s;
  s.field = HealthField::Overheating;
  s.capability = CapabilityState::Supported;
  s.validity = HealthValidity::Valid;
  s.profile = HealthProfile::Hero12V1;
  s.unit = HealthUnit::Boolean;
  s.value = 1;
  s.connection = 1;
  s.sequence = 1;
  s.received_ms = 100;
  TEST_ASSERT_TRUE(e.observe(0, s, 100));
  TEST_ASSERT_TRUE(summarizeCameraHealth(e, true).critical);
  e.tick(31100);
  const auto current = summarizeCameraHealth(e, true);
  TEST_ASSERT_FALSE(current.critical);
  TEST_ASSERT_TRUE(current.warning);
  TEST_ASSERT_EQUAL_INT(HealthSeverity::Critical,
                        e.snapshot(0)->alerts[static_cast<size_t>(HealthReason::Overheating)]);
}
void healthy_episode_retirement_keeps_final_history_correlation() {
  CameraHealthEvaluator e;
  e.configure(0, CameraModel::X5, 1, true, 100);
  Token token;
  token.connection = 1;
  e.healthGroupIntent(0, RecordingState::Recording, 12, token, 100);
  QualifiedHealthSample s;
  s.field = HealthField::Recording;
  s.capability = CapabilityState::Supported;
  s.validity = HealthValidity::Valid;
  s.profile = HealthProfile::X5Ce80;
  s.unit = HealthUnit::RecordingState;
  s.value = static_cast<uint64_t>(RecordingState::Recording);
  s.connection = 1;
  s.sequence = 1;
  s.received_ms = 100;
  TEST_ASSERT_TRUE(e.observe(0, s, 100));
  const auto unchanged = e.coalesced();
  e.noteCoalesced(2);
  TEST_ASSERT_EQUAL_UINT32(unchanged, e.coalesced());
  TEST_ASSERT_EQUAL_UINT32(2, e.historyCoalesced());
  CameraHealthTransition t;
  while (e.popTransition(t)) {
  }
  e.healthRetired(0, token, 110);
  TEST_ASSERT_TRUE(e.popTransition(t));
  TEST_ASSERT_EQUAL_INT(HealthTransitionKind::Retired, t.kind);
  TEST_ASSERT_EQUAL_UINT32(12, t.group_generation);
  TEST_ASSERT_EQUAL_INT(HealthSeverity::None, t.severity);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(serial_transitions_are_bounded_and_explain_reason);
  RUN_TEST(saturated_serial_fields_fit_with_complete_newline);
  RUN_TEST(saturated_serial_transition_fits_default_uart_fifo);
  RUN_TEST(summary_reports_current_severity_and_disables_cleanly);
  RUN_TEST(stale_resource_fault_is_current_warning_with_historical_critical);
  RUN_TEST(healthy_episode_retirement_keeps_final_history_correlation);
  return UNITY_END();
}

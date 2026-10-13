#include "power_shutdown.h"
#include <unity.h>
using namespace ridesync;

PowerShutdownConfig config() {
  PowerShutdownConfig c;
  c.enabled = c.qualified = c.off_qualified = true;
  return c;
}
PowerObservation healthy() {
  PowerObservation o;
  o.acc = PowerSignal::Active;
  o.power_fail = PowerSignal::Inactive;
  o.supply = PowerSupply::Usable;
  o.reserve = PowerReserve::Ready;
  return o;
}
void running(PowerShutdown &p, uint32_t start = 0) {
  p.service(start, healthy());
  TEST_ASSERT_TRUE(p.service(start + 2000, healthy()).admit_session);
}
void no_actions(const PowerShutdownActions &a) {
  TEST_ASSERT_FALSE(a.admit_session || a.seal || a.request_drain || a.report || a.request_off);
}
void disabled_and_unqualified_are_inert() {
  PowerShutdown p;
  no_actions(p.service(0, healthy()));
  no_actions(p.service(2000, healthy()));
  TEST_ASSERT_EQUAL(PowerShutdownState::Disabled, p.status(2000).state);
  TEST_ASSERT_TRUE(p.status(2000).config_valid);
  TEST_ASSERT_FALSE(p.status(2000).admission_allowed);
  auto c = config();
  c.qualified = false;
  PowerShutdown u(c);
  auto o = healthy();
  o.power_fail = PowerSignal::Active;
  no_actions(u.service(0, o));
  TEST_ASSERT_FALSE(u.status(0).automatic_active);
}
void startup_requires_continuous_acc_and_ready_reserve() {
  PowerShutdown p(config());
  auto o = healthy();
  o.reserve = PowerReserve::NotReady;
  no_actions(p.service(0, o));
  no_actions(p.service(3000, o));
  o = healthy();
  no_actions(p.service(3001, o));
  no_actions(p.service(5000, o));
  TEST_ASSERT_TRUE(p.service(5001, o).admit_session);
  TEST_ASSERT_TRUE(p.status(5001).protected_ready);
  TEST_ASSERT_TRUE(p.status(5001).admission_allowed);
  no_actions(p.service(5002, o));
}
void boot_acc_off_or_unusable_resets_qualification() {
  PowerShutdown p(config());
  p.service(0, healthy());
  auto o = healthy();
  o.acc = PowerSignal::Inactive;
  no_actions(p.service(1999, o));
  no_actions(p.service(2000, healthy()));
  o = healthy();
  o.supply = PowerSupply::Unusable;
  no_actions(p.service(3999, o));
  no_actions(p.service(4000, healthy()));
  TEST_ASSERT_TRUE(p.service(6000, healthy()).admit_session);
}
void acc_equality_seals_once_and_restored_power_never_reopens() {
  PowerShutdown p(config());
  running(p);
  auto o = healthy();
  o.acc = PowerSignal::Inactive;
  no_actions(p.service(2100, o));
  TEST_ASSERT_EQUAL(PowerShutdownState::Suspect, p.status(2100).state);
  no_actions(p.service(2599, o));
  auto a = p.service(2600, o);
  TEST_ASSERT_TRUE(a.seal && a.request_drain);
  TEST_ASSERT_FALSE(a.report);
  TEST_ASSERT_EQUAL(PowerShutdownCause::AccOff, p.status(2600).cause);
  TEST_ASSERT_EQUAL(PowerShutdownState::Draining, p.status(2600).state);
  no_actions(p.service(2601, healthy(), true));
  TEST_ASSERT_TRUE(p.status(2601).sealed);
  TEST_ASSERT_FALSE(p.status(2601).admission_allowed);
}
void preseal_bounce_recovers_but_equality_wins_over_return() {
  PowerShutdown p(config());
  running(p);
  auto o = healthy();
  o.acc = PowerSignal::Inactive;
  p.service(2100, o);
  no_actions(p.service(2599, healthy()));
  TEST_ASSERT_EQUAL(PowerShutdownState::Running, p.status(2599).state);
  p.service(2700, o);
  TEST_ASSERT_TRUE(p.service(3200, healthy()).seal);
  TEST_ASSERT_EQUAL(PowerShutdownCause::AccOff, p.status(3200).cause);
}
void emergency_fail_dominates_manual_and_invalid_observations() {
  PowerShutdown p(config());
  running(p);
  PowerObservation o;
  o.power_fail = PowerSignal::Active;
  auto a = p.service(2001, o, true);
  TEST_ASSERT_TRUE(a.seal && a.request_drain);
  TEST_ASSERT_EQUAL(PowerShutdownCause::SupplyFail, p.status(2001).cause);
}
void boot_emergency_fail_seals_without_session_admission() {
  PowerShutdown p(config());
  auto o = healthy();
  o.power_fail = PowerSignal::Active;
  auto a = p.service(0, o);
  TEST_ASSERT_TRUE(a.seal && a.request_drain);
  TEST_ASSERT_FALSE(a.admit_session);
}
void invalid_or_unavailable_running_samples_seal_conservatively() {
  for (unsigned i = 0; i < 8; ++i) {
    PowerShutdown p(config());
    running(p);
    auto o = healthy();
    if (i == 0)
      o.acc = PowerSignal::Unknown;
    if (i == 1)
      o.power_fail = PowerSignal::Unknown;
    if (i == 2)
      o.supply = PowerSupply::Unknown;
    if (i == 3)
      o.reserve = PowerReserve::Unknown;
    if (i == 4)
      o.acc = static_cast<PowerSignal>(255);
    if (i == 5)
      o.reserve = static_cast<PowerReserve>(255);
    if (i == 6)
      o.supply = static_cast<PowerSupply>(255);
    if (i == 7)
      o.power_fail = static_cast<PowerSignal>(255);
    TEST_ASSERT_TRUE(p.service(2001, o).seal);
    TEST_ASSERT_EQUAL(PowerShutdownCause::ObservationInvalid, p.status(2001).cause);
    TEST_ASSERT_FALSE(p.status(2001).protected_ready);
  }
}
void reserve_loss_and_unusable_supply_are_immediate() {
  for (unsigned i = 0; i < 2; ++i) {
    PowerShutdown p(config());
    running(p);
    auto o = healthy();
    if (i == 0)
      o.reserve = PowerReserve::NotReady;
    else
      o.supply = PowerSupply::Unusable;
    TEST_ASSERT_TRUE(p.service(2001, o).seal);
    TEST_ASSERT_EQUAL(i == 0 ? PowerShutdownCause::ReserveLost : PowerShutdownCause::SupplyUnusable,
                      p.status(2001).cause);
  }
}
void manual_shutdown_works_when_automatic_feature_disabled() {
  PowerShutdown p;
  PowerObservation o;
  auto a = p.service(10, o, true);
  TEST_ASSERT_TRUE(a.seal && a.request_drain);
  TEST_ASSERT_FALSE(a.request_off);
  TEST_ASSERT_EQUAL(PowerShutdownCause::Manual, p.status(10).cause);
  no_actions(p.service(20, o, true));
  o.teardown_complete = true;
  a = p.service(30, o, true);
  TEST_ASSERT_TRUE(a.report);
  TEST_ASSERT_FALSE(a.request_off);
  TEST_ASSERT_EQUAL(PowerShutdownOutcome::Clean, p.status(30).outcome);
  no_actions(p.service(40, healthy(), true));
}
void clean_requires_completion_and_reports_off_once() {
  PowerShutdown p(config());
  running(p);
  p.service(2100, healthy(), true);
  no_actions(p.service(2101, healthy()));
  TEST_ASSERT_EQUAL(PowerShutdownOutcome::None, p.status(2101).outcome);
  auto o = healthy();
  o.teardown_complete = true;
  auto a = p.service(2102, o);
  TEST_ASSERT_TRUE(a.report && a.request_off);
  TEST_ASSERT_EQUAL(PowerShutdownState::Clean, p.status(2102).state);
  TEST_ASSERT_EQUAL(PowerShutdownOutcome::Clean, p.status(2102).outcome);
  TEST_ASSERT_TRUE(p.status(2102).teardown_complete);
  no_actions(p.service(2103, o));
  TEST_ASSERT_EQUAL(PowerShutdownState::OffPending, p.status(2103).state);
  TEST_ASSERT_EQUAL(PowerShutdownOutcome::Clean, p.status(2103).outcome);
}
void completion_with_io_error_or_loss_is_failed_not_clean() {
  for (unsigned i = 0; i < 2; ++i) {
    PowerShutdown p(config());
    running(p);
    p.service(2100, healthy(), true);
    auto o = healthy();
    o.teardown_complete = true;
    o.io_error = i == 0;
    o.terminal_loss = i == 1;
    auto a = p.service(2101, o);
    TEST_ASSERT_TRUE(a.report && a.request_off);
    TEST_ASSERT_EQUAL(PowerShutdownOutcome::Failed, p.status(2101).outcome);
    TEST_ASSERT_TRUE(p.status(2101).io_error || p.status(2101).terminal_loss);
  }
}
void failure_with_live_resources_is_not_completion() {
  PowerShutdown p(config());
  running(p);
  p.service(2100, healthy(), true);
  auto o = healthy();
  o.io_error = true;
  TEST_ASSERT_TRUE(p.service(2101, o).report);
  TEST_ASSERT_FALSE(p.status(2101).teardown_complete);
  TEST_ASSERT_EQUAL(PowerShutdownOutcome::Failed, p.status(2101).outcome);
  o = healthy();
  o.teardown_complete = true;
  no_actions(p.service(2102, o));
  TEST_ASSERT_TRUE(p.status(2102).io_error);
  TEST_ASSERT_EQUAL(PowerShutdownOutcome::Failed, p.status(2102).outcome);
}
void original_deadline_survives_bounce_duplicates_and_live_resources() {
  PowerShutdown p(config());
  running(p);
  p.service(2100, healthy(), true);
  auto o = healthy();
  o.power_fail = PowerSignal::Active;
  no_actions(p.service(6000, o, true));
  no_actions(p.service(7099, healthy(), true));
  TEST_ASSERT_EQUAL(4999, p.status(7099).elapsed_ms);
  auto a = p.service(7100, healthy(), true);
  TEST_ASSERT_TRUE(a.report && a.request_off);
  TEST_ASSERT_FALSE(a.seal || a.request_drain);
  TEST_ASSERT_EQUAL(PowerShutdownOutcome::TimedOut, p.status(7100).outcome);
  TEST_ASSERT_EQUAL(PowerShutdownCause::Manual, p.status(7100).cause);
  TEST_ASSERT_FALSE(p.status(7100).teardown_complete);
}
void completion_at_deadline_does_not_claim_timely_clean() {
  PowerShutdown p(config());
  p.service(0, healthy(), true);
  auto o = healthy();
  o.teardown_complete = true;
  TEST_ASSERT_TRUE(p.service(5000, o).report);
  TEST_ASSERT_EQUAL(PowerShutdownOutcome::TimedOut, p.status(5000).outcome);
  TEST_ASSERT_TRUE(p.status(5000).teardown_complete);
}
void voltage_hysteresis_preserves_low_episode_in_band() {
  auto c = config();
  c.voltage_enabled = true;
  c.voltage_enter_mv = 4500;
  c.voltage_clear_mv = 4800;
  PowerShutdown p(c);
  auto o = healthy();
  o.voltage_valid = true;
  o.supply_mv = 4800;
  p.service(0, o);
  TEST_ASSERT_TRUE(p.service(2000, o).admit_session);
  o.supply_mv = 4500;
  no_actions(p.service(2100, o));
  TEST_ASSERT_FALSE(p.status(2100).protected_ready);
  o.supply_mv = 4700;
  no_actions(p.service(2149, o));
  TEST_ASSERT_TRUE(p.service(2150, o).seal);
  TEST_ASSERT_EQUAL(PowerShutdownCause::Undervoltage, p.status(2150).cause);
}
void voltage_startup_requires_clear_threshold_after_unknown_sample() {
  auto c = config();
  c.voltage_enabled = true;
  c.voltage_enter_mv = 4500;
  c.voltage_clear_mv = 4800;
  PowerShutdown p(c);
  auto o = healthy();
  o.voltage_valid = true;
  o.supply_mv = 4700;
  no_actions(p.service(0, o));
  no_actions(p.service(2000, o));
  TEST_ASSERT_FALSE(p.status(2000).protected_ready);
  o.supply_mv = 4800;
  no_actions(p.service(2100, o));
  o.voltage_valid = false;
  no_actions(p.service(3000, o));
  o.voltage_valid = true;
  o.supply_mv = 4700;
  no_actions(p.service(3100, o));
  no_actions(p.service(5100, o));
  o.supply_mv = 4800;
  no_actions(p.service(5200, o));
  TEST_ASSERT_TRUE(p.service(7200, o).admit_session);
}
void voltage_clear_at_equality_cannot_cancel_expired_suspicion() {
  auto c = config();
  c.voltage_enabled = true;
  c.voltage_enter_mv = 4500;
  c.voltage_clear_mv = 4800;
  PowerShutdown p(c);
  auto o = healthy();
  o.voltage_valid = true;
  o.supply_mv = 4800;
  p.service(0, o);
  p.service(2000, o);
  o.supply_mv = 4500;
  p.service(2100, o);
  o.supply_mv = 4800;
  TEST_ASSERT_TRUE(p.service(2150, o).seal);
  TEST_ASSERT_EQUAL(PowerShutdownCause::Undervoltage, p.status(2150).cause);
}
void late_error_revokes_clean_without_duplicate_report_or_off() {
  PowerShutdown p(config());
  p.service(0, healthy(), true);
  auto o = healthy();
  o.teardown_complete = true;
  TEST_ASSERT_TRUE(p.service(1, o).report);
  TEST_ASSERT_EQUAL(PowerShutdownOutcome::Clean, p.status(1).outcome);
  o.io_error = true;
  no_actions(p.service(2, o));
  TEST_ASSERT_EQUAL(PowerShutdownOutcome::Failed, p.status(2).outcome);
  TEST_ASSERT_TRUE(p.status(2).io_error);
  TEST_ASSERT_TRUE(p.status(2).teardown_complete);
}
void unknown_boot_samples_never_establish_readiness() {
  for (unsigned i = 0; i < 4; ++i) {
    PowerShutdown p(config());
    auto o = healthy();
    if (i == 0)
      o.acc = PowerSignal::Unknown;
    if (i == 1)
      o.power_fail = PowerSignal::Unknown;
    if (i == 2)
      o.supply = PowerSupply::Unknown;
    if (i == 3)
      o.reserve = PowerReserve::Unknown;
    no_actions(p.service(0, o));
    no_actions(p.service(2000, o));
    TEST_ASSERT_FALSE(p.status(2000).protected_ready);
    TEST_ASSERT_EQUAL(PowerShutdownState::BootGuard, p.status(2000).state);
  }
}
void voltage_clear_before_deadline_recovers_and_invalid_sample_seals() {
  auto c = config();
  c.voltage_enabled = true;
  c.voltage_enter_mv = 4500;
  c.voltage_clear_mv = 4800;
  PowerShutdown p(c);
  auto o = healthy();
  o.voltage_valid = true;
  o.supply_mv = 4800;
  p.service(0, o);
  p.service(2000, o);
  o.supply_mv = 4499;
  p.service(2100, o);
  o.supply_mv = 4800;
  no_actions(p.service(2149, o));
  TEST_ASSERT_EQUAL(PowerShutdownState::Running, p.status(2149).state);
  TEST_ASSERT_TRUE(p.status(2149).protected_ready);
  o.voltage_valid = false;
  TEST_ASSERT_TRUE(p.service(2150, o).seal);
  TEST_ASSERT_EQUAL(PowerShutdownCause::ObservationInvalid, p.status(2150).cause);
}
void unhealthy_supply_cannot_clear_acc_suspicion() {
  auto c = config();
  c.voltage_enabled = true;
  c.voltage_enter_mv = 4500;
  c.voltage_clear_mv = 4800;
  c.undervoltage_ms = 1000;
  PowerShutdown p(c);
  auto o = healthy();
  o.voltage_valid = true;
  o.supply_mv = 4800;
  p.service(0, o);
  p.service(2000, o);
  o.acc = PowerSignal::Inactive;
  p.service(2100, o);
  o.acc = PowerSignal::Active;
  o.supply_mv = 4500;
  no_actions(p.service(2200, o));
  TEST_ASSERT_TRUE(p.service(2600, o).seal);
  TEST_ASSERT_EQUAL(PowerShutdownCause::AccOff, p.status(2600).cause);
}
void rollover_keeps_startup_acc_and_drain_boundaries() {
  PowerShutdown p(config());
  const uint32_t start = UINT32_MAX - 1000;
  p.service(start, healthy());
  no_actions(p.service(start + 1999, healthy()));
  TEST_ASSERT_TRUE(p.service(start + 2000, healthy()).admit_session);
  auto o = healthy();
  o.acc = PowerSignal::Inactive;
  p.service(start + 2100, o);
  no_actions(p.service(start + 2599, o));
  TEST_ASSERT_TRUE(p.service(start + 2600, o).seal);
  no_actions(p.service(start + 7599, healthy()));
  TEST_ASSERT_TRUE(p.service(start + 7600, healthy()).report);
  TEST_ASSERT_EQUAL(PowerShutdownOutcome::TimedOut, p.status(start + 7600).outcome);
}
void rollover_during_acc_voltage_and_manual_drain_preserves_equality() {
  PowerShutdown acc(config());
  running(acc, UINT32_MAX - 4000);
  auto o = healthy();
  o.acc = PowerSignal::Inactive;
  const uint32_t acc_at = UINT32_MAX - 200;
  acc.service(acc_at, o);
  no_actions(acc.service(acc_at + 499, o));
  TEST_ASSERT_TRUE(acc.service(acc_at + 500, o).seal);
  PowerShutdown manual(config());
  const uint32_t drain_at = UINT32_MAX - 100;
  manual.service(drain_at, healthy(), true);
  no_actions(manual.service(drain_at + 4999, healthy()));
  TEST_ASSERT_TRUE(manual.service(drain_at + 5000, healthy()).report);
  TEST_ASSERT_EQUAL(PowerShutdownOutcome::TimedOut, manual.status(drain_at + 5000).outcome);
  auto c = config();
  c.voltage_enabled = true;
  c.voltage_enter_mv = 4500;
  c.voltage_clear_mv = 4800;
  PowerShutdown voltage(c);
  o = healthy();
  o.voltage_valid = true;
  o.supply_mv = 4800;
  voltage.service(UINT32_MAX - 3020, o);
  voltage.service(UINT32_MAX - 1020, o);
  o.supply_mv = 4500;
  const uint32_t voltage_at = UINT32_MAX - 20;
  voltage.service(voltage_at, o);
  no_actions(voltage.service(voltage_at + 49, o));
  TEST_ASSERT_TRUE(voltage.service(voltage_at + 50, o).seal);
  TEST_ASSERT_EQUAL(PowerShutdownCause::Undervoltage, voltage.status(voltage_at + 50).cause);
}
void discontinuity_resets_boot_and_seals_running() {
  PowerShutdown boot(config());
  boot.service(0, healthy());
  no_actions(boot.service(0x80000000u, healthy()));
  TEST_ASSERT_FALSE(boot.status(0x80000000u).protected_ready);
  no_actions(boot.service(0x80000000u + 1999, healthy()));
  TEST_ASSERT_TRUE(boot.service(0x80000000u + 2000, healthy()).admit_session);
  PowerShutdown p(config());
  running(p);
  auto a = p.service(1999, healthy());
  TEST_ASSERT_TRUE(a.seal && a.request_drain);
  TEST_ASSERT_EQUAL(PowerShutdownCause::ClockDiscontinuity, p.status(1999).cause);
  TEST_ASSERT_FALSE(p.status(1999).protected_ready);
}
void discontinuity_during_drain_reports_uncertain_timeout() {
  PowerShutdown p(config());
  p.service(100, healthy(), true);
  auto a = p.service(99, healthy());
  TEST_ASSERT_TRUE(a.report && a.request_off);
  TEST_ASSERT_EQUAL(PowerShutdownOutcome::TimedOut, p.status(99).outcome);
  TEST_ASSERT_FALSE(p.status(99).teardown_complete);
}
void invalid_intervals_and_voltage_profile_are_inert() {
  for (unsigned i = 0; i < 10; ++i) {
    auto c = config();
    if (i == 0)
      c.startup_ms = 0;
    if (i == 1)
      c.acc_off_ms = 0;
    if (i == 2)
      c.undervoltage_ms = 0;
    if (i == 3)
      c.drain_ms = 0;
    if (i == 4)
      c.startup_ms = 0x80000000u;
    if (i == 5)
      c.acc_off_ms = UINT32_MAX;
    if (i == 6)
      c.undervoltage_ms = 0x80000000u;
    if (i == 7)
      c.drain_ms = 0x80000000u;
    if (i >= 8) {
      c.voltage_enabled = true;
      c.voltage_enter_mv = i == 8 ? 0 : 4800;
      c.voltage_clear_mv = 4800;
    }
    PowerShutdown p(c);
    no_actions(p.service(0, healthy(), true));
    no_actions(p.service(2000, healthy()));
    TEST_ASSERT_FALSE(p.status(2000).config_valid);
    TEST_ASSERT_EQUAL(PowerShutdownState::Disabled, p.status(2000).state);
  }
}
void off_requires_explicit_separate_qualification() {
  auto c = config();
  c.off_qualified = false;
  PowerShutdown p(c);
  p.service(0, healthy(), true);
  auto o = healthy();
  o.teardown_complete = true;
  auto a = p.service(1, o);
  TEST_ASSERT_TRUE(a.report);
  TEST_ASSERT_FALSE(a.request_off);
  no_actions(p.service(2, o));
  TEST_ASSERT_EQUAL(PowerShutdownState::Clean, p.status(2).state);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(disabled_and_unqualified_are_inert);
  RUN_TEST(startup_requires_continuous_acc_and_ready_reserve);
  RUN_TEST(boot_acc_off_or_unusable_resets_qualification);
  RUN_TEST(acc_equality_seals_once_and_restored_power_never_reopens);
  RUN_TEST(preseal_bounce_recovers_but_equality_wins_over_return);
  RUN_TEST(emergency_fail_dominates_manual_and_invalid_observations);
  RUN_TEST(boot_emergency_fail_seals_without_session_admission);
  RUN_TEST(invalid_or_unavailable_running_samples_seal_conservatively);
  RUN_TEST(reserve_loss_and_unusable_supply_are_immediate);
  RUN_TEST(manual_shutdown_works_when_automatic_feature_disabled);
  RUN_TEST(clean_requires_completion_and_reports_off_once);
  RUN_TEST(completion_with_io_error_or_loss_is_failed_not_clean);
  RUN_TEST(failure_with_live_resources_is_not_completion);
  RUN_TEST(original_deadline_survives_bounce_duplicates_and_live_resources);
  RUN_TEST(completion_at_deadline_does_not_claim_timely_clean);
  RUN_TEST(voltage_hysteresis_preserves_low_episode_in_band);
  RUN_TEST(voltage_startup_requires_clear_threshold_after_unknown_sample);
  RUN_TEST(voltage_clear_at_equality_cannot_cancel_expired_suspicion);
  RUN_TEST(late_error_revokes_clean_without_duplicate_report_or_off);
  RUN_TEST(unknown_boot_samples_never_establish_readiness);
  RUN_TEST(voltage_clear_before_deadline_recovers_and_invalid_sample_seals);
  RUN_TEST(unhealthy_supply_cannot_clear_acc_suspicion);
  RUN_TEST(rollover_keeps_startup_acc_and_drain_boundaries);
  RUN_TEST(rollover_during_acc_voltage_and_manual_drain_preserves_equality);
  RUN_TEST(discontinuity_resets_boot_and_seals_running);
  RUN_TEST(discontinuity_during_drain_reports_uncertain_timeout);
  RUN_TEST(invalid_intervals_and_voltage_profile_are_inert);
  RUN_TEST(off_requires_explicit_separate_qualification);
  return UNITY_END();
}

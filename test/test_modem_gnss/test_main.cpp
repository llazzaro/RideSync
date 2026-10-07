#include "../fixtures/gnss/responses.h"
#include "camera_manager.h"
#include "gps_manager.h"
#include "modem_gnss.h"
#include <string>
#include <unity.h>
using namespace ridesync;
struct Uart : ModemUart {
  std::string rx, tx;
  size_t read_count = 0, capacity = 64;
  size_t available() override { return rx.size(); }
  int read() override {
    if (rx.empty())
      return -1;
    char c = rx[0];
    rx.erase(0, 1);
    ++read_count;
    return c;
  }
  size_t writable() override { return capacity; }
  size_t write(const char *p, size_t n) override {
    tx.append(p, n);
    return n;
  }
};
RecordTimestamp timeAt(uint64_t ms, uint64_t session = 1) {
  RecordTimestamp t;
  t.session_id = session;
  t.monotonic_ms = ms;
  t.monotonic_quality = MonotonicQuality::Valid;
  return t;
}
ModemConfig enabled() {
  ModemConfig c;
  c.documentary_profile_opt_in = true;
  c.terminal_retires_transaction = true;
  c.rx_bytes_per_tick = 256;
  return c;
}
void feed(ModemGnss &m, Uart &u, const std::string &s, uint64_t t) {
  u.rx += s;
  m.tick(timeAt(t));
}
void boot(ModemGnss &m, Uart &u) {
  m.tick(timeAt(0));
  feed(m, u, "AT\r\nOK\r\n", 1);
  feed(m, u, "OK\r\n", 2);
  feed(m, u, "+CGNSSPWR: READY!\r\n", 3);
}
void opt_in_boot_and_ready_are_separate() {
  Uart off;
  ModemGnss disabled(off, ModemConfig{});
  disabled.tick(timeAt(0));
  TEST_ASSERT_TRUE(off.tx.empty());
  Uart u;
  ModemGnss m(u, enabled());
  m.tick(timeAt(0));
  TEST_ASSERT_EQUAL_STRING("AT\r", u.tx.c_str());
  feed(m, u, "AT\r\nO", 1);
  TEST_ASSERT_FALSE(m.snapshot(timeAt(1)).gnss_power_enabled);
  feed(m, u, "K\r\n", 2);
  TEST_ASSERT_EQUAL_STRING("AT\rAT+CGNSSPWR=1\r", u.tx.c_str());
  feed(m, u, "+CPIN: NOT INSERTED\r\nOK\r\n", 3);
  TEST_ASSERT_TRUE(m.snapshot(timeAt(3)).gnss_power_enabled);
  TEST_ASSERT_FALSE(m.snapshot(timeAt(3)).receiver_ready);
  feed(m, u, "+CGNSSPWR: READY!\r\n", 4);
  TEST_ASSERT_TRUE(m.snapshot(timeAt(4)).receiver_ready);
  TEST_ASSERT_EQUAL_STRING("AT\rAT+CGNSSPWR=1\rAT+CGPSINFO\r", u.tx.c_str());
}
void timed_out_reply_never_becomes_new_query() {
  Uart u;
  auto c = enabled();
  c.command_ms = 20;
  ModemGnss m(u, c);
  boot(m, u);
  auto sent = u.tx;
  m.tick(timeAt(24));
  TEST_ASSERT_EQUAL((int)ModemState::Desynchronized, (int)m.snapshot(timeAt(24)).state);
  m.tick(timeAt(10000));
  TEST_ASSERT_EQUAL_STRING(sent.c_str(), u.tx.c_str());
  feed(m, u, std::string(kManualCgpsinfo) + "\r\n", 10001);
  TEST_ASSERT_FALSE(m.snapshot(timeAt(10001)).fix.valid);
  feed(m, u, "OK\r\n", 10002);
  TEST_ASSERT_FALSE(m.snapshot(timeAt(10002)).fix.valid);
  TEST_ASSERT_TRUE(u.tx.size() > sent.size());
}
void successful_terminal_required_and_freshness() {
  Uart u;
  ModemGnss m(u, enabled());
  boot(m, u);
  feed(m, u, std::string(kManualCgpsinfo) + "\r\n", 4);
  TEST_ASSERT_FALSE(m.snapshot(timeAt(4)).fix.valid);
  feed(m, u, "OK\r\n", 5);
  auto s = m.snapshot(timeAt(10));
  TEST_ASSERT_TRUE(s.fix.valid);
  TEST_ASSERT_EQUAL_UINT64(4, s.fix.receipt_monotonic_ms);
  TEST_ASSERT_EQUAL_UINT64(6, s.age_ms);
  TEST_ASSERT_EQUAL((int)FixValidity::Stale, (int)m.snapshot(timeAt(4000)).validity);
  TEST_ASSERT_FALSE(m.snapshot(timeAt(4000)).fix.valid);
  m.tick(timeAt(1005));
  feed(m, u, std::string(kObservedCgpsinfoNoFix) + "\r\nOK\r\n", 1006);
  TEST_ASSERT_EQUAL((int)FixValidity::NoFix, (int)m.snapshot(timeAt(1006)).validity);
}
void budgets_overflow_silence_and_restart() {
  Uart u;
  auto c = enabled();
  c.rx_bytes_per_tick = 8;
  c.tx_bytes_per_tick = 2;
  c.command_ms = 10;
  c.max_restarts = 1;
  ModemGnss m(u, c);
  m.tick(timeAt(0));
  TEST_ASSERT_EQUAL(2, u.tx.size());
  u.rx = std::string(1000, 'x');
  m.tick(timeAt(1));
  TEST_ASSERT_EQUAL(8, u.read_count);
  m.tick(timeAt(11));
  auto sent = u.tx;
  m.tick(timeAt(50000));
  TEST_ASSERT_EQUAL_STRING(sent.c_str(), u.tx.c_str());
  u.rx.clear();
  TEST_ASSERT_TRUE(m.restartAfterVerifiedBarrier(timeAt(50001)));
  TEST_ASSERT_FALSE(m.restartAfterVerifiedBarrier(timeAt(50002)));
  Uart v;
  ModemGnss n(v, enabled());
  boot(n, v);
  feed(n, v, std::string(257, 'x') + "\n", 4);
  n.tick(timeAt(5));
  TEST_ASSERT_EQUAL((int)UartHealth::Overflow, (int)n.snapshot(timeAt(5)).health);
}
void errors_and_missing_data_invalidate_fix() {
  Uart u;
  auto c = enabled();
  c.max_attempts = 2;
  ModemGnss m(u, c);
  boot(m, u);
  feed(m, u, std::string(kManualCgpsinfo) + "\r\nERROR\r\n", 4);
  TEST_ASSERT_FALSE(m.snapshot(timeAt(4)).fix.valid);
  feed(m, u, "OK\r\n", 5);
  TEST_ASSERT_EQUAL((int)FixValidity::Invalid, (int)m.snapshot(timeAt(5)).validity);
  TEST_ASSERT_EQUAL((int)ModemState::Failed, (int)m.snapshot(timeAt(5)).state);
}
struct RawClock : Clock {
  uint32_t raw = 0;
  uint32_t now() const override { return raw; }
};
struct Power : GnssPowerControl {
  int enabled = 0;
  bool active = false;
  void enableSupply() override { ++enabled; }
  void key(bool a) override { active = a; }
};
void qualified_power_sequence_uses_shared_clock_across_wrap() {
  RawClock raw;
  raw.raw = 0xfffffff0;
  SessionClock clock(raw, 9, 1000);
  Uart u;
  ModemGnss m(u, enabled());
  Power p;
  QualifiedPowerTiming t;
  t.qualified = true;
  t.key_active_ms = 20;
  t.settle_ms = 10;
  GpsManager gps(clock, m, &p, t);
  gps.tick();
  TEST_ASSERT_EQUAL(1, p.enabled);
  TEST_ASSERT_TRUE(p.active);
  TEST_ASSERT_TRUE(u.tx.empty());
  raw.raw = 4;
  gps.tick();
  TEST_ASSERT_FALSE(p.active);
  TEST_ASSERT_TRUE(u.tx.empty());
  raw.raw = 14;
  gps.tick();
  TEST_ASSERT_EQUAL_STRING("AT\r", u.tx.c_str());
  TEST_ASSERT_EQUAL_UINT64(9, gps.snapshot().session_id);
  Uart v;
  ModemGnss n(v, enabled());
  Power q;
  GpsManager blocked(clock, n, &q);
  blocked.tick();
  TEST_ASSERT_EQUAL(0, q.enabled);
  TEST_ASSERT_TRUE(v.tx.empty());
}
void timeout_checks_before_late_terminal_and_partial_transmit_cannot_retire() {
  Uart u;
  auto c = enabled();
  c.command_ms = 20;
  ModemGnss m(u, c);
  boot(m, u);
  feed(m, u, std::string(kManualCgpsinfo) + "\r\nOK\r\n", 23);
  TEST_ASSERT_FALSE(m.snapshot(timeAt(23)).fix.valid);
  Uart v;
  v.capacity = 0;
  ModemGnss n(v, c);
  n.tick(timeAt(0));
  n.tick(timeAt(20));
  feed(n, v, "OK\r\n", 21);
  TEST_ASSERT_TRUE(v.tx.empty());
  TEST_ASSERT_EQUAL((int)ModemState::Desynchronized, (int)n.snapshot(timeAt(21)).state);
  v.capacity = 32;
  n.tick(timeAt(22));
  TEST_ASSERT_TRUE(v.tx.empty());
}
void ready_timeout_and_command_retries_are_bounded() {
  Uart u;
  auto c = enabled();
  c.max_attempts = 2;
  ModemGnss m(u, c);
  m.tick(timeAt(0));
  feed(m, u, "ERROR\r\n", 1);
  feed(m, u, "ERROR\r\n", 2);
  TEST_ASSERT_EQUAL_STRING("AT\rAT\r", u.tx.c_str());
  TEST_ASSERT_EQUAL((int)ModemState::Failed, (int)m.snapshot(timeAt(2)).state);
  Uart v;
  ModemGnss n(v, c);
  n.tick(timeAt(0));
  feed(n, v, "OK\r\n", 1);
  feed(n, v, "OK\r\n", 2);
  n.tick(timeAt(15002));
  TEST_ASSERT_TRUE(n.snapshot(timeAt(15002)).gnss_power_enabled);
  TEST_ASSERT_FALSE(n.snapshot(timeAt(15002)).receiver_ready);
  TEST_ASSERT_EQUAL((int)ModemState::Failed, (int)n.snapshot(timeAt(15002)).state);
}
void wrong_query_and_duplicate_data_are_invalid_and_session_changes_fail_closed() {
  Uart u;
  ModemGnss m(u, enabled());
  boot(m, u);
  feed(m, u,
       "+CGNSSINFO:2,09,05,00,3113.330650,N,12121.262554,E,131117,091918.00,32.9,0.0,255.0,1.1,0.8,"
       "0.7\r\nOK\r\n",
       4);
  TEST_ASSERT_EQUAL((int)FixValidity::Invalid, (int)m.snapshot(timeAt(4)).validity);
  feed(m, u, std::string(kManualCgpsinfo) + "\r\n" + kManualCgpsinfo + "\r\nOK\r\n", 5);
  m.tick(timeAt(6));
  TEST_ASSERT_FALSE(m.snapshot(timeAt(6)).fix.valid);
  m.tick(timeAt(7, 2));
  TEST_ASSERT_EQUAL((int)UartHealth::InvalidClock, (int)m.snapshot(timeAt(7, 2)).health);
}
void recovery_requires_terminal_profile_and_overflow_drain() {
  Uart off;
  auto c = enabled();
  c.terminal_retires_transaction = false;
  ModemGnss disabled(off, c);
  disabled.tick(timeAt(0));
  TEST_ASSERT_TRUE(off.tx.empty());
  Uart u;
  ModemGnss m(u, enabled());
  boot(m, u);
  auto sent = u.tx;
  feed(m, u, std::string(257, 'x') + "\r\n", 4);
  m.tick(timeAt(5));
  m.tick(timeAt(100));
  TEST_ASSERT_EQUAL_STRING(sent.c_str(), u.tx.c_str());
  feed(m, u, "OK\r\n", 101);
  TEST_ASSERT_TRUE(u.tx.size() > sent.size());
  TEST_ASSERT_FALSE(m.snapshot(timeAt(101)).fix.valid);
}
void unsolicited_flood_cannot_extend_startup_forever() {
  Uart u;
  auto c = enabled();
  c.rx_bytes_per_tick = 8;
  c.command_ms = 20;
  ModemGnss m(u, c);
  u.rx = std::string(1000, 'x');
  m.tick(timeAt(0));
  m.tick(timeAt(20));
  TEST_ASSERT_EQUAL((int)ModemState::Failed, (int)m.snapshot(timeAt(20)).state);
  TEST_ASSERT_TRUE(u.tx.empty());
}
void reboot_urc_needs_verified_barrier_even_if_old_terminal_arrives() {
  Uart u;
  ModemGnss m(u, enabled());
  boot(m, u);
  auto sent = u.tx;
  feed(m, u, "RDY\r\nOK\r\n", 4);
  TEST_ASSERT_EQUAL_STRING(sent.c_str(), u.tx.c_str());
  TEST_ASSERT_EQUAL((int)ModemState::Desynchronized, (int)m.snapshot(timeAt(4)).state);
  TEST_ASSERT_FALSE(m.snapshot(timeAt(4)).gnss_power_enabled);
}
void late_ready_cannot_rescue_expired_receiver_startup() {
  Uart u;
  ModemGnss m(u, enabled());
  m.tick(timeAt(0));
  feed(m, u, "OK\r\n", 1);
  feed(m, u, "OK\r\n", 2);
  feed(m, u, "+CGNSSPWR: READY!\r\n", 15002);
  TEST_ASSERT_EQUAL((int)ModemState::Failed, (int)m.snapshot(timeAt(15002)).state);
  TEST_ASSERT_FALSE(m.snapshot(timeAt(15002)).receiver_ready);
}
QualifiedPowerTiming qualifiedTiming() {
  QualifiedPowerTiming t;
  t.qualified = true;
  t.key_active_ms = 20;
  t.settle_ms = 10;
  return t;
}
void initially_invalid_manager_recovers_only_through_verified_barrier() {
  RawClock raw;
  SessionClock clock(raw, 0, 1000);
  Uart u;
  ModemGnss m(u, enabled());
  GpsManager gps(clock, m, nullptr, qualifiedTiming());
  gps.tick();
  TEST_ASSERT_EQUAL((int)PowerStage::InvalidClock, (int)gps.powerStage());
  TEST_ASSERT_FALSE(gps.restartAfterVerifiedBarrier());
  TEST_ASSERT_TRUE(u.tx.empty());
  TEST_ASSERT_TRUE(clock.reset(8));
  gps.tick();
  TEST_ASSERT_TRUE(u.tx.empty());
  TEST_ASSERT_TRUE(gps.restartAfterVerifiedBarrier());
  u.rx = "OK\r\n";
  ++raw.raw;
  gps.tick();
  TEST_ASSERT_TRUE(u.rx.empty());
  TEST_ASSERT_EQUAL_STRING("AT\rAT+CGNSSPWR=1\r", u.tx.c_str());
  TEST_ASSERT_EQUAL((int)PowerStage::Complete, (int)gps.powerStage());
  TEST_ASSERT_EQUAL_UINT64(8, gps.snapshot().session_id);
  TEST_ASSERT_FALSE(gps.snapshot().fix.valid);
}
void active_power_session_reset_recovery_never_replays_key_pulse() {
  RawClock raw;
  SessionClock clock(raw, 1, 1000);
  Uart u;
  ModemGnss m(u, enabled());
  Power power;
  GpsManager gps(clock, m, &power, qualifiedTiming());
  gps.tick();
  TEST_ASSERT_TRUE(power.active);
  TEST_ASSERT_EQUAL(1, power.enabled);
  TEST_ASSERT_TRUE(clock.reset(2));
  gps.tick();
  TEST_ASSERT_FALSE(power.active);
  TEST_ASSERT_EQUAL((int)PowerStage::InvalidClock, (int)gps.powerStage());
  TEST_ASSERT_TRUE(gps.restartAfterVerifiedBarrier());
  TEST_ASSERT_EQUAL(1, power.enabled);
  TEST_ASSERT_FALSE(power.active);
  u.rx = "OK\r\n";
  raw.raw = 100;
  gps.tick();
  TEST_ASSERT_EQUAL_STRING("AT\rAT+CGNSSPWR=1\r", u.tx.c_str());
  TEST_ASSERT_EQUAL(1, power.enabled);
  TEST_ASSERT_FALSE(power.active);
}
void manager_recovery_preserves_barrier_rejection_and_lifetime_cap() {
  RawClock raw;
  SessionClock clock(raw, 1, 1000);
  Uart u;
  auto c = enabled();
  c.max_restarts = 1;
  ModemGnss m(u, c);
  GpsManager gps(clock, m, nullptr, qualifiedTiming());
  gps.tick();
  TEST_ASSERT_TRUE(clock.reset(2));
  gps.tick();
  auto sent = u.tx;
  u.rx = "OK\r\n";
  TEST_ASSERT_FALSE(gps.restartAfterVerifiedBarrier());
  TEST_ASSERT_EQUAL_STRING(sent.c_str(), u.tx.c_str());
  TEST_ASSERT_EQUAL((int)PowerStage::InvalidClock, (int)gps.powerStage());
  u.rx.clear(); // caller's physical/receive barrier; silence alone is insufficient
  TEST_ASSERT_TRUE(gps.restartAfterVerifiedBarrier());
  sent = u.tx;
  TEST_ASSERT_TRUE(clock.reset(3));
  gps.tick();
  TEST_ASSERT_FALSE(gps.restartAfterVerifiedBarrier());
  gps.tick();
  TEST_ASSERT_EQUAL_STRING(sent.c_str(), u.tx.c_str());
  TEST_ASSERT_EQUAL((int)PowerStage::InvalidClock, (int)gps.powerStage());
}
void manager_barrier_clears_old_fix_and_provisional_query() {
  RawClock raw;
  SessionClock clock(raw, 1, 1000);
  Uart u;
  ModemGnss m(u, enabled());
  GpsManager gps(clock, m, nullptr, qualifiedTiming());
  gps.tick();
  for (auto response : {"OK\r\n", "OK\r\n", "+CGNSSPWR: READY!\r\n"}) {
    u.rx = response;
    ++raw.raw;
    gps.tick();
  }
  u.rx = std::string(kManualCgpsinfo) + "\r\nOK\r\n";
  ++raw.raw;
  gps.tick();
  TEST_ASSERT_TRUE(gps.snapshot().fix.valid);
  raw.raw += 1000;
  gps.tick();
  u.rx = std::string(kManualCgpsinfo) + "\r\n";
  ++raw.raw;
  gps.tick();
  TEST_ASSERT_TRUE(clock.reset(2));
  gps.tick();
  TEST_ASSERT_FALSE(gps.snapshot().fix.valid);
  TEST_ASSERT_TRUE(gps.restartAfterVerifiedBarrier());
  auto s = gps.snapshot();
  TEST_ASSERT_FALSE(s.fix.valid);
  TEST_ASSERT_FALSE(s.age_available);
  TEST_ASSERT_EQUAL((int)FixValidity::Missing, (int)s.validity);
  TEST_ASSERT_EQUAL_UINT64(2, s.session_id);
  u.rx = "OK\r\n";
  ++raw.raw;
  gps.tick();
  TEST_ASSERT_FALSE(gps.snapshot().fix.valid);
  TEST_ASSERT_EQUAL((int)ModemState::Power, (int)gps.snapshot().state);
}
void manager_direct_barrier_releases_active_key_and_requires_qualification() {
  RawClock raw;
  SessionClock clock(raw, 1, 1000);
  Uart u;
  ModemGnss m(u, enabled());
  Power power;
  GpsManager gps(clock, m, &power, qualifiedTiming());
  gps.tick();
  TEST_ASSERT_TRUE(power.active);
  TEST_ASSERT_TRUE(clock.reset(2));
  TEST_ASSERT_TRUE(gps.restartAfterVerifiedBarrier());
  TEST_ASSERT_FALSE(power.active);
  TEST_ASSERT_EQUAL(1, power.enabled);
  TEST_ASSERT_EQUAL_STRING("AT\r", u.tx.c_str());
  TEST_ASSERT_EQUAL_UINT64(2, gps.snapshot().session_id);
  Uart v;
  ModemGnss n(v, enabled());
  GpsManager unqualified(clock, n);
  TEST_ASSERT_FALSE(unqualified.restartAfterVerifiedBarrier());
  TEST_ASSERT_TRUE(v.tx.empty());
  TEST_ASSERT_EQUAL((int)PowerStage::Disabled, (int)unqualified.powerStage());
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(manager_direct_barrier_releases_active_key_and_requires_qualification);
  RUN_TEST(initially_invalid_manager_recovers_only_through_verified_barrier);
  RUN_TEST(active_power_session_reset_recovery_never_replays_key_pulse);
  RUN_TEST(manager_recovery_preserves_barrier_rejection_and_lifetime_cap);
  RUN_TEST(manager_barrier_clears_old_fix_and_provisional_query);
  RUN_TEST(late_ready_cannot_rescue_expired_receiver_startup);
  RUN_TEST(reboot_urc_needs_verified_barrier_even_if_old_terminal_arrives);
  RUN_TEST(unsolicited_flood_cannot_extend_startup_forever);
  RUN_TEST(timeout_checks_before_late_terminal_and_partial_transmit_cannot_retire);
  RUN_TEST(ready_timeout_and_command_retries_are_bounded);
  RUN_TEST(wrong_query_and_duplicate_data_are_invalid_and_session_changes_fail_closed);
  RUN_TEST(recovery_requires_terminal_profile_and_overflow_drain);
  RUN_TEST(qualified_power_sequence_uses_shared_clock_across_wrap);
  RUN_TEST(opt_in_boot_and_ready_are_separate);
  RUN_TEST(timed_out_reply_never_becomes_new_query);
  RUN_TEST(successful_terminal_required_and_freshness);
  RUN_TEST(budgets_overflow_silence_and_restart);
  RUN_TEST(errors_and_missing_data_invalidate_fix);
  return UNITY_END();
}

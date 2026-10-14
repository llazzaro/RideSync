#pragma once
#include "../../../tools/bench/hero12_central/policy.h"
#ifndef HERO12_CHECK
#error "Define the runner assertion before including policy cases"
#endif
namespace hero12_probe_test {
using hero12_probe::Policy;
inline void missing_qualification_refuses_shutter() {
  Policy p;
  HERO12_CHECK(!p.admit(true, 100));
  p.connected(1, 100);
  p.observe(false, false, true, 100);
  HERO12_CHECK(!p.admit(true, 101));
}
inline void explicit_start_stop_use_fresh_encoding_not_a_toggle() {
  Policy p;
  p.connected(1, 100);
  p.qualified = true;
  p.observe(false, false, true, 100);
  HERO12_CHECK(p.admit(true, 101));
  HERO12_CHECK(!p.admit(false, 101));
  p.observe(true, false, true, 200);
  HERO12_CHECK(p.admit(false, 201));
  HERO12_CHECK(!p.admit(true, 201));
  p.observe(false, true, true, 300);
  HERO12_CHECK(!p.admit(true, 301));
  p.observe(false, false, false, 400);
  HERO12_CHECK(!p.admit(true, 401));
  p.observe(true, true, false, 500);
  HERO12_CHECK(p.admit(false, 501));
}
inline void stale_status_and_expired_session_refuse_new_shutter() {
  Policy p;
  p.connected(1, 100);
  p.qualified = true;
  p.observe(false, false, true, 500);
  HERO12_CHECK(!p.admit(true, 2501));
  // Fresh status cannot extend the absolute ten-minute session.
  p.observe(false, false, true, 600100);
  HERO12_CHECK(!p.admit(true, 600101));
}
inline void disconnect_and_reconnect_clear_prior_admission() {
  Policy p;
  p.connected(1, 100);
  p.qualified = true;
  p.observe(false, false, true, 100);
  HERO12_CHECK(p.admit(true, 101));
  p.disconnect();
  HERO12_CHECK(!p.admit(true, 102));
  p.connected(2, 200);
  HERO12_CHECK(!p.admit(true, 201));
  p.qualified = true;
  HERO12_CHECK(!p.admit(true, 201));
  p.observe(false, false, true, 200);
  HERO12_CHECK(p.admit(true, 201));
}
inline void monotonic_wrap_preserves_freshness_and_expiry() {
  Policy p;
  p.connected(1, UINT32_MAX - 100);
  p.qualified = true;
  p.observe(false, false, true, UINT32_MAX - 10);
  HERO12_CHECK(p.admit(true, 20));
  HERO12_CHECK(!p.admit(true, 2500));
  p.observe(false, false, true, 600000);
  HERO12_CHECK(!p.admit(true, 600001));
}
inline void descriptor_search_stops_before_next_characteristic() {
  const uint16_t definitions[] = {10, 20, 30};
  HERO12_CHECK(hero12_probe::descriptorEnd(12, 40, definitions, 3) == 19);
  HERO12_CHECK(hero12_probe::descriptorEnd(32, 40, definitions, 3) == 40);
}
inline void pasted_burst_does_not_execute_its_last_record_command() {
  hero12_probe::LineParser p;
  const char burst[] = "STOP\nREC\n";
  for (unsigned i = 0; i < sizeof(burst) - 1; ++i) {
    char result = p.feed(burst[i], i + 1 < sizeof(burst) - 1);
    HERO12_CHECK(result != 'R' && result != 'S');
  }
  p.drained();
  const char rec[] = "REC\r\n";
  char result = 0;
  for (unsigned i = 0; i < sizeof(rec) - 1; ++i)
    result = p.feed(rec[i], false);
  HERO12_CHECK(result == 'R');
}
inline void oversized_and_blank_burst_prefixes_do_not_hide_record() {
  for (const char *burst :
       {"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\nREC\n", "\nREC\n", "INVALID\nREC\n"}) {
    hero12_probe::LineParser p;
    size_t length = strlen(burst);
    for (size_t i = 0; i < length; ++i)
      HERO12_CHECK(p.feed(burst[i], i + 1 < length) != 'R');
  }
}
inline void emergency_disconnect_does_not_authorize_following_burst_record() {
  hero12_probe::LineParser p;
  const char burst[] = "DISCONNECT\nREC\n";
  unsigned disconnects = 0;
  for (unsigned i = 0; i < sizeof(burst) - 1; ++i) {
    char result = p.feed(burst[i], i + 1 < sizeof(burst) - 1);
    HERO12_CHECK(result != 'R');
    if (result == 'D')
      ++disconnects;
  }
  HERO12_CHECK(disconnects == 1);
}
} // namespace hero12_probe_test

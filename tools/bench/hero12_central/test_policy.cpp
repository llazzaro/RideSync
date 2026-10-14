// Standalone pre-upload runner shares the exact CI cases.
#include <cassert>
#include <initializer_list>
#define HERO12_CHECK(value) assert(value)
#include "../../../test/fixtures/hero12_probe/policy_cases.h"
using namespace hero12_probe_test;
int main() {
  missing_qualification_refuses_shutter();
  explicit_start_stop_use_fresh_encoding_not_a_toggle();
  stale_status_and_expired_session_refuse_new_shutter();
  disconnect_and_reconnect_clear_prior_admission();
  monotonic_wrap_preserves_freshness_and_expiry();
  descriptor_search_stops_before_next_characteristic();
  pasted_burst_does_not_execute_its_last_record_command();
  oversized_and_blank_burst_prefixes_do_not_hide_record();
  emergency_disconnect_does_not_authorize_following_burst_record();
}

#pragma once
#include <cstdint>
namespace ridesync {
// Default-partition evidence only. Named partitions need their own owner status.
struct NvsBootStatus {
  bool init_observed = false, format_refused = false;
  bool init_in_progress = false;
  int32_t first_init_failure = 0, last_init_result = 0, refusal_error = 0;
  bool persistenceAllowed() const {
    return init_observed && !init_in_progress && first_init_failure == 0 && last_init_result == 0 &&
           !format_refused;
  }
};
// Fixed atomic observations, not a transaction with subsequent SDK operations.
// Config/BLE owners must check per admission and serialize init/erase/handle use.
NvsBootStatus nvsBootStatus();
} // namespace ridesync

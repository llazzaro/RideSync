#include "nvs_boot_guard.h"
#ifdef ARDUINO
#include <atomic>
#include <esp_partition.h>
#include <nvs_flash.h>

extern "C" esp_err_t __real_esp_partition_erase_range(const esp_partition_t *, size_t, size_t);
extern "C" esp_err_t __real_nvs_flash_init(void);
namespace {
// Constant-initialized before Arduino startup; no constructors, allocation or logging.
// Fixed lock-free operations only. No retry loops, SDK locks or NVS handle access.
static_assert(ATOMIC_INT_LOCK_FREE == 2, "NVS guard requires lock-free integer atomics");
std::atomic<unsigned> observed = ATOMIC_VAR_INIT(0), refused = ATOMIC_VAR_INIT(0),
                      active = ATOMIC_VAR_INIT(0);
std::atomic<int> first_failure = ATOMIC_VAR_INIT(ESP_OK),
                 last_result = ATOMIC_VAR_INIT(ESP_ERR_NVS_NOT_INITIALIZED);
} // namespace
extern "C" esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t *partition,
                                                      size_t offset, size_t size) {
  if (partition && partition->type == ESP_PARTITION_TYPE_DATA &&
      partition->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS && offset == 0 && partition->size != 0 &&
      size == partition->size) {
    refused.store(1, std::memory_order_release);
    return ESP_ERR_NOT_SUPPORTED;
  }
  return __real_esp_partition_erase_range(partition, offset, size);
}
extern "C" esp_err_t __wrap_nvs_flash_init(void) {
  active.store(1, std::memory_order_release);
  const esp_err_t result = __real_nvs_flash_init();
  if (result != ESP_OK) {
    int expected = ESP_OK;
    first_failure.compare_exchange_strong(expected, result, std::memory_order_release,
                                          std::memory_order_relaxed);
  }
  last_result.store(result, std::memory_order_release);
  observed.store(1, std::memory_order_release);
  active.store(0, std::memory_order_release);
  return result;
}
namespace ridesync {
NvsBootStatus nvsBootStatus() {
  NvsBootStatus status;
  status.init_in_progress = active.load(std::memory_order_acquire) != 0;
  status.init_observed = observed.load(std::memory_order_acquire) != 0;
  status.last_init_result = last_result.load(std::memory_order_acquire);
  status.first_init_failure = first_failure.load(std::memory_order_acquire);
  status.format_refused = refused.load(std::memory_order_acquire) != 0;
  status.refusal_error = status.format_refused ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
  return status;
}
} // namespace ridesync
#endif

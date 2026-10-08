#include "nvs_guard.h"
#include <atomic>
#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace {
std::atomic<uint32_t> refused_init{0}, refused_open{0}, refused_erase{0};
}
// This bench never initializes NVS. The pinned core continues on this explicit
// refusal; its two error-format triggers are different errors. No fake success.
extern "C" esp_err_t __wrap_nvs_flash_init() {
  ++refused_init;
  return ESP_ERR_NVS_NOT_INITIALIZED;
}
// No GNSS route needs an NVS namespace. Refuse all opens explicitly; never
// fabricate successful reads/writes or trigger error-driven formatting.
extern "C" esp_err_t __wrap_nvs_open(const char *, nvs_open_mode_t, nvs_handle_t *handle) {
  ++refused_open;
  if (handle)
    *handle = 0;
  return ESP_ERR_NVS_NOT_INITIALIZED;
}
extern "C" esp_err_t __wrap_nvs_open_from_partition(const char *, const char *, nvs_open_mode_t,
                                                    nvs_handle_t *handle) {
  ++refused_open;
  if (handle)
    *handle = 0;
  return ESP_ERR_NVS_NOT_INITIALIZED;
}
extern "C" esp_err_t __wrap_nvs_flash_erase() {
  ++refused_erase;
  return ESP_ERR_NOT_SUPPORTED;
}
extern "C" esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t *, size_t, size_t) {
  ++refused_erase;
  return ESP_ERR_NOT_SUPPORTED;
}
namespace gnss_bench {
NvsRefusals nvsRefusals() {
  return {refused_init.load(), refused_open.load(), refused_erase.load()};
}
} // namespace gnss_bench

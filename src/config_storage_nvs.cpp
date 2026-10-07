#include "config_storage.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "nvs_boot_guard.h"
#include <nvs.h>
namespace ridesync {
namespace {
const char *key(unsigned slot) { return slot == 0 ? "cfg_a" : "cfg_b"; }
StoreResult readError(esp_err_t e) {
  return {e == ESP_ERR_NVS_NOT_FOUND ? StoreStatus::Missing : StoreStatus::Error, e};
}
// No application init/deinit/erase is allowed concurrently with this owner.
// Startup init has completed before the task exists. Future SDK lifecycle
// changes require stopping/quiescing this owner before any init/deinit/erase.
// Recheck admission at every SDK boundary; never retain a handle across calls.
struct Handle {
  nvs_handle_t value = 0;
  bool opened = false;
  ~Handle() {
    if (opened && nvsBootStatus().persistenceAllowed())
      nvs_close(value);
  }
};
} // namespace
bool NvsConfigStore::allowed() const { return nvsBootStatus().persistenceAllowed(); }
StoreResult NvsConfigStore::read(unsigned slot, ConfigRecord &record) {
  record.size = 0;
  if (slot > 1 || !allowed())
    return {StoreStatus::Refused};
  Handle h;
  auto e = nvs_open("ridesync_cfg", NVS_READONLY, &h.value);
  if (e != ESP_OK)
    return readError(e);
  h.opened = true;
  size_t size = 0;
  if (!allowed())
    return {StoreStatus::Refused};
  e = nvs_get_blob(h.value, key(slot), nullptr, &size);
  if (e != ESP_OK)
    return readError(e);
  if (size > kConfigRecordMax)
    return {StoreStatus::Oversized};
  if (!allowed())
    return {StoreStatus::Refused};
  const size_t expected = size;
  e = nvs_get_blob(h.value, key(slot), record.bytes.data(), &size);
  if (e != ESP_OK)
    return {StoreStatus::Error, e};
  if (size != expected)
    return {StoreStatus::Error, ESP_ERR_NVS_INVALID_LENGTH};
  record.size = size;
  return {};
}
StoreResult NvsConfigStore::write(unsigned slot, const ConfigRecord &record) {
  if (slot > 1 || record.size > kConfigRecordMax || !record.size || !allowed())
    return {StoreStatus::Refused};
  Handle h;
  auto e = nvs_open("ridesync_cfg", NVS_READWRITE, &h.value);
  if (e != ESP_OK)
    return {StoreStatus::Error, e};
  h.opened = true;
  if (!allowed())
    return {StoreStatus::Refused};
  e = nvs_set_blob(h.value, key(slot), record.bytes.data(), record.size);
  if (e != ESP_OK)
    return {StoreStatus::SetError, e};
  if (!allowed())
    return {StoreStatus::Refused};
  e = nvs_commit(h.value);
  return e == ESP_OK ? StoreResult{} : StoreResult{StoreStatus::CommitError, e};
}
} // namespace ridesync
#endif

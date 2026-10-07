#include "storage_sd.h"
#ifdef ARDUINO
namespace ridesync {
ArduinoSdStorage::ArduinoSdStorage(SPIClass &spi, const QualifiedSdConfig &pins,
                                   const StorageConfig &config)
    : sink_(spi, pins), config_(pins), storage_(sink_, config) {}
bool ArduinoSdStorage::Sink::mount() {
  return SD.begin(static_cast<uint8_t>(config_.chip_select), spi_, config_.frequency_hz, "/sd", 1,
                  false);
}
bool ArduinoSdStorage::Sink::openExclusive(const char *path) {
  if (SD.exists(path))
    return false;
  // Arduino SD has no atomic O_EXCL. Caller-qualified exclusive volume ownership
  // closes the check/open race. FILE_WRITE on an absent path creates only.
  file_ = SD.open(path, FILE_WRITE);
  return static_cast<bool>(file_);
}
size_t ArduinoSdStorage::Sink::write(const char *bytes, size_t length) {
  return file_ ? file_.write(reinterpret_cast<const uint8_t *>(bytes), length) : 0;
}
bool ArduinoSdStorage::Sink::flush() {
  if (!file_)
    return false;
  // Arduino File::flush returns void: completion, NOT verified durability/error
  // detection. The backend cannot detect a silent flush/media failure.
  file_.flush();
  return true;
}
void ArduinoSdStorage::Sink::close() {
  if (file_)
    file_.close();
}
bool ArduinoSdStorage::start() {
  if (started_ || !config_.opt_in || !config_.wiring_card_qualified || !config_.exclusive_volume ||
      config_.chip_select < 0 || config_.chip_select > 33 || !config_.frequency_hz ||
      config_.frequency_hz > 20000000)
    return false;
  started_ = xTaskCreate(run, "gps_sd", 8192, this, 1, &task_) == pdPASS;
  return started_;
}
void ArduinoSdStorage::run(void *arg) {
  auto &self = *static_cast<ArduinoSdStorage *>(arg);
  for (;;) {
    self.storage_.workerStep();
    if (self.storage_.health().stopped || self.storage_.health().terminal)
      break;
    vTaskDelay(1);
  }
  // A terminal failure still schedules worker-owned close. It may also block.
  if (!self.storage_.health().stopped)
    self.storage_.workerStep();
  self.finished_.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}
} // namespace ridesync
#endif

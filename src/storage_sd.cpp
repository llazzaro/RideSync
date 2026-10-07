#include "storage_sd.h"
#ifdef ARDUINO
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
namespace ridesync {
ArduinoSdStorage::ArduinoSdStorage(SPIClass &spi, const QualifiedSdConfig &pins,
                                   const StorageConfig &config)
    : sink_(spi, pins), config_(pins), storage_(sink_, config) {}
std::atomic<bool> ArduinoSdStorage::Sink::volume_reserved_{false};
bool ArduinoSdStorage::Sink::mount() {
  // Own a private SDFS; never adopt or end the application's global SD mount.
  // /ridesync is reserved for this adapter. Serialize adapter mount attempts so
  // a failing second begin cannot unregister another adapter's VFS path.
  if (!reserved_) {
    bool free = false;
    if (!volume_reserved_.compare_exchange_strong(free, true, std::memory_order_acq_rel)) {
      error_.store(EBUSY);
      return false;
    }
    reserved_ = true;
  }
  mounted_ = sd_.begin(static_cast<uint8_t>(config_.chip_select), spi_, config_.frequency_hz,
                       "/ridesync", 1, false);
  if (!mounted_)
    error_.store(EIO);
  return mounted_;
}
bool ArduinoSdStorage::Sink::openExclusive(const char *path) {
  if (!mounted_)
    return false;
  char full_path[64];
  const int n = snprintf(full_path, sizeof(full_path), "/ridesync%s", path);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(full_path))
    return false;
  // Pinned IDF 4.4.7 FAT VFS maps O_CREAT|O_EXCL to FA_CREATE_NEW.
  // No exists probe, fopen mode inference, O_TRUNC, fallback or open retries.
  // EEXIST and every allocation/IO/path error fail closed, preserving old logs.
  fd_ = ::open(full_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd_ < 0)
    error_.store(errno ? errno : EIO);
  return fd_ >= 0;
}
size_t ArduinoSdStorage::Sink::write(const char *bytes, size_t length) {
  if (fd_ < 0)
    return 0;
  const ssize_t n = ::write(fd_, bytes, length);
  if (n < 0)
    error_.store(errno ? errno : EIO);
  else if (static_cast<size_t>(n) != length)
    error_.store(EIO);
  return n < 0 ? 0 : static_cast<size_t>(n);
}
bool ArduinoSdStorage::Sink::flush() {
  // VFS fsync reports f_sync failure, but not independent physical durability.
  if (fd_ < 0) {
    error_.store(EBADF);
    return false;
  }
  if (::fsync(fd_) != 0) {
    error_.store(errno ? errno : EIO);
    return false;
  }
  return true;
}
void ArduinoSdStorage::Sink::close() {
  if (fd_ >= 0) {
    if (::close(fd_) != 0)
      error_.store(errno ? errno : EIO);
    fd_ = -1;
  }
  if (mounted_) {
    sd_.end();
    mounted_ = false;
  }
  // Pinned SDFS::begin cleans up its own partial mount on a false return. Never
  // end a mount not owned by this SDFS, including another global SD instance.
  if (reserved_) {
    volume_reserved_.store(false, std::memory_order_release);
    reserved_ = false;
  }
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

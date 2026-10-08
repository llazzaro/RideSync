#include "storage_sd.h"
#ifdef ARDUINO
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
namespace ridesync {
ArduinoSdStorage::ArduinoSdStorage(SPIClass &spi, const QualifiedSdConfig &config)
    : sink_(spi, config), ledger_(sink_), config_(config),
      owner_(sink_, ledger_, config.commissioned_namespace) {}
ArduinoSdStorage::IoStatus ArduinoSdStorage::ioStatus() const {
  const int error = ioError();
  return error == EEXIST ? IoStatus::PathCollision : error ? IoStatus::Error : IoStatus::None;
}
std::atomic<bool> ArduinoSdStorage::Sink::volume_reserved_{false};
bool ArduinoSdStorage::Sink::mount() {
  if (mounted_)
    return true;
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
  if (ledger_fd_ >= 0)
    closeLedger();
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
bool ArduinoSdStorage::Sink::open(unsigned slot, bool writing, bool exclusive_create) {
  if (!mounted_ || ledger_fd_ >= 0 || slot > 1) {
    error_.store(EBADF);
    return false;
  }
  const char *path = slot == 0 ? "/ridesync/.session-id-a" : "/ridesync/.session-id-b";
  const int flags = writing ? O_WRONLY | (exclusive_create ? O_CREAT | O_EXCL : 0) : O_RDONLY;
  ledger_fd_ = ::open(path, flags, 0600);
  if (ledger_fd_ < 0)
    error_.store(errno ? errno : EIO);
  return ledger_fd_ >= 0;
}
int ArduinoSdStorage::Sink::read(uint8_t *bytes, size_t n) {
  const ssize_t result = ::read(ledger_fd_, bytes, n);
  if (result < 0)
    error_.store(errno ? errno : EIO);
  return int(result);
}
int ArduinoSdStorage::Sink::write(const uint8_t *bytes, size_t n) {
  const ssize_t result = ::write(ledger_fd_, bytes, n);
  if (result < 0 || size_t(result) != n)
    error_.store(result < 0 && errno ? errno : EIO);
  return int(result);
}
bool ArduinoSdStorage::Sink::sync() {
  if (::fsync(ledger_fd_) == 0)
    return true;
  error_.store(errno ? errno : EIO);
  return false;
}
bool ArduinoSdStorage::Sink::closeLedger() {
  const int fd = ledger_fd_;
  ledger_fd_ = -1;
  if (::close(fd) == 0)
    return true;
  error_.store(errno ? errno : EIO);
  return false;
}
bool ArduinoSdStorage::start() {
  if (started_ || !config_.opt_in || !config_.wiring_card_qualified || !config_.exclusive_volume ||
      !config_.namespace_commissioned || !config_.commissioned_namespace ||
      config_.chip_select < 0 || config_.chip_select > 33 || !config_.frequency_hz ||
      config_.frequency_hz > 20000000)
    return false;
  started_ = xTaskCreate(run, "gps_sd", 8192, this, 1, &task_) == pdPASS;
  return started_;
}
void ArduinoSdStorage::run(void *arg) {
  auto &self = *static_cast<ArduinoSdStorage *>(arg);
  for (;;) {
    const bool done = self.owner_.workerStep();
    // Allocation/mount/ledger, wait-bind and Storage all return here. Blocked IO
    // cannot publish progress. The wrapper flag below is the lifetime barrier.
    self.progress_.completed(self.ioError() ? DeviceHealth::IoError : DeviceHealth::Ok);
    if (done)
      break;
    vTaskDelay(1);
  }
  self.finished_.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}
} // namespace ridesync
#endif

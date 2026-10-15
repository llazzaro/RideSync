#pragma once
#include "health_supervisor.h"
#include "session_storage_owner.h"
#ifdef ARDUINO
#include <SD.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vfs_api.h>
namespace ridesync {
struct QualifiedSdConfig {
  bool opt_in = false, wiring_card_qualified = false, exclusive_volume = false;
  // Independently commissioned opaque namespace; default-off. Uniqueness and
  // exclusive ownership are premises supplied by the commissioning authority.
  bool namespace_commissioned = false;
  uint32_t commissioned_namespace = 0;
  int chip_select = -1;
  uint32_t frequency_hz = 0;
};
// Dedicated configured SPI, qualified wiring/card and /ridesync namespace.
// One worker retains its private SDFS through allocation -> wait-bind -> Storage
// -> close/unmount. No Storage or SessionClock is constructed by this adapter.
// Control constructs them ONLY after allocation().Committed, using sink() and
// that ID, then bind(). Keep all objects alive until workerFinished; quiesce
// producers before destruction. cancel() also completes unbound owners.
class ArduinoSdStorage {
public:
  ArduinoSdStorage(SPIClass &spi, const QualifiedSdConfig &config);
  // False means no new worker was created (qualification/task refusal, or an
  // already-started owner). On first-call refusal no worker publication follows:
  // allocation stays Pending and destruction is safe without workerFinished.
  // Callers must branch on this return before polling; never admit a session.
  bool start();
  IdentityAllocation allocation() const { return owner_.allocation(); }
  StorageSink &sink() { return sink_; }
  bool bind(Storage &storage) { return owner_.bind(storage); }
  void cancel() { owner_.cancel(); }
  int ioError() const { return sink_.ioError(); }
  // Identity errors are copied in allocation(); EEXIST separately means later
  // CSV PathCollision, all other nonzero ioError values mean storage/media IO.
  enum class IoStatus { None, PathCollision, Error };
  IoStatus ioStatus() const;
  const HealthProgress &progress() const { return progress_; }
  bool workerFinished() const { return finished_.load(std::memory_order_acquire); }

private:
  class Sink : public StorageSink {
  public:
    Sink(SPIClass &spi, const QualifiedSdConfig &config)
        : spi_(spi), config_(config), sd_(fs::FSImplPtr(new VFSImpl())) {}
    int ioError() const override { return error_.load(std::memory_order_relaxed); }
    bool mount() override;
    bool openExclusive(const char *path) override;
    size_t write(const char *bytes, size_t length) override;
    bool flush() override;
    void close() override;
    bool open(unsigned slot, bool write, bool exclusive_create);
    int read(uint8_t *bytes, size_t length);
    int write(const uint8_t *bytes, size_t length);
    bool sync();
    bool closeLedger();

  private:
    SPIClass &spi_;
    QualifiedSdConfig config_;
    fs::SDFS sd_;
    int fd_ = -1, ledger_fd_ = -1;
    std::atomic<int> error_{0};
    bool reserved_ = false, mounted_ = false;
    static std::atomic<bool> volume_reserved_;
  } sink_;
  class Ledger : public IdentityLedgerIO {
  public:
    explicit Ledger(Sink &sink) : sink_(sink) {}
    bool open(unsigned s, bool w, bool c) override { return sink_.open(s, w, c); }
    int read(uint8_t *b, size_t n) override { return sink_.read(b, n); }
    int write(const uint8_t *b, size_t n) override { return sink_.write(b, n); }
    bool sync() override { return sink_.sync(); }
    bool close() override { return sink_.closeLedger(); }
    int error() const override { return sink_.ioError(); }

  private:
    Sink &sink_;
  } ledger_;
  const QualifiedSdConfig config_;
  SessionStorageOwner owner_;
  HealthProgress progress_;
  bool started_ = false;
  std::atomic<bool> finished_{false};
  TaskHandle_t task_ = nullptr;
  static void run(void *self);
};
} // namespace ridesync
#endif

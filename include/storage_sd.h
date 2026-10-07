#pragma once
#include "storage.h"
#ifdef ARDUINO
#include <SD.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace ridesync {
struct QualifiedSdConfig {
  bool opt_in = false, wiring_card_qualified = false, exclusive_volume = false;
  int chip_select = -1;
  uint32_t frequency_hz = 0;
};
// The caller supplies an already configured, dedicated SPI bus and qualified
// CS/card/filesystem. No board pin defaults, SPI.begin, formatting or deletion.
// No other SD/volume user may run while this object exists. Keep it alive until
// workerFinished(), with the producer quiescent, before destruction.
class ArduinoSdStorage {
public:
  ArduinoSdStorage(SPIClass &spi, const QualifiedSdConfig &pins, const StorageConfig &config);
  // Creates a priority-1 task; does no filesystem IO. Disabled by default.
  bool start();
  Storage &storage() { return storage_; }
  bool workerFinished() const { return finished_.load(std::memory_order_acquire); }

private:
  class Sink : public StorageSink {
  public:
    Sink(SPIClass &spi, const QualifiedSdConfig &config) : spi_(spi), config_(config) {}
    bool mount() override;
    bool openExclusive(const char *path) override;
    size_t write(const char *bytes, size_t length) override;
    bool flush() override;
    void close() override;

  private:
    SPIClass &spi_;
    QualifiedSdConfig config_;
    File file_;
  } sink_;
  const QualifiedSdConfig config_;
  Storage storage_;
  bool started_ = false;
  std::atomic<bool> finished_{false};
  TaskHandle_t task_ = nullptr;
  static void run(void *self);
};
} // namespace ridesync
#endif

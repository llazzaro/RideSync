#pragma once
#ifdef ARDUINO
#include "imu_bus.h"
#include "imu_manager.h"
#include <Wire.h>
#include <bmi270_api/bmi270.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace ridesync {
// Caller supplies a dedicated initialized bus, verified buffer >=128 and finite
// timeout. Lifetime/exclusive ownership persists through workerFinished(). No begin/pins.
struct Bmi270Qualification {
  bool enabled = false, dedicated_bus = false, electrically_qualified = false;
  uint8_t address = 0;
  uint32_t sensor_id = 0;
};
class Bmi270Imu : public ImuPort, private ImuBus {
public:
  Bmi270Imu(TwoWire &wire, const Bmi270Qualification &qualification);
  Bmi270Imu(const Bmi270Imu &) = delete;
  Bmi270Imu &operator=(const Bmi270Imu &) = delete;
  Bmi270Imu(Bmi270Imu &&) = delete;
  Bmi270Imu &operator=(Bmi270Imu &&) = delete;
  bool begin(ImuConfig &) override;
  bool read(uint8_t *, uint16_t, uint16_t &, uint8_t &) override;
  bool flush() override;
  uint32_t capacityOverflows() const { return capacity_overflows_; }
  void describeReadFailure(ImuEvidence &e) const override;
  bool drainTimes(uint32_t &start, uint32_t &end) const override {
    start = drain_start_;
    end = drain_end_;
    return drained_;
  }

private:
  bool selectRegister(uint8_t) override;
  uint32_t receive(uint8_t *, uint32_t) override;
  uint32_t transmit(uint8_t, const uint8_t *, uint32_t) override;
  static int8_t readCallback(uint8_t, uint8_t *, uint32_t, void *);
  static int8_t writeCallback(uint8_t, const uint8_t *, uint32_t, void *);
  static void delayCallback(uint32_t, void *);
  TwoWire &wire_;
  Bmi270Qualification qualification_;
  StrictImuBus bus_;
  bmi2_dev device_{};
  bool ready_ = false, drained_ = false;
  uint32_t drain_start_ = 0, drain_end_ = 0;
  uint32_t capacity_overflows_ = 0;
  uint16_t refused_length_ = 0;
  bool capacity_refusal_ = false;
};
// All objects caller-owned; start ONCE, keep them alive through finish/admission/SD close.
class Bmi270Worker {
public:
  explicit Bmi270Worker(ImuManager &manager, HealthProgress &progress)
      : manager_(manager), progress_(progress) {}
  Bmi270Worker(const Bmi270Worker &) = delete;
  Bmi270Worker &operator=(const Bmi270Worker &) = delete;
  Bmi270Worker(Bmi270Worker &&) = delete;
  Bmi270Worker &operator=(Bmi270Worker &&) = delete;
  bool start(bool qualified, bool safe_mode);
  bool workerFinished() const { return finished_.load(std::memory_order_acquire); }

private:
  static void run(void *);
  ImuManager &manager_;
  HealthProgress &progress_;
  bool started_ = false;
  std::atomic<bool> finished_{false};
};
} // namespace ridesync
#endif

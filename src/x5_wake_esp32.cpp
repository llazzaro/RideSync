#include "x5_wake_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "insta360_wake_esp32.h"
#include <Arduino.h>
#include <freertos/task.h>
namespace ridesync {
namespace {
class Radio final : public WakeRadio {
public:
  WakeSubmit begin(const WakeOperation &op, const insta360::WakeEncoding &bytes, uint32_t deadline,
                   uint32_t now) override {
    if (reserved_)
      return WakeSubmit::Busy;
    const auto phase = phase_.load(std::memory_order_acquire);
    if (phase == 3)
      return WakeSubmit::Failed;
    if (phase == 2) {
      const auto result = insta360WakeRadio().begin(op, bytes, deadline, now);
      if (result != WakeSubmit::Accepted)
        return result;
      delegated_ = true;
    } else {
      deadline_ = deadline;
      phase_.store(1, std::memory_order_release);
      TaskHandle_t task = nullptr;
      if (xTaskCreate(startup, "x5_wake_start", 4096, this, 2, &task) != pdPASS) {
        phase_.store(3, std::memory_order_release);
        return WakeSubmit::Failed;
      }
      delegated_ = false;
    }
    operation_ = op;
    bytes_ = bytes;
    advertising_deadline_ = deadline;
    reserved_ = true;
    cancelled_ = false;
    // Startup is an owned lease too. CONNECT waits for its final host access.
    return WakeSubmit::Accepted;
  }
  void cancel(const WakeOperation &op) override {
    if (!reserved_ || !sameWakeOperation(operation_, op))
      return;
    cancelled_ = true;
    if (delegated_)
      insta360WakeRadio().cancel(op);
  }
  WakeRadioResult poll(const WakeOperation &op, uint32_t now) override {
    WakeRadioResult result;
    result.operation = operation_;
    if (!reserved_ || !sameWakeOperation(operation_, op))
      return result;
    if (delegated_) {
      result = insta360WakeRadio().poll(op, now);
      if (sameWakeOperation(result.operation, op) && result.terminal && result.released)
        reserved_ = false;
      return result;
    }
    const auto phase = phase_.load(std::memory_order_acquire);
    const bool expired = uint32_t(now - advertising_deadline_) < 0x80000000u;
    if (phase == 1) {
      result.terminal = cancelled_ || expired;
      return result; // Worker may still access host; released stays false.
    }
    if (cancelled_ || expired || phase == 3) {
      result.terminal = result.released = true;
      result.sdk_error = phase == 3 ? -1 : 0;
      reserved_ = false;
      return result;
    }
    const auto submit = insta360WakeRadio().begin(op, bytes_, advertising_deadline_, now);
    if (submit == WakeSubmit::Accepted) {
      delegated_ = true;
      result = insta360WakeRadio().poll(op, now);
      if (sameWakeOperation(result.operation, op) && result.terminal && result.released)
        reserved_ = false;
      return result;
    }
    if (submit != WakeSubmit::Busy) {
      result.terminal = result.released = true;
      result.sdk_error = -1;
      reserved_ = false;
    }
    return result;
  }

private:
  std::atomic<unsigned> phase_{0};
  uint32_t deadline_ = 0, advertising_deadline_ = 0;
  WakeOperation operation_;
  insta360::WakeEncoding bytes_;
  bool reserved_ = false, delegated_ = false, cancelled_ = false;
  static void startup(void *context) {
    auto &self = *static_cast<Radio *>(context);
    auto &host = Esp32BleHost::instance();
    host.start(true, true); // Configured X5 port supplies exact store/registration.
    while (host.state() == BleHostState::Starting &&
           uint32_t(millis() - self.deadline_) >= 0x80000000u)
      vTaskDelay(pdMS_TO_TICKS(10));
    const bool ready = uint32_t(millis() - self.deadline_) >= 0x80000000u &&
                       host.state() == BleHostState::Ready && insta360WakeRadio().activate(true);
    if (!ready && host.state() == BleHostState::Starting)
      host.sealStartup();
    self.phase_.store(ready ? 2 : 3, std::memory_order_release); // Final owner access.
    // No packet is sent from this worker. A current uncanceled poll alone can
    // admit advertising; a late return only releases the startup barrier.
    for (;;)
      vTaskDelay(pdMS_TO_TICKS(100));
  }
};
} // namespace
WakeRadio &x5WakeRadio() {
  static Radio radio;
  return radio;
}
} // namespace ridesync
extern "C" ridesync::WakeRadio *ridesync_x5_wake_radio() { return &ridesync::x5WakeRadio(); }
#endif

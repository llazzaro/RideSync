#pragma once
#include "ble_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
namespace ridesync {
// Boot-lifetime raw peripheral lease on the existing qualified host.
class Esp32Insta360Wake final : public WakeRadio {
public:
  bool activate(bool opt_in);
  WakeSubmit begin(const WakeOperation &, const insta360::WakeEncoding &, uint32_t,
                   uint32_t) override;
  void cancel(const WakeOperation &) override;
  WakeRadioResult poll(const WakeOperation &, uint32_t) override;

private:
  friend Esp32Insta360Wake &insta360WakeRadio();
  Esp32Insta360Wake() = default;
  struct Lease {
    Esp32Insta360Wake *owner = nullptr;
    WakeOperation operation{};
    insta360::WakeEncoding bytes{};
    uint32_t deadline = 0;
    ble_npl_event barrier{};
    std::atomic<bool> queued{false}, barrier_running{false};
    std::atomic<unsigned> callbacks{0};
    std::atomic<uint16_t> connection{kBleNoHandle};
    std::atomic<bool> complete{false};
  } lease_;
  std::atomic_flag control_ = ATOMIC_FLAG_INIT;
  std::atomic<unsigned> work_phase_{0}; // idle, waiting, running, final access done
  bool worker_done_ = false;
  bool active_ = false, occupied_ = false;
  bool prepared_ = false, started_ = false, stop_attempted_ = false, terminate_attempted_ = false;
  static void worker(void *);
  void cycle();
  bool open(uint32_t now) const;
  void finish();
  void queueBarrier();
  static void barrier(ble_npl_event *);
  static int gap(ble_gap_event *, void *);
};
Esp32Insta360Wake &insta360WakeRadio();
} // namespace ridesync
extern "C" ridesync::Esp32Insta360Wake *ridesync_insta360_wake_backend(bool opt_in);
#endif

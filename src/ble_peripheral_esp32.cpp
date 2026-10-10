#include "ble_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "nvs_boot_guard.h"
namespace ridesync {
namespace {
class PeripheralRoutingGate {
public:
  explicit PeripheralRoutingGate(std::atomic_flag &flag)
      : flag_(flag), held_(!flag.test_and_set(std::memory_order_acquire)) {}
  ~PeripheralRoutingGate() {
    if (held_)
      flag_.clear(std::memory_order_release);
  }
  bool held() const { return held_; }

private:
  std::atomic_flag &flag_;
  bool held_;
};
} // namespace
bool Esp32BleHost::configurePeripheral(void *owner, int (*registration)(void *)) {
  if (leased_ || !owner || !registration ||
      (peripheral_owner_ &&
       (peripheral_owner_ != owner || peripheral_registration_ != registration)))
    return false;
  peripheral_owner_ = owner;
  peripheral_registration_ = registration;
  return true;
}
bool Esp32BleHost::reservePeripheral(void *owner, uint32_t generation) {
  PeripheralRoutingGate gate(routing_lock_);
  if (!gate.held() || owner != peripheral_owner_ || !generation || peripheralReserved() ||
      wakeReserved() || reset_gate_.load(std::memory_order_acquire) || reset_.phase.load() == 1 ||
      reset_.phase.load() == 2 || sdk_calls_.load() || state() != BleHostState::Ready ||
      fault() != BleFault::None || !restore_verified_ || !refusal_installed_ ||
      !nvsBootStatus().persistenceAllowed())
    return false;
  for (const auto &slot : slots_)
    if (slot.context &&
        (slot.context->phase == BlePhase::Scan ||
         (slot.context->phase == BlePhase::Connect &&
          slot.context->connection.load(std::memory_order_acquire) == kBleNoHandle)))
      return false; // Serialize GAP setup; established central links retain their contexts.
  if (ble_gap_disc_active() || ble_gap_conn_active() || ble_gap_adv_active())
    return false;
  peripheral_generation_ = generation;
  peripheral_gap_reserved_.store(true, std::memory_order_release);
  peripheral_reserved_.store(true, std::memory_order_release);
  return true;
}
bool Esp32BleHost::releasePeripheral(void *owner, uint32_t generation) {
  PeripheralRoutingGate gate(routing_lock_);
  if (!gate.held() || owner != peripheral_owner_ || generation != peripheral_generation_ ||
      !peripheralReserved())
    return false;
  peripheral_gap_reserved_.store(false, std::memory_order_release);
  peripheral_reserved_.store(false, std::memory_order_release);
  return true;
}
// Called only from the owner's accepted identity-qualified CONNECT callback.
// It releases GAP setup exclusion, retaining the peripheral ownership and final
// callback/barrier lease. No other owner can reset/wake/reuse that lease.
bool Esp32BleHost::peripheralConnected(void *owner, uint32_t generation) {
  if (owner != peripheral_owner_ || generation != peripheral_generation_ || !peripheralReserved())
    return false;
  peripheral_gap_reserved_.store(false, std::memory_order_release);
  return true;
}
} // namespace ridesync
#endif

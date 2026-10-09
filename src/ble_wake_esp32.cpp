#include "ble_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "nvs_boot_guard.h"
namespace ridesync {
namespace {
class WakeGate {
public:
  explicit WakeGate(std::atomic_flag &flag)
      : flag_(flag), held_(!flag.test_and_set(std::memory_order_acquire)) {}
  ~WakeGate() {
    if (held_)
      flag_.clear(std::memory_order_release);
  }
  bool held() const { return held_; }

private:
  std::atomic_flag &flag_;
  bool held_;
};
} // namespace
WakeSubmit Esp32BleHost::reserveWake(const WakeOperation &op, uint32_t deadline, uint32_t now) {
  WakeGate routing(routing_lock_);
  if (!routing.held() || wakeReserved() || reset_gate_.load(std::memory_order_acquire) ||
      reset_.phase.load(std::memory_order_acquire) == 1 || reset_.phase.load() == 2 ||
      sdk_calls_.load(std::memory_order_acquire))
    return WakeSubmit::Busy;
  if (state() != BleHostState::Ready || fault() != BleFault::None || !restore_verified_ ||
      !refusal_installed_ || !nvsBootStatus().persistenceAllowed())
    return WakeSubmit::Unsupported;
  // Leases remain reserved until the original procedure's callback/barrier is
  // released, not merely until SDK GAP active() becomes false.
  for (const auto &slot : slots_)
    if (slot.context && (slot.context->phase == BlePhase::Scan ||
                         (slot.context->phase == BlePhase::Connect &&
                          slot.context->connection.load() == kBleNoHandle)))
      return WakeSubmit::Busy;
  if (ble_gap_disc_active() || ble_gap_conn_active() || ble_gap_adv_active())
    return WakeSubmit::Busy;
  WakeGate policy(wake_lock_);
  if (!policy.held())
    return WakeSubmit::Busy;
  if (wake_quarantined_.load() || !wake_policy_.reserve(op, deadline, now))
    return WakeSubmit::Failed;
  wake_sealed_.store(false, std::memory_order_release);
  wake_reserved_.store(true, std::memory_order_release);
  return WakeSubmit::Accepted;
}
bool Esp32BleHost::admitWakeStart(const WakeOperation &op, uint32_t now) {
  WakeGate policy(wake_lock_);
  if (!policy.held()) {
    quarantineWake();
    return false;
  }
  if (!wakeReserved() || wake_sealed_.load(std::memory_order_acquire) || wake_quarantined_.load() ||
      reset_gate_.load() || state() != BleHostState::Ready || fault() != BleFault::None)
    return false;
  // This check is the admission point. A later seal cannot retract this call.
  return wake_policy_.admitStart(op, now);
}
void Esp32BleHost::sealWake(const WakeOperation &op) {
  WakeGate policy(wake_lock_);
  if (!policy.held()) {
    // Conservative cancellation under contention, never a lost stop request.
    wake_sealed_.store(true, std::memory_order_release);
    return;
  }
  if (sameWakeOperation(wake_policy_.status().operation, op)) {
    wake_sealed_.store(true, std::memory_order_release);
    wake_policy_.seal(op);
  }
}
bool Esp32BleHost::releaseWake(const WakeOperation &op) {
  WakeGate routing(routing_lock_);
  if (!routing.held())
    return false;
  WakeGate policy(wake_lock_);
  if (!policy.held() || wake_quarantined_.load() ||
      !sameWakeOperation(wake_policy_.status().operation, op) || !wake_policy_.reusable())
    return false;
  wake_reserved_.store(false, std::memory_order_release);
  return true;
}
WakeRadioResult Esp32BleHost::wakeStatus() {
  WakeGate policy(wake_lock_);
  if (!policy.held())
    return {};
  auto out = wake_policy_.status();
  if (wake_quarantined_.load())
    out.released = false;
  return out;
}
void Esp32BleHost::quarantineWake() {
  wake_sealed_.store(true, std::memory_order_release);
  wake_quarantined_.store(true, std::memory_order_release);
}
void Esp32BleHost::wakeReturned(const WakeOperation &op, int error) {
  WakeGate policy(wake_lock_);
  if (policy.held())
    wake_policy_.returned(op, error);
  else
    quarantineWake();
}
bool Esp32BleHost::wakeIncoming(const WakeOperation &op, uint16_t connection) {
  WakeGate policy(wake_lock_);
  if (policy.held())
    return wake_policy_.incoming(op, connection);
  quarantineWake();
  return false;
}
void Esp32BleHost::wakeDisconnected(const WakeOperation &op, uint16_t connection) {
  WakeGate policy(wake_lock_);
  if (policy.held())
    wake_policy_.disconnected(op, connection);
  else
    quarantineWake();
}
void Esp32BleHost::wakeTerminal(const WakeOperation &op) {
  WakeGate policy(wake_lock_);
  if (policy.held())
    wake_policy_.terminal(op);
  else
    quarantineWake();
}
void Esp32BleHost::wakeCallbackEnter() {
  WakeGate policy(wake_lock_);
  if (policy.held())
    wake_policy_.callbackEnter();
  else
    quarantineWake();
}
void Esp32BleHost::wakeCallbackExit() {
  WakeGate policy(wake_lock_);
  if (policy.held())
    wake_policy_.callbackExit();
  else
    quarantineWake();
}
void Esp32BleHost::wakeBarrierReleased(const WakeOperation &op) {
  WakeGate policy(wake_lock_);
  if (policy.held())
    wake_policy_.barrierReleased(op);
  else
    quarantineWake();
}
} // namespace ridesync
#endif

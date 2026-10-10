#include "ble_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "nvs_boot_guard.h"
#include <nimble/nimble/host/include/host/ble_store.h>
#include <nimble/nimble/host/src/ble_hs_resolv_priv.h>
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
  if (!routing.held() || wakeReserved() || peripheralReserved() ||
      reset_gate_.load(std::memory_order_acquire) ||
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
  wake_identity_known_ = false;
  wake_identity_pending_.store(false);
  wake_connection_.store(kBleNoHandle);
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
bool Esp32BleHost::wakeOwnsConnection(uint16_t handle) const {
  return wakeReserved() && handle != kBleNoHandle && wake_connection_.load() == handle;
}
bool Esp32BleHost::wakeIdentify(const WakeOperation &op, uint16_t handle, bool reported_success) {
  wake_identity_pending_.store(true, std::memory_order_release);
  WakeGate routing(routing_lock_);
  if (handle == kBleNoHandle && !reported_success)
    return false;
  if (!routing.held() || handle == kBleNoHandle) {
    quarantineWake();
    return false;
  }
  for (const auto &entry : slots_)
    if (entry.context && entry.context->connection.load() == handle) {
      quarantineWake();
      return false; // Never terminate an established foreign handle.
    }
  ble_gap_conn_desc description{};
  const int found = ble_gap_conn_find(handle, &description);
  if (!reported_success && found == BLE_HS_ENOTCONN)
    return false; // Pinned metadata proves absence; status alone does not.
  if (!wakeIncoming(op, handle)) {
    quarantineWake();
    return false;
  }
  wake_connection_.store(handle, std::memory_order_release);
  if (found) {
    quarantineWake();
    return true; // Actual new handle is owned; identity stays ambiguous.
  }
  WakeGate policy(wake_lock_);
  if (!policy.held()) {
    quarantineWake();
    return true;
  }
  bool nonzero = false;
  for (auto byte : description.peer_id_addr.val)
    nonzero |= byte != 0;
  if (!nonzero || description.peer_id_addr.type > BLE_ADDR_RANDOM ||
      (description.peer_id_addr.type == BLE_ADDR_RANDOM &&
       (description.peer_id_addr.val[5] & 0xc0) != 0xc0)) {
    quarantineWake();
    return true;
  }
  wake_identity_ = description.peer_id_addr;
  wake_ota_ = description.peer_ota_addr;
  wake_identity_known_ = true;
  wake_identity_pending_.store(false, std::memory_order_release);
  return true;
}
bool Esp32BleHost::wakeStoreAllowed(int type, const void *key_pointer, const void *value_pointer) {
  if (peripheralReserved())
    return false; // Exclusive single-X5 lease; no security/store mutation.
  if (!wakeReserved())
    return true;
  if (wake_identity_pending_.load(std::memory_order_acquire))
    return false;
  WakeGate routing(routing_lock_);
  WakeGate policy(wake_lock_);
  if (!routing.held() || !policy.held()) {
    quarantineWake();
    return false;
  }
  const auto same = [](const ble_addr_t &a, const ble_addr_t &b) {
    return a.type == b.type && std::memcmp(a.val, b.val, 6) == 0;
  };
  const auto allowed = [&](const ble_addr_t &address) {
    if (!wake_identity_known_) {
      // CONNECT is delayed by pinned remote feature/version processing. During
      // that interval only independently owned established central identities
      // are attributable; never interpret missing wake identity as permission.
      for (const auto &entry : slots_) {
        if (!entry.context || entry.context->connection.load() == kBleNoHandle ||
            !entry.identity.verified)
          continue;
        ble_addr_t owned{};
        owned.type =
            entry.identity.type == IdentityType::Public ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM;
        std::memcpy(owned.val, entry.identity.address.data(), 6);
        if (same(owned, address))
          return true;
      }
      return false;
    }
    bool nonzero = false;
    for (auto byte : address.val)
      nonzero |= byte != 0;
    return nonzero && address.type <= BLE_ADDR_RANDOM && !same(address, wake_identity_) &&
           !same(address, wake_ota_) &&
           (address.type != BLE_ADDR_RANDOM || (address.val[5] & 0xc0) == 0xc0);
    // Unknown private addresses can alias the incoming identity. Only known
    // foreign stable identities can be classified without resolution I/O.
  };
  const auto *key = static_cast<const ble_store_key *>(key_pointer);
  const auto *value = static_cast<const ble_store_value *>(value_pointer);
  if (!key && !value)
    return false;
  switch (type) {
  case BLE_STORE_OBJ_TYPE_OUR_SEC:
  case BLE_STORE_OBJ_TYPE_PEER_SEC:
    return allowed(value ? value->sec.peer_addr : key->sec.peer_addr);
  case BLE_STORE_OBJ_TYPE_CCCD:
    return allowed(value ? value->cccd.peer_addr : key->cccd.peer_addr);
  case BLE_STORE_OBJ_TYPE_CSFC:
    return allowed(value ? value->csfc.peer_addr : key->csfc.peer_addr);
  case BLE_STORE_OBJ_TYPE_LOCAL_IRK:
    return allowed(value ? value->local_irk.addr : key->local_irk.addr);
  case BLE_STORE_OBJ_TYPE_PEER_ADDR:
    return value ? allowed(value->rpa_rec.peer_rpa_addr) && allowed(value->rpa_rec.peer_addr)
                 : allowed(key->rpa_rec.peer_rpa_addr);
  case BLE_STORE_OBJ_TYPE_PEER_DEV_REC: {
    if (!value_pointer)
      return false; // Pinned public key union has no private record schema.
    const auto &record = *static_cast<const ble_hs_dev_records *>(value_pointer);
    if (!allowed(record.peer_sec.peer_addr))
      return false;
    if (!wake_identity_known_)
      return true; // Typed record belongs to an independently owned foreign link.
    for (const auto *alias : {record.identity_addr, record.rand_addr, record.pseudo_addr})
      if (std::memcmp(alias, wake_identity_.val, 6) == 0 ||
          std::memcmp(alias, wake_ota_.val, 6) == 0)
        return false;
    return true;
  }
  default:
    quarantineWake();
    return false;
  }
}
bool Esp32BleHost::wakeSecurityAllowed(uint16_t handle) {
  if (peripheralReserved())
    return false; // Exclusive single-X5 lease; no security/store mutation.
  if (!wakeReserved())
    return true;
  if (wakeOwnsConnection(handle) || handle == kBleNoHandle)
    return false;
  WakeGate routing(routing_lock_);
  if (!routing.held()) {
    quarantineWake();
    return false;
  }
  for (const auto &entry : slots_)
    if (entry.context && entry.context->phase == BlePhase::Connect &&
        entry.context->connection.load() == handle)
      return true;
  return false;
}
} // namespace ridesync
extern "C" int ridesync_ble_wake_store_allowed(int type, const void *key, const void *value) {
  return ridesync::Esp32BleHost::instance().wakeStoreAllowed(type, key, value);
}
extern "C" int ridesync_ble_wake_peer_allowed(uint8_t type, const uint8_t *address) {
  ble_store_key key{};
  key.sec.peer_addr.type = type;
  std::memcpy(key.sec.peer_addr.val, address, 6);
  return ridesync::Esp32BleHost::instance().wakeStoreAllowed(BLE_STORE_OBJ_TYPE_PEER_SEC, &key,
                                                             nullptr);
}
extern "C" int ridesync_ble_wake_security_allowed(uint16_t handle) {
  return ridesync::Esp32BleHost::instance().wakeSecurityAllowed(handle);
}
#endif

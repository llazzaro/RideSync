#pragma once
#include "ble_pairing_reset.h"
#include "wake_radio_policy.h"
#if defined(ARDUINO_ARCH_ESP32)
#include <NimBLEDevice.h>
#include <nimble/nimble/host/include/host/ble_gatt.h>
#include <nimble/nimble/include/nimble/nimble_npl.h>

namespace ridesync {
class Esp32BleHost final : public BleHost, public NimBLEDeviceCallbacks {
public:
  // One boot-lifetime instance, also the future raw peripheral host/store owner.
  static Esp32BleHost &instance();
  Esp32BleHost(const Esp32BleHost &) = delete;
  Esp32BleHost &operator=(const Esp32BleHost &) = delete;
  Esp32BleHost(Esp32BleHost &&) = delete;
  Esp32BleHost &operator=(Esp32BleHost &&) = delete;
  bool configureRestore(const BleStoreProof &);
  // One immutable peripheral registration before this boot's host start.
  bool configurePeripheral(void *owner, int (*register_services)(void *));
  bool reservePeripheral(void *owner, uint32_t generation);
  bool releasePeripheral(void *owner, uint32_t generation);
  bool peripheralReserved() const { return peripheral_reserved_.load(std::memory_order_acquire); }
  // Copy only the EXACT proof admitted at startup; no NVS reads on this owner.
  bool admittedProof(BleStoreProof &) const;
  // Commissioning readback only: no keys/addresses exposed. Caller must serialize
  // configuration NVS owner and BLE lifecycle; never init/deinit/erase concurrently.
  BleStoreObservation inspectStore(bool compare_stack);
  BleHostState start(bool enabled, bool source_qualified) override;
  BleHostState state() const override;
  BleFault fault() const override;
  void sealStartup() override;
  BleBondAdmission bondAdmission(const BondIdentity &) override;
  int submit(const BleCommand &, BleContext &) override;
  int retire(BleContext &) override;
  int cancelScan(BleContext &) override;
  bool quiescent(const BleContext &) const override;
  bool releaseContext(BleContext &) override;
  uint16_t mtu(uint16_t) const override;
  int onStoreStatus(ble_store_status_event *, void *) override;
  int error() const { return error_.load(); }
  // One serialized caller. Operation IDs strictly increase for this boot.
  // Copied identity must come from independent verified stable peer evidence.
  // Caller first releases matching contexts and relevant GAP activity. No I/O
  // occurs on this caller; a queued host event owns the fixed storage.
  BondResetSubmission requestBondReset(const BondIdentity &, uint32_t operation, uint32_t deadline,
                                       uint32_t now);
  // Permanent revocation, also used by #44 for current-configuration admission.
  // An SDK call already admitted may finish; no subsequent mutation is admitted.
  bool cancelBondReset(uint32_t operation);
  // Deadline/cancel may finish intent while releasable remains false. Never reuse
  // storage based on a timeout: only the host callback's final release permits it.
  BleBondResetResult bondResetResult(uint32_t operation, uint32_t now);

  // One wake-only GAP lease. No host/store initialization occurs here.
  WakeSubmit reserveWake(const WakeOperation &, uint32_t deadline, uint32_t now);
  bool admitWakeStart(const WakeOperation &, uint32_t now);
  void sealWake(const WakeOperation &);
  bool releaseWake(const WakeOperation &);
  bool wakeReserved() const { return wake_reserved_.load(std::memory_order_acquire); }
  bool wakeCancelled() const { return wake_sealed_.load(std::memory_order_acquire); }
  uint8_t wakeOwnAddressType() const { return own_address_type_; }
  WakeRadioResult wakeStatus();
  void wakeReturned(const WakeOperation &, int);
  bool wakeIncoming(const WakeOperation &, uint16_t);
  void wakeDisconnected(const WakeOperation &, uint16_t);
  void wakeTerminal(const WakeOperation &);
  void wakeCallbackEnter();
  void wakeCallbackExit();
  void wakeBarrierReleased(const WakeOperation &);
  void quarantineWake();
  bool wakeIdentify(const WakeOperation &, uint16_t, bool reported_success = true);
  bool wakeStoreAllowed(int type, const void *key, const void *value);
  bool wakeOwnsConnection(uint16_t) const;
  bool wakeSecurityAllowed(uint16_t);

private:
  Esp32BleHost() = default;
  void *peripheral_owner_ = nullptr;
  int (*peripheral_registration_)(void *) = nullptr;
  uint32_t peripheral_generation_ = 0;
  std::atomic<bool> peripheral_reserved_{false};
  std::atomic<bool> wake_reserved_{false}, wake_sealed_{false}, wake_quarantined_{false};
  std::atomic_flag wake_lock_ = ATOMIC_FLAG_INIT;
  WakeRadioPolicy wake_policy_;
  std::atomic<bool> wake_identity_pending_{false};
  std::atomic<uint16_t> wake_connection_{kBleNoHandle};
  ble_addr_t wake_identity_{}, wake_ota_{};
  bool wake_identity_known_ = false;

  struct Slot {
    BleContext *context = nullptr;
    BondIdentity identity;
    ble_npl_event barrier{};
    std::atomic<bool> quiet{true};
    std::atomic<bool> barrier_queued{false};
    std::atomic<unsigned> callbacks{0};
    bool initialized = false;
  };
  std::array<Slot, kBlePeers * 2 + 1> slots_{};
  std::atomic_flag routing_lock_ = ATOMIC_FLAG_INIT;
  std::atomic<BleHostState> state_{BleHostState::Disabled};
  std::atomic<BleFault> fault_{BleFault::None};
  std::atomic<int> error_{0};
  BleStoreProof expected_;
  bool leased_ = false, restore_verified_ = false, refusal_installed_ = false,
       host_started_ = false;
  uint8_t own_address_type_ = 0;
  mutable std::atomic<unsigned> sdk_calls_{0};
  std::atomic<bool> reset_gate_{false};
  struct Reset {
    ble_npl_event event{};
    std::atomic<unsigned> phase{0}; // idle, queued, running, final access released
    std::atomic<bool> cancelled{false}, timed_out{false}, mutation{false};
    uint32_t operation = 0, last_operation = 0, deadline = 0;
    BondIdentity identity;
    BleBondResetResult result;
    // Host-only scratch, never placed on the 4096-byte host task stack.
    std::array<std::array<char, 16>, 80> names{};
    alignas(std::max_align_t) std::array<uint8_t, 512> bytes{};
    std::array<std::array<uint8_t, 32>, 80> digests{};
    bool ambiguous = false, initialized = false;
  } reset_;
  bool resetAllowed();
  bool resetInventory(std::array<uint8_t, 32> &, unsigned &target_records);
  bool resetResolving(std::array<uint8_t, 32> &, bool &found);
  int resetClassify(unsigned schema, const void *blob) const;
  static void resetEvent(ble_npl_event *);
  void performBondReset();

  Slot &slot(BleContext &);
  static void barrier(ble_npl_event *);
  static void terminal(Slot &);
  static void hostTask(void *);
  static int gap(ble_gap_event *, void *);
  static int service(uint16_t, const ble_gatt_error *, const ble_gatt_svc *, void *);
  static int characteristic(uint16_t, const ble_gatt_error *, const ble_gatt_chr *, void *);
  static int descriptor(uint16_t, const ble_gatt_error *, uint16_t, const ble_gatt_dsc *, void *);
  static int attribute(uint16_t, const ble_gatt_error *, ble_gatt_attr *, void *);
  static void deliver(Slot &, BleEvent, bool terminal);
  void fail(BleFault, int);
};
} // namespace ridesync
#endif

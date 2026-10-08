#pragma once
#include "ble_remote.h"
#if defined(ARDUINO_ARCH_ESP32)
#include <NimBLEDevice.h>
#include <nimble/nimble/host/include/host/ble_gatt.h>
#include <nimble/nimble/include/nimble/nimble_npl.h>

namespace ridesync {
// Digest is SHA256 of sorted NVS key names, explicit lengths and exact pinned-ABI
// blobs. Keep independent durable commissioning evidence; never self-authorize
// restoration from a snapshot taken during the boot being admitted.
struct BleStoreProof {
  uint32_t qualification_record = 0;
  std::array<uint8_t, 32> digest{};
  std::array<unsigned, 7> counts{};
};
struct BleStoreObservation {
  bool complete = false;
  int error = 0;
  BleStoreProof snapshot;
};
enum class BondResetSubmission { Queued, Busy, Refused, Stale };
struct BleBondResetResult {
  uint32_t operation = 0;
  BondOutcome outcome = BondOutcome::Busy;
  int error = 0;
  bool finished = false, releasable = false, mutation = false;
  bool requalification_required = false, cancelled = false, timed_out = false;
};
class Esp32BleHost final : public BleHost, public NimBLEDeviceCallbacks {
public:
  // One boot-lifetime instance, also the future raw peripheral host/store owner.
  static Esp32BleHost &instance();
  Esp32BleHost(const Esp32BleHost &) = delete;
  Esp32BleHost &operator=(const Esp32BleHost &) = delete;
  Esp32BleHost(Esp32BleHost &&) = delete;
  Esp32BleHost &operator=(Esp32BleHost &&) = delete;
  bool configureRestore(const BleStoreProof &);
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

private:
  Esp32BleHost() = default;
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

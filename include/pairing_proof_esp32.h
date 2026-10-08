#pragma once
#if defined(ARDUINO_ARCH_ESP32)
#include "ble_pairing_reset.h"
#include <atomic>
namespace ridesync {
struct ProofRevocationResult {
  uint32_t operation = 0;
  int error = 0;
  bool finished = false, releasable = false, durable = false;
  bool write_attempted = false, cancelled = false, timed_out = false;
};
// Boot-lifetime fixed mailbox. Only the existing configuration task calls
// beginOwner/service; the sole application caller requests/cancels/copies/releases.
// No control-context NVS IO, new task, settings reset or proof creation.
class PairingProofMaintenance {
public:
  void beginOwner(); // before configuration publication and BLE host startup
  void service();
  bool restorationAllowed(uint32_t record) const;
  BondResetSubmission request(uint32_t operation, const BleStoreProof &admitted, uint32_t deadline,
                              uint32_t now);
  bool cancel(uint32_t operation);
  ProofRevocationResult result(uint32_t operation) const;
  bool release(uint32_t operation);

private:
  enum class Boot { Pending, Ready, Error };
  std::atomic<Boot> boot_{Boot::Pending};
  std::atomic<uint32_t> floor_{0};
  std::atomic<unsigned> phase_{0}; // idle, queued, worker-owned, final release
  std::atomic<bool> cancelled_{false};
  bool initialized_ = false; // configuration owner only, never reset in this boot
  uint32_t operation_ = 0, last_operation_ = 0, deadline_ = 0;
  BleStoreProof proof_;
  ProofRevocationResult result_;
  bool active() const;
  void perform();
};
PairingProofMaintenance &pairingProofMaintenance();
} // namespace ridesync
#endif

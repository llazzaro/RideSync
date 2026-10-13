#pragma once
#include "ble_remote.h"
#include "config.h"
namespace ridesync {
constexpr size_t kSetupCandidates = 16;
struct SetupCandidate {
  std::array<uint8_t, 6> address{};
  uint8_t address_type = 0xff;
  int8_t rssi = 0;
  char name[33]{};
  std::array<BleUuid, 4> services{};
  uint8_t service_count = 0;
  bool hints_truncated = false;
};
struct SetupIdentityMapping {
  std::array<uint8_t, 6> observed{};
  uint8_t observed_type = 0xff;
  BondIdentity identity;
};
enum class SetupDiscoveryOutcome {
  Unavailable,
  Idle,
  Scanning,
  Releasing,
  Complete,
  Failed,
  Initializing
};
enum class SetupSelectionOutcome {
  NeedsCommissioning,
  Unresolved,
  Unsupported,
  Busy,
  Stale,
  Capacity,
  DuplicateIdentity,
  BondCapacity,
  X5Manual
};
// Serial owner only; callbacks enter solely through BleCentral's copied event queue.
// No scan result is identity evidence. Mappings must come from independently
// authenticated stack resolution or reviewed existing commissioning evidence.
class SetupDiscovery final : public BleResultSink {
public:
  explicit SetupDiscovery(BleHost &host) : host_(host), central_(host, *this) {}
  bool begin(bool qualified, uint32_t now);
  bool initializationRequested() const { return start_requested_; }
  void initialize(bool qualified, uint32_t now);
  bool hostReady() const { return host_.state() == BleHostState::Ready; }
  bool hostSettled() const {
    return host_.state() == BleHostState::Ready || host_.state() == BleHostState::Failed;
  }
  bool scan(uint32_t session, uint32_t now);
  void cancel(uint32_t session);
  void service(uint32_t now);
  bool released() const { return central_.scanReleased(); }
  SetupDiscoveryOutcome outcome() const { return outcome_; }
  uint32_t session() const { return session_; }
  size_t count() const { return count_; }
  bool truncated() const { return truncated_; }
  const SetupCandidate &candidate(size_t i) const { return candidates_[i]; }
  void setIdentityMappings(const SetupIdentityMapping *maps, size_t count);
  SetupSelectionOutcome select(uint32_t session, size_t index, CameraModel model,
                               const SourceConfig &base, size_t slot, CameraConfig &out);
  void result(const BleResult &) override;

private:
  BleHost &host_;
  BleCentral central_;
  std::array<SetupCandidate, kSetupCandidates> candidates_{};
  std::array<SetupIdentityMapping, kBlePeers> mappings_{};
  size_t count_ = 0, mapping_count_ = 0;
  uint32_t session_ = 0, generation_ = 0;
  bool initialized_ = false, truncated_ = false, start_requested_ = false, pending_scan_ = false;
  SetupDiscoveryOutcome outcome_ = SetupDiscoveryOutcome::Unavailable;
  void observe(const BleEvent &);
  const BondIdentity *verified(const SetupCandidate &) const;
};
} // namespace ridesync

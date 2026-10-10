#pragma once
#include "camera_manager.h"
#include "insta360_wake_encoder.h"
#include <array>
namespace ridesync {
constexpr size_t kWakePeers = 4;
struct WakeOperation {
  uint8_t peer = 0;
  uint32_t generation = 0, id = 0;
};
bool sameWakeOperation(const WakeOperation &, const WakeOperation &);
enum class WakeSubmit : uint8_t { Accepted, Busy, Unsupported, Failed };
enum class WakePhase : uint8_t {
  Queued,
  Advertising,
  Releasing,
  Recovering,
  Ready,
  Unsupported,
  Failed,
  Cancelled,
  Timeout
};
struct WakeRadioResult {
  WakeOperation operation;
  bool submitted = false, terminal = false, released = false;
  int sdk_error = 0;
};
// References outlive the manager. Methods copy/return without waiting for SDK.
// Busy/Unsupported/Failed begin results acquire no borrowed storage/lease.
class WakeRadio {
public:
  virtual ~WakeRadio() = default;
  virtual WakeSubmit begin(const WakeOperation &, const insta360::WakeEncoding &, uint32_t deadline,
                           uint32_t now) = 0;
  virtual void cancel(const WakeOperation &) = 0;
  virtual WakeRadioResult poll(const WakeOperation &, uint32_t now) = 0;
};
struct WakeRecoveryResult {
  WakeOperation operation;
  bool terminal = false, released = false, fresh = false;
  RecordingState observed = RecordingState::Unknown;
  CameraError error = CameraError::None;
};
// Provider never issues REC. Released means recovery procedure storage released,
// not closing a successfully established control link. Non-None begin acquires
// no procedure ownership. Fresh state requires independent qualified evidence.
class WakeRecovery {
public:
  virtual ~WakeRecovery() = default;
  // Read-only current control admission. Unknown unless a fresh independently
  // qualified link can already accept recording commands; no I/O or ownership.
  virtual RecordingState currentObserved(uint8_t) const { return RecordingState::Unknown; }
  virtual CameraError begin(const WakeOperation &, uint32_t deadline) = 0;
  virtual WakeRecoveryResult poll(const WakeOperation &, uint32_t now) = 0;
  virtual void cancel(const WakeOperation &) = 0;
};
struct WakePolicy {
  uint32_t total_ms = 15000, slice_ms = 3000;
};
struct WakePeerConfig {
  bool enabled = false, source_qualified = false;
  insta360::WakeProfile profile = insta360::WakeProfile::Disabled;
  std::array<uint8_t, 6> identifier{};
};
// Contains no identifiers, address, bytes or presumed recording state.
struct WakeStatus {
  WakeOperation operation;
  WakePhase phase = WakePhase::Unsupported;
  CameraError error = CameraError::Unsupported;
  bool released = true;
  RecordingState observed = RecordingState::Unknown;
};
// One serialized owner; fixed storage, no manager/group ticks or callbacks.
class WakeManager {
public:
  WakeManager(WakeRadio &, WakeRecovery * = nullptr);
  ~WakeManager();
  WakeManager(const WakeManager &) = delete;
  WakeManager &operator=(const WakeManager &) = delete;
  CameraError request(const WakeOperation &, const WakePeerConfig &, const WakePolicy &,
                      uint32_t now);
  void cancel(uint8_t peer);
  void invalidate(uint32_t new_generation);
  void service(uint32_t now);
  WakeStatus status(uint8_t peer) const;

private:
  struct Slot {
    WakeStatus status;
    insta360::WakeEncoding encoding;
    WakePolicy policy;
    uint32_t started = 0;
    bool occupied = false, sealed = false, radio = false, recovery = false;
    bool radio_cancelled = false, recovery_cancelled = false;
  };
  WakeRadio &radio_;
  WakeRecovery *recovery_;
  std::array<Slot, kWakePeers> slots_{};
  std::array<uint32_t, kWakePeers> last_ids_{};
  uint32_t generation_ = 0;
  size_t owner_ = kWakePeers, cursor_ = 0;
  void seal(Slot &, WakePhase, CameraError);
  void recover(Slot &);
  void updateReleaseOnly(Slot &);
};
} // namespace ridesync

#pragma once
#include "recording_manager.h"
#include "wake_manager.h"
namespace ridesync {
// Explicit owner composition only. Recovery must independently qualify the
// actual CameraManager link; this bridge never emits camera commands/events.
class WakePreparation final : public RecordingPreparation {
public:
  WakePreparation(WakeManager &, Clock &, const std::array<WakePeerConfig, kWakePeers> &,
                  uint32_t generation, WakePolicy = {});
  CameraError prepare(size_t) override;
  RecordingPreparationResult prepared(size_t) override;
  void retire(size_t) override;
  bool commandReady(size_t) const override;
  bool retiring(size_t) const override;
  bool released(size_t) const override;
  void invalidate(uint32_t generation);
  void service();

private:
  struct Peer {
    WakeOperation operation{};
    uint32_t last_id = 0;
    CameraError immediate = CameraError::NotConnected;
    bool used = false, sealed = false, qualified_link = false;
  };
  WakeManager &manager_;
  Clock &clock_;
  const std::array<WakePeerConfig, kWakePeers> configs_;
  uint32_t generation_;
  WakePolicy policy_;
  std::array<Peer, kWakePeers> peers_{};
  bool matches(size_t) const;
};
} // namespace ridesync

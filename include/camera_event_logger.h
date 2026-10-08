#pragma once
#include "recording_manager.h"
#include "telemetry_admission.h"
#include <array>
namespace ridesync {
// Explicit IDs are provisioned independently of BLE address, serial and name.
// Once an event is produced this mapping is immutable for this session.
class CameraEventLogger final : public CameraAudit {
public:
  CameraEventLogger(CameraInbox &inbox, Clock &raw, uint64_t session_id, RecordingManager &group)
      : inbox_(inbox), raw_(raw), session_id_(session_id), group_(group) {}
  bool configurePeer(size_t peer, uint32_t opaque_id, CameraModel model);
  bool readyFor(const CameraManager &manager) const;
  void request(size_t, Operation, CameraError, bool queued, uint32_t intent_id) override;
  void attempt(size_t, Operation, Token, uint32_t intent_id, bool delivered) override;
  void accepted(const Event &, Operation, uint32_t intent_id) override;
  void cancelled(size_t, Operation, Token, uint32_t intent_id) override;
  void failure(size_t, Operation, Token, uint32_t intent_id, CameraError) override;
  void wireAck(size_t, Operation, Token, uint32_t intent_id, CameraAckDomain,
               CameraAckAction) override;

private:
  struct Identity {
    uint32_t id = 0;
    CameraModel model = CameraModel::Unknown;
  };
  CameraInbox &inbox_;
  Clock &raw_;
  uint64_t session_id_;
  RecordingManager &group_;
  std::array<Identity, kMaxCameras> identities_{};
  bool sealed_ = false;
  CameraEvidence base(size_t peer, Operation operation, uint32_t intent_id);
  void publish(const CameraEvidence &e);
};
} // namespace ridesync

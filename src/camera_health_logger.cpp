#include "camera_health_logger.h"
namespace ridesync {
constexpr uint8_t CameraHealthLogger::kQuota;
CameraHealthLogger::CameraHealthLogger(CameraHealthEvaluator &evaluator, TelemetryAdmission &owner,
                                       CameraHealthTransitionSink *sink)
    : evaluator_(evaluator), sink_(sink), attached_(owner.attachHealth(*this)) {}
bool CameraHealthLogger::configurePeer(size_t peer, uint32_t id, CameraModel model) {
  if (!attached_ || sealed_ || peer >= kMaxCameras || !id || model == CameraModel::Unknown ||
      static_cast<unsigned>(model) > static_cast<unsigned>(CameraModel::DJI_OSMO360))
    return false;
  for (size_t i = 0; i < kMaxCameras; ++i)
    if (i != peer && identities_[i].id == id)
      return false;
  identities_[peer].id = id;
  identities_[peer].model = model;
  return true;
}
void CameraHealthLogger::drain(TelemetryAdmission &owner) {
  sealed_ = true;
  const uint32_t dropped = evaluator_.dropped();
  owner.healthDropped(dropped - dropped_seen_);
  dropped_seen_ = dropped;
  for (uint8_t i = 0; i < kQuota; ++i) {
    CameraHealthEvidence evidence;
    if (!evaluator_.popTransition(evidence.transition))
      break;
    const auto peer = evidence.transition.peer;
    if (peer < kMaxCameras) {
      evidence.peer_id = identities_[peer].id;
      evidence.model = identities_[peer].model;
    }
    evidence.transition_dropped = dropped;
    evidence.transition_coalesced = evaluator_.historyCoalesced();
    if (sink_)
      sink_->transition(evidence.transition, evidence.peer_id, evidence.model,
                        evidence.transition_dropped, evidence.transition_coalesced);
    owner.health(evidence);
  }
}
bool CameraHealthLogger::drained() const {
  return finished_ && evaluator_.pendingTransitions() == 0;
}
} // namespace ridesync

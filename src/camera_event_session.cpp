#include "camera_event_session.h"
namespace ridesync {
CameraEventSession::CameraEventSession(Clock &raw, StorageSink &sink, Hero12Adapter &adapter,
                                       CameraManager &manager, RecordingManager &group, uint64_t id,
                                       const char *firmware, const char *provenance)
    : adapter_(adapter), manager_(manager), group_(group), clock_(raw, id, 1000),
      storage_(sink, {id, firmware, provenance, 2, 4, StorageFormat::CameraV3}),
      admission_(clock_, storage_, imu_, &camera_), logger_(camera_, raw, id, group) {}
CameraEventSession::~CameraEventSession() {
  if (route_owned_) {
    manager_.detachAudit(&logger_);
    adapter_.detachGroup(&group_);
  }
}
bool CameraEventSession::configurePeer(size_t peer, uint32_t id, CameraModel model) {
  const auto *configured = manager_.configuredCamera(peer);
  return !active_ && configured && configured->enabled && configured->model == model &&
         logger_.configurePeer(peer, id, model);
}
bool CameraEventSession::activate() {
  if (active_ || stopping_ || !storage_.configValid() || !logger_.readyFor(manager_))
    return false;
  if (!manager_.attachAudit(logger_))
    return false;
  adapter_.attachGroup(group_);
  active_ = true;
  route_owned_ = true;
  return true;
}
bool CameraEventSession::binds(const Hero12Adapter &adapter, const CameraManager &manager,
                               const RecordingManager &group) const {
  return &adapter_ == &adapter && &manager_ == &manager && &group_ == &group;
}
void CameraEventSession::service() {
  if (!route_owned_) {
    // A refused or completed session only drains its own telemetry on stop.
    if (stopping_)
      admission_.tick();
    return;
  }
  adapter_.service();
  if (stopping_ && !camera_finished_ && adapter_.canDestroy()) {
    manager_.detachAudit(&logger_);
    adapter_.detachGroup(&group_);
    route_owned_ = false;
    camera_.finish();
    camera_finished_ = true;
  }
  admission_.tick();
}
void CameraEventSession::requestStop() {
  if (stopping_)
    return;
  if (active_) {
    group_.cancel();
    adapter_.stop();
  } else {
    camera_.finish();
    camera_finished_ = true;
  }
  admission_.requestStop();
  stopping_ = true;
}
void CameraEventSession::finishImu() { imu_.finish(); }
bool CameraEventSession::stopped() const {
  return stopping_ && camera_finished_ && admission_.stopped() && storage_.health().stopped &&
         (!route_owned_ || adapter_.canDestroy());
}
} // namespace ridesync

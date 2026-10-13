#include "camera_event_session.h"
namespace ridesync {
CameraEventSession::CameraEventSession(Clock &raw, StorageSink &sink, CameraRuntimePort &adapter,
                                       CameraManager &manager, RecordingManager &group, uint64_t id,
                                       const char *firmware, const char *provenance,
                                       const MotionAdmissionConfig &motion,
                                       StaticMotionReferenceSource *source, bool bookmark_enabled,
                                       bool native_bookmark_enabled, bool health_enabled,
                                       CameraHealthTransitionSink *health_sink)
    : adapter_(adapter), manager_(manager), group_(group), clock_(raw, id, 1000),
      storage_(sink, {id, firmware, provenance, 2, 4,
                      (native_bookmark_enabled && !bookmark_enabled)
                          ? static_cast<StorageFormat>(UINT8_MAX)
                      : health_enabled         ? StorageFormat::CameraHealthV7
                      : bookmark_enabled       ? StorageFormat::BookmarkV6
                      : motion.dynamic.enabled ? StorageFormat::MotionV5
                      : motion.requested       ? StorageFormat::MotionV4
                                               : StorageFormat::CameraV3}),
      admission_(clock_, storage_, imu_, &camera_, motion, source, bookmark_enabled),
      logger_(camera_, raw, id, group), health_logger_(health_, admission_, health_sink), raw_(raw),
      health_enabled_(health_enabled), native_bookmark_enabled_(native_bookmark_enabled) {
  if (!health_enabled_)
    health_logger_.finish();
}
CameraHealthSummary CameraEventSession::healthSummary() const {
  auto summary = summarizeCameraHealth(health_, health_enabled_ && route_owned_);
  if (summary.enabled)
    summary.wire_failures = adapter_.healthFailures();
  return summary;
}
void CameraEventSession::healthSample(uint8_t peer, const QualifiedHealthSample &sample) {
  if (health_enabled_ && route_owned_ && !stopping_)
    health_.observe(peer, sample, raw_.now());
}
void CameraEventSession::healthPollPeriod(uint8_t peer, uint32_t floor, uint32_t now) {
  if (health_enabled_ && route_owned_ && !stopping_)
    health_.pollPeriod(peer, floor, now);
}
CameraEventSession::~CameraEventSession() {
  if (route_owned_) {
    if (health_enabled_)
      manager_.detachHealthObserver(&health_);
    if (health_sink_owned_)
      adapter_.detachHealthSink(this);
    if (marker_audit_owned_)
      adapter_.detachMarkerAudit(&logger_);
    manager_.detachAudit(&logger_);
    adapter_.detachGroup(&group_);
  }
}
bool CameraEventSession::configurePeer(size_t peer, uint32_t id, CameraModel model) {
  const auto *configured = manager_.configuredCamera(peer);
  if (active_ || !configured || !configured->enabled || configured->model != model ||
      !logger_.configurePeer(peer, id, model))
    return false;
  if (health_enabled_) {
    if (!health_logger_.configurePeer(peer, id, model))
      return false;
    const auto *state = manager_.state(peer);
    health_.configure(peer, model, state->token.connection,
                      state->lifecycle == Lifecycle::Ready ||
                          state->lifecycle == Lifecycle::Operating,
                      raw_.now());
  }
  return true;
}
bool CameraEventSession::activateLocal() {
  if (active_ || stopping_ || !storage_.configValid())
    return false;
  camera_.finish();
  health_logger_.finish();
  camera_finished_ = true;
  active_ = true;
  return true;
}
bool CameraEventSession::activate() {
  if (active_ || stopping_ || !storage_.configValid() || !logger_.readyFor(manager_))
    return false;
  if (!manager_.attachAudit(logger_))
    return false;
  if (health_enabled_) {
    if (!health_logger_.attached() || !manager_.attachHealthObserver(health_)) {
      manager_.detachAudit(&logger_);
      return false;
    }
    health_sink_owned_ = adapter_.attachHealthSink(*this);
    health_coalesced_seen_ = adapter_.healthCoalesced();
  }
  if (native_bookmark_enabled_)
    marker_audit_owned_ = adapter_.attachMarkerAudit(logger_);
  adapter_.attachGroup(group_);
  active_ = true;
  route_owned_ = true;
  return true;
}
bool CameraEventSession::binds(const CameraRuntimePort &adapter, const CameraManager &manager,
                               const RecordingManager &group) const {
  return &adapter_ == &adapter && &manager_ == &manager && &group_ == &group;
}
void CameraEventSession::service(CameraServiceAction *action) {
  admission_.beginMotionPass();
  if (!route_owned_) {
    // Local mode advances only telemetry; a refused session drains only on stop.
    if (active_ && !stopping_ && action)
      action->beforeAdvance();
    if (active_ || stopping_)
      admission_.tick();
    return;
  }
  adapter_.service(action);
  if (health_enabled_ && !stopping_) {
    const auto coalesced = adapter_.healthCoalesced();
    health_.noteCoalesced(coalesced >= health_coalesced_seen_ ? coalesced - health_coalesced_seen_
                                                              : coalesced);
    health_coalesced_seen_ = coalesced;
    health_.tick(raw_.now());
  }
  if (stopping_ && !camera_finished_ && adapter_.canDestroy()) {
    if (health_enabled_)
      manager_.detachHealthObserver(&health_);
    if (health_sink_owned_) {
      adapter_.detachHealthSink(this);
      health_sink_owned_ = false;
    }
    health_logger_.finish();
    if (marker_audit_owned_) {
      adapter_.detachMarkerAudit(&logger_);
      marker_audit_owned_ = false;
    }
    manager_.detachAudit(&logger_);
    adapter_.detachGroup(&group_);
    route_owned_ = false;
    camera_.finish();
    camera_finished_ = true;
  }
  admission_.tick();
}
BookmarkReceipt CameraEventSession::bookmark(BookmarkSource source, BookmarkGesture gesture,
                                             uint64_t expected_session_id, bool allow_native) {
  if (!active_)
    return {};
  const auto receipt = admission_.bookmark(source, gesture, expected_session_id);
  if (receipt.outcome == BookmarkOutcome::Admitted && route_owned_ && marker_audit_owned_ &&
      allow_native) {
    CameraMarkerKey key;
    key.session_id = receipt.session_id;
    key.bookmark_id = receipt.bookmark_id;
    for (size_t i = 0; i < manager_.size(); ++i) {
      const auto *peer = manager_.configuredCamera(i);
      if (peer && peer->enabled)
        adapter_.requestHilight(static_cast<uint8_t>(i), key);
    }
  }
  return receipt;
}
void CameraEventSession::requestStop() {
  if (stopping_)
    return;
  if (route_owned_) {
    group_.cancel();
    if (health_enabled_) {
      const auto coalesced = adapter_.healthCoalesced();
      health_.noteCoalesced(coalesced >= health_coalesced_seen_ ? coalesced - health_coalesced_seen_
                                                                : coalesced);
      health_coalesced_seen_ = coalesced;
      for (size_t i = 0; i < manager_.size(); ++i)
        health_.healthReset(i, manager_.state(i)->token, raw_.now());
      manager_.detachHealthObserver(&health_);
      if (health_sink_owned_) {
        adapter_.detachHealthSink(this);
        health_sink_owned_ = false;
      }
    }
    adapter_.stop();
  } else {
    camera_.finish();
    health_logger_.finish();
    camera_finished_ = true;
  }
  admission_.requestStop();
  stopping_ = true;
}
void CameraEventSession::finishImu() {
  admission_.revokeMotion();
  imu_.finish();
}
bool CameraEventSession::stopped() const {
  return stopping_ && camera_finished_ && admission_.stopped() && storage_.health().stopped &&
         (!route_owned_ || adapter_.canDestroy());
}
} // namespace ridesync

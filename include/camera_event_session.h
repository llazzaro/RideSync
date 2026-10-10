#pragma once
#include "camera_event_logger.h"
#include "profiles/gopro_hero12.h"
namespace ridesync {
// Opt-in, boot-lifetime owner composition. The caller owns a qualified sink and
// isolated storage worker; they outlive this object. The raw Clock is the same
// domain used by the adapter and manager. service() runs in their owner context.
class CameraEventSession {
public:
  CameraEventSession(Clock &raw, StorageSink &sink, CameraRuntimePort &adapter,
                     CameraManager &manager, RecordingManager &group, uint64_t session_id,
                     const char *firmware, const char *provenance,
                     const MotionAdmissionConfig &motion = {},
                     StaticMotionReferenceSource *source = nullptr);
  ~CameraEventSession();
  CameraEventSession(const CameraEventSession &) = delete;
  CameraEventSession &operator=(const CameraEventSession &) = delete;
  bool configurePeer(size_t peer, uint32_t opaque_id, CameraModel model);
  bool activate();
  bool activateLocal();
  bool binds(const CameraRuntimePort &adapter, const CameraManager &manager,
             const RecordingManager &group) const;
  bool active() const { return active_; }
  void service(CameraServiceAction *action = nullptr);
  void requestStop();
  void finishImu(); // Call only after the IMU producer's final publication.
  bool stopped() const;
  CameraInbox &cameraInbox() { return camera_; }
  ImuInbox &imuInbox() { return imu_; }
  TelemetryAdmission &admission() { return admission_; }
  Storage &storage() { return storage_; }
  SessionClock &clock() { return clock_; }

private:
  CameraRuntimePort &adapter_;
  CameraManager &manager_;
  RecordingManager &group_;
  SessionClock clock_;
  Storage storage_;
  ImuInbox imu_;
  CameraInbox camera_;
  TelemetryAdmission admission_;
  CameraEventLogger logger_;
  bool active_ = false, route_owned_ = false, stopping_ = false, camera_finished_ = false;
};
} // namespace ridesync

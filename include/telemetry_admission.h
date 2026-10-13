#pragma once
#include "camera_manager.h"
#include "motion_admission.h"
#include "storage.h"
#include <atomic>
namespace ridesync {
class TelemetryAdmission;
// Serialized owner source; finish follows the final evaluator publication.
// Object lifetime must exceed admission through source finish and Storage stop.
class HealthLogSource {
public:
  virtual ~HealthLogSource() = default;
  virtual void drain(TelemetryAdmission &) = 0;
  virtual void requestStop() = 0;
  virtual bool drained() const = 0;
};
struct ImuBatch {
  static constexpr uint8_t kRecords = 4;
  uint8_t count = 0;
  ImuEvidence records[kRecords];
};
// Session-local nonzero IDs; a new admission owner starts at one.
class BookmarkIdSequence {
public:
  explicit BookmarkIdSequence(uint32_t first = 1) : next_(first) {}
  uint32_t take() {
    if (exhausted_ || !next_)
      return 0;
    const uint32_t id = next_;
    if (next_ == UINT32_MAX)
      exhausted_ = true;
    else
      ++next_;
    return id;
  }

private:
  uint32_t next_;
  bool exhausted_ = false;
};
// One transport producer publish/finish; one admission consumer. finish is
// irreversible and follows the producer's FINAL publication, even after stop.
// Keep inbox, clock, storage and sink alive until all contexts have finished.
class ImuInbox {
public:
  static constexpr uint32_t kCapacity = 4;
  bool publish(const ImuBatch &batch);
  void finish();
  bool stopRequested() const;
  uint32_t dropped(RecordKind kind) const;
  uint32_t rejected() const;

private:
  friend class TelemetryAdmission;
  ImuBatch slots_[kCapacity];
  std::atomic<uint32_t> read_{0}, write_{0}, dropped_[5]{}, rejected_{0};
  std::atomic<bool> stop_{false}, finished_{false};
};
// Serialized camera owner is the sole producer; TelemetryAdmission is the sole
// consumer. Publication copies all evidence; no BLE callback touches storage.
class CameraInbox {
public:
  static constexpr uint32_t kCapacity = 16;
  bool publish(const CameraEvidence &evidence);
  bool publishMarker(const CameraMarkerEvidence &evidence);
  void finish();
  bool stopRequested() const;
  uint32_t dropped() const { return dropped_.load(std::memory_order_acquire); }
  uint32_t rejected() const { return rejected_.load(std::memory_order_acquire); }

private:
  friend class TelemetryAdmission;
  struct Slot {
    CameraEvidence camera;
    CameraMarkerEvidence marker;
    bool is_marker = false;
  };
  Slot slots_[kCapacity];
  std::atomic<uint32_t> read_{0}, write_{0}, dropped_{0}, rejected_{0};
  std::atomic<uint32_t> marker_dropped_{0}, marker_rejected_{0};
  std::atomic<bool> stop_{false}, finished_{false};
};
// All methods run in the SAME context as GPS/SessionClock mutation and Storage
// enqueue. Future app composition delegates ALL Storage producer calls here.
class TelemetryAdmission {
public:
  static constexpr uint8_t kQuota = 2;
  TelemetryAdmission(SessionClock &clock, Storage &storage, ImuInbox &inbox,
                     CameraInbox *camera = nullptr,
                     const MotionAdmissionConfig &motion = MotionAdmissionConfig{},
                     StaticMotionReferenceSource *source = nullptr, bool bookmark_active = true);
  // Prefer this overload with the same owner snapshot used by Modem::snapshot.
  bool gps(const RecordTimestamp &timestamp, const ModemSnapshot &sample);
  bool gps(const ModemSnapshot &sample);
  bool event(const ImuEvidence &evidence);
  BookmarkReceipt bookmark(BookmarkSource source, BookmarkGesture gesture,
                           uint64_t expected_session_id = 0);
  uint8_t tick();
  // Attach once before the first producer pass. Health has its own <=2 quota;
  // tick() retains its existing IMU/camera return-count contract.
  bool attachHealth(HealthLogSource &);
  bool health(CameraHealthEvidence evidence);
  void healthDropped(uint32_t count);
  void requestStop();
  bool stopped() const { return stopped_; }
  void beginMotionPass();
  void withdrawMotionReference();
  void revokeMotion();
  void refuseMotion();
  MotionAdmission motionAdmission() const { return motion_state_; }
  MotionCurrentSnapshot motionSnapshot(uint32_t now) const;

private:
  DynamicMotionInput dynamicInput(const ImuEvidence &e, const MotionEstimate &converted,
                                  bool fresh);
  bool motionFresh(const ImuEvidence &e, const RecordTimestamp &timestamp, uint32_t age);
  void publishCurrentMotion(const ImuEvidence &e, const MotionEvidence &motion,
                            const RecordTimestamp &timestamp, uint32_t raw);
  void prepareStaticMotion(const ImuEvidence &e, MotionEvidence &motion);
  bool inboxesDrained();
  bool admitImuInbox();
  bool admitCameraInbox();
  void observeImuLoss();
  void accountCameraLoss();
  SessionClock &clock_;
  Storage &storage_;
  ImuInbox &inbox_;
  CameraInbox *camera_;
  HealthLogSource *health_ = nullptr;
  const MotionAdmissionConfig motion_options_;
  MotionEstimator estimator_;
  DynamicMotionEstimator dynamic_;
  uint64_t dynamic_time_us_ = 0;
  uint32_t dynamic_dropped_seen_ = 0;
  bool dynamic_discontinuity_ = false;
  DynamicMotionEstimate updateDynamic(const ImuEvidence &, const MotionEstimate &,
                                      uint32_t &declaration, bool fresh);
  StaticMotionReferenceSource *source_;
  const bool bookmark_active_;
  MotionAdmission motion_state_ = MotionAdmission::Disabled;
  MotionCurrentSnapshot current_;
  uint32_t admitted_raw_ = 0;
  bool imu_admission_started_ = false;
  bool admitImu(const ImuEvidence &, MotionInputRoute, bool reserve);
  uint8_t index_ = 0;
  bool camera_turn_ = false;
  uint32_t camera_dropped_seen_ = 0, camera_rejected_seen_ = 0;
  uint32_t marker_dropped_seen_ = 0, marker_rejected_seen_ = 0;
  BookmarkIdSequence bookmark_ids_;
  bool stopping_ = false, stopped_ = false;
};
static_assert(sizeof(ImuBatch) <= 968 && sizeof(ImuInbox) <= 3912,
              "Review inbox RAM bounds when changing batch capacity");
} // namespace ridesync

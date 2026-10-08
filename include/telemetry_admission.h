#pragma once
#include "camera_manager.h"
#include "storage.h"
#include <atomic>
namespace ridesync {
struct ImuBatch {
  static constexpr uint8_t kRecords = 4;
  uint8_t count = 0;
  ImuEvidence records[kRecords];
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
  void finish();
  bool stopRequested() const;
  uint32_t dropped() const { return dropped_.load(std::memory_order_acquire); }
  uint32_t rejected() const { return rejected_.load(std::memory_order_acquire); }

private:
  friend class TelemetryAdmission;
  CameraEvidence slots_[kCapacity];
  std::atomic<uint32_t> read_{0}, write_{0}, dropped_{0}, rejected_{0};
  std::atomic<bool> stop_{false}, finished_{false};
};
// All methods run in the SAME context as GPS/SessionClock mutation and Storage
// enqueue. Future app composition delegates ALL Storage producer calls here.
class TelemetryAdmission {
public:
  static constexpr uint8_t kQuota = 2;
  TelemetryAdmission(SessionClock &clock, Storage &storage, ImuInbox &inbox,
                     CameraInbox *camera = nullptr)
      : clock_(clock), storage_(storage), inbox_(inbox), camera_(camera) {}
  // Prefer this overload with the same owner snapshot used by Modem::snapshot.
  bool gps(const RecordTimestamp &timestamp, const ModemSnapshot &sample);
  bool gps(const ModemSnapshot &sample);
  bool event(const ImuEvidence &evidence);
  uint8_t tick();
  void requestStop();
  bool stopped() const { return stopped_; }

private:
  SessionClock &clock_;
  Storage &storage_;
  ImuInbox &inbox_;
  CameraInbox *camera_;
  uint8_t index_ = 0;
  bool camera_turn_ = false;
  uint32_t camera_dropped_seen_ = 0, camera_rejected_seen_ = 0;
  bool stopping_ = false, stopped_ = false;
};
static_assert(sizeof(ImuBatch) <= 968 && sizeof(ImuInbox) <= 3912,
              "Review inbox RAM bounds when changing batch capacity");
} // namespace ridesync

#pragma once
#include "camera_manager.h"
namespace ridesync {
enum class GroupError { None, NoCameras, UnknownState, InvalidIntent, ResyncPending };
struct RecordingPeerStatus {
  bool enabled = false;
  bool ready = false;
  RecordingState observed = RecordingState::Unknown;
  bool pending = false;
  // Group operation retired in error; distinct from an active camera retry.
  bool terminal_failure = false;
  bool acknowledged = false;
  CameraError error = CameraError::None;
};
struct RecordingStatus {
  RecordingState intent = RecordingState::Unknown;
  GroupError error = GroupError::None;
  size_t enabled = 0, ready = 0, recording = 0, stopped = 0, unknown = 0, pending = 0, errors = 0;
  uint32_t generation = 0;
  std::array<RecordingPeerStatus, kMaxCameras> peers;
};
struct RecordingPreparationResult {
  bool pending = false;
  CameraError error = CameraError::None;
};
class RecordingPreparation {
public:
  virtual ~RecordingPreparation() = default;
  virtual CameraError prepare(size_t peer) = 0;
  virtual RecordingPreparationResult prepared(size_t peer) = 0;
  virtual void retire(size_t peer) = 0;
  virtual bool commandReady(size_t) const { return true; }
  // A canceled/sealed link can briefly look Ready in the manager. Replacement
  // requests wait for its actual disconnect and final host lease release.
  virtual bool retiring(size_t) const { return false; }
  virtual bool released(size_t) const { return true; }
};
using RecordingCallback = void (*)(void *, const RecordingStatus &);
class RecordingManager {
public:
  RecordingManager(CameraManager &cameras, Clock &clock, RecordingCallback callback = nullptr,
                   void *context = nullptr);
  GroupError request(RecordingState intent);
  bool attachPreparation(RecordingPreparation &p);
  void detachPreparation(RecordingPreparation &p);
  GroupError shortPress();
  GroupError resync();
  void cancel();
  // Target-only maintenance; all later group intent keeps this peer sealed.
  CameraError seal(size_t peer);
  // Adapter brackets event/action ingestion; standalone APIs still advance immediately.
  void beginServicePass() { deferred_ = true; }
  void tick();
  uint32_t advancements() const { return advancements_; }
  bool event(const Event &event);
  RecordingStatus status() const;

private:
  CameraManager &cameras_;
  Clock &clock_;
  RecordingPreparation *preparation_ = nullptr;
  RecordingCallback callback_;
  void *context_;
  enum class Stage { Idle, Connect, Command, Confirm, Prepare, Admission, Retiring, Done, Error };
  struct Peer {
    Stage stage = Stage::Idle;
    CameraError error = CameraError::None;
    bool acknowledged = false;
    bool fresh = false;
    uint32_t deadline = 0;
    Token confirm;
    Token retired;
    bool hasRetired = false;
  };
  std::array<Peer, kMaxCameras> peers_;
  RecordingState intent_ = RecordingState::Unknown;
  GroupError error_ = GroupError::None;
  uint32_t generation_ = 0;
  bool syncing_ = false, decideAfterSync_ = false, deferred_ = false;
  uint32_t advancements_ = 0;
  void update();
  void begin(bool query);
  void admit(size_t peer);
  void command(size_t peer);
  void advance();
  void notify();
  void expireConfirm(size_t peer);
};
} // namespace ridesync

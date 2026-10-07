#pragma once
#include "camera_manager.h"
namespace ridesync {
enum class GroupError { None, NoCameras, UnknownState, InvalidIntent, ResyncPending };
struct RecordingPeerStatus {
  bool enabled = false;
  bool ready = false;
  RecordingState observed = RecordingState::Unknown;
  bool pending = false;
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
using RecordingCallback = void (*)(void *, const RecordingStatus &);
class RecordingManager {
public:
  RecordingManager(CameraManager &cameras, Clock &clock, RecordingCallback callback = nullptr,
                   void *context = nullptr);
  GroupError request(RecordingState intent);
  GroupError shortPress();
  GroupError resync();
  void cancel();
  void tick();
  bool event(const Event &event);
  RecordingStatus status() const;

private:
  CameraManager &cameras_;
  Clock &clock_;
  RecordingCallback callback_;
  void *context_;
  enum class Stage { Idle, Connect, Command, Confirm, Done, Error };
  struct Peer {
    Stage stage = Stage::Idle;
    CameraError error = CameraError::None;
    bool acknowledged = false;
    bool fresh = false;
    uint32_t deadline = 0;
  };
  std::array<Peer, kMaxCameras> peers_;
  RecordingState intent_ = RecordingState::Unknown;
  GroupError error_ = GroupError::None;
  uint32_t generation_ = 0;
  bool syncing_ = false, decideAfterSync_ = false;
  void begin(bool query);
  void command(size_t peer);
  void advance();
  void notify();
};
} // namespace ridesync

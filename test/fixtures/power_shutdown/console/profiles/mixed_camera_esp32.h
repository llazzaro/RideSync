#pragma once
#include "bench_application.h"
namespace ridesync {
extern unsigned camera_commands;
struct FakeManager {
  struct Config {
    bool enabled = true;
    int model = 0;
  } config;
  struct State {
    Lifecycle lifecycle = Lifecycle::Ready;
    RecordingState observed = RecordingState::Unknown;
    struct {
      int query = 0, wake = 0, gps = 0, hilight = 0;
    } capabilities;
  } value;
  size_t size() const { return 0; }
  const Config *configuredCamera(size_t) const { return &config; }
  const State *state(size_t) const { return &value; }
  CameraError request(size_t, Operation) const { return CameraError::None; }
};
struct FakeGroup {
  struct Status {
    RecordingState intent = RecordingState::Unknown;
    unsigned enabled = 0, ready = 0, recording = 0, stopped = 0, unknown = 0, pending = 0,
             errors = 0, generation = 0;
    struct {
      unsigned acknowledged = 0, pending = 0, error = 0;
    } peers[4];
  };
  Status status() const { return {}; }
  GroupError resync() const { return GroupError::None; }
  GroupError request(RecordingState) const {
    ++camera_commands;
    return GroupError::None;
  }
};
enum class Hero12RecoveryPhase { Pending, Scanning, Connecting, Observing, Ready };
struct FakeAdapter {
  CameraError requestRecovery(size_t, bool) const { return CameraError::None; }
  unsigned fault(size_t) const { return 0; }
  struct Recovery {
    Hero12RecoveryPhase phase = Hero12RecoveryPhase::Ready;
  };
  Recovery recoveryState(size_t) const { return {}; }
};
struct MixedCameraRuntime {
  FakeManager manager;
  FakeGroup group;
  FakeAdapter adapter;
};
inline MixedCameraRuntime mixedCameraRuntime() { return {}; }
} // namespace ridesync

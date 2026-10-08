#include "recording_manager.h"
namespace ridesync {
namespace {
bool pending(int stage) { return stage >= 1 && stage <= 6; }
bool reached(uint32_t now, uint32_t deadline) { return now - deadline < 0x80000000UL; }
} // namespace
RecordingManager::RecordingManager(CameraManager &m, Clock &c, RecordingCallback cb, void *ctx)
    : cameras_(m), clock_(c), callback_(cb), context_(ctx) {}
bool RecordingManager::attachPreparation(RecordingPreparation &p) {
  if (preparation_ && preparation_ != &p)
    return false;
  preparation_ = &p;
  return true;
}
void RecordingManager::detachPreparation(RecordingPreparation &p) {
  if (preparation_ == &p)
    preparation_ = nullptr;
}
RecordingStatus RecordingManager::status() const {
  RecordingStatus s;
  s.intent = intent_;
  s.error = error_;
  s.generation = generation_;
  for (size_t i = 0; i < cameras_.size(); ++i) {
    const auto &c = *cameras_.state(i);
    auto &p = s.peers[i];
    p.enabled = c.lifecycle != Lifecycle::Disabled;
    if (!p.enabled)
      continue;
    ++s.enabled;
    p.ready = c.lifecycle == Lifecycle::Ready || c.lifecycle == Lifecycle::Operating;
    if (p.ready)
      ++s.ready;
    p.observed = c.observed;
    if (c.has_observation && c.observed == RecordingState::Recording)
      ++s.recording;
    else if (c.has_observation && c.observed == RecordingState::Stopped)
      ++s.stopped;
    else
      ++s.unknown;
    p.pending = pending(static_cast<int>(peers_[i].stage));
    p.terminal_failure = peers_[i].stage == Stage::Error;
    p.acknowledged = peers_[i].acknowledged;
    p.error = peers_[i].error == CameraError::None ? c.error : peers_[i].error;
    if (p.pending)
      ++s.pending;
    if (p.error != CameraError::None)
      ++s.errors;
  }
  return s;
}
void RecordingManager::notify() {
  if (callback_)
    callback_(context_, status());
}
void RecordingManager::cancel() {
  for (size_t i = 0; i < cameras_.size(); ++i) {
    if (preparation_)
      preparation_->retire(i);
    if (cameras_.state(i)->lifecycle != Lifecycle::Disabled) {
      cameras_.cancel(i);
      peers_[i].error = CameraError::Cancelled;
    }
    peers_[i].stage = Stage::Idle;
  }
  intent_ = RecordingState::Unknown;
  syncing_ = decideAfterSync_ = false;
  error_ = GroupError::None;
  ++generation_;
  notify();
}
void RecordingManager::begin(bool query) {
  // This coordinator owns command admission. Retire previous work instead of
  // filling the per-camera FIFO with superseded group requests.
  for (size_t i = 0; i < cameras_.size(); ++i) {
    if (preparation_)
      preparation_->retire(i);
    const auto l = cameras_.state(i)->lifecycle;
    const auto retired = cameras_.state(i)->token;
    if (pending(static_cast<int>(peers_[i].stage)) || l == Lifecycle::Operating ||
        l == Lifecycle::Connecting || l == Lifecycle::Backoff)
      cameras_.cancel(i);
    peers_[i] = Peer{};
    peers_[i].retired = retired;
    peers_[i].hasRetired = true;
  }
  syncing_ = query;
  error_ = GroupError::None;
  ++generation_;
  for (size_t i = 0; i < cameras_.size(); ++i) {
    auto l = cameras_.state(i)->lifecycle;
    if (l == Lifecycle::Disabled)
      continue;
    if (preparation_ && preparation_->retiring(i)) {
      peers_[i].stage = Stage::Retiring;
      peers_[i].deadline = clock_.now() + 140000;
    } else {
      admit(i);
    }
  }
  update();
}
void RecordingManager::admit(size_t i) {
  auto &p = peers_[i];
  if (preparation_ && (syncing_ || intent_ == RecordingState::Recording)) {
    const bool retiring = p.stage == Stage::Retiring;
    const auto e = preparation_->prepare(i);
    p.stage = e == CameraError::None ? Stage::Prepare : Stage::Error;
    p.error = e;
    if (!retiring)
      p.deadline = clock_.now() + 140000;
  } else if (cameras_.state(i)->lifecycle == Lifecycle::Ready) {
    command(i);
  } else {
    p.stage = Stage::Connect;
    const auto e = cameras_.request(i, Operation::Connect);
    if (e != CameraError::None) {
      p.stage = Stage::Error;
      p.error = e;
    }
  }
}
void RecordingManager::command(size_t i) {
  auto &p = peers_[i];
  const auto &c = *cameras_.state(i);
  if (!syncing_ && c.has_observation && c.observed == intent_) {
    p.stage = Stage::Done;
    return;
  }
  if (preparation_ && !preparation_->commandReady(i)) {
    p.stage = Stage::Admission;
    p.deadline = clock_.now() + 2000;
    return;
  }
  p.stage = Stage::Command;
  p.fresh = p.acknowledged = false;
  auto e = cameras_.request(i, syncing_                               ? Operation::Query
                               : intent_ == RecordingState::Recording ? Operation::Start
                                                                      : Operation::Stop);
  if (e != CameraError::None) {
    p.stage = Stage::Error;
    p.error = e;
  }
}
void RecordingManager::expireConfirm(size_t i) {
  auto &p = peers_[i];
  p.retired = p.confirm;
  p.hasRetired = true;
  p.stage = Stage::Error;
  p.error = CameraError::Timeout;
}
GroupError RecordingManager::request(RecordingState intent) {
  if (intent == RecordingState::Unknown)
    return GroupError::InvalidIntent;
  if (status().enabled == 0)
    return GroupError::NoCameras;
  decideAfterSync_ = false;
  intent_ = intent;
  begin(false);
  return GroupError::None;
}
GroupError RecordingManager::resync() {
  if (status().enabled == 0)
    return GroupError::NoCameras;
  decideAfterSync_ = false;
  intent_ = RecordingState::Unknown;
  begin(true);
  return GroupError::None;
}
GroupError RecordingManager::shortPress() {
  const auto s = status();
  if (s.enabled == 0)
    return GroupError::NoCameras;
  if (syncing_)
    return GroupError::ResyncPending;
  if (intent_ != RecordingState::Unknown)
    return request(intent_ == RecordingState::Recording ? RecordingState::Stopped
                                                        : RecordingState::Recording);
  if (s.unknown) {
    decideAfterSync_ = true;
    begin(true);
    return GroupError::None;
  }
  return request(s.recording == s.enabled ? RecordingState::Stopped : RecordingState::Recording);
}
void RecordingManager::update() {
  if (!deferred_) {
    advance();
    notify();
  }
}
void RecordingManager::advance() {
  ++advancements_;
  for (size_t i = 0; i < cameras_.size(); ++i) {
    auto &p = peers_[i];
    const auto &c = *cameras_.state(i);
    if (syncing_ && p.stage == Stage::Done &&
        (c.lifecycle != Lifecycle::Ready || !c.has_observation ||
         c.observed == RecordingState::Unknown)) {
      p.stage = Stage::Error;
      p.error = c.error == CameraError::None ? CameraError::Transport : c.error;
    }
    if (!pending(static_cast<int>(p.stage)))
      continue;
    if (p.stage == Stage::Retiring) {
      if (reached(clock_.now(), p.deadline)) {
        p.stage = Stage::Error;
        p.error = CameraError::Timeout;
      } else if ((c.lifecycle == Lifecycle::Idle || c.lifecycle == Lifecycle::Failed) &&
                 preparation_->released(i)) {
        // The actual Disconnected observation and host lease barrier have both
        // completed. Only now admit replacement work using current generations.
        admit(i);
      }
      continue;
    }
    if (p.stage == Stage::Prepare) {
      if (reached(clock_.now(), p.deadline)) {
        preparation_->retire(i);
        p.stage = Stage::Error;
        p.error = CameraError::Timeout;
        continue;
      }
      const auto result = preparation_->prepared(i);
      if (result.pending)
        continue;
      if (result.error != CameraError::None) {
        p.stage = Stage::Error;
        p.error = result.error;
      } else {
        command(i);
      }
      continue;
    }
    if (c.lifecycle == Lifecycle::Failed || c.lifecycle == Lifecycle::Idle) {
      p.stage = Stage::Error;
      p.error = c.error == CameraError::None ? CameraError::NotConnected : c.error;
    } else if (p.stage == Stage::Admission) {
      if (reached(clock_.now(), p.deadline)) {
        p.stage = Stage::Error;
        p.error = CameraError::Timeout;
      } else if (c.lifecycle == Lifecycle::Ready && preparation_->commandReady(i)) {
        command(i);
      }
    } else if (p.stage == Stage::Connect && c.lifecycle == Lifecycle::Ready)
      command(i);
    else if (p.stage == Stage::Confirm) {
      if (reached(clock_.now(), p.deadline)) {
        expireConfirm(i);
      } else if (p.fresh && c.has_observation && c.observed != RecordingState::Unknown &&
                 (syncing_ || c.observed == intent_))
        p.stage = Stage::Done;
    }
  }
  if (!syncing_ || status().pending)
    return;
  syncing_ = false;
  if (!decideAfterSync_)
    return;
  decideAfterSync_ = false;
  size_t known = 0, recording = 0;
  for (size_t i = 0; i < cameras_.size(); ++i)
    if (peers_[i].stage == Stage::Done) {
      ++known;
      if (cameras_.state(i)->observed == RecordingState::Recording)
        ++recording;
    }
  if (!known) {
    error_ = GroupError::UnknownState;
    return;
  }
  intent_ = recording == known ? RecordingState::Stopped : RecordingState::Recording;
  // Failed resync peers retain explicit errors; successful peers proceed together.
  for (size_t i = 0; i < cameras_.size(); ++i)
    if (peers_[i].stage == Stage::Done)
      command(i);
}
bool RecordingManager::event(const Event &e) {
  if (e.peer < cameras_.size() && peers_[e.peer].stage == Stage::Confirm &&
      reached(clock_.now(), peers_[e.peer].deadline))
    expireConfirm(e.peer);
  const bool connectionEvent =
      e.kind == EventKind::RecordingObserved || e.kind == EventKind::Disconnected;
  const bool retiredResponse = e.peer < cameras_.size() && !connectionEvent &&
                               peers_[e.peer].hasRetired &&
                               e.token.connection == peers_[e.peer].retired.connection &&
                               e.token.operation == peers_[e.peer].retired.operation;
  // A new request can use a confirmed-state shortcut without dispatching an
  // operation. Retire that prior response token here without losing observation.
  if (retiredResponse || !cameras_.event(e)) {
    update();
    return false;
  }
  auto &p = peers_[e.peer];
  if ((p.stage == Stage::Command || p.stage == Stage::Confirm) &&
      (e.kind == EventKind::RecordingObserved || e.kind == EventKind::CommandRecordingObserved))
    p.fresh = true;
  if (p.stage == Stage::Command && e.kind == EventKind::Completed) {
    p.acknowledged = true;
    p.stage = Stage::Confirm;
    p.confirm = e.token;
    p.deadline = clock_.now() + 1000;
  }
  update();
  return true;
}
void RecordingManager::tick() {
  cameras_.tick();
  deferred_ = false;
  update();
}
} // namespace ridesync

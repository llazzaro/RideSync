#include "profiles/mixed_camera.h"
#include <algorithm>
namespace ridesync {
namespace {
class UnsupportedRadio final : public WakeRadio {
public:
  WakeSubmit begin(const WakeOperation &, const insta360::WakeEncoding &, uint32_t,
                   uint32_t) override {
    return WakeSubmit::Unsupported;
  }
  void cancel(const WakeOperation &) override {}
  WakeRadioResult poll(const WakeOperation &, uint32_t) override { return {}; }
};
WakeRadio &unsupportedRadio() {
  static UnsupportedRadio r;
  return r;
}
bool reached(uint32_t now, uint32_t deadline) { return now - deadline < 0x80000000u; }
bool matches(const CameraConfig &camera, const BondIdentity &id) {
  if (!id.verified || camera.identifier.size() != 17 ||
      (id.type == IdentityType::Public
           ? camera.address_type != AddressType::Public
           : id.type != IdentityType::RandomStatic || camera.address_type != AddressType::Random))
    return false;
  auto hex = [](char c) -> int {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                  : -1;
  };
  for (size_t i = 0; i < 6; ++i) {
    int hi = hex(camera.identifier[3 * i]), lo = hex(camera.identifier[3 * i + 1]);
    if (hi < 0 || lo < 0 || uint8_t(hi * 16 + lo) != id.address[5 - i])
      return false;
  }
  return true;
}
} // namespace
MixedCameraAdapter::MixedCameraAdapter(BleHost &host, X5PeripheralPort &port, Clock &clock,
                                       WakeRadio *radio)
    : clock_(clock), wake_recovery_(*this),
      wake_(radio ? *radio : unsupportedRadio(), &wake_recovery_), x5_(port, clock),
      one_(host, clock), go_(host, clock), hero_(host, clock) {}
void MixedCameraAdapter::attach(CameraManager &m, RecordingManager &g) {
  if (manager_)
    return;
  manager_ = &m;
  group_ = &g;
  x5_.attach(m);
  x5_.attachRecording(g);
  one_.attach(m);
  go_.attach(m);
  hero_.attach(m);
  attachGroup(g);
}
void MixedCameraAdapter::attachGroup(RecordingManager &g) {
  group_ = &g;
  one_.attachGroup(g);
  go_.attachGroup(g);
  hero_.attachGroup(g);
}
void MixedCameraAdapter::detachGroup(const RecordingManager *g) {
  if (group_ == g) {
    group_ = nullptr;
    hero_.detachGroup(g);
  }
}
RetryPolicy MixedCameraAdapter::managerPolicy() {
  RetryPolicy p;
  p.timeout_ms = 120000;
  p.max_attempts = 1;
  return p;
}
bool MixedCameraAdapter::configure(const SourceConfig &s, const MixedCameraQualifications &q) {
  if (configured_ || !manager_ || !validate(s).ok() || manager_->size() != s.count)
    return false;
  source_ = s;
  wake_config_ = q.x5_wake;
  wake_policy_ = q.x5_wake_policy;
  if (wake_config_.enabled &&
      (s.count == 0 || !wake_config_.source_qualified || s.cameras[0].model != CameraModel::X5 ||
       insta360::encodeWake(wake_config_.profile, wake_config_.identifier.data(), 6).error !=
           insta360::WakeCodecError::None ||
       !wake_policy_.total_ms || wake_policy_.total_ms >= 0x80000000u || !wake_policy_.slice_ms ||
       wake_policy_.slice_ms > wake_policy_.total_ms || s.cameras[0].wake_identifier.size() != 6 ||
       !std::equal(wake_config_.identifier.begin(), wake_config_.identifier.end(),
                   s.cameras[0].wake_identifier.begin())))
    return false;
  for (size_t i = 0; i < s.count; ++i) {
    if (!s.cameras[i].enabled)
      continue;
    const auto *stored = manager_->configuredCamera(i);
    if (!stored || stored->model != s.cameras[i].model ||
        stored->identifier != s.cameras[i].identifier ||
        stored->address_type != s.cameras[i].address_type)
      return false;
    bool ok = false;
    switch (s.cameras[i].model) {
    case CameraModel::X5:
      ok = i == 0 && matches(s.cameras[i], q.x5.identity) && x5_.configure(q.x5);
      break;
    case CameraModel::ONE_RS:
      ok = matches(s.cameras[i], q.one_rs[i].identity) && one_.configurePeer(i, q.one_rs[i]);
      break;
    case CameraModel::GO3S:
      ok = matches(s.cameras[i], q.go3s[i].identity) && go_.configurePeer(i, q.go3s[i]);
      break;
    case CameraModel::HERO12_BLACK:
      ok = matches(s.cameras[i], q.hero12[i].identity) && hero_.configurePeer(i, q.hero12[i]);
      break;
    default:
      break;
    }
    if (!ok)
      return false;
    if (s.cameras[i].gps_telemetry &&
        (s.cameras[i].model != CameraModel::ONE_RS || !q.gps[i].enabled ||
         !q.gps[i].source_qualified || !one_.configureGps(i, s.cameras[i], q.gps[i])))
      return false;
  }
  return configured_ = true;
}
bool MixedCameraAdapter::start(bool enabled, bool qualified) {
  if (!configured_ || !enabled || !qualified || stopped_)
    return false;
  bool one = false, go = false, hero = false;
  for (size_t i = 0; i < source_.count; ++i)
    if (source_.cameras[i].enabled) {
      one |= source_.cameras[i].model == CameraModel::ONE_RS;
      go |= source_.cameras[i].model == CameraModel::GO3S;
      hero |= source_.cameras[i].model == CameraModel::HERO12_BLACK;
    }
  // X5 registration/restore was configured before any central starts the host.
  return (!one || one_.start(true, true)) && (!go || go_.start(true, true)) &&
         (!hero || hero_.start(true, true));
}
CameraTransport *MixedCameraAdapter::route(size_t i) {
  if (i >= source_.count || !source_.cameras[i].enabled)
    return nullptr;
  switch (source_.cameras[i].model) {
  case CameraModel::X5:
    return &x5_;
  case CameraModel::ONE_RS:
    return &one_;
  case CameraModel::GO3S:
    return &go_;
  case CameraModel::HERO12_BLACK:
    return &hero_;
  default:
    return nullptr;
  }
}
bool MixedCameraAdapter::begin(size_t i, const CameraConfig &c, Operation op, Token t) {
  auto *r = route(i);
  return !stopped_ && r && c.model == source_.cameras[i].model && r->begin(i, c, op, t);
}
void MixedCameraAdapter::cancel(size_t i, Token t) {
  auto *r = route(i);
  if (r)
    r->cancel(i, t);
}
void MixedCameraAdapter::close(size_t i, Token t) {
  auto *r = route(i);
  if (r)
    r->close(i, t);
}
void MixedCameraAdapter::service(CameraServiceAction *action) {
  if (servicing_ || !configured_ || !manager_)
    return;
  servicing_ = true;
  if (group_)
    group_->beginServicePass();
  x5_.service();
  wake_.service(clock_.now());
  one_.service(false);
  go_.service(false);
  hero_.service(nullptr, false);
  if (action)
    action->beforeAdvance();
  uint8_t mask = 0;
  for (size_t i = 0; i < source_.count; ++i) {
    auto *s = manager_->state(i);
    if (s->lifecycle == Lifecycle::Operating || s->lifecycle == Lifecycle::Connecting ||
        (group_ && group_->status().peers[i].pending))
      mask |= uint8_t(1u << i);
  }
  one_.serviceGps(mask);
  if (group_)
    group_->tick();
  else
    manager_->tick();
  progress_.observe(progress_.generation() + 1,
                    stopped_ ? DeviceHealth::Missing : DeviceHealth::Ok);
  if (stopped_ && canDestroy())
    progress_.finished();
  servicing_ = false;
}
void MixedCameraAdapter::stop() {
  stopped_ = true;
  wake_.cancel(0);
  one_.stop();
  go_.stop();
  hero_.stop();
  if (manager_)
    for (size_t i = 0; i < source_.count; ++i)
      if (source_.cameras[i].enabled && source_.cameras[i].model == CameraModel::X5)
        x5_.close(i, manager_->state(i)->token);
}
void MixedCameraAdapter::offer(const RecordTimestamp &t, const ModemSnapshot &s) {
  one_.forwardingTime(t);
  one_.forwarding().offer(t, s);
}
bool MixedCameraAdapter::canDestroy() const {
  return wake_.status(0).released && !x5_.connected() && x5_.connectionReleased() &&
         one_.canDestroy() && one_.forwarding().canRelease() && go_.canDestroy() &&
         hero_.canDestroy();
}
bool MixedCameraAdapter::commandReady(uint8_t i) const {
  if (i >= source_.count)
    return false;
  switch (source_.cameras[i].model) {
  case CameraModel::X5:
    return x5_.ready(Operation::Start);
  case CameraModel::ONE_RS:
    return one_.commandReady(i);
  case CameraModel::GO3S:
    return go_.commandReady(i);
  case CameraModel::HERO12_BLACK:
    return hero_.commandReady(i);
  default:
    return false;
  }
}
CameraError MixedCameraAdapter::requestRecovery(uint8_t i, bool recording,
                                                Hero12PowerCondition condition) {
  if (i >= source_.count || !source_.cameras[i].enabled || stopped_)
    return CameraError::Disabled;
  if (source_.cameras[i].model == CameraModel::HERO12_BLACK)
    return hero_.requestRecovery(i, recording, condition);
  if (recording)
    return CameraError::Unsupported; // preparation never sends a shutter.
  recovery_[i] = true;
  if (source_.cameras[i].model == CameraModel::X5 && wakeSupported(i)) {
    const auto status = wake_.status(0);
    if (wake_id_ && !status.released)
      return CameraError::Busy;
    if (wake_id_ >= UINT32_MAX - 1)
      return CameraError::InvalidPolicy;
    WakeOperation op;
    op.peer = 0;
    op.id = ++wake_id_;
    op.generation = 1;
    return wake_.request(op, wake_config_, wake_policy_, clock_.now());
  }
  auto state = manager_->state(i);
  if (state->lifecycle == Lifecycle::Ready)
    return CameraError::None;
  if (state->lifecycle == Lifecycle::Connecting)
    return CameraError::None;
  return manager_->request(i, Operation::Connect);
}
CameraError MixedCameraAdapter::cancelRecovery(uint8_t i) {
  if (i >= source_.count)
    return CameraError::InvalidPeer;
  if (source_.cameras[i].model == CameraModel::HERO12_BLACK)
    return hero_.cancelRecovery(i);
  if (i == 0 && wakeSupported(i))
    wake_.cancel(0);
  if (recovery_[i] && manager_->state(i)->lifecycle == Lifecycle::Connecting)
    manager_->cancel(i);
  recovery_[i] = false;
  return CameraError::None;
}
Hero12RecoveryState MixedCameraAdapter::recoveryState(uint8_t i) const {
  if (i >= source_.count)
    return {};
  if (source_.cameras[i].model == CameraModel::HERO12_BLACK)
    return hero_.recoveryState(i);
  Hero12RecoveryState s;
  if (!recovery_[i])
    return s;
  if (i == 0 && wakeSupported(i)) {
    const auto w = wake_.status(0);
    switch (w.phase) {
    case WakePhase::Queued:
    case WakePhase::Advertising:
    case WakePhase::Releasing:
      s.phase = Hero12RecoveryPhase::Pending;
      break;
    case WakePhase::Recovering:
      s.phase = Hero12RecoveryPhase::Observing;
      break;
    case WakePhase::Ready:
      s.phase = Hero12RecoveryPhase::Ready;
      break;
    case WakePhase::Timeout:
      s.phase = Hero12RecoveryPhase::Timeout;
      break;
    case WakePhase::Unsupported:
      s.phase = Hero12RecoveryPhase::Unsupported;
      break;
    case WakePhase::Cancelled:
      s.phase = Hero12RecoveryPhase::Cancelled;
      break;
    default:
      s.phase = Hero12RecoveryPhase::Failed;
      break;
    }
    return s;
  }
  auto *c = manager_->state(i);
  s.phase = c->lifecycle == Lifecycle::Ready
                ? (commandReady(i) ? Hero12RecoveryPhase::Ready : Hero12RecoveryPhase::Observing)
            : c->lifecycle == Lifecycle::Connecting ? Hero12RecoveryPhase::Connecting
            : c->error == CameraError::Timeout      ? Hero12RecoveryPhase::Timeout
                                                    : Hero12RecoveryPhase::Failed;
  return s;
}
bool MixedCameraAdapter::recoveryReady(uint8_t i) const { return commandReady(i); }
bool MixedCameraAdapter::linkRetiring(uint8_t i) const {
  if (i >= source_.count)
    return false;
  if (source_.cameras[i].model == CameraModel::HERO12_BLACK)
    return hero_.linkRetiring(i);
  if (source_.cameras[i].model == CameraModel::X5)
    return !wake_.status(0).released || (!x5_.connected() && !x5_.connectionReleased());
  return source_.cameras[i].model == CameraModel::ONE_RS ? one_.linkRetiring(i)
                                                         : go_.linkRetiring(i);
}
bool MixedCameraAdapter::linkReleased(uint8_t i) const {
  if (i >= source_.count)
    return false;
  if (source_.cameras[i].model == CameraModel::HERO12_BLACK)
    return hero_.linkReleased(i);
  if (source_.cameras[i].model == CameraModel::X5)
    return wake_.status(0).released && x5_.connectionReleased();
  return source_.cameras[i].model == CameraModel::ONE_RS ? one_.linkReleased(i)
                                                         : go_.linkReleased(i);
}
Hero12Fault MixedCameraAdapter::fault(uint8_t i) const {
  if (i >= source_.count)
    return Hero12Fault::Qualification;
  if (source_.cameras[i].model == CameraModel::HERO12_BLACK)
    return hero_.fault(i);
  bool error = source_.cameras[i].model == CameraModel::X5       ? x5_.failure() != X5Failure::None
               : source_.cameras[i].model == CameraModel::ONE_RS ? one_.fault(i) != OneRsFault::None
                                                                 : go_.fault(i) != Go3sFault::None;
  return error ? Hero12Fault::Transport : Hero12Fault::None;
}
} // namespace ridesync

namespace ridesync {
RecordingState MixedCameraAdapter::X5Recovery::currentObserved(uint8_t i) const {
  return i == 0 && (!used_ || (result_.terminal && result_.released)) &&
                 owner_.x5_.ready(Operation::Start)
             ? owner_.x5_.observed()
             : RecordingState::Unknown;
}
CameraError MixedCameraAdapter::X5Recovery::begin(const WakeOperation &op, uint32_t deadline) {
  if (op.peer != 0 || !owner_.configured_ || owner_.stopped_)
    return CameraError::Disabled;
  if (reached(owner_.clock_.now(), deadline))
    return CameraError::Timeout;
  if (used_ && (!result_.terminal || !result_.released))
    return CameraError::Busy;
  if (owner_.x5_.connected() || owner_.x5_.active() || !owner_.x5_.connectionReleased())
    return CameraError::Busy;
  auto e = owner_.manager_->request(0, Operation::Connect);
  if (e != CameraError::None)
    return e;
  result_ = {};
  result_.operation = op;
  deadline_ = deadline;
  used_ = true;
  return CameraError::None;
}
void MixedCameraAdapter::X5Recovery::fail(CameraError e) {
  if (result_.terminal)
    return;
  result_.terminal = true;
  result_.error = e;
  result_.fresh = false;
  result_.observed = RecordingState::Unknown;
  owner_.manager_->cancel(0);
}
WakeRecoveryResult MixedCameraAdapter::X5Recovery::poll(const WakeOperation &op, uint32_t now) {
  if (!used_ || !sameWakeOperation(op, result_.operation))
    return {};
  if (!result_.terminal) {
    auto *s = owner_.manager_->state(0);
    if (reached(now, deadline_))
      fail(CameraError::Timeout);
    else if (owner_.stopped_)
      fail(CameraError::Disabled);
    else if (s->error != CameraError::None)
      fail(s->error);
    else if (owner_.x5_.ready(Operation::Start)) {
      result_.terminal = result_.released = result_.fresh = true;
      result_.observed = owner_.x5_.observed();
    }
  }
  if (result_.terminal && result_.error != CameraError::None)
    result_.released = owner_.x5_.connectionReleased();
  return result_;
}
void MixedCameraAdapter::X5Recovery::cancel(const WakeOperation &op) {
  if (used_ && sameWakeOperation(op, result_.operation))
    fail(CameraError::Cancelled);
}
} // namespace ridesync

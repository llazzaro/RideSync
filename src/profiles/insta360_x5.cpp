#include "profiles/insta360_x5.h"
namespace ridesync {
namespace {
bool due(uint32_t now, uint32_t deadline) { return now - deadline < 0x80000000UL; }
bool identityEqual(const BondIdentity &a, const BondIdentity &b) {
  return a.verified && b.verified && a.type == b.type && a.address == b.address;
}
bool validQualification(const X5Qualification &q) {
  if (!q.enabled || !q.identity.verified ||
      (q.identity.type != IdentityType::Public && q.identity.type != IdentityType::RandomStatic) ||
      q.store.qualification_record == 0 || q.store.qualification_record == UINT32_MAX ||
      q.display.profile != insta360::Ce80DisplayProfile::X5CapturedDisplayV1 ||
      q.firmware_size == 0 || q.firmware_size >= q.firmware.size())
    return false;
  bool nonzero = false;
  for (auto b : q.identity.address)
    nonzero = nonzero || b != 0;
  if (!nonzero ||
      (q.identity.type == IdentityType::RandomStatic && (q.identity.address[5] & 0xc0) != 0xc0))
    return false;
  for (size_t i = 0; i < q.firmware.size(); ++i)
    if (i < q.firmware_size ? (q.firmware[i] < 0x21 || q.firmware[i] > 0x7e) : q.firmware[i] != 0)
      return false;
  return true;
}
} // namespace
RetryPolicy X5Adapter::managerPolicy() {
  RetryPolicy p;
  p.timeout_ms = 15000;
  p.backoff_ms = 200;
  p.max_attempts = 1;
  return p;
}
bool X5Adapter::configure(const X5Qualification &q) {
  if (active_ || connected_ || (token_.connection && !port_.released(token_.connection))) {
    failure_ = X5Failure::Busy;
    return false;
  }
  configured_ = false;
  qualification_ = {};
  if (!validQualification(q)) {
    failure_ = q.enabled ? X5Failure::Qualification : X5Failure::Disabled;
    return false;
  }
  if (!port_.configure(q)) {
    failure_ = X5Failure::Host;
    return false;
  }
  qualification_ = q;
  configured_ = true;
  failure_ = X5Failure::None;
  return true;
}
bool X5Adapter::attach(CameraManager &m) {
  if (manager_)
    return false;
  manager_ = &m;
  return true;
}
bool X5Adapter::fresh() const { return has_observation_ && clock_.now() - observed_ms_ <= 5000; }
RecordingState X5Adapter::observed() const {
  return connected_ && subscribed_ && fresh() ? recording_ : RecordingState::Unknown;
}
bool X5Adapter::ready(Operation op) const {
  if (!configured_ || !manager_ || active_)
    return false;
  if (op == Operation::Connect)
    return !connected_ && (!token_.connection || port_.released(token_.connection));
  if (!connected_ || !subscribed_)
    return false;
  if (op == Operation::Query)
    return true;
  if (op != Operation::Start && op != Operation::Stop)
    return false;
  return video_ && fresh() && recording_ != RecordingState::Unknown;
}
bool X5Adapter::begin(size_t peer, const CameraConfig &camera, Operation op, Token token) {
  if (peer != 0 || camera.family != CameraFamily::Insta360 || camera.model != CameraModel::X5 ||
      !camera.enabled || !token.valid() || !token.hasRoom() || !token.connection ||
      !token.operation) {
    failure_ = X5Failure::Qualification;
    return false;
  }
  if (!ready(op)) {
    failure_ = active_                       ? X5Failure::Busy
               : !configured_                ? X5Failure::Qualification
               : !connected_ || !subscribed_ ? X5Failure::Subscription
               : !fresh()                    ? X5Failure::Stale
               : !video_                     ? X5Failure::WrongMode
                                             : X5Failure::UnknownState;
    return false;
  }
  if (op != Operation::Connect && token.connection != token_.connection) {
    failure_ = X5Failure::Qualification;
    return false;
  }
  token_ = token;
  operation_ = op;
  active_ = true;
  send_pending_ = send_consumed_ = returned_ = complete_pending_ = false;
  cutoff_ = 0;
  failure_ = X5Failure::None;
  deadline_ = clock_.now() + (op == Operation::Connect ? 15000 : 5000);
  if (op == Operation::Connect) {
    last_sequence_ = observation_sequence_ = 0;
    handle_ = kBleNoHandle;
    invalidate(true, false);
    if (!port_.connect(token_, deadline_)) {
      active_ = false;
      failure_ = X5Failure::Host;
      return false;
    }
  } else if (op == Operation::Query) {
    complete_pending_ = fresh();
  } else if (desiredObserved()) {
    complete_pending_ = true;
  } else {
    send_pending_ = true;
  }
  return true;
}
void X5Adapter::publishObservation() {
  if (!manager_ || !connected_)
    return;
  Event e(0, token_.connection, EventKind::RecordingObserved);
  e.recording = observed();
  manager_->event(e);
}
void X5Adapter::invalidate(bool mode, bool publish) {
  has_observation_ = false;
  recording_ = RecordingState::Unknown;
  if (mode)
    video_ = false;
  if (publish)
    publishObservation();
}
void X5Adapter::cancel(size_t peer, Token token) {
  if (peer != 0 || token.connection != token_.connection || token.operation != token_.operation)
    return;
  active_ = send_pending_ = complete_pending_ = false;
  port_.cancel(token);
  invalidate(true, false);
  if (failure_ == X5Failure::None)
    failure_ = X5Failure::Cancelled;
}
void X5Adapter::close(size_t peer, Token token) {
  if (peer != 0 || token.connection != token_.connection)
    return;
  active_ = send_pending_ = complete_pending_ = false;
  invalidate(true, false);
  connected_ = subscribed_ = false;
  port_.close(token.connection);
}
void X5Adapter::fail(X5Failure reason) {
  failure_ = reason;
  invalidate();
  if (!active_)
    return;
  // Callback into the manager is allowed only from this serialized service.
  active_ = false;
  send_pending_ = complete_pending_ = false;
  Event e(0, token_, EventKind::Failed);
  manager_->event(e); // max_attempts=1 retires and closes this connection.
}
void X5Adapter::complete() {
  if (!active_)
    return;
  Event e(0, token_, EventKind::Completed);
  e.capabilities.start = e.capabilities.stop = e.capabilities.query = CapabilityState::Supported;
  e.capabilities.wake = e.capabilities.gps = CapabilityState::Unsupported;
  active_ = send_pending_ = complete_pending_ = false;
  failure_ = X5Failure::None;
  manager_->event(e);
}
bool X5Adapter::desiredObserved() const {
  if (!fresh())
    return false;
  if (operation_ == Operation::Query)
    return recording_ != RecordingState::Unknown;
  return video_ && ((operation_ == Operation::Start && recording_ == RecordingState::Recording) ||
                    (operation_ == Operation::Stop && recording_ == RecordingState::Stopped));
}
void X5Adapter::display(const X5Input &e) {
  if (!connected_ || !subscribed_ || e.handle != handle_)
    return;
  if (e.size > e.bytes.size() || clock_.now() - e.received_ms > 5000) {
    invalidate();
    return;
  }
  const auto result = insta360::decodeCe80Display(
      qualification_.display, insta360::Ce80Direction::CameraToRemote, e.bytes.data(), e.size);
  if (result.error != insta360::Ce80DisplayError::None) {
    invalidate();
    return;
  }
  if (result.kind == insta360::Ce80DisplayKind::Unknown ||
      result.kind == insta360::Ce80DisplayKind::Remaining)
    return;
  // A timer cannot rediscover mode after old evidence expired or was revoked.
  if (has_observation_ && !fresh())
    video_ = false;
  if (result.kind == insta360::Ce80DisplayKind::Settings)
    video_ = result.mode == insta360::Ce80CameraMode::Video;
  recording_ = result.recording;
  observed_ms_ = e.received_ms;
  observation_sequence_ = e.sequence;
  has_observation_ = true;
  publishObservation();
  if (active_ && operation_ == Operation::Query && desiredObserved())
    complete();
  else if (active_ && send_consumed_ && returned_ && observation_sequence_ > cutoff_ &&
           desiredObserved())
    complete();
}
void X5Adapter::input(const X5Input &e) {
  if (e.connection != token_.connection)
    return;
  if (e.kind == X5InputKind::SendReturned) {
    if (!active_ || !send_consumed_ || e.operation != token_.operation || e.handle != handle_ ||
        returned_)
      return;
    if (e.status) {
      fail(X5Failure::Transport);
      return;
    }
    returned_ = true;
    cutoff_ = e.sequence;
    if (observation_sequence_ > cutoff_ && desiredObserved())
      complete();
    return;
  }
  if (!e.sequence || e.sequence <= last_sequence_)
    return;
  last_sequence_ = e.sequence;
  switch (e.kind) {
  case X5InputKind::Connected:
    if (!active_ || operation_ != Operation::Connect || connected_ || e.status ||
        e.handle == kBleNoHandle || !identityEqual(e.identity, qualification_.identity)) {
      fail(X5Failure::Qualification);
      return;
    }
    handle_ = e.handle;
    connected_ = true;
    break;
  case X5InputKind::Subscribed:
    if (connected_ && e.handle == handle_ && !e.status) {
      subscribed_ = true;
      if (active_ && operation_ == Operation::Connect)
        complete();
    } else {
      fail(X5Failure::Subscription);
    }
    break;
  case X5InputKind::Unsubscribed:
    if (connected_ && e.handle == handle_) {
      subscribed_ = false;
      invalidate();
      if (active_)
        fail(X5Failure::Subscription);
    }
    break;
  case X5InputKind::Display:
    display(e);
    break;
  case X5InputKind::Disconnected:
    if (connected_ && e.handle == handle_) {
      invalidate();
      active_ = send_pending_ = complete_pending_ = false;
      connected_ = subscribed_ = false;
      failure_ = X5Failure::Transport;
      manager_->event(Event(0, token_.connection, EventKind::Disconnected));
    } else if (active_) {
      fail(X5Failure::Transport);
    }
    break;
  case X5InputKind::Fault:
    fail(X5Failure::Transport);
    break;
  default:
    break;
  }
}
void X5Adapter::service() {
  if (!configured_ || !manager_)
    return;
  port_.service(clock_.now());
  if (token_.connection && port_.takeLoss(token_.connection)) {
    invalidate();
    if (active_)
      fail(X5Failure::LostInput);
  }
  if (active_ && due(clock_.now(), deadline_))
    fail(X5Failure::Timeout);
  if (has_observation_ && !fresh())
    invalidate();
  for (unsigned n = 0; n < 32 && port_.poll(input_); ++n)
    input(input_);
  if (active_ && complete_pending_) {
    if (desiredObserved())
      complete();
    else
      fail(X5Failure::UnknownState);
  }
  if (active_ && send_pending_) {
    // A mode/state change queued before actual admission revokes the request.
    if (!connected_ || !subscribed_ || !video_ || !fresh() || desiredObserved()) {
      fail(X5Failure::UnknownState);
      return;
    }
    send_pending_ = false;
    send_consumed_ = true;
    X5ShutterRequest request;
    request.token = token_;
    request.handle = handle_;
    request.deadline_ms = deadline_;
    request.observation_sequence = observation_sequence_;
    invalidate(false); // Keep Video epoch; no old observation can complete this send.
    if (!port_.notify(request))
      fail(X5Failure::Transport);
  }
}
} // namespace ridesync

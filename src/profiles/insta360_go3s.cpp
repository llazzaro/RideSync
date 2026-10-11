#include "profiles/insta360_go3s.h"
#include "profiles/central_service.h"
#include "profiles/insta360_be80_profile.h"
#include "profiles/qualification.h"
#include "recording_manager.h"
#include <cstring>
namespace ridesync {
Go3sAdapter::Go3sAdapter(BleHost &host, Clock &clock)
    : host_(host), clock_(clock), central_(host, *this, &progress_) {}
bool Go3sAdapter::expectedResponse(uint8_t i, const go3s::Decoded &d) const {
  const auto &p = peers_[i];
  return p.step != Step::None && p.step != Step::AwaitSync && p.step != Step::Nudge &&
         p.step != Step::Sync && d.sequence == p.expected_sequence;
}
bool Go3sAdapter::acknowledged(uint8_t i) const {
  const auto &p = peers_[i];
  return p.step != Step::None && p.step != Step::AwaitSync && p.att_done && p.camera_done;
}
bool Go3sAdapter::heartbeatInterruptible(uint8_t i) const {
  const auto &p = peers_[i];
  return p.ready && !p.sealed && p.step == Step::Heartbeat && p.resume == Step::None;
}
bool Go3sAdapter::reached(uint32_t now, uint32_t deadline) { return now - deadline < 0x80000000UL; }
bool Go3sAdapter::valid(const Go3sQualification &q) {
  const char fw[] = "8.0.4.11";
  return q.source_qualified && q.firmware_size == sizeof(fw) - 1 &&
         std::memcmp(q.firmware.data(), fw, sizeof(fw) - 1) == 0 &&
         qualification::resolvedIdentity(q.identity) &&
         go3s::encode(go3s::Command::Authorize, 1, q.authorization_id.data(), q.authorization_size)
             .size;
}

BleProfileSpec Go3sAdapter::profileSpec() { return insta360Be80ProfileSpec(); }
RetryPolicy Go3sAdapter::managerPolicy() {
  RetryPolicy p;
  p.timeout_ms = 60000;
  p.max_attempts = 1;
  return p;
}
bool Go3sAdapter::configurePeer(uint8_t i, const Go3sQualification &q) {
  if (i >= kBlePeers || peers_[i].connecting || peers_[i].ready || disconnected_[i] ||
      (central_.phase(i) != BlePhase::Empty && central_.phase(i) != BlePhase::Closed))
    return false;
  peers_[i].qualification = q;
  peers_[i].qualified = valid(q);
  peers_[i].fault = peers_[i].qualified ? Go3sFault::None : Go3sFault::Qualification;
  return peers_[i].qualified;
}
bool Go3sAdapter::start(bool enabled, bool qualified) {
  if (started_)
    return false;
  started_ = true;
  if (!enabled || !qualified || !manager_)
    return false;
  enabled_ = central_.begin(true, true, clock_.now());
  return enabled_;
}
bool Go3sAdapter::commandReady(uint8_t i) const {
  return i < kBlePeers && peers_[i].ready && !peers_[i].sealed && peers_[i].step == Step::None &&
         central_.admissionOpen(i);
}
Go3sFault Go3sAdapter::fault(uint8_t i) const {
  return i < kBlePeers ? peers_[i].fault : Go3sFault::Qualification;
}
bool Go3sAdapter::beginConnection(uint8_t i, Token t) {
  auto &p = peers_[i];

  if (p.ready || p.connecting || disconnected_[i] || !t.connection ||
      !central_.connect(i, t.connection, p.qualification.identity, profileSpec()))
    return false;
  const auto qualification = p.qualification;
  p = Peer{};
  p.qualification = qualification;
  p.qualified = p.connecting = true;
  p.token = t;
  p.fault = Go3sFault::None;
  p.deadline = clock_.now() + 60000;
  return true;
}
bool Go3sAdapter::beginCommand(uint8_t i, Operation op, Token t) {
  auto &p = peers_[i];
  const bool heartbeat = heartbeatInterruptible(i);
  if ((!commandReady(i) && !heartbeat) || p.token.connection != t.connection ||
      (!qualification::recordingOperation(op)))
    return false;
  p.token = t;
  p.operation = op;
  p.deadline = clock_.now() + 10000;
  if (heartbeat) {
    p.resume = op == Operation::Start ? Step::Video : Step::Stop;
    return true;
  }
  return send(i, op == Operation::Start ? Step::Video : Step::Stop);
}
bool Go3sAdapter::begin(size_t index, const CameraConfig &camera, Operation op, Token t) {
  if (!enabled_ || !manager_ || index >= kBlePeers || camera.family != CameraFamily::Insta360 ||
      camera.model != CameraModel::GO3S || !qualification::usableToken(t))
    return false;
  const auto i = static_cast<uint8_t>(index);
  auto &p = peers_[i];
  if (!p.qualified || !valid(p.qualification))
    return false;
  if (op == Operation::Connect)
    return beginConnection(i, t);
  return beginCommand(i, op, t);
}
void Go3sAdapter::retire(uint8_t i, Go3sFault fault) {
  auto &p = peers_[i];
  if (p.sealed)
    return;
  p.sealed = true;
  p.ready = p.connecting = false;
  p.step = Step::None;
  p.fault = fault;
  p.receiver.reset();
  disconnected_[i] = true;
  central_.disconnect(i);
}
void Go3sAdapter::cancel(size_t i, Token t) {
  if (i < kBlePeers && peers_[i].token.connection == t.connection &&
      peers_[i].token.operation == t.operation)
    retire(i, Go3sFault::Cancelled);
}
void Go3sAdapter::close(size_t i, Token t) {
  if (i < kBlePeers && peers_[i].token.connection == t.connection)
    retire(i, Go3sFault::Cancelled);
}
void Go3sAdapter::stop() {
  enabled_ = false;
  for (uint8_t i = 0; i < kBlePeers; ++i)
    if (peers_[i].connecting || peers_[i].ready)
      retire(i, Go3sFault::Cancelled);
  central_.stop();
}
go3s::Packet Go3sAdapter::packetForStep(uint8_t i, Step step) {
  auto &p = peers_[i];
  if (step == Step::Nudge) {
    go3s::Packet packet;
    packet.size = 1;
    packet.bytes[0] = 0;
    return packet;
  }
  if (step == Step::Sync)
    return go3s::encodeSync();
  if (p.next_sequence == 254) {
    retire(i, Go3sFault::SequenceExhausted);
    return {};
  }
  go3s::Command command = go3s::Command::Authorize;
  switch (step) {
  case Step::Video:
    command = go3s::Command::VideoMode;
    break;
  case Step::Start:
    command = go3s::Command::StartVideo;
    break;
  case Step::Stop:
    command = go3s::Command::Stop;
    break;
  default:
    break;
  }
  return go3s::encode(command, p.next_sequence + 1, p.qualification.authorization_id.data(),
                      p.qualification.authorization_size);
}
void Go3sAdapter::admitPacket(uint8_t i, Step step) {
  auto &p = peers_[i];
  // Copied admission owns the sequence, even if the later SDK return is uncertain.
  if (step != Step::Sync && step != Step::Nudge)
    p.expected_sequence = ++p.next_sequence;
  p.step = step;
  p.att_done = false;
  p.camera_done = step == Step::Sync || step == Step::Nudge;
  p.response_deadline = clock_.now() + kResponseMs;
  if (step == Step::Heartbeat)
    p.deadline = p.response_deadline;
}
bool Go3sAdapter::send(uint8_t i, Step step) {
  auto &p = peers_[i];
  if (p.sealed || p.step != Step::None || !central_.admissionOpen(i))
    return false;
  const auto packet = packetForStep(i, step);
  if (p.sealed)
    return false; // Sequence exhaustion already selected the fault.
  if (!packet.size || host_.mtu(p.handle) < packet.size + 3) {
    retire(i, Go3sFault::Mtu);
    return false;
  }
  if (!central_.write(i, 0, packet.bytes.data(), packet.size, clock_.now())) {
    retire(i, Go3sFault::Transport);
    return false;
  }
  admitPacket(i, step);
  return true;
}
void Go3sAdapter::complete(uint8_t i) {
  auto &p = peers_[i];
  if (event_count_ == events_.size()) {
    retire(i, Go3sFault::Protocol);
    return;
  }
  auto &e = events_[event_count_++];
  e.peer = i;
  e.token = p.token;
  e.capabilities = Capabilities{};
  if (p.connecting) {
    e.capabilities.start = e.capabilities.stop = CapabilityState::Supported;
    e.capabilities.query = e.capabilities.wake = e.capabilities.gps = CapabilityState::Unsupported;
    p.ready = true;
    p.connecting = false;
  }
  p.keepalive_due = clock_.now() + kKeepAliveMs;
}
void Go3sAdapter::advanceAcknowledged(uint8_t i) {
  auto &p = peers_[i];
  const auto previous = p.step;
  p.step = Step::None;
  switch (previous) {
  case Step::Nudge:
    p.step = Step::AwaitSync;
    p.att_done = p.camera_done = false;
    p.response_deadline = clock_.now() + 1000;
    if (p.sync_seen)
      advance(i);
    break;
  case Step::Sync:
    send(i, Step::Authorize);
    break;
  case Step::Video:
    send(i, Step::Start);
    break;
  case Step::Heartbeat: {
    p.keepalive_due = clock_.now() + kKeepAliveMs;
    const auto resume = p.resume;
    p.resume = Step::None;
    if (resume != Step::None)
      send(i, resume);
    break;
  }
  default:
    complete(i);
    break;
  }
}
void Go3sAdapter::advance(uint8_t i) {
  auto &p = peers_[i];
  if (p.sealed)
    return;
  if (p.step == Step::AwaitSync && p.sync_seen) {
    p.step = Step::None;
    send(i, Step::Sync);
  } else if (acknowledged(i)) {
    advanceAcknowledged(i);
  } else if (p.ready && p.step == Step::None && reached(clock_.now(), p.keepalive_due))
    send(i, Step::Heartbeat);
}
bool Go3sAdapter::cameraResponse(uint8_t i, const go3s::Decoded &d) {
  auto &p = peers_[i];
  if (d.kind == go3s::Kind::Invalid) {
    retire(i, Go3sFault::Protocol);
    return false;
  }
  if (d.kind == go3s::Kind::Sync) {
    if (p.step != Step::AwaitSync && p.step != Step::Nudge && p.step != Step::Sync) {
      retire(i, Go3sFault::Protocol); // No surprise reauthorization/reset replay.
      return false;
    }
    p.sync_seen = true;
  }
  if (d.kind == go3s::Kind::Response && expectedResponse(i, d)) {
    if (d.status != 200) {
      retire(i, Go3sFault::Rejected);
      return false;
    }
    p.camera_done = true;
  }
  return true;
}
void Go3sAdapter::notification(uint8_t i, const BleResult &r) {
  auto &p = peers_[i];

  if (r.endpoint != 1 || !r.event.size || r.event.size > r.event.bytes.size() ||
      (p.receiver.pending() && reached(clock_.now(), p.fragment_started + kFragmentMs))) {
    retire(i, Go3sFault::Protocol);
    return;
  }
  for (size_t n = 0; n < r.event.size; ++n) {
    if (!p.receiver.pending())
      p.fragment_started = clock_.now();
    const auto d = p.receiver.push(r.event.bytes[n]);
    if (!cameraResponse(i, d))
      return;
  }
}
void Go3sAdapter::writeComplete(uint8_t i, const BleResult &r) {
  auto &p = peers_[i];

  if (r.endpoint != 0 || p.step == Step::None || r.event.status) {
    retire(i, Go3sFault::Transport);
    return;
  }
  p.att_done = true;
}
bool Go3sAdapter::transactionExpired(uint8_t i, bool allow_unprompted_sync) const {
  const auto &p = peers_[i];
  return (p.connecting || p.step != Step::None) &&
         (reached(clock_.now(), p.deadline) ||
          (p.step != Step::None && reached(clock_.now(), p.response_deadline) &&
           !(allow_unprompted_sync && p.step == Step::AwaitSync && !p.nudged)));
}
void Go3sAdapter::result(const BleResult &r) {
  const auto i = r.event.peer;
  if (i >= kBlePeers || r.event.generation != peers_[i].token.connection)
    return;
  auto &p = peers_[i];
  if (r.kind == BleResultKind::Retired || r.kind == BleResultKind::LinkClosed) {
    retire(i, Go3sFault::Transport);
    return;
  }
  if (p.sealed)
    return;
  if (transactionExpired(i, true)) {
    retire(i, Go3sFault::Timeout);
    return;
  }
  if (r.kind == BleResultKind::TransportReady) {
    p.handle = r.event.connection;
    p.step = Step::AwaitSync;
    p.response_deadline = clock_.now() + 2000;
  } else if (r.kind == BleResultKind::WriteComplete) {
    writeComplete(i, r);
  } else if (r.kind == BleResultKind::Notification) {
    notification(i, r);
  }
}
void Go3sAdapter::drain() {
  for (uint8_t i = 0; i < kBlePeers; ++i)
    if (disconnected_[i]) {
      disconnected_[i] = false;
      const Event e(i, peers_[i].token.connection, EventKind::Disconnected);
      if (group_)
        group_->event(e);
      else
        manager_->event(e);
    }
  const auto count = event_count_;
  event_count_ = 0;
  for (size_t n = 0; n < count; ++n) {
    const auto e = events_[n];
    if (peers_[e.peer].sealed)
      continue;
    Event completed(e.peer, e.token, EventKind::Completed);
    completed.capabilities = e.capabilities;
    if (group_)
      group_->event(completed);
    else
      manager_->event(completed);
  }
}
void Go3sAdapter::servicePeer(uint8_t i) {
  auto &p = peers_[i];
  if (p.sealed)
    return;
  if (p.step == Step::AwaitSync && !p.nudged && reached(clock_.now(), p.response_deadline) &&
      !reached(clock_.now(), p.deadline)) {
    // Source-backed single zero-byte prompt when subscription-time SYNC was missed.
    p.nudged = true;
    p.step = Step::None;
    send(i, Step::Nudge);
  } else if (transactionExpired(i, false))
    retire(i, Go3sFault::Timeout);
  else if (p.receiver.pending() && reached(clock_.now(), p.fragment_started + kFragmentMs))
    retire(i, Go3sFault::Protocol);
  else
    advance(i);
}
void Go3sAdapter::service(bool advance_manager) {
  serviceCentralProfile(
      servicing_, central_, clock_, manager_, group_, advance_manager,
      [this](uint8_t peer) { servicePeer(peer); }, [this]() { drain(); });
}
} // namespace ridesync

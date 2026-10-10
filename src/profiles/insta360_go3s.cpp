#include "profiles/insta360_go3s.h"
#include "recording_manager.h"
#include <cstring>
namespace ridesync {
Go3sAdapter::Go3sAdapter(BleHost &host, Clock &clock)
    : host_(host), clock_(clock), central_(host, *this, &progress_) {}
bool Go3sAdapter::reached(uint32_t now, uint32_t deadline) { return now - deadline < 0x80000000UL; }
bool Go3sAdapter::valid(const Go3sQualification &q) {
  const char fw[] = "8.0.4.11";
  if (!q.source_qualified || q.firmware_size != sizeof(fw) - 1 ||
      std::memcmp(q.firmware.data(), fw, sizeof(fw) - 1) || !q.identity.verified ||
      (q.identity.type != IdentityType::Public && q.identity.type != IdentityType::RandomStatic))
    return false;
  bool nonzero = false, all_ff = true;
  for (auto b : q.identity.address) {
    nonzero |= b != 0;
    all_ff &= b == 255;
  }
  return nonzero && !all_ff &&
         (q.identity.type != IdentityType::RandomStatic ||
          (q.identity.address[5] & 0xc0) == 0xc0) &&
         go3s::encode(go3s::Command::Authorize, 1, q.authorization_id.data(), q.authorization_size)
             .size;
}
BleProfileSpec Go3sAdapter::profileSpec() {
  BleProfileSpec s;
  s.service_count = 1;
  s.services[0] = BleUuid::shortUuid(0xbe80);
  s.endpoint_count = 2;
  s.endpoints[0].uuid = BleUuid::shortUuid(0xbe81);
  s.endpoints[0].properties = 8; // ATT write response, not a camera ACK.
  s.endpoints[1].uuid = BleUuid::shortUuid(0xbe82);
  s.endpoints[1].properties = 0x10;
  s.endpoints[1].subscribe = 1;
  return s;
}
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
bool Go3sAdapter::begin(size_t index, const CameraConfig &camera, Operation op, Token t) {
  if (!enabled_ || !manager_ || index >= kBlePeers || camera.family != CameraFamily::Insta360 ||
      camera.model != CameraModel::GO3S || !t.valid() || !t.hasRoom())
    return false;
  const auto i = static_cast<uint8_t>(index);
  auto &p = peers_[i];
  if (!p.qualified || !valid(p.qualification))
    return false;
  if (op == Operation::Connect) {
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
  const bool heartbeat =
      p.ready && !p.sealed && p.step == Step::Heartbeat && p.resume == Step::None;
  if ((!commandReady(i) && !heartbeat) || p.token.connection != t.connection ||
      (op != Operation::Start && op != Operation::Stop))
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
bool Go3sAdapter::send(uint8_t i, Step step) {
  auto &p = peers_[i];
  if (p.sealed || p.step != Step::None || !central_.admissionOpen(i))
    return false;
  go3s::Packet packet;
  if (step == Step::Nudge) {
    packet.size = 1;
    packet.bytes[0] = 0;
  } else if (step == Step::Sync)
    packet = go3s::encodeSync();
  else {
    if (p.next_sequence == 254) {
      retire(i, Go3sFault::SequenceExhausted);
      return false;
    }
    const uint8_t seq = p.next_sequence + 1;
    go3s::Command c = go3s::Command::Authorize;
    if (step == Step::Video)
      c = go3s::Command::VideoMode;
    else if (step == Step::Start)
      c = go3s::Command::StartVideo;
    else if (step == Step::Stop)
      c = go3s::Command::Stop;
    packet = go3s::encode(c, seq, p.qualification.authorization_id.data(),
                          p.qualification.authorization_size);
  }
  if (!packet.size || host_.mtu(p.handle) < packet.size + 3) {
    retire(i, Go3sFault::Mtu);
    return false;
  }
  if (!central_.write(i, 0, packet.bytes.data(), packet.size, clock_.now())) {
    retire(i, Go3sFault::Transport);
    return false;
  }
  // Copied admission owns the sequence, even if the later SDK return is uncertain.
  if (step != Step::Sync && step != Step::Nudge)
    p.expected_sequence = ++p.next_sequence;
  p.step = step;
  p.att_done = false;
  p.camera_done = step == Step::Sync || step == Step::Nudge;
  p.response_deadline = clock_.now() + kResponseMs;
  if (step == Step::Heartbeat)
    p.deadline = p.response_deadline;
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
void Go3sAdapter::advance(uint8_t i) {
  auto &p = peers_[i];
  if (p.sealed)
    return;
  if (p.step == Step::AwaitSync && p.sync_seen) {
    p.step = Step::None;
    send(i, Step::Sync);
  } else if (p.step != Step::None && p.step != Step::AwaitSync && p.att_done && p.camera_done) {
    const auto previous = p.step;
    p.step = Step::None;
    if (previous == Step::Nudge) {
      p.step = Step::AwaitSync;
      p.att_done = p.camera_done = false;
      p.response_deadline = clock_.now() + 1000;
      if (p.sync_seen)
        advance(i);
    } else if (previous == Step::Sync)
      send(i, Step::Authorize);
    else if (previous == Step::Video)
      send(i, Step::Start);
    else if (previous == Step::Heartbeat) {
      p.keepalive_due = clock_.now() + kKeepAliveMs;
      const auto resume = p.resume;
      p.resume = Step::None;
      if (resume != Step::None)
        send(i, resume);
    } else
      complete(i);
  } else if (p.ready && p.step == Step::None && reached(clock_.now(), p.keepalive_due))
    send(i, Step::Heartbeat);
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
  if ((p.connecting || p.step != Step::None) &&
      (reached(clock_.now(), p.deadline) ||
       (p.step != Step::None && reached(clock_.now(), p.response_deadline) &&
        !(p.step == Step::AwaitSync && !p.nudged)))) {
    retire(i, Go3sFault::Timeout);
    return;
  }
  if (r.kind == BleResultKind::TransportReady) {
    p.handle = r.event.connection;
    p.step = Step::AwaitSync;
    p.response_deadline = clock_.now() + 2000;
  } else if (r.kind == BleResultKind::WriteComplete) {
    if (r.endpoint != 0 || p.step == Step::None || r.event.status) {
      retire(i, Go3sFault::Transport);
      return;
    }
    p.att_done = true;
  } else if (r.kind == BleResultKind::Notification) {
    if (r.endpoint != 1 || !r.event.size || r.event.size > r.event.bytes.size() ||
        (p.receiver.pending() && reached(clock_.now(), p.fragment_started + kFragmentMs))) {
      retire(i, Go3sFault::Protocol);
      return;
    }
    for (size_t n = 0; n < r.event.size; ++n) {
      if (!p.receiver.pending())
        p.fragment_started = clock_.now();
      const auto d = p.receiver.push(r.event.bytes[n]);
      if (d.kind == go3s::Kind::Invalid) {
        retire(i, Go3sFault::Protocol);
        return;
      }
      if (d.kind == go3s::Kind::Sync) {
        if (p.step != Step::AwaitSync && p.step != Step::Nudge && p.step != Step::Sync) {
          retire(i, Go3sFault::Protocol); // No surprise reauthorization/reset replay.
          return;
        }
        p.sync_seen = true;
      }
      if (d.kind == go3s::Kind::Response && p.step != Step::None && p.step != Step::AwaitSync &&
          p.step != Step::Nudge && p.step != Step::Sync && d.sequence == p.expected_sequence) {
        if (d.status != 200) {
          retire(i, Go3sFault::Rejected);
          return;
        }
        p.camera_done = true;
      }
    }
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
void Go3sAdapter::service(bool advance_manager) {
  if (servicing_ || !manager_)
    return;
  servicing_ = true;
  central_.service(clock_.now());
  for (uint8_t i = 0; i < kBlePeers; ++i) {
    auto &p = peers_[i];
    if (p.sealed)
      continue;
    if (p.step == Step::AwaitSync && !p.nudged && reached(clock_.now(), p.response_deadline) &&
        !reached(clock_.now(), p.deadline)) {
      // Source-backed single zero-byte prompt when subscription-time SYNC was missed.
      p.nudged = true;
      p.step = Step::None;
      send(i, Step::Nudge);
    } else if ((p.connecting || p.step != Step::None) &&
               (reached(clock_.now(), p.deadline) ||
                (p.step != Step::None && reached(clock_.now(), p.response_deadline))))
      retire(i, Go3sFault::Timeout);
    else if (p.receiver.pending() && reached(clock_.now(), p.fragment_started + kFragmentMs))
      retire(i, Go3sFault::Protocol);
    else
      advance(i);
  }
  drain();
  if (advance_manager && group_)
    group_->tick();
  else if (advance_manager)
    manager_->tick();
  servicing_ = false;
}
} // namespace ridesync

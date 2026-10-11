#include "profiles/dji_camera.h"
#include "profiles/central_service.h"
#include "profiles/qualification.h"
#include "recording_manager.h"
namespace ridesync {

DjiAdapter::DjiAdapter(BleHost &host, Clock &clock)
    : host_(host), clock_(clock), central_(host, *this, &progress_) {}
bool DjiAdapter::reached(uint32_t now, uint32_t deadline) { return now - deadline < 0x80000000UL; }
uint32_t DjiAdapter::deviceId(CameraModel m) {
  switch (m) {
  case CameraModel::DJI_ACTION4:
    return dji::modelWireId(4);
  case CameraModel::DJI_ACTION5_PRO:
    return dji::modelWireId(5);
  case CameraModel::DJI_ACTION6:
    return dji::modelWireId(6);
  case CameraModel::DJI_OSMO360:
    return dji::modelWireId(360);
  default:
    return 0;
  }
}
bool DjiAdapter::valid(const DjiQualification &q) {
  if (!q.source_qualified || !deviceId(q.model) || !q.controller_mac_verified ||
      !q.unencrypted_protocol_reviewed || !q.controller_id || q.connection_challenge > 9999 ||
      !q.firmware_size || q.firmware_size > q.firmware.size())
    return false;
  return qualification::resolvedIdentity(q.identity) &&
         qualification::addressBytes(q.controller_mac) &&
         qualification::printable(q.firmware.data(), q.firmware_size, 0x20);
}

BleProfileSpec DjiAdapter::profileSpec() {
  BleProfileSpec s;
  s.service_count = 1;
  s.services[0] = BleUuid::shortUuid(0xfff0);
  s.endpoint_count = 2;
  s.endpoints[0].uuid = BleUuid::shortUuid(0xfff5);
  s.endpoints[0].properties = 4;
  s.require_bonded = false;
  s.endpoints[1].uuid = BleUuid::shortUuid(0xfff4);
  s.endpoints[1].properties = 0x10;
  s.endpoints[1].subscribe = 1;
  return s;
}
RetryPolicy DjiAdapter::managerPolicy() {
  RetryPolicy p;
  p.timeout_ms = 60000;
  p.max_attempts = 1;
  return p;
}
bool DjiAdapter::configurePeer(uint8_t i, const DjiQualification &q) {
  if (i >= kBlePeers || peers_[i].connecting || peers_[i].ready || disconnected_[i] ||
      (central_.phase(i) != BlePhase::Empty && central_.phase(i) != BlePhase::Closed))
    return false;
  peers_[i].qualification = q;
  peers_[i].qualified = valid(q);
  peers_[i].fault = peers_[i].qualified ? DjiFault::None : DjiFault::Qualification;
  return peers_[i].qualified;
}
bool DjiAdapter::start(bool enabled, bool qualified) {
  if (started_)
    return false;
  started_ = true;
  if (!enabled || !qualified || !manager_)
    return false;
  enabled_ = central_.begin(true, true, clock_.now());
  return enabled_;
}
bool DjiAdapter::commandReady(uint8_t i) const {
  return i < kBlePeers && peers_[i].ready && !peers_[i].sealed && peers_[i].step == Step::None &&
         peers_[i].deferred == Step::None && central_.admissionOpen(i);
}
bool DjiAdapter::videoReady(uint8_t i) const {
  const auto &p = peers_[i];
  return p.has_status && p.observed != RecordingState::Unknown &&
         !reached(clock_.now(), p.status_seen + kStatusMs) && p.power == 0 &&
         (p.camera_status == 0 || p.camera_status == 1 || p.camera_status == 3) &&
         (p.mode == 1 || (p.qualification.model == CameraModel::DJI_OSMO360 &&
                          (p.mode == 0x38 || p.mode == 0x41)));
}
DjiFault DjiAdapter::fault(uint8_t i) const {
  return i < kBlePeers ? peers_[i].fault : DjiFault::Qualification;
}
bool DjiAdapter::beginConnection(uint8_t i, Token t) {
  auto &p = peers_[i];

  if (p.ready || p.connecting || disconnected_[i] || !t.connection ||
      !central_.connect(i, t.connection, p.qualification.identity, profileSpec()))
    return false;
  auto q = p.qualification;
  p = Peer{};
  p.qualification = q;
  p.qualified = p.connecting = true;
  p.token = t;
  p.fault = DjiFault::None;
  p.deadline = clock_.now() + 60000;
  return true;
}
bool DjiAdapter::beginCommand(uint8_t i, Operation op, Token t) {
  auto &p = peers_[i];
  // Polls are finite background work. Admission waits for their local write completion;
  // their responses never acquire a subsequently admitted manager token.
  if (!commandReady(i) || p.token.connection != t.connection ||
      (op != Operation::Start && op != Operation::Stop && op != Operation::Query) ||
      ((op == Operation::Start || op == Operation::Stop) && !videoReady(i)))
    return false;
  p.token = t;
  p.operation = op;
  p.deadline = clock_.now() + kResponseMs;
  return send(i, op == Operation::Start  ? Step::Start
                 : op == Operation::Stop ? Step::Stop
                                         : Step::Query);
}
bool DjiAdapter::begin(size_t index, const CameraConfig &camera, Operation op, Token t) {
  if (!enabled_ || !manager_ || index >= kBlePeers || camera.family != CameraFamily::DJI ||
      !t.valid() || !t.hasRoom())
    return false;
  const auto i = static_cast<uint8_t>(index);
  auto &p = peers_[i];
  if (!p.qualified || !valid(p.qualification) || camera.model != p.qualification.model)
    return false;
  if (op == Operation::Connect)
    return beginConnection(i, t);
  return beginCommand(i, op, t);
}
void DjiAdapter::retire(uint8_t i, DjiFault f) {
  auto &p = peers_[i];
  if (p.sealed)
    return;
  p.sealed = true;
  p.ready = p.connecting = false;
  p.step = Step::None;
  p.fault = f;
  p.receiver.reset();
  p.has_status = false;
  disconnected_[i] = true;
  central_.disconnect(i);
}
void DjiAdapter::cancel(size_t i, Token t) {
  if (i < kBlePeers && peers_[i].token.connection == t.connection &&
      peers_[i].token.operation == t.operation)
    retire(i, DjiFault::Cancelled);
}
void DjiAdapter::close(size_t i, Token t) {
  if (i < kBlePeers && peers_[i].token.connection == t.connection)
    retire(i, DjiFault::Cancelled);
}
void DjiAdapter::stop() {
  enabled_ = false;
  for (uint8_t i = 0; i < kBlePeers; ++i)
    if (peers_[i].connecting || peers_[i].ready)
      retire(i, DjiFault::Cancelled);
  central_.stop();
}
dji::Packet DjiAdapter::packetForStep(uint8_t i, Step step, uint16_t seq) const {
  const auto &p = peers_[i];
  dji::Packet packet;
  switch (step) {
  case Step::Pair:
    packet = dji::encodeConnection(
        seq, p.qualification.controller_id, p.qualification.controller_mac,
        p.qualification.already_paired ? 0 : 1, p.qualification.connection_challenge);
    break;
  case Step::Authorize:
    packet = dji::encodeAuthorizationResponse(seq, p.qualification.controller_id, 0, i + 1);
    break;
  case Step::Subscribe:
    packet = dji::encodeStatusSubscription(seq, true);
    break;
  case Step::Query:
  case Step::Poll:
    packet = dji::encodeStatusSubscription(seq, false);
    break;
  case Step::Start:
  case Step::Stop:
    packet = dji::encodeRecord(seq, deviceId(p.qualification.model), step == Step::Start);
    break;
  default:
    return {};
  }
  return packet;
}
void DjiAdapter::admitPacket(uint8_t i, Step step, uint16_t seq) {
  auto &p = peers_[i];
  if (step != Step::Authorize)
    p.next_sequence = seq;
  p.expected_sequence = seq;
  p.step = step;
  p.att_done = false;
  // Status subscriptions are CMD_NO_RESPONSE; decoded push proves state.
  p.camera_done = step == Step::Authorize || step == Step::Subscribe || step == Step::Poll;
  p.response_deadline = clock_.now() + (step == Step::Pair ? kPairMs : kResponseMs);
  if (step == Step::Poll)
    p.deadline = p.response_deadline;
}
bool DjiAdapter::send(uint8_t i, Step step) {
  auto &p = peers_[i];
  if (p.sealed || p.step != Step::None || !central_.admissionOpen(i))
    return false;
  if (step != Step::Authorize && p.next_sequence == UINT16_MAX) {
    retire(i, DjiFault::SequenceExhausted);
    return false;
  }
  auto seq =
      step == Step::Authorize ? p.grant_sequence : static_cast<uint16_t>(p.next_sequence + 1);
  const auto packet = packetForStep(i, step, seq);
  const auto mtu = host_.mtu(p.handle);
  if (mtu == kBleMtuReserved) {
    p.deferred = step;
    return true;
  }
  p.deferred = Step::None;
  if (!packet.size || mtu < packet.size + 3) {
    retire(i, DjiFault::Mtu);
    return false;
  }
  if (!central_.writeWithoutResponse(i, 0, packet.bytes.data(), packet.size, clock_.now())) {
    retire(i, DjiFault::Transport);
    return false;
  }
  admitPacket(i, step, seq);
  return true;
}
void DjiAdapter::complete(uint8_t i) {
  auto &p = peers_[i];
  if (event_count_ == events_.size()) {
    retire(i, DjiFault::Protocol);
    return;
  }
  auto &e = events_[event_count_++];
  e = Pending{};
  e.peer = i;
  e.token = p.token;
  if (p.connecting) {
    e.capabilities.start = e.capabilities.stop = e.capabilities.query = CapabilityState::Supported;
    e.capabilities.wake = e.capabilities.gps = CapabilityState::Unsupported;
    p.connecting = false;
    p.ready = true;
    // Subscription data may beat local write completion. Publish the accepted
    // connection before its stored camera observation (manager rejects state
    // while Connecting).
    if (p.has_status) {
      if (event_count_ == events_.size()) {
        retire(i, DjiFault::Protocol);
        return;
      }
      auto &observed = events_[event_count_++];
      observed = Pending{};
      observed.peer = i;
      observed.token.connection = p.token.connection;
      observed.kind = EventKind::RecordingObserved;
      observed.recording = p.observed;
    }
  }
  p.poll_due = clock_.now() + kPollMs;
}
void DjiAdapter::advanceAcknowledged(uint8_t i) {
  auto &p = peers_[i];
  auto step = p.step;
  p.step = Step::None;
  if (step == Step::Pair)
    send(i, Step::Authorize);
  else if (step == Step::Authorize)
    send(i, Step::Subscribe);
  else if (step == Step::Subscribe)
    complete(i);
  else if (step == Step::Poll)
    p.poll_due = clock_.now() + kPollMs;
  else
    complete(i);
}
void DjiAdapter::advance(uint8_t i) {
  auto &p = peers_[i];
  if (p.sealed)
    return;
  if (p.deferred != Step::None) {
    send(i, p.deferred);
    return;
  }
  if (p.step != Step::None && p.att_done && p.camera_done && central_.admissionOpen(i) &&
      (p.step != Step::Pair || p.grant)) {
    advanceAcknowledged(i);
  } else if (p.ready && p.step == Step::None && reached(clock_.now(), p.poll_due))
    send(i, Step::Poll);
}
void DjiAdapter::observe(uint8_t i, const dji::Decoded &d) {
  auto &p = peers_[i];
  // Camera push sequence is independent of our request sequence. Accept only
  // forward serial numbers within this link; duplicates cannot extend freshness.
  if (p.have_status_sequence && (d.sequence == p.status_sequence ||
                                 static_cast<uint16_t>(d.sequence - p.status_sequence) >= 0x8000))
    return;
  p.have_status_sequence = true;
  p.status_sequence = d.sequence;
  p.observed = d.recording;
  p.mode = d.camera_mode;
  p.camera_status = d.camera_status;
  p.power = d.power_mode;
  p.status_seen = clock_.now();
  p.has_status = true;
  if (p.connecting)
    return;
  if (event_count_ == events_.size()) {
    retire(i, DjiFault::Protocol);
    return;
  }
  auto &e = events_[event_count_++];
  e = Pending{};
  e.peer = i;
  e.token.connection = p.token.connection;
  e.kind = EventKind::RecordingObserved;
  e.recording = d.recording;
  if (p.step == Step::Query)
    p.camera_done = true;
}
bool DjiAdapter::awaitingResponse(const Peer &p, dji::Kind kind) {
  if (kind == dji::Kind::ConnectionResponse)
    return p.step == Step::Pair;
  return p.step == Step::Start || p.step == Step::Stop;
}
bool DjiAdapter::acceptsStatus(const Peer &p) {
  return p.ready || p.step == Step::Subscribe || p.step == Step::Query || p.step == Step::Poll;
}
bool DjiAdapter::cameraResponse(uint8_t i, const dji::Decoded &d) {
  auto &p = peers_[i];
  switch (d.kind) {
  case dji::Kind::Invalid:
    retire(i, DjiFault::Protocol);
    return false;
  case dji::Kind::ConnectionRequest:
    return connectionRequest(i, d);
  case dji::Kind::ConnectionResponse:
  case dji::Kind::RecordResponse:
    if (awaitingResponse(p, d.kind) && d.sequence == p.expected_sequence)
      return d.kind == dji::Kind::ConnectionResponse ? connectionResponse(i, d)
                                                     : recordResponse(i, d);
    break;
  case dji::Kind::Status:
    if (acceptsStatus(p))
      observe(i, d);
    break;
  default:
    break;
  }
  return true;
}
void DjiAdapter::notification(uint8_t i, const BleResult &r) {
  auto &p = peers_[i];

  if (r.endpoint != 1 || !r.event.size || r.event.size > r.event.bytes.size() ||
      (p.receiver.pending() && reached(clock_.now(), p.fragment_started + kFragmentMs))) {
    retire(i, DjiFault::Protocol);
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
void DjiAdapter::writeComplete(uint8_t i, const BleResult &r) {
  auto &p = peers_[i];

  if (r.endpoint != 0 || p.step == Step::None || r.event.status) {
    retire(i, DjiFault::Transport);
    return;
  }
  p.att_done = true;
}
bool DjiAdapter::connectionRequest(uint8_t i, const dji::Decoded &d) {
  auto &p = peers_[i];

  if (p.step != Step::Pair) {
    retire(i, DjiFault::Protocol);
    return false;
  }
  if (d.device_id != deviceId(p.qualification.model)) {
    retire(i, DjiFault::Qualification);
    return false;
  }
  if (d.verify_mode != 2) {
    retire(i, DjiFault::Protocol);
    return false;
  }
  if (d.verify_data != 0) {
    retire(i, DjiFault::Rejected);
    return false;
  }
  if (p.grant && p.grant_sequence != d.sequence) {
    retire(i, DjiFault::Protocol);
    return false;
  }
  p.grant = true;
  // The official helper also accepts the approved active request directly
  // when the camera omits its preliminary response. This explicit grant
  // proves authorization, independently of local write acceptance.
  p.camera_done = true;
  p.grant_sequence = d.sequence;

  return true;
}
bool DjiAdapter::connectionResponse(uint8_t i, const dji::Decoded &d) {
  auto &p = peers_[i];

  if (d.device_id != deviceId(p.qualification.model)) {
    retire(i, DjiFault::Qualification);
    return false;
  }
  if (d.return_code) {
    retire(i, DjiFault::Rejected);
    return false;
  }
  p.camera_done = true;

  return true;
}
bool DjiAdapter::recordResponse(uint8_t i, const dji::Decoded &d) {
  auto &p = peers_[i];

  if (d.return_code) {
    retire(i, DjiFault::Rejected);
    return false;
  }
  p.camera_done = true;

  return true;
}
bool DjiAdapter::transactionExpired(uint8_t i) const {
  const auto &p = peers_[i];
  return (p.connecting || p.step != Step::None || p.deferred != Step::None) &&
         (reached(clock_.now(), p.deadline) ||
          (p.step != Step::None && reached(clock_.now(), p.response_deadline)));
}
void DjiAdapter::result(const BleResult &r) {
  const auto i = r.event.peer;
  if (i >= kBlePeers || r.event.generation != peers_[i].token.connection)
    return;
  auto &p = peers_[i];
  if (r.kind == BleResultKind::Retired || r.kind == BleResultKind::LinkClosed) {
    retire(i, DjiFault::Transport);
    return;
  }
  if (p.sealed)
    return;
  if (transactionExpired(i)) {
    retire(i, DjiFault::Timeout);
    return;
  }
  if (r.kind == BleResultKind::TransportReady) {
    p.handle = r.event.connection;
    send(i, Step::Pair);
  } else if (r.kind == BleResultKind::WriteComplete) {
    writeComplete(i, r);
  } else if (r.kind == BleResultKind::Notification) {
    notification(i, r);
  }
}
void DjiAdapter::drain() {
  for (uint8_t i = 0; i < kBlePeers; ++i)
    if (disconnected_[i]) {
      disconnected_[i] = false;
      Event e(i, peers_[i].token.connection, EventKind::Disconnected);
      if (group_)
        group_->event(e);
      else
        manager_->event(e);
    }
  const auto count = event_count_;
  event_count_ = 0;
  for (size_t n = 0; n < count; ++n) {
    const auto pending = events_[n];
    if (peers_[pending.peer].sealed)
      continue;
    Event e(pending.peer, pending.token, pending.kind);
    e.capabilities = pending.capabilities;
    e.recording = pending.recording;
    if (group_)
      group_->event(e);
    else
      manager_->event(e);
  }
}
void DjiAdapter::servicePeer(uint8_t i) {
  auto &p = peers_[i];
  if (p.sealed)
    return;
  if (transactionExpired(i))
    retire(i, DjiFault::Timeout);
  else if (p.receiver.pending() && reached(clock_.now(), p.fragment_started + kFragmentMs))
    retire(i, DjiFault::Protocol);
  else {
    if (p.has_status && reached(clock_.now(), p.status_seen + kStatusMs)) {
      p.has_status = false;
      if (event_count_ == events_.size())
        retire(i, DjiFault::Protocol);
      else {
        auto &e = events_[event_count_++];
        e = Pending{};
        e.peer = i;
        e.token.connection = p.token.connection;
        e.kind = EventKind::RecordingObserved;
      }
    }
    advance(i);
  }
}
void DjiAdapter::service(bool advance_manager) {
  serviceCentralProfile(
      servicing_, central_, clock_, manager_, group_, advance_manager,
      [this](uint8_t peer) { servicePeer(peer); }, [this]() { drain(); });
}
} // namespace ridesync

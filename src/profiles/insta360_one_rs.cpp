#include "profiles/insta360_one_rs.h"
#include "protocol/insta360_be80_codec.h"
#include "recording_manager.h"
namespace ridesync {
OneRsAdapter::OneRsAdapter(BleHost &h, Clock &c)
    : host_(h), clock_(c), central_(h, *this, &progress_) {}
bool OneRsAdapter::reached(uint32_t now, uint32_t deadline) {
  return now - deadline < 0x80000000UL;
}
bool OneRsAdapter::valid(const OneRsQualification &q) {
  if (!q.source_qualified || !q.core_one_rs || !q.ordinary_360_lens || !q.video_mode_declared ||
      !q.firmware_size || q.firmware_size > q.firmware.size() || !q.identity.verified ||
      (q.identity.type != IdentityType::Public && q.identity.type != IdentityType::RandomStatic))
    return false;
  for (uint8_t n = 0; n < q.firmware_size; ++n)
    if (q.firmware[n] < 0x21 || q.firmware[n] > 0x7e)
      return false;
  bool nonzero = false, all_ff = true;
  for (auto b : q.identity.address) {
    nonzero |= b != 0;
    all_ff &= b == 255;
  }
  return nonzero && !all_ff &&
         (q.identity.type != IdentityType::RandomStatic || (q.identity.address[5] & 0xc0) == 0xc0);
}
bool OneRsAdapter::matches(const CameraConfig &c, const BondIdentity &id) {
  if (!c.enabled || c.family != CameraFamily::Insta360 || c.model != CameraModel::ONE_RS ||
      c.identifier.size() != 17 ||
      (id.type == IdentityType::Public ? c.address_type != AddressType::Public
                                       : c.address_type != AddressType::Random))
    return false;
  auto hex = [](char b) -> int {
    if (b >= '0' && b <= '9')
      return b - '0';
    if (b >= 'A' && b <= 'F')
      return b - 'A' + 10;
    if (b >= 'a' && b <= 'f')
      return b - 'a' + 10;
    return -1;
  };
  for (size_t n = 0; n < 6; ++n) {
    const int hi = hex(c.identifier[n * 3]), lo = hex(c.identifier[n * 3 + 1]);
    if (hi < 0 || lo < 0 || (n != 5 && c.identifier[n * 3 + 2] != ':') ||
        id.address[5 - n] != static_cast<uint8_t>(hi * 16 + lo))
      return false;
  }
  return true;
}
BleProfileSpec OneRsAdapter::profileSpec() {
  BleProfileSpec s;
  s.service_count = 1;
  s.services[0] = BleUuid::shortUuid(0xbe80);
  s.endpoint_count = 2;
  s.endpoints[0].uuid = BleUuid::shortUuid(0xbe81);
  s.endpoints[0].properties = 8;
  s.endpoints[1].uuid = BleUuid::shortUuid(0xbe82);
  s.endpoints[1].properties = 0x10;
  s.endpoints[1].subscribe = 1;
  return s;
}
RetryPolicy OneRsAdapter::managerPolicy() {
  RetryPolicy p;
  p.timeout_ms = kConnectMs;
  p.max_attempts = 1;
  return p;
}
bool OneRsAdapter::configurePeer(uint8_t i, const OneRsQualification &q) {
  if (i >= kBlePeers || peers_[i].connecting || peers_[i].ready || peers_[i].pending ||
      disconnected_[i] ||
      (central_.phase(i) != BlePhase::Empty && central_.phase(i) != BlePhase::Closed))
    return false;
  peers_[i].qualification = q;
  peers_[i].qualified = valid(q);
  peers_[i].fault = peers_[i].qualified ? OneRsFault::None : OneRsFault::Qualification;
  return peers_[i].qualified;
}
bool OneRsAdapter::start(bool enabled, bool source_qualified) {
  if (started_)
    return false;
  started_ = true;
  if (!enabled || !source_qualified || !manager_)
    return false;
  enabled_ = central_.begin(true, true, clock_.now());
  return enabled_;
}
bool OneRsAdapter::commandReady(uint8_t i) const {
  return i < kBlePeers && peers_[i].ready && !peers_[i].sealed && !peers_[i].pending &&
         central_.admissionOpen(i);
}
OneRsFault OneRsAdapter::fault(uint8_t i) const {
  return i < kBlePeers ? peers_[i].fault : OneRsFault::Qualification;
}
bool OneRsAdapter::begin(size_t index, const CameraConfig &camera, Operation op, Token t) {
  if (!enabled_ || !manager_ || index >= kBlePeers || !t.valid() || !t.hasRoom())
    return false;
  const auto i = static_cast<uint8_t>(index);
  auto &p = peers_[i];
  if (!p.qualified || !valid(p.qualification) || !matches(camera, p.qualification.identity))
    return false;
  if (op == Operation::Connect) {
    if (p.ready || p.connecting || disconnected_[i] || !t.connection ||
        !central_.connect(i, t.connection, p.qualification.identity, profileSpec()))
      return false;
    const auto q = p.qualification;
    p = Peer{};
    p.qualification = q;
    p.qualified = p.connecting = true;
    p.token = t;
    p.fault = OneRsFault::None;
    p.deadline = clock_.now() + kConnectMs;
    return true;
  }
  if (!commandReady(i) || p.token.connection != t.connection ||
      (op != Operation::Start && op != Operation::Stop))
    return false;
  if (p.sequence == 254) {
    retire(i, OneRsFault::SequenceExhausted);
    return false;
  }
  const auto mtu = host_.mtu(p.handle);
  if (mtu == kBleMtuReserved || mtu < 21) {
    // Reserved host means no admission; no packet or sequence was consumed.
    if (mtu != kBleMtuReserved)
      retire(i, OneRsFault::Mtu);
    return false;
  }
  insta360::Be80ControlConfig config;
  config.profile = insta360::Be80ControlProfile::GarminBe80ControlV1;
  const auto packet = insta360::encodeRecordingCommand(
      config,
      op == Operation::Start ? insta360::Be80RecordingCommand::StartVideo
                             : insta360::Be80RecordingCommand::Stop,
      p.sequence + 1);
  p.token = t;
  if (!packet.size || !central_.write(i, 0, packet.bytes.data(), packet.size, clock_.now())) {
    retire(i, OneRsFault::Transport);
    return false;
  }
  ++p.sequence; // Copied admission consumes it even if later ATT/SDK is ambiguous.
  p.pending = true;
  p.deadline = clock_.now() + kCommandMs;
  return true;
}
void OneRsAdapter::retire(uint8_t i, OneRsFault reason) {
  auto &p = peers_[i];
  if (p.sealed)
    return;
  p.sealed = true;
  p.ready = p.connecting = p.pending = false;
  p.fault = reason;
  disconnected_[i] = true;
  central_.disconnect(i);
}
void OneRsAdapter::cancel(size_t i, Token t) {
  if (i < kBlePeers && peers_[i].token.connection == t.connection &&
      peers_[i].token.operation == t.operation)
    retire(i, OneRsFault::Cancelled);
}
void OneRsAdapter::close(size_t i, Token t) {
  if (i < kBlePeers && peers_[i].token.connection == t.connection)
    retire(i, OneRsFault::Cancelled);
}
void OneRsAdapter::stop() {
  enabled_ = false;
  for (uint8_t i = 0; i < kBlePeers; ++i)
    if (peers_[i].connecting || peers_[i].ready || peers_[i].pending)
      retire(i, OneRsFault::Cancelled);
  central_.stop();
}
void OneRsAdapter::complete(uint8_t i) {
  auto &p = peers_[i];
  if (event_count_ == events_.size()) {
    retire(i, OneRsFault::Transport);
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
  p.pending = false;
}
void OneRsAdapter::result(const BleResult &r) {
  const uint8_t i = r.event.peer;
  if (i >= kBlePeers || r.event.generation != peers_[i].token.connection)
    return;
  auto &p = peers_[i];
  if (r.kind == BleResultKind::Retired || r.kind == BleResultKind::LinkClosed) {
    retire(i, OneRsFault::Transport);
    return;
  }
  if (p.sealed)
    return;
  if ((p.connecting || p.pending) && reached(clock_.now(), p.deadline)) {
    retire(i, OneRsFault::Timeout);
    return;
  }
  if (r.kind == BleResultKind::TransportReady && p.connecting) {
    p.handle = r.event.connection;
    const auto mtu = host_.mtu(p.handle);
    if (mtu == kBleMtuReserved || mtu < 21) {
      retire(i, OneRsFault::Mtu);
      return;
    }
    complete(i);
  } else if (r.kind == BleResultKind::WriteComplete) {
    if (!p.pending || r.endpoint != 0 || r.event.status) {
      retire(i, OneRsFault::Transport);
      return;
    }
    complete(i); // ATT completion only. No wireAck or RecordingObserved emission.
  }
  // BE82 is opaque. No invented SYNC, auth, ACK, recording or timer semantics.
}
void OneRsAdapter::drain() {
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
    Event done(e.peer, e.token, EventKind::Completed);
    done.capabilities = e.capabilities;
    if (group_)
      group_->event(done);
    else
      manager_->event(done);
  }
}
void OneRsAdapter::service() {
  if (servicing_ || !manager_)
    return;
  servicing_ = true;
  central_.service(clock_.now());
  for (uint8_t i = 0; i < kBlePeers; ++i)
    if (!peers_[i].sealed && (peers_[i].connecting || peers_[i].pending) &&
        reached(clock_.now(), peers_[i].deadline))
      retire(i, OneRsFault::Timeout);
  drain();
  if (group_)
    group_->tick();
  else
    manager_->tick();
  servicing_ = false;
}
} // namespace ridesync

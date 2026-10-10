#include "profiles/gopro_hero12.h"
#include "recording_manager.h"
#include <cstring>

namespace ridesync {
namespace {
constexpr uint32_t kCameraResponseMs = 2000;
constexpr uint32_t kKeepAliveMs = 3000;
constexpr uint32_t kHardwarePollMs = 500;
constexpr uint8_t kHardwarePolls = 20;
// Canonical GP UUID b5f9XXXX-aa8d-11e3-9046-0002a5d5c51b in NimBLE's
// little-endian byte representation.
BleUuid gp(uint16_t part) {
  BleUuid u;
  u.size = 16;
  const uint8_t bytes[] = {0x1b,
                           0xc5,
                           0xd5,
                           0xa5,
                           0x02,
                           0x00,
                           0x46,
                           0x90,
                           0xe3,
                           0x11,
                           0x8d,
                           0xaa,
                           static_cast<uint8_t>(part),
                           static_cast<uint8_t>(part >> 8),
                           0xf9,
                           0xb5};
  std::memcpy(u.bytes.data(), bytes, sizeof(bytes));
  return u;
}
bool validQualification(const Hero12Qualification &q) {
  if (!q.source_qualified || !q.classic_profile_confirmed || !q.identity.verified ||
      q.identity.type == IdentityType::UnresolvedPrivate || q.firmware_size == 0 ||
      q.firmware_size > q.firmware.size())
    return false;
  bool nonzero = false, all_ff = true;
  for (uint8_t b : q.identity.address) {
    nonzero |= b != 0;
    all_ff &= b == 0xff;
  }
  return nonzero && !all_ff &&
         (q.identity.type != IdentityType::RandomStatic || (q.identity.address[5] & 0xc0) == 0xc0);
}
uint8_t channelEndpoint(gopro::Channel c, bool write) {
  switch (c) {
  case gopro::Channel::Command:
    return write ? 0 : 1;
  case gopro::Channel::Settings:
    return write ? 2 : 3;
  case gopro::Channel::Query:
    return write ? 4 : 5;
  case gopro::Channel::Management:
    return write ? 6 : 7;
  }
  return 0xff;
}
} // namespace

Hero12Adapter::Hero12Adapter(BleHost &host, Clock &clock)
    : clock_(clock), central_(host, *this, &progress_) {}
void Hero12Adapter::attach(CameraManager &manager) { manager_ = &manager; }
RetryPolicy Hero12Adapter::managerPolicy() {
  RetryPolicy p;
  p.timeout_ms = 120000;
  p.backoff_ms = 200;
  p.max_attempts = 1; // Camera delivery is never replayed by manager retries.
  return p;
}
bool Hero12Adapter::reached(uint32_t now, uint32_t deadline) {
  return now - deadline < 0x80000000UL;
}
BleProfileSpec Hero12Adapter::profileSpec() {
  BleProfileSpec s;
  s.service_count = 2;
  s.services[0] = BleUuid::shortUuid(0xfea6);
  s.services[1] = gp(0x0090);
  s.endpoint_count = 8;
  const uint16_t chars[] = {0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0091, 0x0092};
  for (uint8_t n = 0; n < 8; ++n) {
    s.endpoints[n].uuid = gp(chars[n]);
    s.endpoints[n].service = n >= 6 ? 1 : 0;
    s.endpoints[n].properties = (n & 1) ? 0x10 : 0x08;
    s.endpoints[n].subscribe = (n & 1) ? 1 : 0;
  }
  // BLE bond encryption and verified identity are required by BleCentral;
  // NimBLE's authenticated flag specifically means MITM, not basic pairing.
  s.require_authenticated = false;
  return s;
}
bool Hero12Adapter::start(bool enabled, bool qualified) {
  if (started_)
    return false;
  started_ = true;
  if (!enabled || !qualified || !manager_)
    return false;
  enabled_ = central_.begin(true, true, clock_.now());
  return enabled_;
}
bool Hero12Adapter::configurePeer(uint8_t i, const Hero12Qualification &q) {
  if (i >= kBlePeers || maintenance_[i] ||
      (central_.phase(i) != BlePhase::Empty && central_.phase(i) != BlePhase::Closed) ||
      peers_[i].connecting || peers_[i].ready || disconnect_pending_[i])
    return false;
  peers_[i].qualification = q;
  peers_[i].qualified = validQualification(q);
  peers_[i].fault = peers_[i].qualified ? Hero12Fault::None : Hero12Fault::Qualification;
  return peers_[i].qualified;
}
Hero12Fault Hero12Adapter::fault(uint8_t i) const {
  return i < kBlePeers ? peers_[i].fault : Hero12Fault::Capacity;
}
Hero12RecoveryState Hero12Adapter::recoveryState(uint8_t i) const {
  return i < kBlePeers ? recovery_[i].state : Hero12RecoveryState{};
}
bool Hero12Adapter::linkRetiring(uint8_t i) const {
  return i < kBlePeers && (peers_[i].sealed || disconnect_pending_[i]);
}
bool Hero12Adapter::linkReleased(uint8_t i) const {
  if (i >= kBlePeers || disconnect_pending_[i])
    return false;
  const auto phase = central_.phase(i);
  return phase == BlePhase::Empty || phase == BlePhase::Closed;
}
bool Hero12Adapter::sealForMaintenance(uint8_t i) {
  if (i >= kBlePeers || !manager_ || i >= manager_->size())
    return false;
  maintenance_[i] = true; // Before cancelling any queued/delayed recording work.
  maintenance_scan_ = true;
  if (scan_owner_ < kBlePeers)
    cancelRecovery(scan_owner_);
  central_.cancelScan();
  recovery_[i] = Recovery{};
  recovery_[i].state.phase = Hero12RecoveryPhase::Cancelled;
  manager_->seal(i);
  retire(i, Hero12Fault::DeliveryUncertain);
  return true;
}
bool Hero12Adapter::maintenanceReleased(uint8_t i) const {
  return i < kBlePeers && maintenance_[i] && linkReleased(i) && central_.scanReleased();
}
bool Hero12Adapter::commandReady(uint8_t i) const {
  return i < kBlePeers && peers_[i].ready && !peers_[i].sealed && peers_[i].step == Step::None &&
         central_.admissionOpen(i);
}
bool Hero12Adapter::recoveryReady(uint8_t i) const {
  if (i >= kBlePeers || !manager_ || i >= manager_->size())
    return false;
  const auto &r = recovery_[i];
  const auto &s = *manager_->state(i);
  return r.state.phase == Hero12RecoveryPhase::Ready && r.operation &&
         s.token.connection == r.connection && s.token.operation == r.operation &&
         s.lifecycle == Lifecycle::Ready && s.has_observation &&
         s.observed != RecordingState::Unknown;
}
CameraError Hero12Adapter::requestRecovery(uint8_t i, bool ensure_recording,
                                           Hero12PowerCondition condition) {
  if (i >= kBlePeers || !manager_ || i >= manager_->size())
    return CameraError::InvalidPeer;
  if (maintenance_[i])
    return CameraError::Cancelled;
  if (!enabled_)
    return CameraError::Disabled;
  if (!peers_[i].qualified || !validQualification(peers_[i].qualification))
    return CameraError::Unsupported;
  auto &r = recovery_[i];
  if (r.state.phase == Hero12RecoveryPhase::Pending ||
      r.state.phase == Hero12RecoveryPhase::Scanning ||
      r.state.phase == Hero12RecoveryPhase::Connecting ||
      r.state.phase == Hero12RecoveryPhase::Observing ||
      r.state.phase == Hero12RecoveryPhase::Starting)
    return CameraError::Busy;
  const auto *state = manager_->state(i);
  if (!state || (state->lifecycle != Lifecycle::Ready && state->lifecycle != Lifecycle::Idle &&
                 state->lifecycle != Lifecycle::Failed))
    return CameraError::Busy;
  r = Recovery{};
  if (condition == Hero12PowerCondition::PowerRemoved) {
    r.state.phase = Hero12RecoveryPhase::Unsupported;
    return CameraError::Unsupported;
  }
  r.ensure_recording = ensure_recording;
  r.deadline = clock_.now() + 140000;
  r.source_connection = state->token.connection;
  // A live RideSync link is queried in place; no discovery or replacement link.
  r.state.phase = state->lifecycle == Lifecycle::Ready ? Hero12RecoveryPhase::Observing
                                                       : Hero12RecoveryPhase::Pending;
  return CameraError::None;
}
CameraError Hero12Adapter::cancelRecovery(uint8_t i) {
  if (i >= kBlePeers || !manager_ || i >= manager_->size())
    return CameraError::InvalidPeer;
  auto &r = recovery_[i];
  if (r.state.phase == Hero12RecoveryPhase::Scanning && scan_owner_ == i) {
    central_.cancelScan();
    scan_owner_ = kBlePeers;
  }
  if ((r.state.phase == Hero12RecoveryPhase::Connecting ||
       r.state.phase == Hero12RecoveryPhase::Observing ||
       r.state.phase == Hero12RecoveryPhase::Starting) &&
      r.operation && manager_->state(i)->token.connection == r.connection &&
      manager_->state(i)->token.operation == r.operation &&
      (manager_->state(i)->lifecycle == Lifecycle::Connecting ||
       manager_->state(i)->lifecycle == Lifecycle::Operating))
    manager_->cancel(i);
  r.state.phase = Hero12RecoveryPhase::Cancelled;
  return CameraError::None;
}
bool Hero12Adapter::hasFea6(const BleEvent &e) {
  bool found = false;
  for (size_t offset = 0; offset < e.size;) {
    const uint8_t length = e.bytes[offset++];
    if (!length)
      return found;
    if (length > e.size - offset)
      return false;
    const uint8_t type = e.bytes[offset];
    if ((type == 0x02 || type == 0x03) && (!(length & 1) || length < 3))
      return false;
    if (type == 0x02 || type == 0x03)
      for (size_t n = offset + 1; n + 1 < offset + length; n += 2)
        if (e.bytes[n] == 0xa6 && e.bytes[n + 1] == 0xfe)
          found = true;
    offset += length;
  }
  return found;
}
bool Hero12Adapter::begin(size_t index, const CameraConfig &camera, Operation op, Token token) {
  if (!enabled_ || !manager_ || index >= kBlePeers || camera.family != CameraFamily::GoPro ||
      camera.model != CameraModel::HERO12_BLACK) {
    if (index < kBlePeers)
      peers_[index].fault = enabled_ ? Hero12Fault::Qualification : Hero12Fault::Disabled;
    return false;
  }
  const auto i = static_cast<uint8_t>(index);
  auto &p = peers_[i];
  if (maintenance_[i] || !p.qualified || !validQualification(p.qualification)) {
    p.fault = Hero12Fault::Qualification;
    return false;
  }
  if (op == Operation::Connect) {
    if (p.connecting || p.ready || disconnect_pending_[i] || token.connection == 0 ||
        (p.sealed && central_.phase(i) != BlePhase::Closed) ||
        !central_.connect(i, token.connection, p.qualification.identity, profileSpec())) {
      p.fault = Hero12Fault::Transport;
      return false;
    }
    p = [&] {
      Peer fresh;
      fresh.qualification = p.qualification;
      fresh.qualified = true;
      fresh.connection = token.connection;
      fresh.token = token;
      fresh.connecting = true;
      fresh.operation = Operation::Connect;
      return fresh;
    }();
    return true;
  }
  if (!p.ready || p.sealed || p.step != Step::None || p.connection != token.connection ||
      (op != Operation::Start && op != Operation::Stop && op != Operation::Query))
    return false;
  p.operation = op;
  p.token = token;
  p.target_encoding = op == Operation::Start;
  // A fresh encoding query avoids redundant shutter delivery. Start then gets
  // fresh Busy and Ready before video mode; Stop can proceed while encoding.
  return send(i, Step::QueryEncoding);
}
void Hero12Adapter::cancel(size_t index, Token token) {
  if (index >= kBlePeers)
    return;
  auto &p = peers_[index];
  if (p.connection != token.connection || p.token.operation != token.operation)
    return;
  if (p.connecting || p.ready)
    retire(index, Hero12Fault::DeliveryUncertain);
}
void Hero12Adapter::close(size_t index, Token token) {
  if (index < kBlePeers && peers_[index].connection == token.connection)
    retire(index, Hero12Fault::DeliveryUncertain);
}
void Hero12Adapter::stop() {
  if (scan_owner_ < kBlePeers)
    scan_owner_ = kBlePeers;
  for (auto &r : recovery_)
    r.state.phase = Hero12RecoveryPhase::Cancelled;
  for (uint8_t i = 0; i < kBlePeers; ++i)
    if (peers_[i].connecting || peers_[i].ready)
      retire(i, Hero12Fault::Transport);
  central_.stop();
  enabled_ = false;
}
bool Hero12Adapter::canDestroy() const { return central_.canDestroy(); }
void Hero12Adapter::retire(uint8_t i, Hero12Fault fault) {
  auto &p = peers_[i];
  if (p.sealed)
    return;
  p.sealed = true;
  p.connecting = p.ready = false;
  p.step = Step::None;
  p.fault = fault;
  reassembler_.resetPeer(i);
  disconnect_pending_[i] = true;
  central_.disconnect(i);
}
void Hero12Adapter::enqueue(Event e) {
  if (event_count_ == events_.size()) {
    retire(static_cast<uint8_t>(e.peer), Hero12Fault::Protocol);
    return;
  }
  auto &slot = events_[event_count_++];
  slot.peer = static_cast<uint8_t>(e.peer);
  slot.token = e.token;
  slot.kind = e.kind;
  slot.recording = e.recording;
  slot.capabilities = e.capabilities;
}
void Hero12Adapter::drain() {
  if (!manager_)
    return;
  for (uint8_t i = 0; i < kBlePeers; ++i)
    if (disconnect_pending_[i]) {
      disconnect_pending_[i] = false;
      if (group_)
        group_->event(Event(i, peers_[i].connection, EventKind::Disconnected));
      else
        manager_->event(Event(i, peers_[i].connection, EventKind::Disconnected));
    }
  const uint8_t count = event_count_;
  event_count_ = 0;
  for (uint8_t n = 0; n < count; ++n) {
    const auto &slot = events_[n];
    if (peers_[slot.peer].sealed)
      continue;
    Event e(slot.peer, slot.token, slot.kind);
    e.recording = slot.recording;
    e.capabilities = slot.capabilities;
    if (group_)
      group_->event(e);
    else
      manager_->event(e);
  }
}
bool Hero12Adapter::send(uint8_t i, Step step) {
  auto &p = peers_[i];
  if (p.sealed || p.step != Step::None || !central_.admissionOpen(i))
    return false;
  if (p.ready && step != Step::KeepAlive && reached(clock_.now(), p.keepalive_due)) {
    p.resume = step;
    step = Step::KeepAlive;
  }
  uint8_t bytes[20] = {};
  size_t length = 0;
  gopro::Channel route = gopro::Channel::Command;
  uint8_t id = 0, status = 0;
  if (step == Step::Pair || step == Step::Claim) {
    gopro::SetupPacket packet;
    const auto op = step == Step::Pair ? gopro::SetupOperation::PairingFinish
                                       : gopro::SetupOperation::ClaimExternalControl;
    if (!gopro::encodeSetup(op, packet))
      return false;
    route = packet.channel;
    id = packet.feature;
    length = packet.size;
    std::memcpy(bytes, packet.bytes, length);
  } else {
    gopro::Request request = gopro::Request::HardwareInfo;
    switch (step) {
    case Step::Hardware:
      request = gopro::Request::HardwareInfo;
      break;
    case Step::Api:
      request = gopro::Request::ApiVersion;
      break;
    case Step::RegisterBusy:
      request = gopro::Request::RegisterBusy;
      status = 8;
      break;
    case Step::RegisterEncoding:
      request = gopro::Request::RegisterEncoding;
      status = 10;
      break;
    case Step::RegisterReady:
      request = gopro::Request::RegisterReady;
      status = 82;
      break;
    case Step::GetBusy:
      request = gopro::Request::GetBusy;
      status = 8;
      break;
    case Step::GetEncoding:
    case Step::QueryEncoding:
    case Step::ConfirmEncoding:
      request = gopro::Request::GetEncoding;
      status = 10;
      break;
    case Step::GetReady:
      request = gopro::Request::GetReady;
      status = 82;
      break;
    case Step::Video:
      request = gopro::Request::Video;
      break;
    case Step::Shutter:
      request = p.target_encoding ? gopro::Request::ShutterOn : gopro::Request::ShutterOff;
      break;
    case Step::KeepAlive:
      request = gopro::Request::KeepAlive;
      break;
    default:
      return false;
    }
    gopro::Packet packet;
    if (!gopro::encode(request, packet))
      return false;
    route = packet.channel;
    id = packet.id;
    length = packet.size;
    std::memcpy(bytes, packet.bytes, length);
  }
  const uint8_t endpoint = channelEndpoint(route, true);
  if (step == Step::Hardware && p.hardware_attempts >= kHardwarePolls)
    return false;
  if (!central_.write(i, endpoint, bytes, length, clock_.now()))
    return false;
  if (step == Step::Hardware)
    ++p.hardware_attempts;
  p.step = step;
  p.route = route;
  p.expected_id = id;
  p.expected_status = status;
  p.write_endpoint = endpoint;
  p.deadline = clock_.now() + kCameraResponseMs;
  p.att_done = p.camera_done = false;
  p.waiting_write = true;
  p.fragment = false;
  return true;
}
void Hero12Adapter::observe(uint8_t i, bool value, bool command) {
  auto &p = peers_[i];
  p.encoding_known = true;
  p.encoding = value;
  if (!p.ready)
    return; // Manager discards observations during Connecting.
  Token observed_token = p.token;
  observed_token.connection = p.connection;
  if (!command)
    observed_token.operation = 0;
  Event e(i, observed_token,
          command ? EventKind::CommandRecordingObserved : EventKind::RecordingObserved);
  e.recording = value ? RecordingState::Recording : RecordingState::Stopped;
  enqueue(e);
}
void Hero12Adapter::cameraMessage(uint8_t i, gopro::Channel route, const gopro::Message &message) {
  auto &p = peers_[i];
  // Central dispatches queued notifications before service() checks deadlines.
  // Retire the expired transaction before any status mutation or wire-ACK hook.
  if (p.step != Step::None && reached(clock_.now(), p.deadline)) {
    retire(i, Hero12Fault::Timeout);
    return;
  }
  if (route == gopro::Channel::Query && message.size >= 2 && message.bytes[0] == 0x93) {
    const auto r = gopro::decode(route, message);
    if (r.outcome != gopro::Outcome::Complete || r.result != 0) {
      retire(i, Hero12Fault::Protocol);
      return;
    }
    if (r.busy_known) {
      p.busy_known = true;
      p.busy = r.busy;
    }
    if (r.ready_known) {
      p.ready_known = true;
      p.camera_ready = r.ready;
    }
    if (r.encoding_known)
      observe(i, r.encoding, false);
    return;
  }
  if (p.step == Step::None || p.camera_done || route != p.route || message.size < 2 ||
      message.bytes[0] != p.expected_id)
    return; // Unrelated notification is never a transaction completion.
  if (p.step == Step::Pair || p.step == Step::Claim) {
    const auto expected = p.step == Step::Pair ? gopro::SetupOperation::PairingFinish
                                               : gopro::SetupOperation::ClaimExternalControl;
    const auto r = gopro::decodeSetup(route, message, expected);
    if (r.outcome != gopro::Outcome::Complete ||
        r.domain != gopro::SetupResultDomain::GenericProtobuf || !r.result_present ||
        !r.known_result || !r.success) {
      retire(i, r.outcome == gopro::Outcome::Complete ? Hero12Fault::SetupRejected
                                                      : Hero12Fault::Protocol);
      return;
    }
  } else if (p.step == Step::Hardware || p.step == Step::Api) {
    const auto r = gopro::decodeIdentity(route, message);
    if (r.outcome != gopro::Outcome::Complete) {
      retire(i, Hero12Fault::Protocol);
      return;
    }
    if (p.step == Step::Hardware && r.classic_result != 0) {
      p.hardware_not_ready = true;
      p.camera_done = true;
      return;
    }
    if (r.classic_result != 0) {
      retire(i, Hero12Fault::CameraRejected);
      return;
    }
    if (p.step == Step::Hardware) {
      const auto f = r.fields[3];
      if (!r.model_known || r.model != 62 || f.size != p.qualification.firmware_size ||
          std::memcmp(r.raw.bytes + f.offset, p.qualification.firmware.data(), f.size) != 0) {
        retire(i, Hero12Fault::IdentityMismatch);
        return;
      }
    } else if (!r.api_known || r.api_major != p.qualification.api_major ||
               r.api_minor != p.qualification.api_minor) {
      retire(i, Hero12Fault::IdentityMismatch);
      return;
    }
  } else {
    const auto r = gopro::decode(route, message);
    if (r.outcome != gopro::Outcome::Complete || r.result != 0) {
      retire(i, r.outcome == gopro::Outcome::Complete ? Hero12Fault::CameraRejected
                                                      : Hero12Fault::Protocol);
      return;
    }
    if (p.expected_status) {
      if ((p.step == Step::GetBusy && !r.busy_known) ||
          ((p.step == Step::GetEncoding || p.step == Step::QueryEncoding ||
            p.step == Step::ConfirmEncoding) &&
           !r.encoding_known) ||
          (p.step == Step::GetReady && !r.ready_known))
        return; // A same-ID query without its requested status cannot complete.
      if (p.step == Step::RegisterBusy || p.step == Step::RegisterEncoding ||
          p.step == Step::RegisterReady) {
        if ((p.expected_status == 8 && !r.busy_known) ||
            (p.expected_status == 10 && !r.encoding_known) ||
            (p.expected_status == 82 && !r.ready_known))
          return; // Successful Register must echo the requested status element.
        if ((r.busy_known && p.expected_status != 8) ||
            (r.encoding_known && p.expected_status != 10) ||
            (r.ready_known && p.expected_status != 82))
          return;
      }
      if (r.busy_known) {
        p.busy_known = true;
        p.busy = r.busy;
      }
      if (r.ready_known) {
        p.ready_known = true;
        p.camera_ready = r.ready;
      }
      if (r.encoding_known)
        observe(i, r.encoding, p.step == Step::ConfirmEncoding);
    }
  }
  if (manager_ && p.step != Step::KeepAlive) {
    CameraAckDomain domain = (p.step == Step::Pair || p.step == Step::Claim)
                                 ? CameraAckDomain::Protobuf
                                 : CameraAckDomain::Classic;
    CameraAckAction action = CameraAckAction::None;
    switch (p.step) {
    case Step::Pair:
      action = CameraAckAction::Pair;
      break;
    case Step::Claim:
      action = CameraAckAction::Claim;
      break;
    case Step::Hardware:
      action = CameraAckAction::Hardware;
      break;
    case Step::Api:
      action = CameraAckAction::Api;
      break;
    case Step::RegisterBusy:
      action = CameraAckAction::RegisterBusy;
      break;
    case Step::RegisterEncoding:
      action = CameraAckAction::RegisterEncoding;
      break;
    case Step::RegisterReady:
      action = CameraAckAction::RegisterReady;
      break;
    case Step::GetBusy:
      action = CameraAckAction::GetBusy;
      break;
    case Step::GetEncoding:
      action = CameraAckAction::GetEncoding;
      break;
    case Step::GetReady:
      action = CameraAckAction::GetReady;
      break;
    case Step::Video:
      action = CameraAckAction::Video;
      break;
    case Step::Shutter:
      action = p.target_encoding ? CameraAckAction::ShutterOn : CameraAckAction::ShutterOff;
      break;
    case Step::ConfirmEncoding:
      action = CameraAckAction::ConfirmEncoding;
      break;
    case Step::QueryEncoding:
      action = CameraAckAction::QueryEncoding;
      break;
    default:
      break;
    }
    manager_->wireAck(i, p.token, domain, action);
  }
  p.camera_done = true;
}
void Hero12Adapter::advance(uint8_t i) {
  auto &p = peers_[i];
  if (p.sealed || p.step == Step::None || !p.att_done || !p.camera_done ||
      !central_.admissionOpen(i))
    return;
  const Step done = p.step;
  p.step = Step::None;
  Step next = Step::None;
  switch (done) {
  case Step::Pair:
    next = Step::Claim;
    break;
  case Step::Claim:
    next = Step::Hardware;
    break;
  case Step::Hardware:
    if (p.hardware_not_ready) {
      p.hardware_not_ready = false;
      if (p.hardware_attempts == kHardwarePolls) {
        retire(i, Hero12Fault::Timeout);
        return;
      }
      p.hardware_retry_due = clock_.now() + kHardwarePollMs;
      return;
    }
    next = Step::Api;
    break;
  case Step::Api:
    next = Step::RegisterBusy;
    break;
  case Step::RegisterBusy:
    next = Step::RegisterEncoding;
    break;
  case Step::RegisterEncoding:
    next = Step::RegisterReady;
    break;
  case Step::RegisterReady:
    next = Step::GetBusy;
    break;
  case Step::GetBusy:
    next = p.connecting ? Step::GetEncoding : Step::GetReady;
    break;
  case Step::GetEncoding:
    next = Step::GetReady;
    break;
  case Step::GetReady:
    if (p.connecting) {
      if (!p.busy_known || !p.encoding_known || !p.ready_known) {
        retire(i, Hero12Fault::Protocol);
        return;
      }
      p.connecting = false;
      p.ready = true;
      p.keepalive_due = clock_.now() + kKeepAliveMs;
      Event e(i, p.token, EventKind::Completed);
      e.capabilities.start = e.capabilities.stop = e.capabilities.query =
          CapabilityState::Supported;
      enqueue(e);
      // Publish initial state only after the manager accepts Connect completion.
      Event observed(i, p.connection, EventKind::RecordingObserved);
      observed.recording = p.encoding ? RecordingState::Recording : RecordingState::Stopped;
      enqueue(observed);
      return;
    }
    if (p.operation == Operation::Start) {
      if (p.busy || !p.camera_ready || p.encoding) {
        retire(i, Hero12Fault::CameraRejected);
        return;
      }
      next = Step::Video;
    }
    break;
  case Step::QueryEncoding:
    if (p.operation == Operation::Query) {
      enqueue(Event(i, p.token, EventKind::Completed));
      return;
    }
    if (p.encoding == p.target_encoding) {
      enqueue(Event(i, p.token, EventKind::Completed));
      return;
    }
    next = p.operation == Operation::Start ? Step::GetBusy : Step::Shutter;
    break;
  case Step::Video:
    next = Step::Shutter;
    break;
  case Step::Shutter:
    next = Step::ConfirmEncoding;
    break;
  case Step::ConfirmEncoding:
    if (p.encoding != p.target_encoding) {
      retire(i, Hero12Fault::CameraRejected);
      return;
    }
    enqueue(Event(i, p.token, EventKind::Completed));
    return;
  case Step::KeepAlive:
    p.keepalive_due = clock_.now() + kKeepAliveMs;
    next = p.resume;
    p.resume = Step::None;
    break;
  default:
    break;
  }
  if (next != Step::None && !send(i, next))
    retire(i, Hero12Fault::DeliveryUncertain);
}
void Hero12Adapter::result(const BleResult &r) {
  if (r.kind == BleResultKind::Advertisement && scan_owner_ < kBlePeers &&
      r.event.generation == scan_generation_) {
    auto &recovery = recovery_[scan_owner_];
    const auto &identity = peers_[scan_owner_].qualification.identity;
    if (recovery.state.phase == Hero12RecoveryPhase::Scanning &&
        (r.event.advertisement_type == BleAdvertisementType::ConnectableUndirected ||
         r.event.advertisement_type == BleAdvertisementType::ConnectableDirected) &&
        r.event.identity.type == identity.type && r.event.identity.address == identity.address &&
        hasFea6(r.event)) {
      recovery.observed_advertisement = true;
      central_.cancelScan();
    }
    return;
  }
  if (r.kind == BleResultKind::ScanComplete && scan_owner_ < kBlePeers &&
      r.event.generation == scan_generation_) {
    auto &recovery = recovery_[scan_owner_];
    scan_owner_ = kBlePeers;
    if (recovery.state.phase == Hero12RecoveryPhase::Scanning) {
      if (recovery.observed_advertisement)
        recovery.state.phase = Hero12RecoveryPhase::Connecting;
      else if (r.fault == BleFault::Host || r.fault == BleFault::Overflow)
        recovery.state.phase = Hero12RecoveryPhase::Failed;
      else
        recovery.state.phase = recovery.state.scans == 2 ? Hero12RecoveryPhase::Unavailable
                                                         : Hero12RecoveryPhase::Pending;
    }
    return;
  }
  const uint8_t i = r.event.peer;
  if (i >= kBlePeers)
    return;
  auto &p = peers_[i];
  if (r.event.generation != p.connection)
    return;
  if (r.kind == BleResultKind::Retired || r.kind == BleResultKind::LinkClosed) {
    Hero12Fault fault = Hero12Fault::Transport;
    switch (r.fault) {
    case BleFault::Store:
      fault = Hero12Fault::Store;
      break;
    case BleFault::MissingService:
      fault = Hero12Fault::MissingService;
      break;
    case BleFault::MissingProperty:
      fault = Hero12Fault::MissingProperty;
      break;
    case BleFault::MissingCccd:
      fault = Hero12Fault::MissingCccd;
      break;
    case BleFault::Att:
      fault = Hero12Fault::Att;
      break;
    case BleFault::Timeout:
      fault = Hero12Fault::Timeout;
      break;
    default:
      break;
    }
    retire(i, fault);
    return;
  }
  if (p.sealed)
    return;
  if (r.kind == BleResultKind::TransportReady) {
    if (!p.connecting || !send(i, Step::Pair))
      retire(i, Hero12Fault::DeliveryUncertain);
  } else if (r.kind == BleResultKind::WriteComplete) {
    if (p.waiting_write && r.endpoint == p.write_endpoint && p.step != Step::None) {
      p.att_done = true;
      p.waiting_write = false;
    }
  } else if (r.kind == BleResultKind::Notification) {
    gopro::Channel route;
    switch (r.endpoint) {
    case 1:
      route = gopro::Channel::Command;
      break;
    case 3:
      route = gopro::Channel::Settings;
      break;
    case 5:
      route = gopro::Channel::Query;
      break;
    case 7:
      route = gopro::Channel::Management;
      break;
    default:
      retire(i, Hero12Fault::Protocol);
      return;
    }
    gopro::Message message;
    const auto outcome =
        reassembler_.feed(i, route, r.event.bytes.data(), r.event.size, clock_.now(), message);
    if (outcome == gopro::Outcome::Pending) {
      if (p.fragment && p.fragment_route != route) {
        retire(i, Hero12Fault::Protocol);
        return;
      }
      if (!p.fragment) {
        p.fragment_started = clock_.now();
        p.fragment_route = route;
      }
      p.fragment = true;
    } else if (outcome == gopro::Outcome::Complete) {
      if (p.fragment && p.fragment_route == route)
        p.fragment = false;
      cameraMessage(i, route, message);
    } else {
      retire(i, Hero12Fault::Protocol);
    }
  }
}
void Hero12Adapter::advanceRecovery() {
  for (uint8_t i = 0; i < kBlePeers; ++i) {
    auto &r = recovery_[i];
    const auto phase = r.state.phase;
    if (phase != Hero12RecoveryPhase::Pending && phase != Hero12RecoveryPhase::Scanning &&
        phase != Hero12RecoveryPhase::Connecting && phase != Hero12RecoveryPhase::Observing &&
        phase != Hero12RecoveryPhase::Starting)
      continue;
    if (reached(clock_.now(), r.deadline)) {
      cancelRecovery(i);
      r.state.phase = Hero12RecoveryPhase::Timeout;
      continue;
    }
    const auto *state = manager_->state(i);
    if (!r.operation && phase != Hero12RecoveryPhase::Starting &&
        state->token.connection != r.source_connection) {
      cancelRecovery(i);
      r.state.phase = Hero12RecoveryPhase::Failed;
      continue;
    }
    if (phase == Hero12RecoveryPhase::Connecting) {
      if (!r.operation) {
        if (manager_->request(i, Operation::Connect) != CameraError::None) {
          r.state.phase = Hero12RecoveryPhase::Failed;
          continue;
        }
        state = manager_->state(i);
        r.operation = state->token.operation;
        r.connection = state->token.connection;
      } else if (state->token.connection != r.connection || state->token.operation != r.operation ||
                 state->lifecycle == Lifecycle::Idle || state->lifecycle == Lifecycle::Failed) {
        r.state.phase =
            peers_[i].fault == Hero12Fault::Timeout || state->error == CameraError::Timeout
                ? Hero12RecoveryPhase::Timeout
                : Hero12RecoveryPhase::Failed;
      } else if (state->lifecycle == Lifecycle::Ready) {
        if (!state->has_observation) {
          r.state.phase = Hero12RecoveryPhase::Unavailable;
        } else if (!r.ensure_recording) {
          r.state.phase = Hero12RecoveryPhase::Ready;
        } else if (state->observed == RecordingState::Recording) {
          r.state.phase = Hero12RecoveryPhase::Recording;
        } else {
          r.state.phase = Hero12RecoveryPhase::Starting;
          r.operation = 0;
        }
      }
    } else if (phase == Hero12RecoveryPhase::Observing) {
      if (!r.operation) {
        if (state->lifecycle == Lifecycle::Idle || state->lifecycle == Lifecycle::Failed ||
            peers_[i].sealed || !peers_[i].ready) {
          r.state.phase = Hero12RecoveryPhase::Failed;
          continue;
        }
        // CameraManager::request queues behind an active operation. Never take
        // that path or overlap the profile's serialized keepalive transaction.
        if (state->lifecycle != Lifecycle::Ready || peers_[i].step != Step::None ||
            !central_.admissionOpen(i))
          continue;
        if (manager_->request(i, Operation::Query) != CameraError::None) {
          r.state.phase = Hero12RecoveryPhase::Failed;
          continue;
        }
        r.operation = manager_->state(i)->token.operation;
        r.connection = manager_->state(i)->token.connection;
      } else if (state->token.connection != r.connection || state->token.operation != r.operation ||
                 state->lifecycle == Lifecycle::Idle || state->lifecycle == Lifecycle::Failed) {
        r.state.phase =
            peers_[i].fault == Hero12Fault::Timeout || state->error == CameraError::Timeout
                ? Hero12RecoveryPhase::Timeout
                : Hero12RecoveryPhase::Failed;
      } else if (state->lifecycle == Lifecycle::Ready && state->token.operation == r.operation) {
        if (!state->has_observation || state->observed == RecordingState::Unknown)
          r.state.phase = Hero12RecoveryPhase::Unavailable;
        else if (!r.ensure_recording)
          r.state.phase = Hero12RecoveryPhase::Ready;
        else if (state->observed == RecordingState::Recording)
          r.state.phase = Hero12RecoveryPhase::Recording;
        else {
          r.state.phase = Hero12RecoveryPhase::Starting;
          r.operation = 0;
        }
      }
    } else if (phase == Hero12RecoveryPhase::Starting) {
      if (!r.operation) {
        if (state->lifecycle == Lifecycle::Idle || state->lifecycle == Lifecycle::Failed ||
            peers_[i].sealed || !peers_[i].ready) {
          r.state.phase = Hero12RecoveryPhase::Failed;
          continue;
        }
        // If another owner operated between observe and Start, wait for its
        // completion. Start itself always re-queries Encoding before shutter.
        if (state->lifecycle != Lifecycle::Ready || peers_[i].step != Step::None ||
            !central_.admissionOpen(i))
          continue;
        if (manager_->request(i, Operation::Start) != CameraError::None) {
          r.state.phase = Hero12RecoveryPhase::Failed;
          continue;
        }
        r.operation = manager_->state(i)->token.operation;
        r.connection = manager_->state(i)->token.connection;
      } else if (state->token.connection != r.connection || state->token.operation != r.operation ||
                 state->lifecycle == Lifecycle::Idle || state->lifecycle == Lifecycle::Failed) {
        r.state.phase =
            peers_[i].fault == Hero12Fault::Timeout || state->error == CameraError::Timeout
                ? Hero12RecoveryPhase::Timeout
                : Hero12RecoveryPhase::Failed;
      } else if (state->lifecycle == Lifecycle::Ready && state->token.operation == r.operation) {
        r.state.phase = state->has_observation && state->observed == RecordingState::Recording
                            ? Hero12RecoveryPhase::Recording
                            : Hero12RecoveryPhase::Unavailable;
      }
    }
  }
  if (!maintenance_scan_ && scan_owner_ == kBlePeers)
    for (uint8_t n = 0; n < kBlePeers; ++n) {
      const uint8_t i = (scan_cursor_ + n) % kBlePeers;
      auto &r = recovery_[i];
      if (r.state.phase != Hero12RecoveryPhase::Pending)
        continue;
      const auto link = central_.phase(i);
      if (link != BlePhase::Empty && link != BlePhase::Closed)
        continue; // A retired connection must release its host lease before rediscovery.
      const uint32_t before = central_.scanGeneration();
      const bool admitted = central_.scan(3000, clock_.now());
      if (central_.scanGeneration() != before) {
        ++r.state.scans;
        scan_cursor_ = (i + 1) % kBlePeers;
        if (admitted) {
          r.state.phase = Hero12RecoveryPhase::Scanning;
          r.observed_advertisement = false;
          scan_owner_ = i;
          scan_generation_ = central_.scanGeneration();
        } else {
          // submit() owned a generation even when it failed synchronously. The
          // central keeps that context until its host barrier; never retry it.
          r.state.phase = Hero12RecoveryPhase::Failed;
        }
      }
      break;
    }
}
void Hero12Adapter::service(CameraServiceAction *action, bool advance_managers) {
  if (servicing_)
    return;
  servicing_ = true;
  if (advance_managers && group_)
    group_->beginServicePass();
  central_.service(clock_.now());
  for (uint8_t n = 0; n < kBlePeers; ++n) {
    const uint8_t i = (cursor_ + n) % kBlePeers;
    auto &p = peers_[i];
    if (p.sealed)
      continue;
    if ((p.fragment && reached(clock_.now(), p.fragment_started + 1000)) ||
        (p.step != Step::None && reached(clock_.now(), p.deadline))) {
      retire(i, Hero12Fault::Timeout);
      continue;
    }
    if (p.step != Step::None)
      advance(i);
    else if (p.connecting && p.hardware_retry_due && reached(clock_.now(), p.hardware_retry_due) &&
             central_.admissionOpen(i)) {
      p.hardware_retry_due = 0;
      if (!send(i, Step::Hardware))
        retire(i, Hero12Fault::DeliveryUncertain);
    } else if (p.ready && reached(clock_.now(), p.keepalive_due) && central_.admissionOpen(i) &&
               !send(i, Step::KeepAlive))
      retire(i, Hero12Fault::DeliveryUncertain);
  }
  cursor_ = (cursor_ + 1) % kBlePeers;
  drain(); // Connection-scoped Disconnected precedes every manager tick.
  if (action)
    action->beforeAdvance();
  if (advance_managers && group_)
    group_->tick();
  else if (advance_managers && manager_)
    manager_->tick();
  if (manager_ && enabled_)
    advanceRecovery();
  servicing_ = false;
}
} // namespace ridesync

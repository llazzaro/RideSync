#include "profiles/gopro_hero12.h"
#include "profiles/qualification.h"
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
  return qualification::staticIdentity(q.identity);
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
void Hero12Adapter::advanceManagers(bool advance_managers) {
  if (advance_managers && group_)
    group_->tick();
  else if (advance_managers && manager_)
    manager_->tick();
}
void Hero12Adapter::recordAdmissionFailure(size_t index) {
  if (index < kBlePeers)
    peers_[index].fault = enabled_ ? Hero12Fault::Qualification : Hero12Fault::Disabled;
}
void Hero12Adapter::cancelRecoveryOperation(uint8_t i) {
  const auto &r = recovery_[i];
  if ((r.state.phase == Hero12RecoveryPhase::Connecting ||
       r.state.phase == Hero12RecoveryPhase::Observing ||
       r.state.phase == Hero12RecoveryPhase::Starting) &&
      r.operation && manager_->state(i)->token.connection == r.connection &&
      manager_->state(i)->token.operation == r.operation &&
      (manager_->state(i)->lifecycle == Lifecycle::Connecting ||
       manager_->state(i)->lifecycle == Lifecycle::Operating))
    manager_->cancel(i);
}
void Hero12Adapter::attach(CameraManager &manager) { manager_ = &manager; }
RetryPolicy Hero12Adapter::managerPolicy() {
  RetryPolicy p;
  p.timeout_ms = 120000;
  p.backoff_ms = 200;
  p.max_attempts = 1; // Camera delivery is never replayed by manager retries.
  return p;
}
bool Hero12Adapter::transactionExpired(uint8_t i) const {
  const auto &p = peers_[i];
  return (p.fragment && reached(clock_.now(), p.fragment_started + 1000)) ||
         (p.step != Step::None && reached(clock_.now(), p.deadline));
}
bool Hero12Adapter::validateIdentity(uint8_t i, const gopro::IdentityResponse &r) {
  const auto &p = peers_[i];
  if (p.step == Step::Hardware) {
    const auto f = r.fields[3];
    if (!r.model_known || r.model != 62 || f.size != p.qualification.firmware_size ||
        std::memcmp(r.raw.bytes + f.offset, p.qualification.firmware.data(), f.size) != 0) {
      retire(i, Hero12Fault::IdentityMismatch);
      return false;
    }
  } else if (!r.api_known || r.api_major != p.qualification.api_major ||
             r.api_minor != p.qualification.api_minor) {
    retire(i, Hero12Fault::IdentityMismatch);
    return false;
  }

  return true;
}
bool Hero12Adapter::notificationRoute(uint8_t endpoint, gopro::Channel &route) {
  constexpr gopro::Channel routes[] = {gopro::Channel::Command, gopro::Channel::Settings,
                                       gopro::Channel::Query, gopro::Channel::Management};
  if (endpoint < 1 || endpoint > 7 || (endpoint & 1) == 0)
    return false;
  route = routes[endpoint / 2];
  return true;
}
bool Hero12Adapter::admitPacket(uint8_t i, Step step, const OutgoingPacket &out) {
  auto &p = peers_[i];
  const uint8_t endpoint = channelEndpoint(out.route, true);
  if (step == Step::Hardware && p.hardware_attempts >= kHardwarePolls)
    return false;
  if (!central_.write(i, endpoint, out.bytes, out.length, clock_.now()))
    return false;
  if (step == Step::Hardware)
    ++p.hardware_attempts;
  p.step = step;
  p.route = out.route;
  p.expected_id = out.id;
  p.expected_status = out.status;
  p.write_endpoint = endpoint;
  p.deadline = clock_.now() + kCameraResponseMs;
  p.att_done = p.camera_done = false;
  p.waiting_write = true;
  p.fragment = false;
  return true;
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
bool Hero12Adapter::activeRecoveryPhase(Hero12RecoveryPhase phase) {
  return phase == Hero12RecoveryPhase::Pending || phase == Hero12RecoveryPhase::Scanning ||
         phase == Hero12RecoveryPhase::Connecting || phase == Hero12RecoveryPhase::Observing ||
         phase == Hero12RecoveryPhase::Starting;
}
CameraError Hero12Adapter::recoveryAdmission(uint8_t i) {
  if (i >= kBlePeers || !manager_ || i >= manager_->size())
    return CameraError::InvalidPeer;
  if (maintenance_[i])
    return CameraError::Cancelled;
  if (!enabled_)
    return CameraError::Disabled;
  if (!peers_[i].qualified || !validQualification(peers_[i].qualification))
    return CameraError::Unsupported;
  return CameraError::None;
}
CameraError Hero12Adapter::requestRecovery(uint8_t i, bool ensure_recording,
                                           Hero12PowerCondition condition) {
  const auto admission = recoveryAdmission(i);
  if (admission != CameraError::None)
    return admission;
  auto &r = recovery_[i];
  if (activeRecoveryPhase(r.state.phase))
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
  cancelRecoveryOperation(i);
  r.state.phase = Hero12RecoveryPhase::Cancelled;
  return CameraError::None;
}
bool Hero12Adapter::serviceListHasFea6(const BleEvent &e, size_t offset, size_t length) {
  for (size_t n = offset + 1; n + 1 < offset + length; n += 2)
    if (e.bytes[n] == 0xa6 && e.bytes[n + 1] == 0xfe)
      return true;
  return false;
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
      found |= serviceListHasFea6(e, offset, length);
    offset += length;
  }
  return found;
}
bool Hero12Adapter::beginConnection(uint8_t i, Token token) {
  auto &p = peers_[i];

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
bool Hero12Adapter::beginCommand(uint8_t i, Operation op, Token token) {
  auto &p = peers_[i];
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
bool Hero12Adapter::begin(size_t index, const CameraConfig &camera, Operation op, Token token) {
  if (!enabled_ || !manager_ || index >= kBlePeers || camera.family != CameraFamily::GoPro ||
      camera.model != CameraModel::HERO12_BLACK) {
    recordAdmissionFailure(index);
    return false;
  }
  const auto i = static_cast<uint8_t>(index);
  auto &p = peers_[i];
  if (maintenance_[i] || !p.qualified || !validQualification(p.qualification)) {
    p.fault = Hero12Fault::Qualification;
    return false;
  }
  if (op == Operation::Connect)
    return beginConnection(i, token);
  return beginCommand(i, op, token);
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
const Hero12Adapter::StepSpec &Hero12Adapter::stepSpec(Step step) {
  // Indexed by the private Step enum; shared by encode, ACK publication and advancement.
  static const StepSpec specs[] = {
      {gopro::Request::HardwareInfo, 0, CameraAckAction::None, Step::None, false},      // None
      {gopro::Request::HardwareInfo, 0, CameraAckAction::Pair, Step::Claim, false},     // Pair
      {gopro::Request::HardwareInfo, 0, CameraAckAction::Claim, Step::Hardware, false}, // Claim
      {gopro::Request::HardwareInfo, 0, CameraAckAction::Hardware, Step::Api, true},    // Hardware
      {gopro::Request::ApiVersion, 0, CameraAckAction::Api, Step::RegisterBusy, true},  // Api
      {gopro::Request::RegisterBusy, 8, CameraAckAction::RegisterBusy, Step::RegisterEncoding,
       true}, // RegisterBusy
      {gopro::Request::RegisterEncoding, 10, CameraAckAction::RegisterEncoding, Step::RegisterReady,
       true}, // RegisterEncoding
      {gopro::Request::RegisterReady, 82, CameraAckAction::RegisterReady, Step::GetBusy,
       true},                                                                   // RegisterReady
      {gopro::Request::GetBusy, 8, CameraAckAction::GetBusy, Step::None, true}, // GetBusy
      {gopro::Request::GetEncoding, 10, CameraAckAction::GetEncoding, Step::GetReady,
       true},                                                                      // GetEncoding
      {gopro::Request::GetReady, 82, CameraAckAction::GetReady, Step::None, true}, // GetReady
      {gopro::Request::Video, 0, CameraAckAction::Video, Step::Shutter, true},     // Video
      {gopro::Request::ShutterOn, 0, CameraAckAction::ShutterOn, Step::ConfirmEncoding,
       true}, // Shutter
      {gopro::Request::GetEncoding, 10, CameraAckAction::ConfirmEncoding, Step::None,
       true}, // ConfirmEncoding
      {gopro::Request::GetEncoding, 10, CameraAckAction::QueryEncoding, Step::None,
       true},                                                                  // QueryEncoding
      {gopro::Request::KeepAlive, 0, CameraAckAction::None, Step::None, true}, // KeepAlive
  };
  const auto index = static_cast<size_t>(step);
  return specs[index < sizeof(specs) / sizeof(specs[0]) ? index : 0];
}
bool Hero12Adapter::preparePacket(uint8_t i, Step step, OutgoingPacket &out) const {
  const auto &p = peers_[i];
  if (setupStep(step)) {
    gopro::SetupPacket encoded;
    const auto op = step == Step::Pair ? gopro::SetupOperation::PairingFinish
                                       : gopro::SetupOperation::ClaimExternalControl;
    if (!gopro::encodeSetup(op, encoded))
      return false;
    out.route = encoded.channel;
    out.id = encoded.feature;
    out.length = encoded.size;
    std::memcpy(out.bytes, encoded.bytes, out.length);
  } else {
    const auto &spec = stepSpec(step);
    if (!spec.classic)
      return false;
    const auto request =
        step == Step::Shutter && !p.target_encoding ? gopro::Request::ShutterOff : spec.request;
    out.status = spec.status;
    gopro::Packet encoded;
    if (!gopro::encode(request, encoded))
      return false;
    out.route = encoded.channel;
    out.id = encoded.id;
    out.length = encoded.size;
    std::memcpy(out.bytes, encoded.bytes, out.length);
  }
  return true;
}
bool Hero12Adapter::send(uint8_t i, Step step) {
  auto &p = peers_[i];
  if (p.sealed || p.step != Step::None || !central_.admissionOpen(i))
    return false;
  if (p.ready && step != Step::KeepAlive && reached(clock_.now(), p.keepalive_due)) {
    p.resume = step;
    step = Step::KeepAlive;
  }
  OutgoingPacket out;
  if (!preparePacket(i, step, out))
    return false;
  return admitPacket(i, step, out);
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
bool Hero12Adapter::requestedStatusPresent(const Peer &p, const gopro::Response &r) {
  switch (p.step) {
  case Step::GetBusy:
    return r.busy_known;
  case Step::GetEncoding:
  case Step::QueryEncoding:
  case Step::ConfirmEncoding:
    return r.encoding_known;
  case Step::GetReady:
    return r.ready_known;
  case Step::RegisterBusy:
  case Step::RegisterEncoding:
  case Step::RegisterReady:
    return registrationStatusPresent(p.expected_status, r);
  default:
    return true;
  }
}
bool Hero12Adapter::registrationStatusPresent(uint8_t expected, const gopro::Response &r) {
  const unsigned known = static_cast<unsigned>(r.busy_known) |
                         (static_cast<unsigned>(r.encoding_known) << 1) |
                         (static_cast<unsigned>(r.ready_known) << 2);
  switch (expected) {
  case 8:
    return known == 1;
  case 10:
    return known == 2;
  case 82:
    return known == 4;
  default:
    return known == 0;
  }
}
bool Hero12Adapter::setupStep(Step step) { return step == Step::Pair || step == Step::Claim; }
bool Hero12Adapter::matchesCameraResponse(const Peer &p, gopro::Channel route,
                                          const gopro::Message &message) {
  return p.step != Step::None && !p.camera_done && route == p.route && message.size >= 2 &&
         message.bytes[0] == p.expected_id;
}
bool Hero12Adapter::classicResponse(uint8_t i, gopro::Channel route,
                                    const gopro::Message &message) {
  auto &p = peers_[i];

  const auto r = gopro::decode(route, message);
  if (r.outcome != gopro::Outcome::Complete || r.result != 0) {
    retire(i, r.outcome == gopro::Outcome::Complete ? Hero12Fault::CameraRejected
                                                    : Hero12Fault::Protocol);
    return false;
  }
  if (p.expected_status) {
    if (!requestedStatusPresent(p, r))
      return false;
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

  return true;
}
bool Hero12Adapter::completeCameraResponse(uint8_t i, gopro::Channel route,
                                           const gopro::Message &message) {
  auto &p = peers_[i];
  if (setupStep(p.step)) {
    if (!setupResponse(i, route, message))
      return false;
  } else if (p.step == Step::Hardware || p.step == Step::Api) {
    if (!identityResponse(i, route, message))
      return false;
  } else {
    if (!classicResponse(i, route, message))
      return false;
  }
  return true;
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
    unsolicitedStatus(i, route, message);
    return;
  }
  if (!matchesCameraResponse(p, route, message))
    return; // Unrelated notification is never a transaction completion.
  if (!completeCameraResponse(i, route, message))
    return;
  publishWireAck(i);
  p.camera_done = true;
}
Hero12Adapter::Step Hero12Adapter::advanceHardware(uint8_t i) {
  auto &p = peers_[i];
  if (p.hardware_not_ready) {
    p.hardware_not_ready = false;
    if (p.hardware_attempts == kHardwarePolls) {
      retire(i, Hero12Fault::Timeout);
      return Step::None;
    }
    p.hardware_retry_due = clock_.now() + kHardwarePollMs;
    return Step::None;
  }
  return Step::Api;
  return Step::None;
}
Hero12Adapter::Step Hero12Adapter::advanceReady(uint8_t i) {
  auto &p = peers_[i];
  if (p.connecting) {
    if (!p.busy_known || !p.encoding_known || !p.ready_known) {
      retire(i, Hero12Fault::Protocol);
      return Step::None;
    }
    p.connecting = false;
    p.ready = true;
    p.keepalive_due = clock_.now() + kKeepAliveMs;
    Event e(i, p.token, EventKind::Completed);
    e.capabilities.start = e.capabilities.stop = e.capabilities.query = CapabilityState::Supported;
    enqueue(e);
    // Publish initial state only after the manager accepts Connect completion.
    Event observed(i, p.connection, EventKind::RecordingObserved);
    observed.recording = p.encoding ? RecordingState::Recording : RecordingState::Stopped;
    enqueue(observed);
    return Step::None;
  }
  if (p.operation == Operation::Start) {
    if (p.busy || !p.camera_ready || p.encoding) {
      retire(i, Hero12Fault::CameraRejected);
      return Step::None;
    }
    return Step::Video;
  }
  return Step::None;
}
Hero12Adapter::Step Hero12Adapter::advanceQuery(uint8_t i) {
  auto &p = peers_[i];
  if (p.operation == Operation::Query) {
    enqueue(Event(i, p.token, EventKind::Completed));
    return Step::None;
  }
  if (p.encoding == p.target_encoding) {
    enqueue(Event(i, p.token, EventKind::Completed));
    return Step::None;
  }
  return p.operation == Operation::Start ? Step::GetBusy : Step::Shutter;
  return Step::None;
}
Hero12Adapter::Step Hero12Adapter::advanceConfirmation(uint8_t i) {
  auto &p = peers_[i];
  if (p.encoding != p.target_encoding) {
    retire(i, Hero12Fault::CameraRejected);
    return Step::None;
  }
  enqueue(Event(i, p.token, EventKind::Completed));
  return Step::None;
  return Step::None;
}
Hero12Adapter::Step Hero12Adapter::nextStep(uint8_t i, Step done) {
  auto &p = peers_[i];
  Step next = stepSpec(done).next;
  switch (done) {
  case Step::Hardware:
    return advanceHardware(i);
  case Step::GetBusy:
    next = p.connecting ? Step::GetEncoding : Step::GetReady;
    break;
  case Step::GetReady:
    return advanceReady(i);
  case Step::QueryEncoding:
    return advanceQuery(i);
  case Step::ConfirmEncoding:
    return advanceConfirmation(i);
  case Step::KeepAlive:
    p.keepalive_due = clock_.now() + kKeepAliveMs;
    next = p.resume;
    p.resume = Step::None;
    break;
  default:
    break;
  }
  return next;
}
void Hero12Adapter::advance(uint8_t i) {
  auto &p = peers_[i];
  if (!acknowledged(i))
    return;
  const auto done = p.step;
  p.step = Step::None;
  const auto next = nextStep(i, done);
  if (next != Step::None && !send(i, next))
    retire(i, Hero12Fault::DeliveryUncertain);
}
bool Hero12Adapter::acknowledged(uint8_t i) const {
  const auto &p = peers_[i];
  return !p.sealed && p.step != Step::None && p.att_done && p.camera_done &&
         central_.admissionOpen(i);
}

void Hero12Adapter::notification(uint8_t i, const BleResult &r) {
  auto &p = peers_[i];

  gopro::Channel route;
  if (!notificationRoute(r.endpoint, route)) {
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
void Hero12Adapter::writeComplete(uint8_t i, const BleResult &r) {
  auto &p = peers_[i];

  if (p.waiting_write && r.endpoint == p.write_endpoint && p.step != Step::None) {
    p.att_done = true;
    p.waiting_write = false;
  }
}
bool Hero12Adapter::setupResponse(uint8_t i, gopro::Channel route, const gopro::Message &message) {
  auto &p = peers_[i];

  const auto expected = p.step == Step::Pair ? gopro::SetupOperation::PairingFinish
                                             : gopro::SetupOperation::ClaimExternalControl;
  const auto r = gopro::decodeSetup(route, message, expected);
  if (r.outcome != gopro::Outcome::Complete ||
      r.domain != gopro::SetupResultDomain::GenericProtobuf || !r.result_present ||
      !r.known_result || !r.success) {
    retire(i, r.outcome == gopro::Outcome::Complete ? Hero12Fault::SetupRejected
                                                    : Hero12Fault::Protocol);
    return false;
  }

  return true;
}
bool Hero12Adapter::identityResponse(uint8_t i, gopro::Channel route,
                                     const gopro::Message &message) {
  auto &p = peers_[i];

  const auto r = gopro::decodeIdentity(route, message);
  if (r.outcome != gopro::Outcome::Complete) {
    retire(i, Hero12Fault::Protocol);
    return false;
  }
  if (p.step == Step::Hardware && r.classic_result != 0) {
    p.hardware_not_ready = true;
    p.camera_done = true;
    return false;
  }
  if (r.classic_result != 0) {
    retire(i, Hero12Fault::CameraRejected);
    return false;
  }
  return validateIdentity(i, r);
}
void Hero12Adapter::publishWireAck(uint8_t i) {
  auto &p = peers_[i];
  if (!manager_ || p.step == Step::KeepAlive)
    return;

  CameraAckDomain domain =
      (setupStep(p.step)) ? CameraAckDomain::Protobuf : CameraAckDomain::Classic;
  const auto action = p.step == Step::Shutter && !p.target_encoding ? CameraAckAction::ShutterOff
                                                                    : stepSpec(p.step).ack;
  manager_->wireAck(i, p.token, domain, action);
}
void Hero12Adapter::unsolicitedStatus(uint8_t i, gopro::Channel route,
                                      const gopro::Message &message) {
  auto &p = peers_[i];

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
bool Hero12Adapter::failedRecoveryOperation(uint8_t i) {
  auto &r = recovery_[i];
  const auto *state = manager_->state(i);
  if (state->token.connection == r.connection && state->token.operation == r.operation &&
      state->lifecycle != Lifecycle::Idle && state->lifecycle != Lifecycle::Failed)
    return false;
  r.state.phase = peers_[i].fault == Hero12Fault::Timeout || state->error == CameraError::Timeout
                      ? Hero12RecoveryPhase::Timeout
                      : Hero12RecoveryPhase::Failed;
  return true;
}
void Hero12Adapter::requestRecoveryOperation(uint8_t i, Operation operation) {
  auto &r = recovery_[i];
  const auto *state = manager_->state(i);
  if (operation != Operation::Connect) {
    if (state->lifecycle == Lifecycle::Idle || state->lifecycle == Lifecycle::Failed ||
        peers_[i].sealed || !peers_[i].ready) {
      r.state.phase = Hero12RecoveryPhase::Failed;
      return;
    }
    // Never queue behind a competing owner or overlap serialized keepalive.
    if (state->lifecycle != Lifecycle::Ready || peers_[i].step != Step::None ||
        !central_.admissionOpen(i))
      return;
  }
  if (manager_->request(i, operation) != CameraError::None) {
    r.state.phase = Hero12RecoveryPhase::Failed;
    return;
  }
  state = manager_->state(i);
  r.operation = state->token.operation;
  r.connection = state->token.connection;
}
void Hero12Adapter::finishRecoveryObservation(uint8_t i, bool require_known) {
  auto &r = recovery_[i];
  const auto *state = manager_->state(i);
  if (!state->has_observation || (require_known && state->observed == RecordingState::Unknown))
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
void Hero12Adapter::recoveryConnect(uint8_t i) {
  auto &r = recovery_[i];
  const auto *state = manager_->state(i);
  if (!r.operation) {
    requestRecoveryOperation(i, Operation::Connect);
    return;
  }
  if (failedRecoveryOperation(i))
    return;
  if (state->lifecycle == Lifecycle::Ready) {
    finishRecoveryObservation(i, false);
  }
}
void Hero12Adapter::recoveryObserve(uint8_t i) {
  auto &r = recovery_[i];
  const auto *state = manager_->state(i);
  if (!r.operation) {
    requestRecoveryOperation(i, Operation::Query);
    return;
  }
  if (failedRecoveryOperation(i))
    return;
  if (state->lifecycle == Lifecycle::Ready && state->token.operation == r.operation) {
    finishRecoveryObservation(i, true);
  }
}
void Hero12Adapter::recoveryStart(uint8_t i) {
  auto &r = recovery_[i];
  const auto *state = manager_->state(i);
  if (!r.operation) {
    requestRecoveryOperation(i, Operation::Start);
    return;
  }
  if (failedRecoveryOperation(i))
    return;
  if (state->lifecycle == Lifecycle::Ready && state->token.operation == r.operation) {
    r.state.phase = state->has_observation && state->observed == RecordingState::Recording
                        ? Hero12RecoveryPhase::Recording
                        : Hero12RecoveryPhase::Unavailable;
  }
}
void Hero12Adapter::recoveryAdvertisement(const BleResult &r) {

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
void Hero12Adapter::recoveryScanComplete(const BleResult &r) {

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
void Hero12Adapter::transportClosed(const BleResult &r) {
  const auto i = r.event.peer;

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
void Hero12Adapter::transportEvent(uint8_t i, const BleResult &r) {
  auto &p = peers_[i];
  if (r.kind == BleResultKind::TransportReady) {
    if (!p.connecting || !send(i, Step::Pair))
      retire(i, Hero12Fault::DeliveryUncertain);
  } else if (r.kind == BleResultKind::WriteComplete) {
    writeComplete(i, r);
  } else if (r.kind == BleResultKind::Notification) {
    notification(i, r);
  }
}
bool Hero12Adapter::ownsScanResult(const BleResult &r) const {
  return scan_owner_ < kBlePeers && r.event.generation == scan_generation_;
}
void Hero12Adapter::result(const BleResult &r) {
  if (r.kind == BleResultKind::Advertisement && ownsScanResult(r)) {
    recoveryAdvertisement(r);
    return;
  }
  if (r.kind == BleResultKind::ScanComplete && ownsScanResult(r)) {
    recoveryScanComplete(r);
    return;
  }
  const uint8_t i = r.event.peer;
  if (i >= kBlePeers)
    return;
  auto &p = peers_[i];
  if (r.event.generation != p.connection)
    return;
  if (r.kind == BleResultKind::Retired || r.kind == BleResultKind::LinkClosed) {
    transportClosed(r);
    return;
  }
  if (p.sealed)
    return;
  transportEvent(i, r);
}
void Hero12Adapter::advanceRecoveryPeer(uint8_t i) {

  auto &r = recovery_[i];
  const auto phase = r.state.phase;
  if (!activeRecoveryPhase(phase))
    return;
  if (reached(clock_.now(), r.deadline)) {
    cancelRecovery(i);
    r.state.phase = Hero12RecoveryPhase::Timeout;
    return;
  }
  const auto *state = manager_->state(i);
  if (!r.operation && phase != Hero12RecoveryPhase::Starting &&
      state->token.connection != r.source_connection) {
    cancelRecovery(i);
    r.state.phase = Hero12RecoveryPhase::Failed;
    return;
  }
  if (phase == Hero12RecoveryPhase::Connecting) {
    recoveryConnect(i);
  } else if (phase == Hero12RecoveryPhase::Observing) {
    recoveryObserve(i);
  } else if (phase == Hero12RecoveryPhase::Starting) {
    recoveryStart(i);
  }
}
void Hero12Adapter::beginRecoveryScan() {
  if (maintenance_scan_ || scan_owner_ != kBlePeers)
    return;
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
void Hero12Adapter::advanceRecovery() {
  for (uint8_t i = 0; i < kBlePeers; ++i) {
    advanceRecoveryPeer(i);
  }
  beginRecoveryScan();
}
bool Hero12Adapter::hardwareRetryReady(uint8_t i) const {
  const auto &p = peers_[i];
  return p.connecting && p.hardware_retry_due && reached(clock_.now(), p.hardware_retry_due) &&
         central_.admissionOpen(i);
}
void Hero12Adapter::servicePeer(uint8_t i) {
  auto &p = peers_[i];
  if (p.sealed)
    return;
  if (transactionExpired(i)) {
    retire(i, Hero12Fault::Timeout);
    return;
  }
  if (p.step != Step::None) {
    advance(i);
    return;
  }
  if (hardwareRetryReady(i)) {
    p.hardware_retry_due = 0;
    if (!send(i, Step::Hardware))
      retire(i, Hero12Fault::DeliveryUncertain);
    return;
  }
  if (p.ready && reached(clock_.now(), p.keepalive_due) && central_.admissionOpen(i) &&
      !send(i, Step::KeepAlive))
    retire(i, Hero12Fault::DeliveryUncertain);
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
    servicePeer(i);
  }
  cursor_ = (cursor_ + 1) % kBlePeers;
  drain(); // Connection-scoped Disconnected precedes every manager tick.
  if (action)
    action->beforeAdvance();
  advanceManagers(advance_managers);
  if (manager_ && enabled_)
    advanceRecovery();
  servicing_ = false;
}
} // namespace ridesync

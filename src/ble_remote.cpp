#include "ble_remote.h"
#include <cstdlib>
#include <cstring>
#include <limits>

namespace ridesync {
namespace {
bool expired(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }
bool validIdentity(const BondIdentity &id) {
  bool nonzero = false, all_ff = true;
  for (auto b : id.address) {
    nonzero |= b != 0;
    all_ff &= b == 0xff;
  }
  return id.verified && nonzero && !all_ff &&
         (id.type == IdentityType::Public ||
          (id.type == IdentityType::RandomStatic && (id.address[5] & 0xc0) == 0xc0));
}
bool sameIdentity(const BondIdentity &a, const BondIdentity &b) {
  return a.type == b.type && a.address == b.address;
}
bool validUuid(const BleUuid &u) { return u.size == 2 || u.size == 16; }
} // namespace
BleUuid BleUuid::shortUuid(uint16_t v) {
  BleUuid u;
  u.bytes[0] = v & 0xff;
  u.bytes[1] = v >> 8;
  return u;
}
bool BleUuid::operator==(const BleUuid &u) const {
  return size == u.size && validUuid(*this) && std::memcmp(bytes.data(), u.bytes.data(), size) == 0;
}
bool BleBondAdmission::allowed() const {
  return identity_matches && persistence_allowed &&
         admitBond(stack_ready, restore_verified, refusal_installed, used, capacity,
                   existing_verified_identity);
}
BleCentral::BleCentral(BleHost &host, BleResultSink &sink, HealthProgress *health)
    : host_(host), sink_(sink), health_(health) {}
BleCentral::~BleCentral() {
  // Runtime callers must keep this fixed object alive while cleanup is unresolved.
  // No destructor may free callback contexts merely because stop was requested.
  if (!canDestroy())
    std::abort();
}
bool BleCentral::begin(bool enabled, bool qualified, uint32_t now) {
  if (begun_)
    return false;
  begun_ = true;
  if (!enabled || !qualified)
    return false;
  const auto state = host_.start(true, true);
  enabled_ = state == BleHostState::Starting || state == BleHostState::Ready;
  startup_deadline_ = now + kStartupDeadlineMs;
  startup_confirmed_ = state == BleHostState::Ready;
  return enabled_;
}
void BleCentral::resetContext(BleContext &c, uint8_t peer, uint32_t generation, BlePhase phase,
                              uint32_t procedure, uint16_t connection, uint16_t handle) {
  c.receiver = this;
  c.peer = peer;
  c.generation = generation;
  c.phase = phase;
  c.procedure = procedure;
  c.expected_handle = handle;
  c.connection.store(connection);
  c.sealed.store(false);
  c.terminal.store(false);
  c.terminal_status.store(0);
  c.fault.store(BleFault::None);
}
bool BleCentral::connect(uint8_t i, uint32_t gen, const BondIdentity &id,
                         const BleProfileSpec &spec) {
  if (!enabled_ || stopping_ || startup_failed_ || host_.state() != BleHostState::Ready ||
      host_.fault() != BleFault::None || i >= kBlePeers || gen == 0 || !validIdentity(id) ||
      spec.service_count == 0 || spec.service_count > kBleServices || spec.endpoint_count == 0 ||
      spec.endpoint_count > kBleEndpoints)
    return false;
  for (uint8_t n = 0; n < spec.service_count; ++n) {
    if (!validUuid(spec.services[n]))
      return false;
    for (uint8_t previous = 0; previous < n; ++previous)
      if (spec.services[n] == spec.services[previous])
        return false;
  }
  for (uint8_t n = 0; n < spec.endpoint_count; ++n) {
    const auto &e = spec.endpoints[n];
    if (!validUuid(e.uuid) || e.service >= spec.service_count || !e.properties || e.subscribe > 2 ||
        (e.subscribe && !(e.properties & (e.subscribe == 1 ? 0x10 : 0x20))))
      return false;
    for (uint8_t previous = 0; previous < n; ++previous)
      if (e.service == spec.endpoints[previous].service && e.uuid == spec.endpoints[previous].uuid)
        return false;
  }
  auto &p = peers_[i];
  if (p.phase != BlePhase::Empty && p.phase != BlePhase::Closed)
    return false;
  if (p.phase == BlePhase::Closed && gen <= p.link.generation)
    return false; // Connection generations may not wrap/replay in this lifetime.
  for (uint8_t n = 0; n < kBlePeers; ++n)
    if (n != i && peers_[n].phase != BlePhase::Empty && peers_[n].phase != BlePhase::Closed &&
        sameIdentity(peers_[n].identity, id))
      return false;
  p.identity = id;
  p.spec = spec;
  p.phase = BlePhase::Queued;
  p.service = p.endpoint = p.char_count = 0;
  p.endpoints = {};
  p.service_start = {};
  p.service_end = {};
  p.next_procedure = 0;
  p.deferred = p.security_waiting = false;
  p.active = p.complete = p.retired_reported = p.terminate_submitted = p.cancel_submitted = false;
  p.retirement = BleFault::None;
  p.error = 0;
  // Queued peers have no SDK references yet, but seal checks still work.
  resetContext(p.link, i, gen, BlePhase::Connect, 0, kBleNoHandle, 0);
  p.link.terminal.store(true);
  p.procedure.receiver = nullptr;
  p.procedure.sealed.store(false);
  p.procedure.fault.store(BleFault::None);
  return true;
}
bool BleCentral::scan(uint32_t duration, uint32_t now) {
  if (!enabled_ || stopping_ || startup_failed_ || host_.state() != BleHostState::Ready ||
      host_.fault() != BleFault::None || scanning_ || initiating_ != kBlePeers || !duration ||
      duration > kPhaseDeadlineMs || (scan_.receiver && !host_.quiescent(scan_)))
    return false;
  if (scan_.generation == UINT32_MAX)
    return false;
  resetContext(scan_, kBlePeers, scan_.generation + 1, BlePhase::Scan, 0, kBleNoHandle, 0);
  scanning_ = true;
  scan_reported_ = false;
  scan_cancel_attempted_ = false;
  scan_cancel_error_ = 0;
  scan_duration_ = duration;
  scan_deadline_ = now + duration;
  BleCommand c;
  c.phase = BlePhase::Scan;
  c.duration_ms = duration;
  const int rc = host_.submit(c, scan_);
  scan_pending_ = rc == kBleHostReserved;
  if (scan_pending_)
    scan_.terminal.store(false);
  if (rc && !scan_pending_) {
    scan_.sealed.store(true);
    scan_.fault.store(BleFault::Host);
    return false;
  }
  return true;
}
bool BleCentral::cancelScan() {
  if (!scanning_ || scan_.sealed.load() || scan_.terminal.load())
    return false;
  scan_.sealed.store(true);
  scan_.fault.store(BleFault::Stopped);
  cancelScanOnce();
  return true;
}
bool BleCentral::admissionOpen(uint8_t i) const {
  if (!enabled_ || stopping_ || startup_failed_ || i >= kBlePeers ||
      host_.fault() != BleFault::None)
    return false;
  const auto &p = peers_[i];
  return p.phase == BlePhase::ReadyForProfile && !p.link.sealed.load() &&
         !p.procedure.sealed.load() && p.link.fault.load() == BleFault::None &&
         p.procedure.fault.load() == BleFault::None && !p.link.terminal.load();
}
bool BleCentral::read(uint8_t i, uint8_t endpoint, uint32_t now) {
  if (!admissionOpen(i) || endpoint >= peers_[i].spec.endpoint_count ||
      !(peers_[i].spec.endpoints[endpoint].properties & 2))
    return false;
  auto &p = peers_[i];
  p.endpoint = endpoint;
  BleCommand c;
  c.phase = BlePhase::Read;
  c.handle = p.endpoints[endpoint].value;
  return launch(i, c, now);
}
bool BleCentral::write(uint8_t i, uint8_t endpoint, const uint8_t *data, size_t size,
                       uint32_t now) {
  if (!admissionOpen(i) || endpoint >= peers_[i].spec.endpoint_count || !data || !size ||
      size > kBlePayload || !(peers_[i].spec.endpoints[endpoint].properties & 8))
    return false;
  auto &p = peers_[i];
  p.endpoint = endpoint;
  BleCommand c;
  c.phase = BlePhase::Write;
  c.handle = p.endpoints[endpoint].value;
  c.size = size;
  std::memcpy(c.bytes.data(), data, size);
  return launch(i, c, now);
}
void BleCentral::disconnect(uint8_t i) {
  if (i < kBlePeers)
    retire(i, BleFault::Stopped);
}
void BleCentral::cancelScanOnce() {
  if (!scanning_ || scan_cancel_attempted_ || scan_.terminal.load())
    return;
  if (scan_pending_) {
    scan_pending_ = false;
    scan_.terminal.store(true);
    return;
  }
  scan_cancel_attempted_ = true; // Before call: immediate terminal callbacks are safe.
  scan_cancel_error_ = host_.cancelScan(scan_);
  if (scan_cancel_error_ == kBleHostReserved) {
    scan_cancel_attempted_ = false;
    scan_cancel_error_ = 0;
    return;
  }
  if (scan_cancel_error_) {
    BleResult result;
    result.kind = BleResultKind::ScanCancelFailed;
    result.fault = BleFault::Host;
    result.event.peer = kBlePeers;
    result.event.generation = scan_.generation;
    result.event.phase = BlePhase::Scan;
    result.event.status = scan_cancel_error_;
    sink_.result(result);
  }
}
void BleCentral::stop() {
  stopping_ = true;
  scan_.sealed.store(true);
  if (scanning_) {
    if (scan_.fault.load() == BleFault::None)
      scan_.fault.store(BleFault::Stopped);
    cancelScanOnce();
  }
  for (uint8_t i = 0; i < kBlePeers; ++i)
    retire(i, BleFault::Stopped);
}
BlePhase BleCentral::phase(uint8_t i) const {
  return i < kBlePeers ? peers_[i].phase : BlePhase::Empty;
}
int BleCentral::error(uint8_t i) const { return i < kBlePeers ? peers_[i].error : 0; }
void BleCentral::copied(BleContext &c, BleEvent e) {
  // Metadata comes only from the immutable initiating context, never a live map.
  e.peer = c.peer;
  e.generation = c.generation;
  e.phase = c.phase;
  e.procedure = c.procedure;
  if (e.kind == BleEventKind::Disconnected || e.kind == BleEventKind::ScanComplete ||
      (e.kind == BleEventKind::Connected && e.status != 0)) {
    c.terminal_status.store(e.status);
    c.terminal.store(true, std::memory_order_release);
  }
  if (e.kind == BleEventKind::Connected && e.status == 0)
    c.connection.store(e.connection, std::memory_order_release);
  if (e.size > kBlePayload || (c.peer < kBlePeers && e.kind != BleEventKind::Connected &&
                               e.connection != c.connection.load())) {
    c.sealed.store(true);
    c.fault.store(BleFault::Malformed);
    return;
  }
  if (e.kind == BleEventKind::Connected && e.status != 0) {
    c.sealed.store(true);
    c.fault.store(BleFault::Host);
    return;
  }
  if (e.kind == BleEventKind::Disconnected) {
    // Terminal observation is independent of the copied queue's capacity.
    c.sealed.store(true);
    return;
  }
  if (c.sealed.load())
    return;
  if (queue_lock_.test_and_set(std::memory_order_acquire)) {
    c.sealed.store(true);
    c.fault.store(BleFault::Overflow);
    return;
  }
  if (count_ == kBleQueue) {
    c.sealed.store(true);
    c.fault.store(BleFault::Overflow);
  } else {
    queue_[(head_ + count_) % kBleQueue] = e;
    ++count_;
  }
  queue_lock_.clear(std::memory_order_release);
}
bool BleCentral::pop(BleEvent &e) {
  if (queue_lock_.test_and_set(std::memory_order_acquire))
    return false;
  const bool found = count_ != 0;
  if (found) {
    e = queue_[head_];
    head_ = (head_ + 1) % kBleQueue;
    --count_;
  }
  queue_lock_.clear(std::memory_order_release);
  return found;
}
void BleCentral::emit(uint8_t i, BleResultKind kind, const BleEvent &event) {
  BleResult r;
  r.kind = kind;
  r.event = event;
  if (i < kBlePeers) {
    const auto &p = peers_[i];
    r.event.peer = i;
    r.event.generation = p.link.generation;
    r.event.connection = p.link.connection.load();
    r.endpoint = p.endpoint;
    r.fault = p.retirement;
    if (kind == BleResultKind::Retired)
      r.event.status = p.error;
  }
  sink_.result(r);
}
void BleCentral::retire(uint8_t i, BleFault fault, int error) {
  auto &p = peers_[i];
  if (p.phase == BlePhase::Empty || p.phase == BlePhase::Closed)
    return;
  p.link.sealed.store(true);
  p.procedure.sealed.store(true);
  if (!p.retired_reported) {
    p.retirement = fault;
    p.error = error;
    p.retired_reported = true;
    const bool queued = p.phase == BlePhase::Queued;
    p.phase = BlePhase::Retiring;
    emit(i, BleResultKind::Retired);
    if (queued) {
      p.phase = BlePhase::Closed;
      p.link.receiver = nullptr;
      emit(i, BleResultKind::LinkClosed);
      return;
    }
  }
  const bool connecting = p.link.connection.load() == kBleNoHandle;
  if (!p.link.terminal.load() && !(connecting ? p.cancel_submitted : p.terminate_submitted)) {
    const int rc = host_.retire(p.link);
    if (rc == kBleHostReserved)
      return;
    if (connecting)
      p.cancel_submitted = true;
    else
      p.terminate_submitted = true;
    p.phase = BlePhase::Quarantined; // Submission never proves teardown.
    if (rc)
      p.error = rc;
  }
}
bool BleCentral::launch(uint8_t i, BleCommand c, uint32_t now) {
  auto &p = peers_[i];
  if (p.link.sealed.load() || p.link.fault.load() != BleFault::None || stopping_ ||
      p.next_procedure == UINT32_MAX || (p.procedure.receiver && !host_.quiescent(p.procedure))) {
    retire(i, BleFault::Admission);
    return false;
  }
  p.phase = c.phase;
  if (!p.deferred)
    p.deadline = now + kPhaseDeadlineMs;
  p.deferred = true;
  p.deferred_command = c;
  if (c.phase == BlePhase::Write || c.phase == BlePhase::Subscribe) {
    const auto mtu = host_.mtu(p.link.connection.load());
    if (mtu == kBleMtuReserved)
      return true;
    if (mtu < 3 || c.size > static_cast<size_t>(mtu - 3)) {
      p.deferred = false;
      p.phase = BlePhase::ReadyForProfile;
      return false;
    }
  }
  p.active = true;
  p.complete = false;
  p.error = 0;
  c.connection = p.link.connection.load();
  resetContext(p.procedure, i, p.link.generation, c.phase, ++p.next_procedure, c.connection,
               c.handle);
  const int rc = host_.submit(c, p.procedure);
  if (rc == kBleHostReserved) {
    p.active = false;
    p.procedure.receiver = nullptr;
    return true;
  }
  p.deferred = false;
  if (rc) {
    retire(i, BleFault::Host, rc);
    return false;
  }
  return true;
}
void BleCentral::finishDiscovery(uint8_t i) {
  auto &p = peers_[i];
  for (uint8_t n = 0; n < p.spec.endpoint_count; ++n) {
    const auto &spec = p.spec.endpoints[n];
    auto &endpoint = p.endpoints[n];
    for (uint8_t c = 0; c < p.char_count; ++c) {
      const auto &chr = p.chars[c];
      if (chr.service == spec.service && chr.uuid == spec.uuid) {
        if (endpoint.value || (chr.properties & spec.properties) != spec.properties) {
          retire(i, BleFault::MissingProperty);
          return;
        }
        endpoint.value = chr.value;
        endpoint.end = p.service_end[spec.service];
        for (uint8_t next = 0; next < p.char_count; ++next) {
          const auto &other = p.chars[next];
          if (other.service == spec.service && other.declaration > chr.declaration &&
              other.declaration <= endpoint.end)
            endpoint.end = other.declaration - 1;
        }
      }
    }
    if (!endpoint.value) {
      retire(i, BleFault::MissingProperty);
      return;
    }
  }
  p.endpoint = 0;
  p.phase = BlePhase::Descriptors;
}
void BleCentral::event(const BleEvent &e) {
  if (e.peer == kBlePeers) {
    if (scanning_ && e.generation == scan_.generation && !scan_.sealed.load()) {
      BleResult r;
      r.kind = e.kind == BleEventKind::Advertisement ? BleResultKind::Advertisement
                                                     : BleResultKind::ScanComplete;
      r.event = e;
      if (e.kind == BleEventKind::ScanComplete)
        scan_reported_ = true;
      sink_.result(r);
    }
    return;
  }
  if (e.peer >= kBlePeers)
    return;
  auto &p = peers_[e.peer];
  if (e.generation != p.link.generation || p.link.sealed.load() ||
      p.procedure.fault.load() != BleFault::None || p.phase == BlePhase::Closed)
    return;
  if (e.kind == BleEventKind::Connected) {
    if (p.phase != BlePhase::Connect)
      return;
    if (e.status || e.connection == kBleNoHandle) {
      retire(e.peer, BleFault::Host, e.status);
      return;
    }
    initiating_ = kBlePeers;
    p.phase = BlePhase::Security;
    p.active = false;
    return;
  }
  if (e.kind == BleEventKind::Security) {
    if (p.link.connection.load() == kBleNoHandle)
      return;
    if (e.status || !e.encrypted || !e.bonded ||
        (p.spec.require_authenticated && !e.authenticated) ||
        !sameIdentity(e.identity, p.identity)) {
      retire(e.peer, BleFault::Identity, e.status);
      return;
    }
    if (p.phase == BlePhase::Security) {
      p.active = false;
      p.phase = BlePhase::Services;
    }
    return;
  }
  if (e.kind == BleEventKind::Notification) {
    if (p.phase != BlePhase::ReadyForProfile && p.phase != BlePhase::Read &&
        p.phase != BlePhase::Write)
      return;
    for (uint8_t n = 0; n < p.spec.endpoint_count; ++n)
      if (p.spec.endpoints[n].subscribe && p.endpoints[n].value == e.handle) {
        BleResult r;
        r.kind = BleResultKind::Notification;
        r.event = e;
        r.endpoint = n;
        sink_.result(r);
        return;
      }
    retire(e.peer, BleFault::Malformed);
    return;
  }
  if (!p.active || e.procedure != p.procedure.procedure || e.phase != p.phase)
    return;
  if (e.kind == BleEventKind::Complete) {
    if ((p.phase == BlePhase::Subscribe || p.phase == BlePhase::VerifySubscription ||
         p.phase == BlePhase::Read || p.phase == BlePhase::Write) &&
        e.status == 0 && e.handle != p.procedure.expected_handle) {
      retire(e.peer, BleFault::Malformed);
      return;
    }
    if (e.status) {
      retire(e.peer, BleFault::Att, e.status);
      return;
    }
    if (p.phase == BlePhase::VerifySubscription &&
        (e.size != 2 || e.bytes[0] != p.spec.endpoints[p.endpoint].subscribe || e.bytes[1] != 0)) {
      retire(e.peer, BleFault::MissingCccd);
      return;
    }
    if (p.phase == BlePhase::Read || p.phase == BlePhase::Write)
      emit(e.peer,
           p.phase == BlePhase::Read ? BleResultKind::ReadComplete : BleResultKind::WriteComplete,
           e);
    p.complete = true;
    return;
  }
  if (p.phase == BlePhase::Services && e.kind == BleEventKind::Service) {
    if (!(e.uuid == p.spec.services[p.service]) || !e.start || e.end < e.start ||
        p.service_start[p.service]) {
      retire(e.peer, BleFault::Malformed);
      return;
    }
    for (uint8_t s = 0; s < p.service; ++s)
      if (!(e.end < p.service_start[s] || e.start > p.service_end[s])) {
        retire(e.peer, BleFault::Malformed);
        return;
      }
    p.service_start[p.service] = e.start;
    p.service_end[p.service] = e.end;
  } else if (p.phase == BlePhase::Characteristics && e.kind == BleEventKind::Characteristic) {
    if (p.char_count == kBleDiscoveredChars || !validUuid(e.uuid) ||
        e.start < p.service_start[p.service] || e.start >= e.handle ||
        e.handle > p.service_end[p.service]) {
      retire(e.peer, BleFault::Malformed);
      return;
    }
    for (uint8_t n = 0; n < p.char_count; ++n)
      if (p.chars[n].declaration == e.start || p.chars[n].value == e.handle) {
        retire(e.peer, BleFault::Malformed);
        return;
      }
    auto &chr = p.chars[p.char_count++];
    chr.uuid = e.uuid;
    chr.service = p.service;
    chr.declaration = e.start;
    chr.value = e.handle;
    chr.properties = e.properties;
  } else if (p.phase == BlePhase::Descriptors && e.kind == BleEventKind::Descriptor) {
    auto &endpoint = p.endpoints[p.endpoint];
    if (e.handle <= endpoint.value || e.handle > endpoint.end) {
      retire(e.peer, BleFault::Malformed);
      return;
    }
    if (e.uuid == BleUuid::shortUuid(0x2902)) {
      if (endpoint.cccd) {
        retire(e.peer, BleFault::Malformed);
        return;
      }
      endpoint.cccd = e.handle;
    }
  } else {
    retire(e.peer, BleFault::Malformed);
  }
}
void BleCentral::advance(uint8_t i, uint32_t now) {
  auto &p = peers_[i];
  if (p.link.sealed.load())
    return;
  if (p.deferred) {
    launch(i, p.deferred_command, now);
    return;
  }
  if (p.active) {
    if (!p.complete || !host_.quiescent(p.procedure))
      return;
    p.active = p.complete = false;
    switch (p.phase) {
    case BlePhase::Services:
      if (!p.service_start[p.service]) {
        retire(i, BleFault::MissingService);
        return;
      }
      if (++p.service == p.spec.service_count) {
        p.service = 0;
        p.phase = BlePhase::Characteristics;
      }
      break;
    case BlePhase::Characteristics:
      if (++p.service == p.spec.service_count)
        finishDiscovery(i);
      break;
    case BlePhase::Descriptors:
      if (!p.endpoints[p.endpoint].cccd) {
        retire(i, BleFault::MissingCccd);
        return;
      }
      p.phase = BlePhase::Subscribe;
      break;
    case BlePhase::Subscribe:
      p.phase = BlePhase::VerifySubscription;
      break;
    case BlePhase::VerifySubscription:
      ++p.endpoint;
      p.phase = BlePhase::Descriptors;
      break;
    case BlePhase::Read:
    case BlePhase::Write:
      p.phase = BlePhase::ReadyForProfile;
      return;
    default:
      return;
    }
    if (p.link.sealed.load())
      return;
  }
  BleCommand c;
  c.phase = p.phase;
  if (p.phase == BlePhase::Services) {
    c.uuid = p.spec.services[p.service];
  } else if (p.phase == BlePhase::Characteristics) {
    c.start = p.service_start[p.service];
    c.end = p.service_end[p.service];
  } else if (p.phase == BlePhase::Descriptors) {
    while (p.endpoint < p.spec.endpoint_count && !p.spec.endpoints[p.endpoint].subscribe)
      ++p.endpoint;
    if (p.endpoint == p.spec.endpoint_count) {
      p.phase = BlePhase::ReadyForProfile;
      emit(i, BleResultKind::TransportReady);
      return;
    }
    c.start = p.endpoints[p.endpoint].value;
    c.end = p.endpoints[p.endpoint].end;
    if (c.end <= c.start) {
      retire(i, BleFault::MissingCccd);
      return;
    }
  } else if (p.phase == BlePhase::Subscribe || p.phase == BlePhase::VerifySubscription) {
    c.handle = p.endpoints[p.endpoint].cccd;
    c.size = 2;
    c.bytes[0] = p.spec.endpoints[p.endpoint].subscribe;
  } else if (p.phase == BlePhase::Security && !p.active) {
    const auto admission = host_.bondAdmission(p.identity);
    if (admission.reserved)
      return;
    if (!admission.allowed()) {
      retire(i, BleFault::Store);
      return;
    }
    p.active = true;
    if (!p.security_waiting)
      p.deadline = now + kPhaseDeadlineMs;
    p.security_waiting = true;
    c.connection = p.link.connection.load();
    const int rc = host_.submit(c, p.link);
    if (rc == kBleHostReserved) {
      p.active = false;
      return;
    }
    p.security_waiting = false;
    if (rc)
      retire(i, BleFault::Host, rc);
    return;
  } else {
    return;
  }
  launch(i, c, now);
}
void BleCentral::service(uint32_t now) {
  if (!enabled_)
    return;
  const auto hs = host_.state();
  if (!startup_confirmed_ && expired(now, startup_deadline_)) {
    startup_failed_ = true;
    host_.sealStartup();
  }
  if (hs == BleHostState::Ready && !startup_failed_)
    startup_confirmed_ = true;
  if (hs == BleHostState::Failed || host_.fault() != BleFault::None)
    startup_failed_ = true;
  // Every independent latch is processed before any ordinary copied event.
  auto faults = [&] {
    for (uint8_t i = 0; i < kBlePeers; ++i) {
      auto &p = peers_[i];
      if (p.phase == BlePhase::Empty || p.phase == BlePhase::Closed)
        continue;
      const auto lf = p.link.fault.load(), pf = p.procedure.fault.load();
      if (startup_failed_)
        retire(i, host_.fault() == BleFault::Store ? BleFault::Store : BleFault::Host);
      else if (lf != BleFault::None || pf != BleFault::None)
        retire(i, lf != BleFault::None ? lf : pf, p.link.terminal_status.load());
      else if (p.link.terminal.load() && p.phase != BlePhase::Queued)
        retire(i, BleFault::Att, p.link.terminal_status.load());
      else if (p.phase != BlePhase::Queued && p.phase != BlePhase::ReadyForProfile &&
               p.phase != BlePhase::Retiring && p.phase != BlePhase::Quarantined &&
               expired(now, p.deadline))
        retire(i, BleFault::Timeout);
    }
  };
  faults();
  if (scan_pending_ && !scan_.sealed.load() && !expired(now, scan_deadline_)) {
    BleCommand command;
    command.phase = BlePhase::Scan;
    command.duration_ms = scan_deadline_ - now;
    scan_.terminal.store(false);
    const int rc = host_.submit(command, scan_);
    if (rc == kBleHostReserved)
      scan_.terminal.store(false);
    else {
      scan_pending_ = false;
      if (rc) {
        scan_.sealed.store(true);
        scan_.fault.store(BleFault::Host);
      }
    }
  }
  if (scanning_ && (scan_.fault.load() != BleFault::None || stopping_ || startup_failed_ ||
                    (!scan_reported_ && expired(now, scan_deadline_)))) {
    scan_.sealed.store(true);
    if (!stopping_ && scan_.fault.load() == BleFault::None)
      scan_.fault.store(startup_failed_ ? BleFault::Host : BleFault::Timeout);
    cancelScanOnce();
  }
  BleEvent e;
  for (size_t n = 0; n < kBleQueue && pop(e); ++n) {
    faults(); // A callback may have sealed a peer while another event was drained.
    event(e);
  }
  if (scanning_ && scan_.terminal.load() && host_.quiescent(scan_) && host_.releaseContext(scan_)) {
    if (!scan_reported_) {
      BleResult r;
      r.kind = BleResultKind::ScanComplete;
      r.fault = scan_.fault.load();
      r.event.peer = kBlePeers;
      r.event.generation = scan_.generation;
      r.event.phase = BlePhase::Scan;
      r.event.status = scan_.terminal_status.load();
      sink_.result(r);
    }
    scanning_ = false;
    scan_.receiver = nullptr;
  }
  bool admitted = false;
  for (uint8_t n = 0; n < kBlePeers; ++n) {
    const uint8_t i = (cursor_ + n) % kBlePeers;
    auto &p = peers_[i];
    if (p.phase == BlePhase::Retiring || p.phase == BlePhase::Quarantined) {
      // A CONNECT can win cancellation. Terminate its actual captured handle.
      if (!p.link.terminal.load() &&
          !(p.link.connection.load() == kBleNoHandle ? p.cancel_submitted : p.terminate_submitted))
        retire(i, p.retirement, p.error);
      if (p.link.terminal.load() && host_.quiescent(p.link) &&
          (!p.procedure.receiver || host_.quiescent(p.procedure)) && host_.releaseContext(p.link) &&
          (!p.procedure.receiver || host_.releaseContext(p.procedure))) {
        if (initiating_ == i)
          initiating_ = kBlePeers;
        p.phase = BlePhase::Closed;
        p.link.receiver = p.procedure.receiver = nullptr;
        emit(i, BleResultKind::LinkClosed);
      }
      continue;
    }
    if (p.phase == BlePhase::Queued && initiating_ == kBlePeers && !scanning_ && !stopping_ &&
        !startup_failed_ && !admitted) {
      initiating_ = i;
      admitted = true;
      p.phase = BlePhase::Connect;
      p.deadline = now + kPhaseDeadlineMs;
      p.link.terminal.store(false);
      BleCommand c;
      c.phase = BlePhase::Connect;
      c.identity = p.identity;
      c.duration_ms = kPhaseDeadlineMs;
      const int rc = host_.submit(c, p.link);
      if (rc == kBleHostReserved) {
        p.phase = BlePhase::Queued;
        p.link.terminal.store(true);
        initiating_ = kBlePeers;
      } else if (rc) {
        retire(i, BleFault::Host, rc);
      }
      // At most one initiating admission per pass, including immediate callbacks.
      continue;
    }
    advance(i, now);
  }
  cursor_ = (cursor_ + 1) % kBlePeers;
  if (health_) {
    DeviceHealth outcome = startup_failed_ ? DeviceHealth::IoError : DeviceHealth::Ok;
    for (const auto &peer : peers_) {
      if (peer.retirement == BleFault::Store) {
        outcome = DeviceHealth::IoError;
        break;
      }
      if (outcome != DeviceHealth::IoError &&
          (peer.retirement == BleFault::Malformed || peer.retirement == BleFault::Overflow))
        outcome = DeviceHealth::Desynchronized;
      else if (outcome == DeviceHealth::Ok && peer.retirement != BleFault::None)
        outcome = DeviceHealth::Missing;
    }
    health_->completed(outcome);
    if (stopping_ && canDestroy()) {
      enabled_ = false;
      health_->finished(); // No further owner work or callback accesses admitted.
    }
  }
}
bool BleCentral::canDestroy() const {
  if (scanning_)
    return false;
  for (const auto &p : peers_)
    if (p.phase != BlePhase::Empty && p.phase != BlePhase::Closed)
      return false;
  return true;
}
} // namespace ridesync

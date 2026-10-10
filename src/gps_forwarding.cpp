// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy was not distributed with this file, obtain one
// at https://mozilla.org/MPL/2.0/.
// Source-derived BE80 video framing/chunking: arsfabula/Insta360-Remote-CIQ,
// BLE Barrel/BLEBarrel.mc at 39c51b3aa7c453227831d811355899371bbb8b94.
#include "gps_forwarding.h"
#include <algorithm>
#include <cstdlib>
namespace ridesync {
namespace {
void count(uint32_t &value) {
  if (value != UINT32_MAX)
    ++value;
}
} // namespace
bool Be80GpsForwarder::configure(uint8_t i, const CameraConfig &camera,
                                 const GpsForwardingConfig &c, Be80CommandSequence &sequence) {
  if (cancelled_ || i >= kBlePeers)
    return false;
  auto &p = peers_[i];
  const bool enabled = c.enabled && camera.enabled && camera.gps_telemetry;
  if (!enabled) {
    discard(i, p.status.active);
    p.status.enabled = false;
    return true;
  }
  if (p.status.linked || p.status.active || camera.family != CameraFamily::Insta360 ||
      camera.model != CameraModel::ONE_RS || !c.source_qualified ||
      c.encoder.profile != insta360::GpsWireProfile::GarminBe80VideoV1 || !c.encoder.max_age_ms ||
      c.encoder.max_age_ms > 60000 || c.interval_ms < 100 || c.interval_ms > 60000 ||
      !c.packet_deadline_ms || c.packet_deadline_ms > 5000)
    return false;
  for (uint8_t n = 0; n < kBlePeers; ++n)
    if (n != i && peers_[n].status.enabled && peers_[n].sequence == &sequence)
      return false;
  p.config = c;
  p.sequence = &sequence;
  p.status.enabled = true;
  return true;
}
BleProfileSpec Be80GpsForwarder::profileSpec() {
  BleProfileSpec spec;
  spec.service_count = spec.endpoint_count = 1;
  spec.services[0] = BleUuid::shortUuid(0xbe80);
  spec.endpoints[0].uuid = BleUuid::shortUuid(0xbe81);
  spec.endpoints[0].properties = 8;
  return spec;
}
insta360::GpsEncodingResult Be80GpsForwarder::encode(const Peer &p, const RecordTimestamp &t,
                                                     const ModemSnapshot &snapshot, uint8_t seq) {
  auto s = snapshot;
  // The retained observation ages even when GNSS stops publishing.
  if (t.monotonic_ms >= s.fix.receipt_monotonic_ms)
    s.age_ms = t.monotonic_ms - s.fix.receipt_monotonic_ms;
  else
    s.age_available = false;
  return insta360::encodeGps(p.config.encoder, t, s, seq);
}
Be80GpsForwarder::~Be80GpsForwarder() {
  if (!canRelease())
    std::abort();
}
bool Be80GpsForwarder::canRelease() const {
  for (const auto &p : peers_)
    if (p.status.active || p.retire_pending)
      return false;
  return true;
}
void Be80GpsForwarder::discard(uint8_t i, bool seal, bool defer) {
  auto &p = peers_[i];
  if (p.status.pending || p.status.active)
    count(p.status.dropped);
  p.status.pending = p.status.active = p.waiting = false;
  p.snapshot = p.active_snapshot = {};
  p.packet = {};
  p.offset = p.chunk = 0;
  seal = seal || p.retire_pending;
  p.retire_pending = seal && defer;
  if (seal) {
    p.status.linked = false; // Before central synchronously emits retirement.
    if (!defer)
      central_.disconnect(i);
  }
  if (!p.retire_pending)
    central_.releaseWrites(i, this);
}
void Be80GpsForwarder::offer(const RecordTimestamp &t, const ModemSnapshot &snapshot) {
  if (cancelled_)
    return;
  for (uint8_t i = 0; i < kBlePeers; ++i) {
    auto &p = peers_[i];
    if (!p.status.enabled || !p.status.linked)
      continue;
    const auto validation = encode(p, t, snapshot, 1);
    p.status.encoding_error = validation.error;
    if (validation.error != insta360::GpsEncodingError::None) {
      discard(i, p.status.active, true);
      continue;
    }
    if (p.status.pending)
      count(p.status.coalesced);
    p.snapshot = snapshot;
    p.pending_session = t.session_id;
    p.status.pending = true;
  }
}
void Be80GpsForwarder::cancel() {
  cancelled_ = true;
  for (uint8_t i = 0; i < kBlePeers; ++i)
    discard(i, peers_[i].status.active, true);
}
void Be80GpsForwarder::service(const RecordTimestamp &t, uint8_t controls) {
  // Producer publication/cancellation never enters the BLE SDK. Drain retained
  // teardown here, even after terminal cancellation, before any command work.
  for (uint8_t i = 0; i < kBlePeers; ++i)
    if (peers_[i].retire_pending)
      discard(i, true);
  if (cancelled_)
    return;
  bool submitted = false;
  for (uint8_t n = 0; n < kBlePeers; ++n) {
    const uint8_t i = (cursor_ + n) % kBlePeers;
    auto &p = peers_[i];
    if (!p.status.enabled || !p.status.linked)
      continue;
    if (controls & (1u << i)) {
      discard(i, p.status.active);
      continue;
    }
    if (p.status.active) {
      const auto valid = encode(p, t, p.active_snapshot, 1);
      p.status.encoding_error = valid.error;
      if (valid.error != insta360::GpsEncodingError::None || t.session_id != p.active_session ||
          t.monotonic_ms < p.started_ms ||
          t.monotonic_ms - p.started_ms >= p.config.packet_deadline_ms) {
        discard(i, true);
        continue;
      }
    } else if (p.status.pending) {
      const auto valid = encode(p, t, p.snapshot, 1);
      p.status.encoding_error = valid.error;
      if (valid.error != insta360::GpsEncodingError::None || t.session_id != p.pending_session ||
          (p.sent && t.monotonic_ms < p.last_sent_ms)) {
        discard(i, false);
        continue;
      }
      if (p.sent && t.monotonic_ms - p.last_sent_ms < p.config.interval_ms)
        continue;
    } else
      continue;
    if (submitted || p.waiting || !central_.admissionOpen(i))
      continue;
    if (!p.status.active) {
      if (!central_.reserveWrites(i, this))
        continue;
      const auto packet = encode(p, t, p.snapshot, p.sequence->take());
      p.packet = packet.bytes;
      p.active_snapshot = p.snapshot;
      p.active_session = t.session_id;
      p.started_ms = p.last_sent_ms = t.monotonic_ms;
      p.sent = true;
      p.status.pending = false;
      p.status.active = true;
      p.offset = 0;
      count(p.status.admitted_packets);
    }
    p.chunk = uint8_t(std::min<size_t>(20, p.packet.size() - p.offset));
    p.waiting = true; // Before a possible synchronous failure/retirement.
    if (!central_.write(i, 0, p.packet.data() + p.offset, p.chunk, uint32_t(t.monotonic_ms),
                        this)) {
      // Even local refusal consumes this observation/sequence. Never retry it.
      discard(i, p.offset != 0 || !central_.admissionOpen(i));
    }
    submitted = true;
  }
  cursor_ = (cursor_ + 1) % kBlePeers;
}
void Be80GpsForwarder::result(const BleResult &r) {
  if (r.event.peer >= kBlePeers)
    return;
  const uint8_t i = r.event.peer;
  auto &p = peers_[i];
  if (cancelled_ && r.kind != BleResultKind::Retired && r.kind != BleResultKind::LinkClosed)
    return;
  if (!p.status.enabled)
    return;
  if (r.kind == BleResultKind::TransportReady) {
    if (r.fault != BleFault::None || !r.event.generation || r.event.generation <= p.generation ||
        r.event.connection == kBleNoHandle ||
        !central_.endpointMatches(i, 0, BleUuid::shortUuid(0xbe80), BleUuid::shortUuid(0xbe81)))
      return;
    discard(i, false);
    p.generation = r.event.generation;
    p.connection = r.event.connection;
    p.last_completed_procedure = 0;
    p.status.linked = true;
    p.sent = false;
    return;
  }
  if (r.event.generation != p.generation || r.event.connection != p.connection)
    return;
  if (r.kind == BleResultKind::Retired || r.kind == BleResultKind::LinkClosed) {
    p.status.linked = false;
    discard(i, false);
  } else if (r.kind == BleResultKind::WriteComplete && p.status.active && p.waiting &&
             r.fault == BleFault::None && !r.event.status && r.endpoint == 0 &&
             r.event.phase == BlePhase::Write && r.event.procedure > p.last_completed_procedure) {
    p.last_completed_procedure = r.event.procedure;
    p.waiting = false;
    p.offset += p.chunk;
    if (p.offset == p.packet.size()) {
      central_.releaseWrites(i, this);
      p.status.active = false;
      p.packet = {};
      p.active_snapshot = {};
      count(p.status.att_completed_packets);
    }
  }
}
GpsForwardingStatus Be80GpsForwarder::status(uint8_t i) const {
  return i < kBlePeers ? peers_[i].status : GpsForwardingStatus{};
}
} // namespace ridesync
#if defined(ARDUINO_ARCH_ESP32)
extern "C" void ridesync_insta360_gps_forwarding_backend(ridesync::Be80GpsForwarder &owner,
                                                         const ridesync::RecordTimestamp &now,
                                                         uint8_t control_mask) {
  owner.service(now, control_mask);
}
#endif

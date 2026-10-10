// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy was not distributed with this file, obtain one
// at https://mozilla.org/MPL/2.0/.
// Source-derived BE80 video framing/chunking: arsfabula/Insta360-Remote-CIQ,
// BLE Barrel/BLEBarrel.mc at 39c51b3aa7c453227831d811355899371bbb8b94.
#pragma once
#include "ble_remote.h"
#include "config.h"
#include "gps_manager.h"
#include "insta360_gps_encoder.h"
namespace ridesync {
// One per connected BE80 command stream, shared with ALL command producers.
// Reservation consumes the sequence even on uncertain delivery. No rollback.
class Be80CommandSequence {
public:
  uint8_t take() {
    if (!next_)
      return 0;
    const uint8_t value = next_;
    next_ = next_ == 254 ? (wrap_ ? 1 : 0) : uint8_t(next_ + 1);
    return value;
  }

  uint8_t peek() const { return next_; }
  void reset(bool wrap = true) {
    next_ = 1;
    wrap_ = wrap;
  }

private:
  uint8_t next_ = 1;
  bool wrap_ = true;
};
struct GpsForwardingConfig {
  bool enabled = false, source_qualified = false;
  insta360::GpsEncoderConfig encoder;
  uint32_t interval_ms = 1000, packet_deadline_ms = 1000;
};
struct GpsForwardingStatus {
  bool enabled = false, linked = false, pending = false, active = false;
  uint32_t coalesced = 0, dropped = 0, admitted_packets = 0, att_completed_packets = 0;
  insta360::GpsEncodingError encoding_error = insta360::GpsEncodingError::Disabled;
};
// One serialized owner. Central, sequence objects and consumer outlive bindings.
// The BLE owner routes its actual results here and services central first and checks current
// control intent before control submissions. service submits at most ONE <=20-byte ATT write across
// all peers per pass. No scan/connect/retry, manager tick, recording state or metadata ACK is
// invented.
class Be80GpsForwarder final : public GpsSnapshotConsumer {
public:
  explicit Be80GpsForwarder(BleCentral &central) : central_(central) {}
  ~Be80GpsForwarder();
  Be80GpsForwarder(const Be80GpsForwarder &) = delete;
  Be80GpsForwarder &operator=(const Be80GpsForwarder &) = delete;
  bool configure(uint8_t, const CameraConfig &, const GpsForwardingConfig &, Be80CommandSequence &);
  static BleProfileSpec profileSpec();
  void offer(const RecordTimestamp &, const ModemSnapshot &) override;
  void cancel() override;
  bool canRelease() const;
  // Current per-peer control intent/busy mask; never feed an old cached mask.
  // Run with this mask BEFORE control submissions when new control intent arrives.
  // The central write lease also refuses all other producers while partial.
  // Pending telemetry is discarded. A partial stream is sealed, not truncated
  // into the next control command. That peer needs explicit fresh reconnection.
  void service(const RecordTimestamp &, uint8_t control_mask = 0);
  void result(const BleResult &);
  GpsForwardingStatus status(uint8_t peer) const;

private:
  struct Peer {
    GpsForwardingConfig config;
    GpsForwardingStatus status;
    Be80CommandSequence *sequence = nullptr;
    ModemSnapshot snapshot, active_snapshot;
    std::array<uint8_t, 71> packet{};
    uint64_t pending_session = 0, active_session = 0, started_ms = 0, last_sent_ms = 0;
    uint32_t generation = 0, last_completed_procedure = 0;
    uint16_t connection = kBleNoHandle;
    uint8_t offset = 0, chunk = 0;
    bool waiting = false, sent = false, retire_pending = false;
  };
  BleCentral &central_;
  std::array<Peer, kBlePeers> peers_{};
  uint8_t cursor_ = 0;
  bool cancelled_ = false;
  void discard(uint8_t, bool seal, bool defer = false);
  static insta360::GpsEncodingResult encode(const Peer &, const RecordTimestamp &,
                                            const ModemSnapshot &, uint8_t);
};
} // namespace ridesync

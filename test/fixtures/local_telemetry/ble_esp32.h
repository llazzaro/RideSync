#include "../ble/adapter_host.h"
#pragma once
#include "ble_pairing_reset.h"
#include "nvs_boot_guard.h"
#include "pairing_proof_esp32.h"
#include "profiles/gopro_hero12.h"
#include <Arduino.h>
#include <algorithm>
#include <cassert>
#include <vector>
extern bool host_failure;
namespace ridesync {
namespace fixture_ble_model {
template <typename Host> bool processLink(Host &host, size_t n, const BleCommand &c) {
  auto &ctx = *host.contexts[n];
  BleEvent e;
  e.connection = c.connection;
  if (c.phase == BlePhase::Connect) {
    host.links[ctx.peer] = &ctx;
    host.identities[ctx.peer] = c.identity;
    e.kind = BleEventKind::Connected;
    e.connection = 10 + ctx.peer;
    ctx.connection.store(e.connection);
    host.deliver(ctx, e);
    return true;
  }
  if (c.phase == BlePhase::Security) {
    e.kind = BleEventKind::Security;
    e.encrypted = e.bonded = true;
    e.identity = host.identities[ctx.peer];
    host.deliver(ctx, e);
    return true;
  }
  return false;
}
template <typename Host>
void characteristics(Host &host, size_t n, const BleCommand &c, BleEvent e) {
  auto &ctx = *host.contexts[n];
  const auto spec = Hero12Adapter::profileSpec();
  const bool management = c.start == 41;
  for (unsigned j = management ? 6 : 0; j < (management ? 8u : 6u); ++j) {
    e.kind = BleEventKind::Characteristic;
    e.uuid = spec.endpoints[j].uuid;
    e.start = (management ? 42 : 2) + (j - (management ? 6 : 0)) * 3;
    e.handle = e.start + 1;
    e.properties = spec.endpoints[j].properties;
    host.deliver(ctx, e);
  }
  host.complete(n);
}
template <typename Host> bool processDiscovery(Host &host, size_t n, const BleCommand &c) {
  auto &ctx = *host.contexts[n];
  BleEvent e;
  e.connection = c.connection;
  switch (c.phase) {
  case BlePhase::Services:
    e.kind = BleEventKind::Service;
    e.uuid = c.uuid;
    e.start = c.uuid == Hero12Adapter::profileSpec().services[0] ? 1 : 41;
    e.end = e.start == 1 ? 40 : 60;
    host.deliver(ctx, e);
    host.complete(n);
    return true;
  case BlePhase::Characteristics:
    characteristics(host, n, c, e);
    return true;
  case BlePhase::Descriptors:
    e.kind = BleEventKind::Descriptor;
    e.uuid = BleUuid::shortUuid(0x2902);
    e.handle = c.start + 1;
    host.deliver(ctx, e);
    host.complete(n);
    return true;
  default:
    return false;
  }
}
template <typename Host>
std::vector<uint8_t> commandReply(Host &host, const uint8_t *payload, uint8_t peer) {
  switch (payload[0]) {
  case 0xf1:
    return {0xf1, 0xe9, 8, 1};
  case 0x3c:
    return {0x3c, 0, 1, 62, 1, 'x', 0, 1, 'x', 0, 0, 0};
  case 0x51:
    return {0x51, 0, 1, 0, 1, 0};
  default:
    if (payload[0] == 1)
      host.encoding[peer] = payload[2] == 1;
    return {payload[0], 0};
  }
}
template <typename Host>
std::vector<uint8_t> queryReply(Host &host, const uint8_t *payload, uint8_t peer) {
  if (payload[0] == 0x53)
    return {0x53, 0, payload[1], 1, 1};
  return {0x13, 0, payload[1], 1,
          uint8_t(payload[1] == 10 ? host.encoding[peer] : payload[1] == 82)};
}
template <typename Host> void processWrite(Host &host, size_t n, const BleCommand &c) {
  auto &ctx = *host.contexts[n];
  host.complete(n);
  if (!host.auto_camera)
    return;
  const auto *payload = c.bytes.data() + 2;
  std::vector<uint8_t> reply;
  uint16_t notify = 0;
  switch (c.handle) {
  case 43:
    reply = {3, 0x81, 8, 1};
    notify = 46;
    break;
  case 3:
    reply = commandReply(host, payload, ctx.peer);
    notify = 6;
    break;
  case 9:
    reply = {0x5b, 0};
    notify = 12;
    break;
  case 15:
    reply = queryReply(host, payload, ctx.peer);
    notify = 18;
    break;
  default:
    break;
  }
  assert(reply.size() < 63);
  host.notification(*host.links[ctx.peer], notify, reply);
}
template <typename Host> void process(Host &host, size_t n) {
  const auto c = host.commands[n];
  if (processLink(host, n, c) || processDiscovery(host, n, c))
    return;
  switch (c.phase) {
  case BlePhase::Subscribe:
  case BlePhase::VerifySubscription:
  case BlePhase::Read:
    host.complete(n);
    break;
  case BlePhase::Write:
    processWrite(host, n, c);
    break;
  default:
    break;
  }
}
template <typename Host> void driveProcedures(Host &host) {
  while (host.handled < host.commands.size())
    process(host, host.handled++);
}
} // namespace fixture_ble_model
namespace fixture_ble_reset {
template <typename Host> bool resetBackendReady(const Host &host, uint32_t now, uint32_t deadline) {
  return !host_failure && host.host_fault == BleFault::None &&
         nvsBootStatus().persistenceAllowed() && now - deadline >= 0x80000000UL;
}
template <typename Host>
BondResetSubmission requestBondReset(Host &host, const BondIdentity &, uint32_t operation,
                                     uint32_t deadline, uint32_t now) {
  ++host.reset_submissions;
  assert(!pairingProofMaintenance().restorationAllowed(
      host.proof.qualification_record)); // Durable ACK precedes deletion request.
  if (host.reset_phase && host.reset_phase != 3)
    return BondResetSubmission::Busy;
  if (host.reset_busy) {
    --host.reset_busy;
    return BondResetSubmission::Busy;
  }
  if (!operation || operation <= host.last_reset)
    return BondResetSubmission::Stale;
  if (!host.started || !resetBackendReady(host, now, deadline))
    return BondResetSubmission::Refused;
  host.reset_operation = host.last_reset = operation;
  host.reset_deadline = deadline;
  host.reset_cancelled = false;
  host.reset_result = {};
  host.reset_result.operation = operation;
  host.reset_phase = 1;
  return BondResetSubmission::Queued;
}
template <typename Host> bool cancelBondReset(Host &host, uint32_t operation) {
  if (!host.reset_phase || operation != host.reset_operation)
    return false;
  host.reset_cancelled = true;
  ++host.reset_cancels;
  return true;
}
template <typename Host>
BleBondResetResult bondResetResult(Host &host, uint32_t operation, uint32_t now) {
  if (!host.reset_phase || operation != host.reset_operation)
    return {};
  if (host.reset_phase == 3)
    return host.reset_result;
  BleBondResetResult r;
  r.operation = operation;
  r.mutation = host.reset_result.mutation;
  r.requalification_required = r.mutation;
  r.cancelled = host.reset_cancelled;
  r.timed_out = now - host.reset_deadline < 0x80000000UL;
  r.finished = r.cancelled || r.timed_out;
  r.outcome = r.mutation ? BondOutcome::Indeterminate : BondOutcome::Refused;
  return r;
}
template <typename Host> void applyResetOutcome(Host &host) {
  const bool admitted =
      !host.reset_cancelled && resetBackendReady(host, millis(), host.reset_deadline);
  host.reset_result.outcome = admitted                     ? host.reset_outcome
                              : host.reset_result.mutation ? BondOutcome::Indeterminate
                                                           : BondOutcome::Refused;
  if (admitted && (host.reset_outcome == BondOutcome::Removed ||
                   host.reset_outcome == BondOutcome::Indeterminate))
    host.reset_result.mutation = true;
}
template <typename Host> void driveReset(Host &host, bool final = true) {
  assert(host.reset_phase == 1 || host.reset_phase == 2);
  if (host.reset_phase == 1) {
    ++host.reset_events;
    host.reset_phase = 2;
  }
  if (!final)
    return;
  applyResetOutcome(host);
  host.reset_result.requalification_required = host.reset_result.mutation;
  host.reset_result.cancelled = host.reset_cancelled;
  host.reset_result.timed_out = millis() - host.reset_deadline < 0x80000000UL;
  host.reset_result.finished = host.reset_result.releasable = true;
  if (host.reset_result.outcome == BondOutcome::Indeterminate)
    host.host_fault = BleFault::Store; // Actual backend faults on uncertain deletion.
  host.reset_phase = 3;
}
} // namespace fixture_ble_reset
namespace fixture_ble_lifetime {
template <typename Host> bool quiescent(const Host &host, const BleContext &ctx) {
  return &ctx != host.held && ctx.terminal.load() &&
         std::find(host.quiet.begin(), host.quiet.end(), &ctx) != host.quiet.end();
}
template <typename Host> bool releaseContext(Host &host, BleContext &ctx) {
  if (!host.quiescent(ctx))
    return false;
  ++host.releases;
  return true;
}
template <typename Host> void driveRetirements(Host &host) {
  const auto pending = host.retired;
  host.retired.clear();
  for (auto *ctx : pending) {
    BleEvent e;
    e.connection = ctx->connection.load();
    e.kind = ctx->phase == BlePhase::Scan ? BleEventKind::ScanComplete : BleEventKind::Disconnected;
    host.deliver(*ctx, e, true);
  }
}
} // namespace fixture_ble_lifetime
// Synthetic SDK scheduler boundary. Real application/adapter/central/proof-owner
// code executes; explicit drive/held leases model callbacks and final access.
class Esp32BleHost : public BleHost {
public:
  static Esp32BleHost &instance() {
    static Esp32BleHost h;
    return h;
  }
  Esp32BleHost() {
    proof.qualification_record = 42; // Synthetic independent previous-boot evidence.
    proof.digest[0] = 1;
    proof.digest[1] = 2;
    proof.digest[2] = 3;
    proof.digest[3] = 6;
  }
  BleStoreProof proof;
  BleFault host_fault = BleFault::None;
  bool started = false, auto_camera = true;
  BleContext *held = nullptr;
  std::vector<BleCommand> commands;
  std::vector<BleContext *> contexts, quiet, retired;
  std::array<BleContext *, kBlePeers> links{};
  std::array<BondIdentity, kBlePeers> identities{};
  std::array<bool, kBlePeers> encoding{};
  size_t handled = 0;
  unsigned releases = 0, reset_submissions = 0, reset_cancels = 0, reset_events = 0;
  unsigned reset_busy = 0, reset_phase = 0;
  uint32_t reset_operation = 0, reset_deadline = 0, last_reset = 0;
  bool reset_cancelled = false;
  BondOutcome reset_outcome = BondOutcome::Removed;
  BleBondResetResult reset_result;
  BleHostState start(bool, bool) override {
    started =
        !host_failure && pairingProofMaintenance().restorationAllowed(proof.qualification_record);
    return started ? BleHostState::Ready : BleHostState::Failed;
  }
  BleHostState state() const override {
    return host_failure || host_fault != BleFault::None ? BleHostState::Failed
                                                        : BleHostState::Ready;
  }
  BleFault fault() const override { return host_fault; }
  bool admittedProof(BleStoreProof &out) const {
    if (!started || host_failure || host_fault != BleFault::None ||
        !nvsBootStatus().persistenceAllowed())
      return false;
    out = proof;
    return true;
  }
  void sealStartup() override {}
  BleBondAdmission bondAdmission(const BondIdentity &) override {
    BleBondAdmission b;
    b.stack_ready = b.restore_verified = b.refusal_installed = true;
    b.existing_verified_identity = b.identity_matches = b.persistence_allowed = true;
    b.used = 4;
    b.capacity = 5;
    b.reserved = reset_phase == 2;
    return b;
  }
  int submit(const BleCommand &c, BleContext &ctx) override {
    if (reset_phase == 2)
      return kBleHostReserved;
    commands.push_back(c);
    contexts.push_back(&ctx);
    quiet.erase(std::remove(quiet.begin(), quiet.end(), &ctx), quiet.end());
    return 0;
  }
  int retire(BleContext &ctx) override {
    if (reset_phase == 2)
      return kBleHostReserved;
    if (std::find(retired.begin(), retired.end(), &ctx) == retired.end())
      retired.push_back(&ctx);
    return 0;
  }
  int cancelScan(BleContext &ctx) override { return retire(ctx); }

  uint16_t mtu(uint16_t) const override { return reset_phase == 2 ? kBleMtuReserved : 67; }
  void deliver(BleContext &ctx, BleEvent event, bool terminal = false) {
    if (terminal) {
      ctx.terminal.store(true);
      quiet.push_back(&ctx);
    }
    assert(ctx.receiver);
    ctx.receiver->copied(ctx, event);
  }
  void complete(size_t n) { adapter_fixture::complete(*this, n); }
  void notification(BleContext &link, uint16_t handle, const std::vector<uint8_t> &body) {
    BleEvent e;
    e.kind = BleEventKind::Notification;
    e.connection = link.connection.load();
    e.handle = handle;
    e.size = body.size() + 1;
    e.bytes[0] = body.size();
    std::copy(body.begin(), body.end(), e.bytes.begin() + 1);
    deliver(link, e);
  }

  bool quiescent(const BleContext &ctx) const override {
    return fixture_ble_lifetime::quiescent(*this, ctx);
  }
  bool releaseContext(BleContext &ctx) override {
    return fixture_ble_lifetime::releaseContext(*this, ctx);
  }
  void driveRetirements() { fixture_ble_lifetime::driveRetirements(*this); }
  void driveProcedures() { fixture_ble_model::driveProcedures(*this); }
  BondResetSubmission requestBondReset(const BondIdentity &identity, uint32_t operation,
                                       uint32_t deadline, uint32_t now) {
    return fixture_ble_reset::requestBondReset(*this, identity, operation, deadline, now);
  }
  bool cancelBondReset(uint32_t operation) {
    return fixture_ble_reset::cancelBondReset(*this, operation);
  }
  BleBondResetResult bondResetResult(uint32_t operation, uint32_t now) {
    return fixture_ble_reset::bondResetResult(*this, operation, now);
  }
  void driveReset(bool final = true) { fixture_ble_reset::driveReset(*this, final); }
};
} // namespace ridesync

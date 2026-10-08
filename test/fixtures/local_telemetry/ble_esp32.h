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
    return host_failure ? BleHostState::Failed : BleHostState::Ready;
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
  bool quiescent(const BleContext &ctx) const override {
    return &ctx != held && ctx.terminal.load() &&
           std::find(quiet.begin(), quiet.end(), &ctx) != quiet.end();
  }
  bool releaseContext(BleContext &ctx) override {
    if (!quiescent(ctx))
      return false;
    ++releases;
    return true;
  }
  uint16_t mtu(uint16_t) const override { return reset_phase == 2 ? kBleMtuReserved : 67; }
  void deliver(BleContext &ctx, BleEvent event, bool terminal = false) {
    if (terminal) {
      ctx.terminal.store(true);
      quiet.push_back(&ctx);
    }
    assert(ctx.receiver);
    ctx.receiver->copied(ctx, event);
  }
  void complete(size_t n) {
    BleEvent e;
    e.kind = BleEventKind::Complete;
    e.connection = commands[n].connection;
    e.handle = commands[n].handle;
    if (commands[n].phase == BlePhase::VerifySubscription) {
      e.size = 2;
      e.bytes[0] = 1;
    }
    deliver(*contexts[n], e, true);
  }
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
  void driveRetirements() {
    const auto pending = retired;
    retired.clear();
    for (auto *ctx : pending) {
      BleEvent e;
      e.connection = ctx->connection.load();
      e.kind =
          ctx->phase == BlePhase::Scan ? BleEventKind::ScanComplete : BleEventKind::Disconnected;
      deliver(*ctx, e, true);
    }
  }
  void process(size_t n) {
    const auto c = commands[n];
    auto &ctx = *contexts[n];
    BleEvent e;
    e.connection = c.connection;
    if (c.phase == BlePhase::Scan)
      return;
    if (c.phase == BlePhase::Connect) {
      links[ctx.peer] = &ctx;
      identities[ctx.peer] = c.identity;
      e.kind = BleEventKind::Connected;
      e.connection = 10 + ctx.peer;
      ctx.connection.store(e.connection);
      deliver(ctx, e);
    } else if (c.phase == BlePhase::Security) {
      e.kind = BleEventKind::Security;
      e.encrypted = e.bonded = true;
      e.identity = identities[ctx.peer];
      deliver(ctx, e);
    } else if (c.phase == BlePhase::Services) {
      e.kind = BleEventKind::Service;
      e.uuid = c.uuid;
      e.start = c.uuid == Hero12Adapter::profileSpec().services[0] ? 1 : 41;
      e.end = e.start == 1 ? 40 : 60;
      deliver(ctx, e);
      complete(n);
    } else if (c.phase == BlePhase::Characteristics) {
      const auto spec = Hero12Adapter::profileSpec();
      const bool management = c.start == 41;
      for (unsigned j = management ? 6 : 0; j < (management ? 8u : 6u); ++j) {
        e.kind = BleEventKind::Characteristic;
        e.uuid = spec.endpoints[j].uuid;
        e.start = (management ? 42 : 2) + (j - (management ? 6 : 0)) * 3;
        e.handle = e.start + 1;
        e.properties = spec.endpoints[j].properties;
        deliver(ctx, e);
      }
      complete(n);
    } else if (c.phase == BlePhase::Descriptors) {
      e.kind = BleEventKind::Descriptor;
      e.uuid = BleUuid::shortUuid(0x2902);
      e.handle = c.start + 1;
      deliver(ctx, e);
      complete(n);
    } else if (c.phase == BlePhase::Subscribe || c.phase == BlePhase::VerifySubscription ||
               c.phase == BlePhase::Read)
      complete(n);
    else if (c.phase == BlePhase::Write) {
      complete(n);
      if (!auto_camera)
        return;
      const auto *payload = c.bytes.data() + 2;
      std::vector<uint8_t> reply;
      uint16_t notify = 0;
      if (c.handle == 43) {
        reply = {3, 0x81, 8, 1};
        notify = 46;
      } else if (c.handle == 3) {
        notify = 6;
        if (payload[0] == 0xf1)
          reply = {0xf1, 0xe9, 8, 1};
        else if (payload[0] == 0x3c) {
          reply = {0x3c, 0, 1, 62, 1, 'x', 0, 1, 'x', 0, 0, 0};
        } else if (payload[0] == 0x51)
          reply = {0x51, 0, 1, 0, 1, 0};
        else {
          reply = {payload[0], 0};
          if (payload[0] == 1)
            encoding[ctx.peer] = payload[2] == 1;
        }
      } else if (c.handle == 9) {
        reply = {0x5b, 0};
        notify = 12;
      } else if (c.handle == 15) {
        notify = 18;
        if (payload[0] == 0x53)
          reply = {0x53, 0, payload[1], 1, 1};
        else
          reply = {0x13, 0, payload[1], 1,
                   uint8_t(payload[1] == 10 ? encoding[ctx.peer] : payload[1] == 82)};
      }
      assert(reply.size() < 63);
      notification(*links[ctx.peer], notify, reply);
    }
  }
  void driveProcedures() {
    while (handled < commands.size())
      process(handled++);
  }
  BondResetSubmission requestBondReset(const BondIdentity &, uint32_t operation, uint32_t deadline,
                                       uint32_t now) {
    ++reset_submissions;
    assert(!pairingProofMaintenance().restorationAllowed(
        proof.qualification_record)); // Durable ACK precedes deletion request.
    if (reset_phase && reset_phase != 3)
      return BondResetSubmission::Busy;
    if (reset_busy) {
      --reset_busy;
      return BondResetSubmission::Busy;
    }
    if (!operation || operation <= last_reset)
      return BondResetSubmission::Stale;
    if (!started || host_failure || host_fault != BleFault::None ||
        !nvsBootStatus().persistenceAllowed() || now - deadline < 0x80000000UL)
      return BondResetSubmission::Refused;
    reset_operation = last_reset = operation;
    reset_deadline = deadline;
    reset_cancelled = false;
    reset_result = {};
    reset_result.operation = operation;
    reset_phase = 1;
    return BondResetSubmission::Queued;
  }
  bool cancelBondReset(uint32_t operation) {
    if (!reset_phase || operation != reset_operation)
      return false;
    reset_cancelled = true;
    ++reset_cancels;
    return true;
  }
  BleBondResetResult bondResetResult(uint32_t operation, uint32_t now) {
    if (!reset_phase || operation != reset_operation)
      return {};
    if (reset_phase == 3)
      return reset_result;
    BleBondResetResult r;
    r.operation = operation;
    r.mutation = reset_result.mutation;
    r.requalification_required = r.mutation;
    r.cancelled = reset_cancelled;
    r.timed_out = now - reset_deadline < 0x80000000UL;
    r.finished = r.cancelled || r.timed_out;
    r.outcome = r.mutation ? BondOutcome::Indeterminate : BondOutcome::Refused;
    return r;
  }
  void driveReset(bool final = true) {
    assert(reset_phase == 1 || reset_phase == 2);
    if (reset_phase == 1) {
      ++reset_events;
      reset_phase = 2;
    }
    if (!final)
      return;
    const bool admitted = !reset_cancelled && millis() - reset_deadline >= 0x80000000UL &&
                          !host_failure && host_fault == BleFault::None &&
                          nvsBootStatus().persistenceAllowed();
    reset_result.outcome = admitted                ? reset_outcome
                           : reset_result.mutation ? BondOutcome::Indeterminate
                                                   : BondOutcome::Refused;
    if (admitted &&
        (reset_outcome == BondOutcome::Removed || reset_outcome == BondOutcome::Indeterminate))
      reset_result.mutation = true;
    reset_result.requalification_required = reset_result.mutation;
    reset_result.cancelled = reset_cancelled;
    reset_result.timed_out = millis() - reset_deadline < 0x80000000UL;
    reset_result.finished = reset_result.releasable = true;
    reset_phase = 3;
  }
};
} // namespace ridesync

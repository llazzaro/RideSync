#include "ble_remote.h"
#include <algorithm>
#include <type_traits>
#include <unity.h>
#include <vector>
using namespace ridesync;
struct Sink : BleResultSink {
  std::vector<BleResult> results;
  void result(const BleResult &r) override { results.push_back(r); }
  unsigned count(BleResultKind kind) {
    unsigned n = 0;
    for (auto &r : results)
      n += r.kind == kind;
    return n;
  }
};
struct FakeHost : BleHost {
  BleHostState current = BleHostState::Ready;
  BleFault failure = BleFault::None;
  BleBondAdmission bond;
  std::vector<BleCommand> commands;
  std::vector<BleContext *> contexts;
  std::vector<BleContext *> quiet;
  int submit_error = 0, retire_error = 0;
  bool immediate_connect = false, allow_release = true;
  unsigned releases = 0;
  unsigned starts = 0, retirements = 0, scan_cancellations = 0;
  FakeHost() {
    bond.stack_ready = bond.restore_verified = bond.refusal_installed = true;
    bond.existing_verified_identity = bond.identity_matches = bond.persistence_allowed = true;
    bond.used = bond.capacity = 4;
  }
  BleHostState start(bool enabled, bool qualified) override {
    ++starts;
    return enabled && qualified ? current : BleHostState::Disabled;
  }
  BleHostState state() const override { return current; }
  BleFault fault() const override { return failure; }
  void sealStartup() override {
    current = BleHostState::Failed;
    failure = BleFault::Timeout;
  }
  BleBondAdmission bondAdmission(const BondIdentity &) override { return bond; }
  int submit(const BleCommand &c, BleContext &ctx) override {
    commands.push_back(c);
    contexts.push_back(&ctx);
    quiet.erase(std::remove(quiet.begin(), quiet.end(), &ctx), quiet.end());
    if (submit_error) {
      ctx.terminal.store(true);
      quiet.push_back(&ctx);
      return submit_error;
    }
    if (c.phase == BlePhase::Connect && immediate_connect) {
      BleEvent e;
      e.kind = BleEventKind::Connected;
      e.connection = 10 + ctx.peer;
      deliver(ctx, e, false);
    }
    return 0;
  }
  int retire(BleContext &) override {
    ++retirements;
    return retire_error;
  }
  int cancelScan(BleContext &ctx) override {
    ++scan_cancellations;
    if (!retire_error) {
      ctx.terminal.store(true);
      quiet.push_back(&ctx);
    }
    return retire_error;
  }
  bool quiescent(const BleContext &ctx) const override {
    return ctx.terminal.load() && std::find(quiet.begin(), quiet.end(), &ctx) != quiet.end();
  }
  bool releaseContext(BleContext &ctx) override {
    if (!allow_release || !quiescent(ctx))
      return false;
    ++releases;
    return true;
  }
  uint16_t mtu(uint16_t) const override { return 67; }
  void deliver(BleContext &ctx, BleEvent e, bool terminal) {
    if (e.kind == BleEventKind::Connected)
      ctx.connection.store(e.connection);
    if (terminal) {
      ctx.terminal.store(true);
      quiet.push_back(&ctx);
    }
    ctx.receiver->copied(ctx, e);
  }
  void done(size_t i, int status = 0) {
    BleEvent e;
    e.kind = BleEventKind::Complete;
    e.connection = commands[i].connection;
    e.handle = commands[i].handle;
    e.status = status;
    deliver(*contexts[i], e, true);
  }
};
BondIdentity identity(uint8_t n = 1) {
  BondIdentity id;
  id.type = IdentityType::Public;
  id.verified = true;
  id.address[0] = n;
  return id;
}
BleProfileSpec profile() {
  BleProfileSpec s;
  s.service_count = 1;
  s.services[0] = BleUuid::shortUuid(0xfea6);
  s.endpoint_count = 2;
  s.endpoints[0].uuid = BleUuid::shortUuid(0x72);
  s.endpoints[0].properties = 8 | 2;
  s.endpoints[1].uuid = BleUuid::shortUuid(0x73);
  s.endpoints[1].properties = 16;
  s.endpoints[1].subscribe = 1;
  return s;
}
void disabled_and_unqualified_do_not_initialize_or_publish_health() {
  FakeHost h;
  Sink sink;
  HealthProgress health;
  {
    BleCentral owner(h, sink, &health);
    TEST_ASSERT_FALSE(owner.begin(false, true, 0));
    owner.service(10);
    TEST_ASSERT_EQUAL(0, h.starts);
    TEST_ASSERT_EQUAL(0, health.generation());
  }
  {
    BleCentral owner(h, sink, &health);
    TEST_ASSERT_FALSE(owner.begin(true, false, 0));
    TEST_ASSERT_EQUAL(0, h.starts);
  }
}
void one_global_connect_and_immediate_callback_are_safe() {
  FakeHost h;
  Sink sink;
  BleCentral owner(h, sink);
  TEST_ASSERT_TRUE(owner.begin(true, true, 0));
  h.immediate_connect = true;
  TEST_ASSERT_TRUE(owner.connect(0, 7, identity(), profile()));
  TEST_ASSERT_TRUE(owner.connect(1, 8, identity(2), profile()));
  owner.service(0);
  TEST_ASSERT_EQUAL(1, h.commands.size());
  TEST_ASSERT_EQUAL((int)BlePhase::Connect, (int)h.commands[0].phase);
  owner.service(1);
  TEST_ASSERT_EQUAL((int)BlePhase::Security, (int)owner.phase(0));
  TEST_ASSERT_EQUAL(3, h.commands.size()); // peer0 security and peer1 connect
  owner.stop();
  for (auto *ctx : h.contexts) {
    if (ctx->phase == BlePhase::Connect) {
      BleEvent e;
      e.kind = BleEventKind::Disconnected;
      e.connection = ctx->connection.load();
      h.deliver(*ctx, e, true);
    }
  }
  owner.service(2);
  TEST_ASSERT_TRUE(owner.canDestroy());
}

size_t command(FakeHost &h, uint8_t peer, BlePhase phase) {
  for (size_t i = h.commands.size(); i-- > 0;)
    if (h.contexts[i]->peer == peer && h.commands[i].phase == phase)
      return i;
  TEST_FAIL_MESSAGE("required procedure was not submitted");
  return 0;
}
void closed(FakeHost &h, BleCentral &owner, uint8_t peer, uint32_t now = 100) {
  BleContext *link = nullptr;
  for (auto *ctx : h.contexts) {
    if (ctx->peer != peer)
      continue;
    if (ctx->phase == BlePhase::Connect)
      link = ctx;
    else {
      ctx->terminal.store(true);
      h.quiet.push_back(ctx);
    }
  }
  if (link) {
    BleEvent e;
    e.kind = BleEventKind::Disconnected;
    e.connection = link->connection.load();
    h.deliver(*link, e, true);
  }
  owner.service(now);
}
void secure(FakeHost &h, BleCentral &owner, uint8_t peer, uint32_t now = 1) {
  const size_t link = command(h, peer, BlePhase::Connect);
  if (owner.phase(peer) == BlePhase::Connect) {
    BleEvent e;
    e.kind = BleEventKind::Connected;
    e.connection = 10 + peer;
    h.deliver(*h.contexts[link], e, false);
    owner.service(now++);
  }
  BleEvent e;
  e.kind = BleEventKind::Security;
  e.connection = 10 + peer;
  e.identity = identity(peer + 1);
  e.encrypted = e.bonded = e.authenticated = true;
  h.deliver(*h.contexts[link], e, false);
  owner.service(now);
}
void discover(FakeHost &h, BleCentral &owner, uint8_t peer, uint8_t properties = 16,
              bool cccd = true, uint32_t now = 10) {
  size_t current = command(h, peer, BlePhase::Services);
  BleEvent e;
  e.kind = BleEventKind::Service;
  e.connection = 10 + peer;
  e.uuid = BleUuid::shortUuid(0xfea6);
  e.start = 1;
  e.end = 20;
  h.deliver(*h.contexts[current], e, false);
  h.done(current);
  owner.service(now++);
  current = command(h, peer, BlePhase::Characteristics);
  e.kind = BleEventKind::Characteristic;
  e.uuid = BleUuid::shortUuid(0x72);
  e.start = 2;
  e.handle = 3;
  e.properties = 8 | 2;
  h.deliver(*h.contexts[current], e, false);
  e.uuid = BleUuid::shortUuid(0x73);
  e.start = 4;
  e.handle = 5;
  e.properties = properties;
  h.deliver(*h.contexts[current], e, false);
  // An unrelated next declaration bounds descriptor search at 8, not service end20.
  e.uuid = BleUuid::shortUuid(0x9999);
  e.start = 9;
  e.handle = 10;
  e.properties = 2;
  h.deliver(*h.contexts[current], e, false);
  h.done(current);
  owner.service(now++);
  if (owner.phase(peer) == BlePhase::Quarantined)
    return;
  current = command(h, peer, BlePhase::Descriptors);
  TEST_ASSERT_EQUAL(5, h.commands[current].start);
  TEST_ASSERT_EQUAL(8, h.commands[current].end);
  if (cccd) {
    e.kind = BleEventKind::Descriptor;
    e.uuid = BleUuid::shortUuid(0x2902);
    e.handle = 6;
    h.deliver(*h.contexts[current], e, false);
  }
  h.done(current);
  owner.service(now++);
  if (!cccd)
    return;
  current = command(h, peer, BlePhase::Subscribe);
  TEST_ASSERT_EQUAL(6, h.commands[current].handle);
  const uint8_t expected[] = {1, 0};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, h.commands[current].bytes.data(), 2);
  h.done(current);
  owner.service(now++);
  current = command(h, peer, BlePhase::VerifySubscription);
  e = {};
  e.kind = BleEventKind::Complete;
  e.connection = 10 + peer;
  e.handle = 6;
  e.size = 2;
  e.bytes[0] = 1;
  h.deliver(*h.contexts[current], e, true);
  owner.service(now);
}
void ready(FakeHost &h, BleCentral &owner, uint8_t peer = 0, uint32_t generation = 1,
           bool authenticated = false) {
  auto spec = profile();
  spec.require_authenticated = authenticated;
  TEST_ASSERT_TRUE(owner.connect(peer, generation, identity(peer + 1), spec));
  owner.service(0);
  secure(h, owner, peer);
  discover(h, owner, peer);
  TEST_ASSERT_TRUE(owner.admissionOpen(peer));
}
void subscription_requires_actual_cccd_att_completion_and_readback() {
  FakeHost h;
  Sink sink;
  BleCentral owner(h, sink);
  owner.begin(true, true, 0);
  ready(h, owner);
  TEST_ASSERT_EQUAL(1, sink.count(BleResultKind::TransportReady));
  const uint8_t payload[] = {4, 1, 0};
  TEST_ASSERT_TRUE(owner.write(0, 0, payload, sizeof payload, 20));
  TEST_ASSERT_FALSE(owner.write(0, 0, payload, sizeof payload, 20));
  const size_t write = command(h, 0, BlePhase::Write);
  TEST_ASSERT_EQUAL(3, h.commands[write].handle);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, h.commands[write].bytes.data(), 3);
  h.done(write);
  owner.service(21);
  TEST_ASSERT_EQUAL(1, sink.count(BleResultKind::WriteComplete));
  TEST_ASSERT_TRUE(owner.read(0, 0, 22));
  const size_t read = command(h, 0, BlePhase::Read);
  BleEvent e;
  e.kind = BleEventKind::Complete;
  e.connection = 10;
  e.handle = 3;
  e.size = 2;
  e.bytes[0] = 0xaa;
  e.bytes[1] = 0x55;
  h.deliver(*h.contexts[read], e, true);
  owner.service(23);
  TEST_ASSERT_EQUAL(1, sink.count(BleResultKind::ReadComplete));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(e.bytes.data(), sink.results.back().event.bytes.data(), 2);
  owner.stop();
  closed(h, owner, 0);
}
void missing_properties_or_cccd_never_publish_transport_ready() {
  for (int scenario = 0; scenario < 2; ++scenario) {
    FakeHost h;
    Sink sink;
    BleCentral owner(h, sink);
    owner.begin(true, true, 0);
    owner.connect(0, 1, identity(), profile());
    owner.service(0);
    secure(h, owner, 0);
    discover(h, owner, 0, scenario == 0 ? 2 : 16, scenario == 0);
    TEST_ASSERT_EQUAL(0, sink.count(BleResultKind::TransportReady));
    TEST_ASSERT_EQUAL(1, sink.count(BleResultKind::Retired));
    TEST_ASSERT_EQUAL((int)(scenario == 0 ? BleFault::MissingProperty : BleFault::MissingCccd),
                      (int)sink.results.back().fault);
    closed(h, owner, 0);
  }
}
void overflow_seals_before_service_and_fault_precedes_queued_notifications() {
  FakeHost h;
  Sink sink;
  HealthProgress health;
  BleCentral owner(h, sink, &health);
  owner.begin(true, true, 0);
  ready(h, owner, 0);
  ready(h, owner, 1);
  const size_t link = command(h, 0, BlePhase::Connect);
  BleEvent e;
  e.kind = BleEventKind::Notification;
  e.connection = 10;
  e.handle = 5;
  e.size = 1;
  e.bytes[0] = 0x42;
  for (unsigned n = 0; n <= kBleQueue; ++n)
    h.deliver(*h.contexts[link], e, false);
  TEST_ASSERT_FALSE(owner.admissionOpen(0));
  const uint8_t payload[] = {1};
  TEST_ASSERT_FALSE(owner.write(0, 0, payload, 1, 30));
  owner.service(30);
  TEST_ASSERT_EQUAL(1, sink.count(BleResultKind::Retired));
  TEST_ASSERT_EQUAL(0, sink.count(BleResultKind::Notification));
  TEST_ASSERT_TRUE(owner.admissionOpen(1));
  e.connection = 11;
  h.deliver(*h.contexts[command(h, 1, BlePhase::Connect)], e, false);
  owner.service(31);
  TEST_ASSERT_EQUAL(1, sink.count(BleResultKind::Notification));
  TEST_ASSERT_EQUAL(1, sink.results.back().event.peer);
  owner.stop();
  TEST_ASSERT_FALSE(health.isFinished());
  closed(h, owner, 0);
  TEST_ASSERT_FALSE(health.isFinished());
  closed(h, owner, 1);
  TEST_ASSERT_TRUE(health.isFinished());
  const auto generation = health.generation();
  owner.service(500);
  TEST_ASSERT_EQUAL(generation, health.generation());
}
void deadline_at_completion_rollover_and_failed_termination_quarantine() {
  FakeHost h;
  Sink sink;
  BleCentral owner(h, sink);
  owner.begin(true, true, UINT32_MAX - 10);
  owner.connect(0, 1, identity(), profile());
  owner.service(UINT32_MAX - 10);
  TEST_ASSERT_EQUAL((int)BlePhase::Connect, (int)owner.phase(0));
  h.retire_error = 77;
  owner.service(4989); // exactly 5000ms across wrap
  TEST_ASSERT_EQUAL((int)BlePhase::Quarantined, (int)owner.phase(0));
  TEST_ASSERT_EQUAL((int)BleFault::Timeout, (int)sink.results.back().fault);
  TEST_ASSERT_FALSE(owner.connect(0, 2, identity(), profile()));
  TEST_ASSERT_FALSE(owner.canDestroy());
  const auto retirements = h.retirements;
  owner.service(100000);
  TEST_ASSERT_EQUAL(retirements, h.retirements);
  closed(h, owner, 0, 100001);
  TEST_ASSERT_TRUE(owner.canDestroy());
}
void cancellation_losing_to_connect_terminates_the_captured_link_once() {
  FakeHost h;
  Sink sink;
  BleCentral owner(h, sink);
  owner.begin(true, true, 0);
  owner.connect(0, 1, identity(), profile());
  owner.service(0);
  owner.disconnect(0);
  TEST_ASSERT_EQUAL(1, h.retirements); // Global connect cancellation.
  BleEvent e;
  e.kind = BleEventKind::Connected;
  e.connection = 10;
  h.deliver(*h.contexts[0], e, false);
  owner.service(1);
  TEST_ASSERT_EQUAL(2, h.retirements); // Winning link gets its own termination.
  owner.service(2);
  TEST_ASSERT_EQUAL(2, h.retirements);
  closed(h, owner, 0);
}
void terminal_disconnect_needs_procedure_barrier_before_slot_reuse() {
  FakeHost h;
  Sink sink;
  BleCentral owner(h, sink);
  owner.begin(true, true, 0);
  ready(h, owner);
  TEST_ASSERT_TRUE(owner.read(0, 0, 20));
  const size_t read = command(h, 0, BlePhase::Read);
  owner.disconnect(0);
  BleEvent e;
  e.kind = BleEventKind::Disconnected;
  e.connection = 10;
  h.deliver(*h.contexts[command(h, 0, BlePhase::Connect)], e, true);
  owner.service(21);
  TEST_ASSERT_FALSE(owner.canDestroy());
  TEST_ASSERT_FALSE(owner.connect(0, 2, identity(), profile()));
  h.contexts[read]->terminal.store(true);
  owner.service(22);
  TEST_ASSERT_FALSE(owner.canDestroy());
  h.quiet.push_back(h.contexts[read]);
  owner.service(23);
  TEST_ASSERT_TRUE(owner.canDestroy());
  ready(h, owner, 0, 2); // Same numerical connection handle, new captured generation.
  BleContext old;
  old.receiver = &owner;
  old.peer = 0;
  old.generation = 1;
  old.phase = BlePhase::Connect;
  old.connection.store(10);
  e.kind = BleEventKind::Notification;
  e.handle = 5;
  e.size = 1;
  e.bytes[0] = 0x99;
  owner.copied(old, e);
  owner.service(25);
  TEST_ASSERT_EQUAL(0, sink.count(BleResultKind::Notification));
  owner.stop();
  closed(h, owner, 0);
}
void invalid_identity_capacity_restore_and_refusal_are_fail_closed() {
  for (int scenario = 0; scenario < 7; ++scenario) {
    FakeHost h;
    Sink sink;
    BleCentral owner(h, sink);
    owner.begin(true, true, 0);
    auto id = identity();
    if (scenario == 0)
      id.verified = false;
    if (scenario == 1)
      id.type = IdentityType::UnresolvedPrivate;
    if (scenario < 2) {
      TEST_ASSERT_FALSE(owner.connect(0, 1, id, profile()));
      TEST_ASSERT_EQUAL(0, h.commands.size());
      continue;
    }
    if (scenario == 2)
      h.bond.restore_verified = false;
    if (scenario == 3)
      h.bond.refusal_installed = false;
    if (scenario == 4)
      h.bond.persistence_allowed = false;
    if (scenario == 5)
      h.bond.existing_verified_identity = false;
    if (scenario == 6)
      h.bond.identity_matches = false;
    owner.connect(0, 1, id, profile());
    owner.service(0);
    BleEvent e;
    e.kind = BleEventKind::Connected;
    e.connection = 10;
    h.deliver(*h.contexts[0], e, false);
    owner.service(1);
    TEST_ASSERT_EQUAL(1, h.commands.size()); // No security procedure may escape admission.
    TEST_ASSERT_EQUAL((int)BleFault::Store, (int)sink.results.back().fault);
    closed(h, owner, 0);
  }
}
void startup_sync_deadline_and_source_failure_never_activate_peers() {
  FakeHost h;
  h.current = BleHostState::Starting;
  Sink sink;
  HealthProgress health;
  BleCentral owner(h, sink, &health);
  TEST_ASSERT_TRUE(owner.begin(true, true, UINT32_MAX - 50));
  TEST_ASSERT_FALSE(owner.connect(0, 1, identity(), profile()));
  owner.service(948);
  TEST_ASSERT_EQUAL((int)BleHostState::Starting, (int)h.state());
  owner.service(949);
  TEST_ASSERT_EQUAL((int)BleHostState::Failed, (int)h.state());
  TEST_ASSERT_FALSE(owner.connect(0, 1, identity(), profile()));
  TEST_ASSERT_EQUAL(0, h.commands.size());
  owner.stop();
  owner.service(950);
  TEST_ASSERT_TRUE(owner.canDestroy());
}
void malformed_callback_seals_without_payload_truncation_and_write_respects_mtu() {
  FakeHost h;
  Sink sink;
  BleCentral owner(h, sink);
  owner.begin(true, true, 0);
  ready(h, owner);
  std::array<uint8_t, 65> too_large{};
  TEST_ASSERT_FALSE(owner.write(0, 0, too_large.data(), too_large.size(), 20));
  BleEvent e;
  e.kind = BleEventKind::Notification;
  e.connection = 10;
  e.handle = 5;
  e.size = 65;
  h.deliver(*h.contexts[command(h, 0, BlePhase::Connect)], e, false);
  TEST_ASSERT_FALSE(owner.admissionOpen(0));
  owner.service(21);
  TEST_ASSERT_EQUAL((int)BleFault::Malformed, (int)sink.results.back().fault);
  TEST_ASSERT_EQUAL(0, sink.count(BleResultKind::Notification));
  closed(h, owner, 0);
}

void final_access_boundary_waits_for_backend_routing_detachment() {
  static_assert(!std::is_copy_constructible<BleCentral>::value, "Fixed contexts cannot copy");
  static_assert(!std::is_move_constructible<BleCentral>::value, "Fixed contexts cannot move");
  FakeHost h;
  Sink sink;
  HealthProgress health;
  BleCentral owner(h, sink, &health);
  owner.begin(true, true, 0);
  ready(h, owner);
  owner.stop();
  h.allow_release = false;
  closed(h, owner, 0);
  TEST_ASSERT_FALSE(owner.canDestroy());
  TEST_ASSERT_FALSE(health.isFinished());
  h.allow_release = true;
  owner.service(101);
  TEST_ASSERT_TRUE(owner.canDestroy());
  TEST_ASSERT_TRUE(health.isFinished());
  TEST_ASSERT_EQUAL(2, h.releases);
}

void multiple_services_map_required_endpoints_without_global_characteristic_pointers() {
  FakeHost h;
  Sink sink;
  BleCentral owner(h, sink);
  owner.begin(true, true, 0);
  auto spec = profile();
  spec.service_count = 2;
  spec.services[1] = BleUuid::shortUuid(0x90);
  spec.endpoints[1].subscribe = 0;
  spec.endpoint_count = 3;
  spec.endpoints[2].service = 1;
  spec.endpoints[2].uuid = BleUuid::shortUuid(0x91);
  spec.endpoints[2].properties = 8;
  TEST_ASSERT_TRUE(owner.connect(0, 1, identity(), spec));
  TEST_ASSERT_FALSE(owner.connect(4, 1, identity(5), spec));
  owner.service(0);
  secure(h, owner, 0);
  uint32_t now = 10;
  for (uint8_t service = 0; service < 2; ++service) {
    const auto current = command(h, 0, BlePhase::Services);
    BleEvent e;
    e.kind = BleEventKind::Service;
    e.connection = 10;
    e.uuid = spec.services[service];
    e.start = service == 0 ? 1 : 30;
    e.end = service == 0 ? 20 : 40;
    h.deliver(*h.contexts[current], e, false);
    h.done(current);
    owner.service(now++);
  }
  for (uint8_t service = 0; service < 2; ++service) {
    const auto current = command(h, 0, BlePhase::Characteristics);
    TEST_ASSERT_EQUAL(service == 0 ? 1 : 30, h.commands[current].start);
    TEST_ASSERT_EQUAL(service == 0 ? 20 : 40, h.commands[current].end);
    for (uint8_t endpoint = 0; endpoint < 3; ++endpoint) {
      if (spec.endpoints[endpoint].service != service)
        continue;
      BleEvent e;
      e.kind = BleEventKind::Characteristic;
      e.connection = 10;
      e.uuid = spec.endpoints[endpoint].uuid;
      e.start = endpoint == 0 ? 2 : endpoint == 1 ? 4 : 31;
      e.handle = endpoint == 0 ? 3 : endpoint == 1 ? 5 : 32;
      e.properties = spec.endpoints[endpoint].properties;
      h.deliver(*h.contexts[current], e, false);
    }
    h.done(current);
    owner.service(now++);
  }
  TEST_ASSERT_TRUE(owner.admissionOpen(0));
  const uint8_t payload[] = {0x01};
  TEST_ASSERT_TRUE(owner.write(0, 2, payload, 1, now));
  const auto write = command(h, 0, BlePhase::Write);
  TEST_ASSERT_EQUAL(32, h.commands[write].handle);
  h.done(write);
  owner.service(now + 1);
  owner.stop();
  closed(h, owner, 0);
}
void completion_at_deadline_is_retired_before_any_att_success_publication() {
  FakeHost h;
  Sink sink;
  BleCentral owner(h, sink);
  owner.begin(true, true, 0);
  ready(h, owner);
  TEST_ASSERT_TRUE(owner.read(0, 0, 100));
  h.done(command(h, 0, BlePhase::Read));
  owner.service(5100);
  TEST_ASSERT_EQUAL(0, sink.count(BleResultKind::ReadComplete));
  TEST_ASSERT_EQUAL((int)BleFault::Timeout, (int)sink.results.back().fault);
  closed(h, owner, 0, 5101);
}
void bounded_scan_and_four_peer_admission_make_progress_without_catchup_burst() {
  FakeHost h;
  Sink sink;
  BleCentral owner(h, sink);
  owner.begin(true, true, 0);
  TEST_ASSERT_TRUE(owner.scan(50, 0));
  for (uint8_t peer = 0; peer < 4; ++peer)
    TEST_ASSERT_TRUE(owner.connect(peer, 1, identity(peer + 1), profile()));
  owner.service(0);
  TEST_ASSERT_EQUAL(1, h.commands.size());
  owner.service(50); // One cancelled scan terminal barrier; one new connect.
  TEST_ASSERT_EQUAL(2, h.commands.size());
  TEST_ASSERT_EQUAL(1, sink.count(BleResultKind::ScanComplete));
  TEST_ASSERT_EQUAL((int)BleFault::Timeout, (int)sink.results.back().fault);
  for (unsigned n = 0; n < 4; ++n) {
    uint8_t peer = 4;
    for (uint8_t i = 0; i < 4; ++i)
      if (owner.phase(i) == BlePhase::Connect)
        peer = i;
    TEST_ASSERT_NOT_EQUAL(4, peer);
    secure(h, owner, peer, 51 + n * 10);
    owner.service(54 + n * 10);
  }
  unsigned starts = 0;
  std::array<bool, 4> served{};
  for (size_t n = 0; n < h.commands.size(); ++n)
    if (h.commands[n].phase == BlePhase::Connect) {
      ++starts;
      served[h.contexts[n]->peer] = true;
    }
  TEST_ASSERT_EQUAL(4, starts);
  for (bool value : served)
    TEST_ASSERT_TRUE(value);
  owner.stop();
  for (uint8_t peer = 0; peer < 4; ++peer)
    closed(h, owner, peer);
  TEST_ASSERT_TRUE(owner.canDestroy());
}

void peer_failure_reports_device_health_only_after_a_completed_owner_pass() {
  FakeHost h;
  Sink sink;
  HealthProgress health;
  BleCentral owner(h, sink, &health);
  owner.begin(true, true, 0);
  ready(h, owner);
  const auto generation = health.generation();
  owner.disconnect(0);
  TEST_ASSERT_EQUAL(generation, health.generation());
  owner.service(30);
  TEST_ASSERT_EQUAL(generation + 1, health.generation());
  TEST_ASSERT_EQUAL((int)DeviceHealth::Missing, (int)health.outcome());
  closed(h, owner, 0);
  owner.stop();
  owner.service(101);
}
void later_security_loss_retires_ready_and_active_links_without_restarting_discovery() {
  for (unsigned failure = 0; failure < 9; ++failure) {
    FakeHost h;
    Sink sink;
    BleCentral owner(h, sink);
    owner.begin(true, true, 0);
    if (failure == 7) {
      auto spec = profile();
      spec.require_authenticated = true;
      owner.connect(0, 1, identity(), spec);
      owner.service(0);
      secure(h, owner, 0);
    } else {
      ready(h, owner, 0, 1, true);
    }
    if (failure == 5 || failure == 8)
      TEST_ASSERT_TRUE(owner.read(0, 0, 20));
    BleEvent security;
    security.kind = BleEventKind::Security;
    security.connection = 10;
    security.identity = identity();
    security.encrypted = security.bonded = security.authenticated = true;
    if (failure == 0)
      security.status = 71;
    if (failure == 1 || failure == 5 || failure == 7)
      security.encrypted = false;
    if (failure == 2)
      security.identity = identity(2);
    if (failure == 3)
      security.authenticated = false;
    if (failure == 4)
      security.bonded = false;
    const auto before = h.commands.size();
    h.deliver(*h.contexts[command(h, 0, BlePhase::Connect)], security, false);
    owner.service(21);
    const bool still_open = owner.admissionOpen(0);
    const auto phase = owner.phase(0);
    const auto retired = sink.count(BleResultKind::Retired);
    const auto calls = h.retirements;
    const auto after = h.commands.size();
    const auto status = retired ? sink.results.back().event.status : 0;
    if (failure == 8) {
      h.done(command(h, 0, BlePhase::Read));
      owner.service(22);
    }
    const auto reads = sink.count(BleResultKind::ReadComplete);
    owner.stop();
    closed(h, owner, 0);
    if (failure == 6 || failure == 8) {
      TEST_ASSERT_EQUAL(failure == 6, still_open);
      TEST_ASSERT_EQUAL((int)(failure == 6 ? BlePhase::ReadyForProfile : BlePhase::Read),
                        (int)phase);
      TEST_ASSERT_EQUAL(failure == 8 ? 1 : 0, reads);
      TEST_ASSERT_EQUAL(before, after);
      TEST_ASSERT_EQUAL(0, retired);
    } else {
      TEST_ASSERT_FALSE(still_open);
      TEST_ASSERT_EQUAL(1, retired);
      TEST_ASSERT_EQUAL(1, calls);
      TEST_ASSERT_EQUAL(failure == 0 ? 71 : 0, status);
    }
  }
}
void failed_scan_cancellation_is_submitted_once_and_cleanup_stays_quarantined() {
  for (unsigned cause = 0; cause < 3; ++cause) {
    FakeHost h;
    Sink sink;
    BleCentral owner(h, sink);
    owner.begin(true, true, 0);
    owner.scan(50, 0);
    h.retire_error = 81;
    auto &scan = *h.contexts[0];
    if (cause == 0)
      owner.stop();
    if (cause == 2)
      scan.fault.store(BleFault::Overflow);
    for (uint32_t pass = 50; pass < 100; ++pass)
      owner.service(pass);
    const auto cancels = h.scan_cancellations;
    const bool destroyable = owner.canDestroy();
    const auto completed = sink.count(BleResultKind::ScanComplete);
    const auto cancel_error = owner.scanCancelError();
    const auto cancel_failures = sink.count(BleResultKind::ScanCancelFailed);
    const auto cancel_status = sink.results.back().event.status;
    BleEvent end;
    end.kind = BleEventKind::ScanComplete;
    end.status = 99;
    h.deliver(scan, end, true);
    owner.service(100);
    const auto completion_status = sink.results.back().event.status;
    if (cause != 0) {
      h.retire_error = 0;
      TEST_ASSERT_TRUE(owner.scan(50, 101));
      TEST_ASSERT_EQUAL(0, owner.scanCancelError());
    }
    owner.stop();
    owner.service(102);
    TEST_ASSERT_EQUAL(81, cancel_error);
    TEST_ASSERT_EQUAL(1, cancel_failures);
    TEST_ASSERT_EQUAL(81, cancel_status);
    TEST_ASSERT_EQUAL(99, completion_status);
    TEST_ASSERT_EQUAL(cause == 0 ? 1 : 2, h.scan_cancellations);
    TEST_ASSERT_EQUAL(1, cancels);
    TEST_ASSERT_FALSE(destroyable);
    TEST_ASSERT_EQUAL(0, completed);
    TEST_ASSERT_TRUE(owner.canDestroy());
  }
}
void scan_completion_at_absolute_deadline_loses_to_timeout_even_if_terminal() {
  for (uint32_t lag = 0; lag < 2; ++lag) {
    FakeHost h;
    Sink sink;
    BleCentral owner(h, sink);
    owner.begin(true, true, 0);
    owner.scan(50, 0);
    BleEvent end;
    end.kind = BleEventKind::ScanComplete;
    h.deliver(*h.contexts[0], end, true);
    owner.service(50 + lag);
    const auto fault = sink.results.back().fault;
    owner.stop();
    owner.service(52);
    TEST_ASSERT_EQUAL((int)BleFault::Timeout, (int)fault);
    TEST_ASSERT_EQUAL(1, sink.count(BleResultKind::ScanComplete));
    TEST_ASSERT_TRUE(owner.canDestroy());
  }
  // A completion accepted before the deadline remains valid while its barrier drains.
  FakeHost h;
  Sink sink;
  BleCentral owner(h, sink);
  owner.begin(true, true, 0);
  owner.scan(50, 0);
  auto &scan = *h.contexts[0];
  BleEvent end;
  end.kind = BleEventKind::ScanComplete;
  h.deliver(scan, end, true);
  h.quiet.clear();
  owner.service(49);
  owner.service(50);
  const auto fault = scan.fault.load();
  h.quiet.push_back(&scan);
  owner.service(51);
  owner.stop();
  TEST_ASSERT_EQUAL((int)BleFault::None, (int)fault);
  TEST_ASSERT_EQUAL(0, h.scan_cancellations);
  TEST_ASSERT_EQUAL(1, sink.count(BleResultKind::ScanComplete));
  TEST_ASSERT_TRUE(owner.canDestroy());
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(disabled_and_unqualified_do_not_initialize_or_publish_health);
  RUN_TEST(one_global_connect_and_immediate_callback_are_safe);
  RUN_TEST(subscription_requires_actual_cccd_att_completion_and_readback);
  RUN_TEST(missing_properties_or_cccd_never_publish_transport_ready);
  RUN_TEST(overflow_seals_before_service_and_fault_precedes_queued_notifications);
  RUN_TEST(deadline_at_completion_rollover_and_failed_termination_quarantine);
  RUN_TEST(cancellation_losing_to_connect_terminates_the_captured_link_once);
  RUN_TEST(terminal_disconnect_needs_procedure_barrier_before_slot_reuse);
  RUN_TEST(invalid_identity_capacity_restore_and_refusal_are_fail_closed);
  RUN_TEST(startup_sync_deadline_and_source_failure_never_activate_peers);
  RUN_TEST(malformed_callback_seals_without_payload_truncation_and_write_respects_mtu);
  RUN_TEST(final_access_boundary_waits_for_backend_routing_detachment);
  RUN_TEST(multiple_services_map_required_endpoints_without_global_characteristic_pointers);
  RUN_TEST(completion_at_deadline_is_retired_before_any_att_success_publication);
  RUN_TEST(bounded_scan_and_four_peer_admission_make_progress_without_catchup_burst);
  RUN_TEST(peer_failure_reports_device_health_only_after_a_completed_owner_pass);
  RUN_TEST(later_security_loss_retires_ready_and_active_links_without_restarting_discovery);
  RUN_TEST(failed_scan_cancellation_is_submitted_once_and_cleanup_stays_quarantined);
  RUN_TEST(scan_completion_at_absolute_deadline_loses_to_timeout_even_if_terminal);
  return UNITY_END();
}

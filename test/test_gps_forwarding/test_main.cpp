// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy was not distributed with this file, obtain one
// at https://mozilla.org/MPL/2.0/.
#include "../fixtures/insta360/be80_gps.h"
#include "camera_manager.h"
#include "gps_forwarding.h"
#include <algorithm>
#include <cstring>
#include <unity.h>
#include <vector>
using namespace ridesync;
struct Host : BleHost {
  std::vector<BleCommand> commands;
  std::vector<BleContext *> contexts, quiet;
  unsigned retired = 0;
  uint16_t negotiated_mtu = 23;
  int failure = 0;
  BleHostState start(bool e, bool q) override {
    return e && q ? BleHostState::Ready : BleHostState::Disabled;
  }
  BleHostState state() const override { return BleHostState::Ready; }
  BleFault fault() const override { return BleFault::None; }
  void sealStartup() override {}
  BleBondAdmission bondAdmission(const BondIdentity &) override {
    BleBondAdmission a;
    a.stack_ready = a.restore_verified = a.refusal_installed = true;
    a.existing_verified_identity = a.identity_matches = a.persistence_allowed = true;
    a.capacity = 4;
    return a;
  }
  int submit(const BleCommand &c, BleContext &ctx) override {
    commands.push_back(c);
    contexts.push_back(&ctx);
    quiet.erase(std::remove(quiet.begin(), quiet.end(), &ctx), quiet.end());
    if (failure) {
      ctx.terminal.store(true);
      quiet.push_back(&ctx);
    }
    return failure;
  }
  int retire(BleContext &) override {
    ++retired;
    return 0;
  }
  int cancelScan(BleContext &) override { return 0; }
  bool quiescent(const BleContext &c) const override {
    return c.terminal.load() && std::find(quiet.begin(), quiet.end(), &c) != quiet.end();
  }
  bool releaseContext(BleContext &c) override { return quiescent(c); }
  uint16_t mtu(uint16_t) const override { return negotiated_mtu; }
  void emit(size_t i, BleEvent e, bool terminal = false) {
    auto &ctx = *contexts[i];
    if (e.kind == BleEventKind::Connected)
      ctx.connection.store(e.connection);
    if (terminal) {
      ctx.terminal.store(true);
      quiet.push_back(&ctx);
    }
    ctx.receiver->copied(ctx, e);
  }
  void done(size_t i) {
    BleEvent e;
    e.kind = BleEventKind::Complete;
    e.connection = commands[i].connection;
    e.handle = commands[i].handle;
    emit(i, e, true);
  }
};
struct Sink : BleResultSink {
  Be80GpsForwarder *gps = nullptr;
  void result(const BleResult &r) override {
    if (gps)
      gps->result(r);
  }
};
RecordTimestamp now(uint64_t ms = 2000) {
  RecordTimestamp t;
  t.session_id = 7;
  t.monotonic_ms = ms;
  t.monotonic_quality = MonotonicQuality::Valid;
  return t;
}
ModemSnapshot fix(uint64_t receipt = 2000) {
  ModemSnapshot s;
  s.session_id = 7;
  s.validity = FixValidity::Valid;
  s.age_available = true;
  s.fix.valid = true;
  s.fix.receipt_monotonic_ms = receipt;
  s.fix.latitude_degrees = 1;
  s.fix.longitude_degrees = -2;
  s.fix.utc_date.available = s.fix.utc_time.available = true;
  s.fix.utc_date.value.year = 2000;
  s.fix.utc_date.value.month = s.fix.utc_date.value.day = 1;
  s.fix.speed_metres_per_second.available = s.fix.course_degrees.available = true;
  s.fix.altitude_msl_metres.available = true;
  s.fix.speed_metres_per_second.value = 3;
  s.fix.course_degrees.value = 90;
  s.fix.altitude_msl_metres.value = 4;
  return s;
}
CameraConfig camera(bool enabled = true) {
  CameraConfig c;
  c.family = CameraFamily::Insta360;
  c.model = CameraModel::ONE_RS;
  c.enabled = enabled;
  c.gps_telemetry = enabled;
  return c;
}
GpsForwardingConfig config() {
  GpsForwardingConfig c;
  c.enabled = true;
  c.source_qualified = true;
  c.encoder.profile = insta360::GpsWireProfile::GarminBe80VideoV1;
  c.encoder.max_age_ms = 1000;
  c.interval_ms = 100;
  c.packet_deadline_ms = 200;
  return c;
}
struct Rig {
  Host host;
  Sink sink;
  BleCentral central;
  Be80GpsForwarder gps;
  Be80CommandSequence sequences[kBlePeers];
  Rig() : central(host, sink), gps(central) {
    sink.gps = &gps;
    central.begin(true, true, 0);
  }
  ~Rig() {
    central.stop();
    for (size_t i = 0; i < host.contexts.size(); ++i) {
      auto &ctx = *host.contexts[i];
      if (ctx.phase == BlePhase::Connect && !ctx.terminal.load()) {
        BleEvent e;
        e.kind = BleEventKind::Disconnected;
        e.connection = ctx.connection.load();
        host.emit(i, e, true);
      } else {
        ctx.terminal.store(true);
        host.quiet.push_back(&ctx);
      }
    }
    central.service(30000);
    TEST_ASSERT_TRUE(central.canDestroy());
  }
  void enable(uint8_t peer = 0) {
    TEST_ASSERT_TRUE(gps.configure(peer, camera(), config(), sequences[peer]));
  }
  void connect(uint8_t peer = 0, uint32_t generation = 1, bool wrong_route = false) {
    BondIdentity id;
    id.type = IdentityType::Public;
    id.verified = true;
    id.address[0] = peer + 1;
    auto spec = Be80GpsForwarder::profileSpec();
    if (wrong_route)
      spec.endpoints[0].uuid = BleUuid::shortUuid(0xbe82);
    TEST_ASSERT_TRUE(central.connect(peer, generation, id, spec));
    central.service(0);
    size_t index = host.commands.size() - 1;
    BleEvent e;
    e.kind = BleEventKind::Connected;
    e.connection = 10 + peer;
    host.emit(index, e);
    central.service(1);
    e.kind = BleEventKind::Security;
    e.identity = id;
    e.encrypted = e.authenticated = e.bonded = true;
    host.emit(index, e);
    central.service(2);
    index = host.commands.size() - 1;
    e = {};
    e.kind = BleEventKind::Service;
    e.connection = 10 + peer;
    e.uuid = BleUuid::shortUuid(0xbe80);
    e.start = 1;
    e.end = 20;
    host.emit(index, e);
    host.done(index);
    central.service(3);
    index = host.commands.size() - 1;
    e.kind = BleEventKind::Characteristic;
    e.uuid = BleUuid::shortUuid(wrong_route ? 0xbe82 : 0xbe81);
    e.start = 2;
    e.handle = 3;
    e.properties = 8;
    host.emit(index, e);
    host.done(index);
    central.service(4);
    TEST_ASSERT_TRUE(central.admissionOpen(peer));
  }
  void complete(uint64_t ms) {
    host.done(host.commands.size() - 1);
    central.service(uint32_t(ms));
  }
  std::vector<uint8_t> writes(uint8_t peer = 0) const {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < host.commands.size(); ++i)
      if (host.commands[i].phase == BlePhase::Write && host.contexts[i]->peer == peer)
        bytes.insert(bytes.end(), host.commands[i].bytes.begin(),
                     host.commands[i].bytes.begin() + host.commands[i].size);
    return bytes;
  }
  void packet(uint64_t ms = 2000) {
    for (unsigned n = 0; n < 4; ++n) {
      gps.service(now(ms + n));
      complete(ms + n);
    }
  }
};
void default_off_and_model_selection() {
  Rig r;
  r.connect();
  TEST_ASSERT_TRUE(r.gps.configure(0, camera(), GpsForwardingConfig{}, r.sequences[0]));
  r.gps.offer(now(), fix());
  r.gps.service(now());
  TEST_ASSERT_EQUAL_UINT(0, r.writes().size());
  auto c = camera();
  c.model = CameraModel::HERO12_BLACK;
  TEST_ASSERT_FALSE(r.gps.configure(1, c, config(), r.sequences[1]));
  auto cfg = config();
  cfg.source_qualified = false;
  TEST_ASSERT_FALSE(r.gps.configure(1, camera(), cfg, r.sequences[1]));
}
void literal_packet_real_central_and_shared_sequence() {
  Rig r;
  r.enable();
  r.connect();
  r.gps.offer(now(), fix());
  r.packet();
  auto bytes = r.writes();
  TEST_ASSERT_EQUAL_UINT(71, bytes.size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(insta360_fixture::kGpsNorthWest, bytes.data(), 71);
  TEST_ASSERT_EQUAL_UINT(1, r.gps.status(0).att_completed_packets);
  TEST_ASSERT_EQUAL_UINT8(2, r.sequences[0].take()); // Other command consumes the shared stream.
  auto s = fix(2100);
  s.fix.latitude_degrees = -1;
  s.fix.longitude_degrees = 2;
  r.gps.offer(now(2100), s);
  r.packet(2100);
  bytes = r.writes();
  TEST_ASSERT_EQUAL_UINT8(3, bytes[71 + 10]);
  TEST_ASSERT_EQUAL_UINT8('S', bytes[71 + 37]);
  TEST_ASSERT_EQUAL_UINT8('E', bytes[71 + 46]);
}
void latest_fix_coalesces_and_rate_is_bounded() {
  Rig r;
  r.enable();
  r.connect();
  r.gps.offer(now(), fix());
  r.packet();
  auto s = fix(2010);
  r.gps.offer(now(2010), s);
  s.fix.latitude_degrees = -1;
  r.gps.offer(now(2010), s);
  r.gps.service(now(2099));
  TEST_ASSERT_EQUAL_UINT(71, r.writes().size());
  r.packet(2100);
  auto bytes = r.writes();
  TEST_ASSERT_EQUAL_UINT(142, bytes.size());
  TEST_ASSERT_EQUAL_UINT8('S', bytes[71 + 37]);
  TEST_ASSERT_EQUAL_UINT(1, r.gps.status(0).coalesced);
}
void invalid_and_stale_fixes_never_transmit() {
  Rig r;
  r.enable();
  r.connect();
  for (auto v :
       {FixValidity::NoFix, FixValidity::Missing, FixValidity::Invalid, FixValidity::Stale}) {
    auto s = fix();
    s.validity = v;
    r.gps.offer(now(), s);
    r.gps.service(now());
  }
  r.gps.offer(now(), fix());
  r.gps.service(now(3001));
  TEST_ASSERT_EQUAL_UINT(0, r.writes().size());
  auto s = fix(3001);
  s.fix.speed_metres_per_second.available = false;
  r.gps.offer(now(3001), s);
  r.gps.service(now(3001));
  s = fix(3001);
  s.fix.altitude_msl_metres.value = -1;
  r.gps.offer(now(3001), s);
  r.gps.service(now(3001));
  TEST_ASSERT_EQUAL_UINT(0, r.writes().size());
}
void control_drops_pending_and_seals_partial_without_replay() {
  Rig r;
  r.enable();
  r.connect();
  r.gps.offer(now(), fix());
  r.gps.service(now(), 1);
  TEST_ASSERT_EQUAL_UINT(0, r.writes().size());
  const uint8_t shutter[] = {8, 1};
  TEST_ASSERT_TRUE(r.central.write(0, 0, shutter, 2, 2000));
  r.complete(2000);
  r.gps.offer(now(2001), fix(2001));
  r.gps.service(now(2001));
  TEST_ASSERT_EQUAL_UINT(22, r.writes().size());
  r.gps.service(now(2002), 1);
  TEST_ASSERT_FALSE(r.central.admissionOpen(0));
  r.complete(2003);
  r.gps.offer(now(2004), fix(2004));
  r.gps.service(now(2004));
  TEST_ASSERT_EQUAL_UINT(22, r.writes().size());
  TEST_ASSERT_EQUAL_UINT(0, r.gps.status(0).att_completed_packets);
}
void mtu_deadline_and_session_change_abandon_uncertain_stream() {
  {
    Rig r;
    r.enable();
    r.connect();
    r.host.negotiated_mtu = 22;
    r.gps.offer(now(), fix());
    r.gps.service(now());
    TEST_ASSERT_EQUAL_UINT(0, r.writes().size());
  }
  {
    Rig r;
    r.enable();
    r.connect();
    r.gps.offer(now(), fix());
    r.gps.service(now());
    r.gps.service(now(2200));
    r.complete(2201);
    r.gps.service(now(2202));
    TEST_ASSERT_EQUAL_UINT(20, r.writes().size());
    TEST_ASSERT_FALSE(r.central.admissionOpen(0));
  }
  {
    Rig r;
    r.enable();
    r.connect();
    r.gps.offer(now(), fix());
    r.gps.service(now());
    auto t = now(2001);
    t.session_id = 8;
    r.gps.service(t);
    TEST_ASSERT_EQUAL_UINT(20, r.writes().size());
    TEST_ASSERT_FALSE(r.central.admissionOpen(0));
  }
}
void stalled_peer_does_not_block_another_peer_or_control() {
  Rig r;
  r.enable(0);
  r.enable(1);
  r.connect(0);
  r.connect(1);
  r.gps.offer(now(), fix());
  r.gps.service(now());     // peer 0 ATT remains pending.
  r.gps.service(now(2001)); // peer 1 can write.
  for (unsigned n = 0; n < 4; ++n) {
    r.complete(2001 + n);
    r.gps.service(now(2002 + n));
  }
  TEST_ASSERT_EQUAL_UINT(20, r.writes(0).size());
  TEST_ASSERT_EQUAL_UINT(71, r.writes(1).size());
  const uint8_t command[] = {0x10, 1};
  TEST_ASSERT_TRUE(r.central.write(1, 0, command, 2, 2006));
}
void cancellation_discards_pending_and_future_offer() {
  Rig r;
  r.enable();
  r.connect();
  r.gps.offer(now(), fix());
  r.gps.cancel();
  r.gps.offer(now(2001), fix(2001));
  r.gps.service(now(2001));
  TEST_ASSERT_EQUAL_UINT(0, r.writes().size());
  TEST_ASSERT_FALSE(r.gps.configure(1, camera(), config(), r.sequences[1]));
}
void partial_packet_cannot_be_interleaved_by_another_command_producer() {
  Rig r;
  r.enable();
  r.connect();
  r.gps.offer(now(), fix());
  r.gps.service(now());
  r.complete(2000);
  const uint8_t control[] = {0x10, 1};
  TEST_ASSERT_FALSE(r.central.write(0, 0, control, 2, 2001));
  TEST_ASSERT_FALSE(r.central.read(0, 0, 2001));
  r.gps.service(now(2001), 1);
  TEST_ASSERT_EQUAL_UINT(20, r.writes().size());
  TEST_ASSERT_FALSE(r.central.admissionOpen(0));
}
struct RawClock : Clock {
  uint32_t value = 0;
  uint32_t now() const override { return value; }
};
struct Uart : ModemUart {
  std::string rx, tx;
  size_t available() override { return rx.size(); }
  int read() override {
    int c = rx[0];
    rx.erase(0, 1);
    return c;
  }
  size_t writable() override { return 256; }
  size_t write(const char *b, size_t n) override {
    tx.append(b, n);
    return n;
  }
};
struct Consumer : GpsSnapshotConsumer {
  unsigned offers = 0, cancellations = 0;
  ModemSnapshot latest;
  void offer(const RecordTimestamp &, const ModemSnapshot &s) override {
    ++offers;
    latest = s;
  }
  void cancel() override { ++cancellations; }
};
void gnss_binding_publishes_copied_observations_and_cancels_once() {
  RawClock raw;
  SessionClock clock(raw, 7, 1000);
  Uart uart;
  ModemConfig c;
  c.documentary_profile_opt_in = c.terminal_retires_transaction = true;
  c.rx_bytes_per_tick = 256;
  c.tx_bytes_per_tick = 32;
  ModemGnss modem(uart, c);
  Consumer consumer;
  QualifiedPowerTiming timing;
  timing.qualified = true;
  GpsManager manager(clock, modem, nullptr, timing, &consumer);
  manager.tick();
  TEST_ASSERT_EQUAL_UINT(1, consumer.offers);
  TEST_ASSERT_EQUAL_STRING("AT\r", uart.tx.c_str());
  uart.rx = "OK\r\n";
  raw.value = 1;
  manager.tick();
  uart.rx = "OK\r\n+CGNSSPWR: READY!\r\n";
  raw.value = 2;
  manager.tick();
  uart.rx = "+CGPSINFO:0100.000000,N,00200.000000,W,010100,000000.00,4,0,90\r\nOK\r\n";
  raw.value = 3;
  manager.tick();
  TEST_ASSERT_TRUE(consumer.latest.fix.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.00001, 1, float(consumer.latest.fix.latitude_degrees));
  const unsigned offers = consumer.offers;
  manager.cancel();
  manager.cancel();
  manager.tick();
  TEST_ASSERT_EQUAL_UINT(1, consumer.cancellations);
  TEST_ASSERT_EQUAL_UINT(offers, consumer.offers);
}
void real_gnss_to_forwarder_delivers_and_no_fix_revokes_partial() {
  Rig r;
  r.enable();
  r.connect();
  RawClock raw;
  SessionClock clock(raw, 7, 1000);
  Uart uart;
  ModemConfig c;
  c.documentary_profile_opt_in = c.terminal_retires_transaction = true;
  c.rx_bytes_per_tick = 256;
  c.tx_bytes_per_tick = 32;
  c.poll_ms = 1;
  ModemGnss modem(uart, c);
  QualifiedPowerTiming timing;
  timing.qualified = true;
  GpsManager manager(clock, modem, nullptr, timing, &r.gps);
  manager.tick();
  uart.rx = "OK\r\n";
  raw.value = 1;
  manager.tick();
  uart.rx = "OK\r\n+CGNSSPWR: READY!\r\n";
  raw.value = 2;
  manager.tick();
  uart.rx = "+CGPSINFO:0100.000000,N,00200.000000,W,010100,000000.00,4,0,90\r\nOK\r\n";
  raw.value = 3;
  manager.tick();
  TEST_ASSERT_TRUE(manager.snapshot().fix.valid);
  r.gps.service(clock.snapshot());
  r.complete(3);
  TEST_ASSERT_EQUAL_UINT(20, r.writes().size());
  raw.value = 4;
  manager.tick(); // Actual next poll must be submitted before its reply.
  uart.rx = "+CGPSINFO:,,,,,,,,\r\nOK\r\n";
  raw.value = 5;
  const unsigned retirements = r.host.retired;
  manager.tick();
  TEST_ASSERT_EQUAL_UINT(retirements, r.host.retired);
  r.gps.service(clock.snapshot());
  TEST_ASSERT_TRUE(r.host.retired > retirements);
  TEST_ASSERT_FALSE(r.central.admissionOpen(0));
  TEST_ASSERT_EQUAL_UINT(20, r.writes().size());
}
void invalid_config_shared_stream_and_new_generation_are_guarded() {
  Rig r;
  auto c = config();
  c.interval_ms = 99;
  TEST_ASSERT_FALSE(r.gps.configure(0, camera(), c, r.sequences[0]));
  c = config();
  c.packet_deadline_ms = 5001;
  TEST_ASSERT_FALSE(r.gps.configure(0, camera(), c, r.sequences[0]));
  r.enable();
  TEST_ASSERT_FALSE(r.gps.configure(1, camera(), config(), r.sequences[0]));
  r.connect();
  r.gps.offer(now(), fix());
  BleResult ready;
  ready.kind = BleResultKind::TransportReady;
  ready.event.peer = 0;
  ready.event.generation = 1;
  ready.event.connection = 10;
  r.gps.result(ready); // Duplicate readiness cannot discard a new observation.
  TEST_ASSERT_TRUE(r.gps.status(0).pending);
  r.gps.service(now());
  BleResult old;
  old.kind = BleResultKind::WriteComplete;
  old.event.peer = 0;
  old.event.generation = 2;
  old.event.connection = 10;
  old.event.phase = BlePhase::Write;
  old.event.procedure = 100;
  r.gps.result(old);
  r.gps.service(now(2001));
  TEST_ASSERT_EQUAL_UINT(20, r.writes().size());
  r.gps.cancel();
  r.gps.service(now(2002));
  TEST_ASSERT_FALSE(r.central.admissionOpen(0));
}
void sequence_wrap_uses_source_range() {
  Be80CommandSequence sequence;
  for (unsigned n = 1; n <= 254; ++n)
    TEST_ASSERT_EQUAL_UINT8(n, sequence.take());
  TEST_ASSERT_EQUAL_UINT8(1, sequence.take());
}
void transport_failure_and_invalid_new_fix_never_replay() {
  {
    Rig r;
    r.enable();
    r.connect();
    r.host.failure = 17;
    r.gps.offer(now(), fix());
    r.gps.service(now());
    const size_t count = r.host.commands.size();
    r.gps.offer(now(2001), fix(2001));
    r.gps.service(now(2001));
    TEST_ASSERT_EQUAL_UINT(count, r.host.commands.size());
    TEST_ASSERT_FALSE(r.gps.status(0).linked);
  }
  {
    Rig r;
    r.enable();
    r.connect();
    r.gps.offer(now(), fix());
    r.gps.service(now());
    auto s = fix(2001);
    s.validity = FixValidity::NoFix;
    r.gps.offer(now(2001), s);
    r.complete(2002);
    r.gps.service(now(2002));
    TEST_ASSERT_EQUAL_UINT(20, r.writes().size());
  }
}
void wrong_discovered_route_never_receives_gps_bytes() {
  Rig r;
  r.enable();
  r.connect(0, 1, true);
  r.gps.offer(now(), fix());
  r.gps.service(now());
  TEST_ASSERT_FALSE(r.gps.status(0).linked);
  TEST_ASSERT_EQUAL_UINT(0, r.writes().size());
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(default_off_and_model_selection);
  RUN_TEST(literal_packet_real_central_and_shared_sequence);
  RUN_TEST(latest_fix_coalesces_and_rate_is_bounded);
  RUN_TEST(invalid_and_stale_fixes_never_transmit);
  RUN_TEST(control_drops_pending_and_seals_partial_without_replay);
  RUN_TEST(mtu_deadline_and_session_change_abandon_uncertain_stream);
  RUN_TEST(stalled_peer_does_not_block_another_peer_or_control);
  RUN_TEST(cancellation_discards_pending_and_future_offer);
  RUN_TEST(partial_packet_cannot_be_interleaved_by_another_command_producer);
  RUN_TEST(gnss_binding_publishes_copied_observations_and_cancels_once);
  RUN_TEST(real_gnss_to_forwarder_delivers_and_no_fix_revokes_partial);
  RUN_TEST(invalid_config_shared_stream_and_new_generation_are_guarded);
  RUN_TEST(sequence_wrap_uses_source_range);
  RUN_TEST(transport_failure_and_invalid_new_fix_never_replay);
  RUN_TEST(wrong_discovered_route_never_receives_gps_bytes);
  return UNITY_END();
}

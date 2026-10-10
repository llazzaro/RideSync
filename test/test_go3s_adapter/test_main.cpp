#include "profiles/insta360_go3s.h"
#include <algorithm>
#include <cstring>
#include <unity.h>
#include <vector>
using namespace ridesync;
struct Time : Clock {
  uint32_t value = 0;
  uint32_t now() const override { return value; }
};
struct Host : BleHost {
  std::vector<BleCommand> commands;
  std::vector<BleContext *> contexts, quiet;
  BleContext *links[4] = {};
  bool reject = false, drop = false, bad_crc = false, missing = false, cccd = true, property = true;
  bool ack_first = false, fragment = false;
  uint16_t negotiated = 67;
  unsigned starts = 0;
  BleHostState start(bool, bool) override {
    ++starts;
    return BleHostState::Ready;
  }
  BleHostState state() const override { return BleHostState::Ready; }
  BleFault fault() const override { return BleFault::None; }
  void sealStartup() override {}
  BleBondAdmission bondAdmission(const BondIdentity &) override {
    BleBondAdmission b;
    b.stack_ready = b.restore_verified = b.refusal_installed = true;
    b.existing_verified_identity = b.identity_matches = b.persistence_allowed = true;
    b.used = b.capacity = 4;
    return b;
  }
  int submit(const BleCommand &c, BleContext &x) override {
    commands.push_back(c);
    contexts.push_back(&x);
    quiet.erase(std::remove(quiet.begin(), quiet.end(), &x), quiet.end());
    return 0;
  }
  void deliver(BleContext &x, BleEvent e, bool terminal = false) {
    if (terminal) {
      x.terminal.store(true);
      quiet.push_back(&x);
    }
    x.receiver->copied(x, e);
  }
  int retire(BleContext &x) override {
    BleEvent e;
    e.kind = BleEventKind::Disconnected;
    e.connection = x.connection.load();
    deliver(x, e, true);
    return 0;
  }
  int cancelScan(BleContext &) override { return 0; }
  bool quiescent(const BleContext &x) const override {
    return x.terminal.load() && std::find(quiet.begin(), quiet.end(), &x) != quiet.end();
  }
  bool releaseContext(BleContext &) override { return true; }
  uint16_t mtu(uint16_t) const override { return negotiated; }
  void complete(size_t n) {
    BleEvent e;
    e.connection = commands[n].connection;
    e.handle = commands[n].handle;
    if (commands[n].phase == BlePhase::VerifySubscription) {
      e.size = 2;
      e.bytes[0] = 1;
    }
    deliver(*contexts[n], e, true);
  }
  // Independent synthetic camera fixture builder; deliberately separate from
  // production encoding/CRC. Known CRC vectors are checked by codec tests.
  std::vector<uint8_t> response(uint8_t seq, uint16_t status = 200) {
    std::vector<uint8_t> p = {255,
                              6,
                              64,
                              16,
                              0,
                              16,
                              0,
                              0,
                              0,
                              4,
                              0,
                              0,
                              static_cast<uint8_t>(status),
                              static_cast<uint8_t>(status >> 8),
                              2,
                              seq,
                              0,
                              0,
                              0xc0,
                              0,
                              0};
    unsigned crc = 65535;
    for (auto byte : p) {
      crc ^= byte;
      for (unsigned b = 0; b < 8; ++b) {
        const unsigned low = crc % 2;
        crc /= 2;
        if (low)
          crc ^= 40961;
      }
    }
    p.push_back(crc & 255);
    p.push_back(crc >> 8);
    if (bad_crc)
      p.back() ^= 1;
    return p;
  }
  void notify(uint8_t peer, const std::vector<uint8_t> &p, size_t from = 0, size_t size = 0) {
    BleEvent e;
    e.kind = BleEventKind::Notification;
    e.connection = 10 + peer;
    e.handle = 6;
    if (!size)
      size = p.size() - from;
    e.size = size;
    std::copy(p.begin() + from, p.begin() + from + size, e.bytes.begin());
    deliver(*links[peer], e);
  }
};
struct Rig {
  Host host;
  Time clock;
  Go3sAdapter adapter;
  CameraManager manager;
  size_t handled = 0;
  unsigned count;
  bool auto_sync = true;
  bool subscribed[4] = {}, synced[4] = {};
  Rig(unsigned peers = 1)
      : adapter(host, clock), manager(clock, adapter, Go3sAdapter::managerPolicy()), count(peers) {
    adapter.attach(manager);
    SourceConfig c;
    c.count = peers;
    Go3sQualification q;
    q.source_qualified = true;
    q.identity.verified = true;
    q.identity.type = IdentityType::Public;
    const char fw[] = "8.0.4.11";
    std::memcpy(q.firmware.data(), fw, sizeof(fw) - 1);
    q.firmware_size = sizeof(fw) - 1;
    q.authorization_size = 4;
    std::memcpy(q.authorization_id.data(), "test", 4);
    for (unsigned i = 0; i < peers; ++i) {
      c.cameras[i].family = CameraFamily::Insta360;
      c.cameras[i].model = CameraModel::GO3S;
      c.cameras[i].name = "go";
      c.cameras[i].identifier = "01:02:03:04:05:0" + std::to_string(i + 1);
      c.cameras[i].address_type = AddressType::Public;
      q.identity.address[0] = i + 1;
      TEST_ASSERT_TRUE(adapter.configurePeer(i, q));
    }
    TEST_ASSERT_TRUE(manager.configure(c).ok());
    TEST_ASSERT_TRUE(adapter.start(true, true));
  }
  ~Rig() {
    adapter.stop();
    for (unsigned n = 0; n < 20 && !adapter.canDestroy(); ++n)
      adapter.service();
    TEST_ASSERT_TRUE(adapter.canDestroy());
  }
  void handle(size_t n) {
    const auto c = host.commands[n];
    auto &x = *host.contexts[n];
    BleEvent e;
    e.connection = 10 + x.peer;
    if (c.phase == BlePhase::Connect) {
      host.links[x.peer] = &x;
      subscribed[x.peer] = synced[x.peer] = false;
      e.kind = BleEventKind::Connected;
      host.deliver(x, e);
    } else if (c.phase == BlePhase::Security) {
      e.kind = BleEventKind::Security;
      e.encrypted = e.bonded = true;
      e.identity.verified = true;
      e.identity.type = IdentityType::Public;
      e.identity.address[0] = x.peer + 1;
      host.deliver(x, e);
    } else if (c.phase == BlePhase::Services) {
      if (!host.missing) {
        e.kind = BleEventKind::Service;
        e.uuid = c.uuid;
        e.start = 1;
        e.end = 12;
        host.deliver(x, e);
      }
      host.complete(n);
    } else if (c.phase == BlePhase::Characteristics) {
      for (unsigned j = 0; j < 2; ++j) {
        e.kind = BleEventKind::Characteristic;
        e.uuid = BleUuid::shortUuid(j ? 0xbe82 : 0xbe81);
        e.start = 2 + j * 3;
        e.handle = e.start + 1;
        e.properties = j ? 0x10 : (host.property ? 8 : 4);
        host.deliver(x, e);
      }
      host.complete(n);
    } else if (c.phase == BlePhase::Descriptors) {
      if (host.cccd) {
        e.kind = BleEventKind::Descriptor;
        e.uuid = BleUuid::shortUuid(0x2902);
        e.handle = c.start + 1;
        host.deliver(x, e);
      }
      host.complete(n);
    } else if (c.phase == BlePhase::Subscribe || c.phase == BlePhase::VerifySubscription) {
      host.complete(n);
      if (c.phase == BlePhase::VerifySubscription)
        subscribed[x.peer] = true;
    } else if (c.phase == BlePhase::Write) {
      const bool sync = c.size < 23;
      auto reply = [&] {
        if (!sync && !host.drop) {
          const auto p = host.response(c.bytes[15], host.reject ? 400 : 200);
          if (host.fragment) {
            host.notify(x.peer, p, 0, 4);
            host.notify(x.peer, p, 4);
          } else
            host.notify(x.peer, p);
        }
      };
      if (host.ack_first)
        reply();
      host.complete(n);
      if (!host.ack_first)
        reply();
    }
  }
  void pump() {
    for (unsigned n = 0; n < 100; ++n) {
      adapter.service();
      adapter.service();
      if (handled < host.commands.size())
        handle(handled++);
      else {
        bool injected = false;
        for (unsigned i = 0; i < count; ++i)
          if (auto_sync && subscribed[i] && !synced[i]) {
            cameraSync(i);
            synced[i] = injected = true;
          }
        if (!injected)
          break;
      }
    }
    adapter.service();
  }
  void cameraSync(uint8_t peer = 0) {
    // Literal incoming SYNC fixture, CRC independently calculated.
    host.notify(peer, {0xff, 0x06, 0x41, 0x01, 0x00, 0xab, 0x98, 0x57});
  }
  unsigned nudges() {
    unsigned n = 0;
    for (const auto &c : host.commands)
      if (c.phase == BlePhase::Write && c.size == 1 && c.bytes[0] == 0)
        ++n;
    return n;
  }
  void connect() {
    for (unsigned i = 0; i < count; ++i)
      TEST_ASSERT_EQUAL(static_cast<int>(CameraError::None),
                        static_cast<int>(manager.request(i, Operation::Connect)));
    pump();
  }
  unsigned sent(unsigned code, uint8_t peer = 0) {
    unsigned n = 0;
    for (size_t i = 0; i < host.commands.size(); ++i) {
      const auto &c = host.commands[i];
      if (host.contexts[i]->peer == peer && c.phase == BlePhase::Write && c.size >= 23 &&
          c.bytes[2] == 64 && c.bytes[12] == code)
        ++n;
    }
    return n;
  }
};
void real_start_stop_ack_remains_unknown() {
  Rig r;
  r.host.ack_first = r.host.fragment = true;
  r.connect();
  TEST_ASSERT_EQUAL(static_cast<int>(Lifecycle::Ready),
                    static_cast<int>(r.manager.state(0)->lifecycle));
  TEST_ASSERT_EQUAL_UINT(1, r.sent(0x27));
  TEST_ASSERT_EQUAL(static_cast<int>(CameraError::Unsupported),
                    static_cast<int>(r.manager.request(0, Operation::Query)));
  TEST_ASSERT_EQUAL(static_cast<int>(CameraError::None),
                    static_cast<int>(r.manager.request(0, Operation::Start)));
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.sent(2));
  TEST_ASSERT_EQUAL_UINT(1, r.sent(4));
  TEST_ASSERT_EQUAL(static_cast<int>(Lifecycle::Ready),
                    static_cast<int>(r.manager.state(0)->lifecycle));
  TEST_ASSERT_FALSE(r.manager.state(0)->has_observation);
  TEST_ASSERT_EQUAL(static_cast<int>(RecordingState::Unknown),
                    static_cast<int>(r.manager.state(0)->observed));
  r.manager.request(0, Operation::Stop);
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.sent(5));
  TEST_ASSERT_FALSE(r.manager.state(0)->has_observation);
}
void refused_auth_never_reaches_shutter() {
  Rig r;
  r.host.reject = true;
  r.connect();
  TEST_ASSERT_FALSE(r.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(0, r.sent(4));
  TEST_ASSERT_EQUAL(static_cast<int>(Go3sFault::Rejected), static_cast<int>(r.adapter.fault(0)));
}
void missing_prerequisites_fail_without_commands() {
  for (unsigned mode = 0; mode < 4; ++mode) {
    Rig r;
    if (mode == 0)
      r.host.missing = true;
    if (mode == 1)
      r.host.cccd = false;
    if (mode == 2)
      r.host.property = false;
    if (mode == 3)
      r.host.negotiated = 23;
    r.connect();
    TEST_ASSERT_FALSE(r.adapter.commandReady(0));
    TEST_ASSERT_EQUAL_UINT(0, r.sent(4));
  }
}
void missing_or_malformed_ack_no_replay_and_other_peer_progress() {
  Rig r(2);
  r.connect();
  r.host.drop = true;
  r.manager.request(0, Operation::Stop);
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.sent(5));
  r.host.drop = false;
  r.manager.request(1, Operation::Start);
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.sent(4, 1));
  r.clock.value = Go3sAdapter::kResponseMs;
  r.adapter.service();
  r.pump();
  TEST_ASSERT_FALSE(r.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(1, r.sent(5));
  TEST_ASSERT_EQUAL(static_cast<int>(Go3sFault::Timeout), static_cast<int>(r.adapter.fault(0)));
  Rig bad;
  bad.connect();
  bad.host.bad_crc = true;
  bad.manager.request(0, Operation::Stop);
  bad.pump();
  TEST_ASSERT_FALSE(bad.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(1, bad.sent(5));
}
void reset_and_reconnect_never_replay() {
  Rig r;
  r.connect();
  r.manager.request(0, Operation::Start);
  r.pump();
  r.manager.reset();
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.sent(4));
  TEST_ASSERT_FALSE(r.manager.state(0)->has_observation);
  r.connect();
  TEST_ASSERT_TRUE(r.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(1, r.sent(4));
}
void control_waits_for_pending_keepalive_without_replay() {
  Rig r;
  r.connect();
  r.clock.value = Go3sAdapter::kKeepAliveMs;
  r.adapter.service();
  TEST_ASSERT_EQUAL(static_cast<int>(CameraError::None),
                    static_cast<int>(r.manager.request(0, Operation::Stop)));
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.sent(5));
  TEST_ASSERT_EQUAL(static_cast<int>(Lifecycle::Ready),
                    static_cast<int>(r.manager.state(0)->lifecycle));
  TEST_ASSERT_FALSE(r.manager.state(0)->has_observation);
}
void stale_sequence_and_expired_ack_cannot_complete() {
  Rig r;
  r.connect();
  r.host.drop = true;
  r.manager.request(0, Operation::Stop);
  r.pump();
  r.host.notify(0, r.host.response(1));
  r.adapter.service();
  TEST_ASSERT_EQUAL(static_cast<int>(Lifecycle::Operating),
                    static_cast<int>(r.manager.state(0)->lifecycle));
  r.clock.value = Go3sAdapter::kResponseMs;
  r.host.notify(0, r.host.response(2));
  r.adapter.service();
  r.pump();
  TEST_ASSERT_FALSE(r.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(1, r.sent(5));
  TEST_ASSERT_FALSE(r.manager.state(0)->has_observation);
}
void partial_frame_expiry_and_sequence_exhaustion_retire() {
  Rig r;
  r.connect();
  auto bytes = r.host.response(99);
  r.host.notify(0, bytes, 0, 4);
  r.adapter.service();
  r.clock.value = Go3sAdapter::kFragmentMs;
  r.adapter.service();
  r.pump();
  TEST_ASSERT_FALSE(r.adapter.commandReady(0));
  Rig exhausted;
  exhausted.connect();
  for (unsigned n = 0; n < 253; ++n) {
    TEST_ASSERT_EQUAL(static_cast<int>(CameraError::None),
                      static_cast<int>(exhausted.manager.request(0, Operation::Stop)));
    exhausted.pump();
  }
  TEST_ASSERT_EQUAL_UINT(253, exhausted.sent(5));
  exhausted.manager.request(0, Operation::Stop);
  exhausted.pump();
  TEST_ASSERT_FALSE(exhausted.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(253, exhausted.sent(5));
  TEST_ASSERT_EQUAL(static_cast<int>(Go3sFault::SequenceExhausted),
                    static_cast<int>(exhausted.adapter.fault(0)));
}
void qualification_and_disabled_start_have_no_radio_side_effects() {
  Host h;
  Time c;
  Go3sAdapter a(h, c);
  CameraManager m(c, a, Go3sAdapter::managerPolicy());
  a.attach(m);
  Go3sQualification q;
  TEST_ASSERT_FALSE(a.configurePeer(0, q));
  q.source_qualified = true;
  q.identity.verified = true;
  q.identity.type = IdentityType::Public;
  q.identity.address[0] = 1;
  q.firmware_size = 8;
  std::memcpy(q.firmware.data(), "8.0.4.12", 8);
  q.authorization_size = 1;
  q.authorization_id[0] = 'A';
  TEST_ASSERT_FALSE(a.configurePeer(0, q));
  TEST_ASSERT_FALSE(a.start(false, true));
  a.service();
  TEST_ASSERT_EQUAL_UINT(0, h.starts);
  TEST_ASSERT_TRUE(h.commands.empty());
  a.stop();
  a.service();
}
void delayed_initial_sync_is_bounded_and_accepted() {
  Rig r;
  r.auto_sync = false;
  r.connect();
  TEST_ASSERT_FALSE(r.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(0, r.sent(0x27));
  r.clock.value = 2000;
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.nudges());
  TEST_ASSERT_EQUAL_UINT(0, r.sent(0x27));
  r.cameraSync();
  r.pump();
  TEST_ASSERT_TRUE(r.adapter.commandReady(0));
  TEST_ASSERT_EQUAL_UINT(1, r.sent(0x27));
  TEST_ASSERT_EQUAL_UINT(0, r.sent(4));
}
void absent_initial_sync_times_out_without_replay() {
  Rig r;
  r.auto_sync = false;
  r.connect();
  r.clock.value = 2000;
  r.pump();
  r.clock.value = 3000;
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.nudges());
  TEST_ASSERT_EQUAL_UINT(0, r.sent(0x27));
  TEST_ASSERT_EQUAL(static_cast<int>(Go3sFault::Timeout), static_cast<int>(r.adapter.fault(0)));
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(delayed_initial_sync_is_bounded_and_accepted);
  RUN_TEST(absent_initial_sync_times_out_without_replay);
  RUN_TEST(control_waits_for_pending_keepalive_without_replay);
  RUN_TEST(stale_sequence_and_expired_ack_cannot_complete);
  RUN_TEST(partial_frame_expiry_and_sequence_exhaustion_retire);
  RUN_TEST(qualification_and_disabled_start_have_no_radio_side_effects);
  RUN_TEST(real_start_stop_ack_remains_unknown);
  RUN_TEST(refused_auth_never_reaches_shutter);
  RUN_TEST(missing_prerequisites_fail_without_commands);
  RUN_TEST(missing_or_malformed_ack_no_replay_and_other_peer_progress);
  RUN_TEST(reset_and_reconnect_never_replay);
  return UNITY_END();
}

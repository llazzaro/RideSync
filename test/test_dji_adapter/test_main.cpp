#include "../fixtures/ble/adapter_host.h"
#include "profiles/dji_camera.h"
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
  bool ack_first = false, fragment = false, grant_enabled = true, status_enabled = true;
  bool wrong_seq = false, hold_write = false, write_error = false;
  uint32_t model = 0xff33;
  uint8_t denied = 0;
  uint16_t next_status = 200;
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
    return write_error && c.phase == BlePhase::WriteWithoutResponse ? -77 : 0;
  }
  void deliver(BleContext &x, BleEvent e, bool terminal = false) {
    if (terminal) {
      x.terminal.store(true);
      quiet.push_back(&x);
    }
    TEST_ASSERT_NOT_NULL(x.receiver);
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
    return !(hold_write && x.phase == BlePhase::WriteWithoutResponse) && x.terminal.load() &&
           std::find(quiet.begin(), quiet.end(), &x) != quiet.end();
  }
  bool releaseContext(BleContext &) override { return true; }
  uint16_t mtu(uint16_t) const override { return negotiated; }
  void complete(size_t n) { adapter_fixture::complete(*this, n); }
  // Independent synthetic camera fixture builder; deliberately separate from
  // production encoding/CRC. Known CRC vectors are checked by codec tests.
  std::vector<uint8_t> frame(uint16_t seq, uint8_t type, uint8_t set, uint8_t id,
                             std::vector<uint8_t> data) {
    std::vector<uint8_t> p(14, 0);
    p[0] = 0xaa;
    p[3] = type;
    p[8] = seq;
    p[9] = seq >> 8;
    p[12] = set;
    p[13] = id;
    const auto len = p.size() + data.size() + 4;
    p[1] = len;
    p[2] = len >> 8;
    auto crc = [](const std::vector<uint8_t> &v, size_t n, uint32_t polynomial) {
      uint32_t r = 0x3aa3;
      for (size_t j = 0; j < n; ++j) {
        r ^= v[j];
        for (unsigned k = 0; k < 8; ++k)
          r = (r >> 1) ^ ((r & 1) ? polynomial : 0);
      }
      return r;
    };
    auto c = crc(p, 10, 0xa001);
    p[10] = c;
    p[11] = c >> 8;
    p.insert(p.end(), data.begin(), data.end());
    c = crc(p, p.size(), 0xedb88320);
    for (unsigned j = 0; j < 4; ++j)
      p.push_back(c >> (8 * j));
    return p;
  }
  std::vector<uint8_t> response(uint16_t seq, uint8_t set, uint8_t id) {
    return set == 0 ? frame(seq, 0x20, set, id,
                            {static_cast<uint8_t>(model), static_cast<uint8_t>(model >> 8), 0, 0,
                             static_cast<uint8_t>(reject ? 2 : 0), 0, 0, 0, 0})
                    : frame(seq, 0x20, set, id, {static_cast<uint8_t>(reject ? 2 : 0)});
  }
  std::vector<uint8_t> grant(uint32_t model = 0xff33, uint8_t result = 0) {
    std::vector<uint8_t> d(33, 0);
    for (unsigned j = 0; j < 4; ++j)
      d[j] = model >> (8 * j);
    d[4] = 6;
    d[26] = 2;
    d[27] = result;
    return frame(100, 2, 0, 0x19, d);
  }
  std::vector<uint8_t> status(uint16_t seq = 200, uint8_t mode = 1, uint8_t state = 1,
                              uint8_t power = 0) {
    std::vector<uint8_t> d(38, 0);
    d[0] = mode;
    d[1] = state;
    d[28] = power;
    d[29] = mode;
    return frame(seq, 0, 0x1d, 2, d);
  }
  void notify(uint8_t peer, const std::vector<uint8_t> &p, size_t from = 0, size_t size = 0) {
    adapter_fixture::notify(*this, peer, p, from, size);
  }
};
struct DjiReplyFixture {
  Host &host;
  explicit DjiReplyFixture(Host &h) : host(h) {}
  void replyConnection(uint8_t peer, uint16_t seq) {
    if (!host.drop)
      host.notify(peer, host.response(host.wrong_seq ? seq + 1 : seq, 0, 0x19));
    if (host.grant_enabled) {
      auto g = host.grant(host.model, host.denied);
      if (host.fragment) {
        host.notify(peer, g, 0, 7);
        host.notify(peer, g, 7);
      } else
        host.notify(peer, g);
    }
  }
  void reply(const BleCommand &c, BleContext &x) {
    const uint16_t seq = c.bytes[8] | (c.bytes[9] << 8);
    if (c.bytes[3] == 0x20)
      return;
    if (c.bytes[12] == 0 && c.bytes[13] == 0x19) {
      replyConnection(x.peer, seq);
    } else if (c.bytes[12] == 0x1d && c.bytes[13] == 5) {
      if (host.status_enabled)
        host.notify(x.peer, host.status(++host.next_status));
    } else if (!host.drop)
      host.notify(x.peer, host.response(seq, c.bytes[12], c.bytes[13]));
  }
  void writeResponse(size_t n, const BleCommand &c, BleContext &x, BleEvent &) {
    if (host.ack_first)
      reply(c, x);
    host.complete(n);
    if (!host.ack_first)
      reply(c, x);
  }
};

struct Rig {
  Host host;
  Time clock;
  DjiAdapter adapter;
  CameraManager manager;
  size_t handled = 0;
  unsigned count;
  bool auto_sync = false;
  bool subscribed[4] = {}, synced[4] = {};
  Rig(unsigned peers = 1, CameraModel model = CameraModel::DJI_ACTION4)
      : adapter(host, clock), manager(clock, adapter, DjiAdapter::managerPolicy()), count(peers) {
    adapter.attach(manager);
    SourceConfig c;
    c.count = peers;
    DjiQualification q;
    q.source_qualified = true;
    q.identity.verified = true;
    q.identity.type = IdentityType::Public;
    const char fw[] = "synthetic-47";
    std::memcpy(q.firmware.data(), fw, sizeof(fw) - 1);
    q.firmware_size = sizeof(fw) - 1;
    q.model = model;
    host.model = model == CameraModel::DJI_ACTION4       ? 0xff33
                 : model == CameraModel::DJI_ACTION5_PRO ? 0xff44
                 : model == CameraModel::DJI_ACTION6     ? 0xff55
                                                         : 0xff66;
    q.controller_mac = {1, 2, 3, 4, 5, 6};
    q.controller_mac_verified = true;
    q.unencrypted_protocol_reviewed = true;
    q.controller_id = 0x12345678;
    q.connection_challenge = 1234;
    for (unsigned i = 0; i < peers; ++i) {
      c.cameras[i].family = CameraFamily::DJI;
      c.cameras[i].model = model;
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
  void connected(size_t, const BleCommand &, BleContext &x, BleEvent &e) {
    host.links[x.peer] = &x;
    subscribed[x.peer] = synced[x.peer] = false;
    e.kind = BleEventKind::Connected;
    host.deliver(x, e);
  }
  void secured(size_t, const BleCommand &, BleContext &x, BleEvent &e) {
    e.kind = BleEventKind::Security;
    e.encrypted = e.bonded = true;
    e.identity.verified = true;
    e.identity.type = IdentityType::Public;
    e.identity.address[0] = x.peer + 1;
    host.deliver(x, e);
  }
  void characteristics(size_t n, const BleCommand &, BleContext &x, BleEvent &e) {
    adapter_fixture::characteristics(host, n, x, e, BleUuid::shortUuid(0xfff5),
                                     BleUuid::shortUuid(0xfff4), 4, 8);
  }
  void subscribedPhase(size_t n, const BleCommand &c, BleContext &x, BleEvent &) {
    host.complete(n);
    if (c.phase == BlePhase::VerifySubscription)
      subscribed[x.peer] = true;
  }

  void writeResponse(size_t n, const BleCommand &c, BleContext &x, BleEvent &e) {
    DjiReplyFixture(host).writeResponse(n, c, x, e);
  }
  void handle(size_t n) { adapter_fixture::handle(*this, n, BlePhase::WriteWithoutResponse); }
  void pump() { adapter_fixture::pump(*this); }
  void cameraSync(uint8_t peer = 0) { host.notify(peer, host.grant()); }
  unsigned nudges() {
    unsigned n = 0;
    for (const auto &c : host.commands)
      if (c.phase == BlePhase::WriteWithoutResponse && c.size == 1 && c.bytes[0] == 0)
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
      if (host.contexts[i]->peer == peer && c.phase == BlePhase::WriteWithoutResponse &&
          c.size >= 23 && c.bytes[2] == 64 && c.bytes[12] == code)
        ++n;
    }
    return n;
  }
};

void setUp() {}
void tearDown() {}
void pairing_and_status() {
  Rig r;
  r.host.ack_first = r.host.fragment = true;
  r.connect();
  TEST_ASSERT_EQUAL((int)Lifecycle::Ready, (int)r.manager.state(0)->lifecycle);
  TEST_ASSERT_EQUAL((int)RecordingState::Stopped, (int)r.manager.state(0)->observed);
  TEST_ASSERT_TRUE(r.adapter.commandReady(0));
}
void missing_authorization_is_not_ready() {
  Rig r;
  r.host.drop = true;
  r.host.grant_enabled = false;
  r.connect();
  TEST_ASSERT_FALSE(r.adapter.commandReady(0));
  r.clock.value = 30001;
  r.pump();
  TEST_ASSERT_EQUAL((int)DjiFault::Timeout, (int)r.adapter.fault(0));
}
void denied_and_wrong_model() {
  {
    Rig r;
    r.host.denied = 1;
    r.connect();
    TEST_ASSERT_EQUAL((int)DjiFault::Rejected, (int)r.adapter.fault(0));
  }
  {
    Rig r;
    r.host.model = 0xff44;
    r.connect();
    TEST_ASSERT_EQUAL((int)DjiFault::Qualification, (int)r.adapter.fault(0));
  }
}
void status_stale_and_cancel() {
  Rig r;
  r.connect();
  r.host.status_enabled = false;
  r.clock.value = 1600;
  r.pump();
  TEST_ASSERT_EQUAL((int)RecordingState::Unknown, (int)r.manager.state(0)->observed);
  TEST_ASSERT_FALSE(r.adapter.begin(0, *r.manager.configuredCamera(0), Operation::Start,
                                    r.manager.state(0)->token));
  r.manager.reset();
  r.pump();
  TEST_ASSERT_FALSE(r.adapter.commandReady(0));
}
void ack_not_recording_and_explicit_stop() {
  Rig r;
  r.connect();
  TEST_ASSERT_EQUAL((int)CameraError::None, (int)r.manager.request(0, Operation::Start));
  r.pump();
  TEST_ASSERT_EQUAL((int)RecordingState::Stopped, (int)r.manager.state(0)->observed);
  TEST_ASSERT_EQUAL((int)CameraError::None, (int)r.manager.request(0, Operation::Stop));
  r.pump();
  unsigned rec = 0, stop = 0;
  for (auto &c : r.host.commands)
    if (c.phase == BlePhase::WriteWithoutResponse && c.bytes[12] == 0x1d && c.bytes[13] == 3) {
      rec += c.bytes[18] == 0;
      stop += c.bytes[18] == 1;
    }
  TEST_ASSERT_EQUAL(1, rec);
  TEST_ASSERT_EQUAL(1, stop);
}
void wrong_sequence_and_missing_grant() {
  {
    Rig r;
    r.host.wrong_seq = true;
    r.host.grant_enabled = false;
    r.connect();
    TEST_ASSERT_FALSE(r.adapter.commandReady(0));
    r.clock.value = 30001;
    r.pump();
    TEST_ASSERT_EQUAL((int)DjiFault::Timeout, (int)r.adapter.fault(0));
  }
  {
    Rig r;
    r.host.grant_enabled = false;
    r.connect();
    TEST_ASSERT_FALSE(r.adapter.commandReady(0));
    r.clock.value = 30001;
    r.pump();
    TEST_ASSERT_EQUAL((int)DjiFault::Timeout, (int)r.adapter.fault(0));
  }
}
void malformed_fragment_and_properties() {
  {
    Rig r;
    r.connect();
    auto p = r.host.status(900);
    r.host.notify(0, p, 0, 4);
    r.pump();
    r.clock.value = 1001;
    r.pump();
    TEST_ASSERT_EQUAL((int)DjiFault::Protocol, (int)r.adapter.fault(0));
  }
  {
    Rig r;
    r.host.property = false;
    r.connect();
    TEST_ASSERT_EQUAL((int)DjiFault::Transport, (int)r.adapter.fault(0));
  }
}
void duplicate_state_does_not_refresh_and_photo_blocks_rec() {
  Rig r;
  r.connect();
  r.host.notify(0, r.host.status(300, 5, 3));
  r.pump();
  TEST_ASSERT_EQUAL((int)RecordingState::Unknown, (int)r.manager.state(0)->observed);
  TEST_ASSERT_FALSE(r.adapter.begin(0, *r.manager.configuredCamera(0), Operation::Start,
                                    r.manager.state(0)->token));
  r.host.notify(0, r.host.status(301, 1, 3));
  r.pump();
  TEST_ASSERT_EQUAL((int)RecordingState::Recording, (int)r.manager.state(0)->observed);
  r.clock.value = 1400;
  r.host.notify(0, r.host.status(301, 1, 3));
  r.pump();
  r.clock.value = 1500;
  r.host.status_enabled = false;
  r.pump();
  TEST_ASSERT_EQUAL((int)RecordingState::Unknown, (int)r.manager.state(0)->observed);
}
void reconnect_has_no_replay() {
  Rig r;
  r.connect();
  r.manager.reset();
  r.pump();
  r.connect();
  TEST_ASSERT_TRUE(r.adapter.commandReady(0));
  unsigned recording = 0;
  for (const auto &c : r.host.commands)
    if (c.phase == BlePhase::WriteWithoutResponse && c.bytes[12] == 0x1d && c.bytes[13] == 3)
      ++recording;
  TEST_ASSERT_EQUAL(0, recording);
}
void local_write_barrier_and_error() {
  {
    Rig r;
    r.host.hold_write = true;
    r.connect();
    TEST_ASSERT_FALSE(r.adapter.commandReady(0));
    unsigned writes = 0;
    for (const auto &c : r.host.commands)
      writes += c.phase == BlePhase::WriteWithoutResponse;
    TEST_ASSERT_EQUAL(1, writes);
    r.host.hold_write = false;
    r.pump();
    TEST_ASSERT_TRUE(r.adapter.commandReady(0));
  }
  {
    Rig r;
    r.host.write_error = true;
    r.connect();
    TEST_ASSERT_EQUAL((int)DjiFault::Transport, (int)r.adapter.fault(0));
  }
}
void query_and_wrong_command_ack() {
  Rig r;
  r.connect();
  TEST_ASSERT_EQUAL((int)CameraError::None, (int)r.manager.request(0, Operation::Query));
  r.pump();
  TEST_ASSERT_EQUAL((int)Lifecycle::Ready, (int)r.manager.state(0)->lifecycle);
  r.host.drop = true;
  TEST_ASSERT_EQUAL((int)CameraError::None, (int)r.manager.request(0, Operation::Start));
  r.pump();
  const auto &c = r.host.commands.back();
  const uint16_t seq = c.bytes[8] | (c.bytes[9] << 8);
  r.host.notify(0, r.host.response(seq + 1, 0x1d, 3));
  r.pump();
  TEST_ASSERT_EQUAL((int)Lifecycle::Operating, (int)r.manager.state(0)->lifecycle);
  r.manager.cancel(0);
  r.pump();
  TEST_ASSERT_FALSE(r.adapter.commandReady(0));
}
void missing_service_cccd_and_short_mtu() {
  {
    Rig r;
    r.host.missing = true;
    r.connect();
    TEST_ASSERT_EQUAL((int)DjiFault::Transport, (int)r.adapter.fault(0));
  }
  {
    Rig r;
    r.host.cccd = false;
    r.connect();
    TEST_ASSERT_EQUAL((int)DjiFault::Transport, (int)r.adapter.fault(0));
  }
  {
    Rig r;
    r.host.negotiated = 23;
    r.connect();
    TEST_ASSERT_EQUAL((int)DjiFault::Mtu, (int)r.adapter.fault(0));
  }
}
void model_ids_and_camera_indexes() {
  for (auto model : {CameraModel::DJI_ACTION4, CameraModel::DJI_ACTION5_PRO,
                     CameraModel::DJI_ACTION6, CameraModel::DJI_OSMO360}) {
    Rig r(1, model);
    r.connect();
    TEST_ASSERT_TRUE(r.adapter.commandReady(0));
  }
  Rig r(4);
  r.connect();
  unsigned replies = 0;
  for (size_t j = 0; j < r.host.commands.size(); ++j) {
    const auto &c = r.host.commands[j];
    if (c.phase == BlePhase::WriteWithoutResponse && c.bytes[3] == 0x20) {
      TEST_ASSERT_EQUAL(r.host.contexts[j]->peer + 1, c.bytes[19]);
      TEST_ASSERT_EQUAL(100, c.bytes[8]);
      ++replies;
    }
  }
  TEST_ASSERT_EQUAL(4, replies);
}
void direct_approved_grant() {
  {
    Rig r;
    r.host.drop = true;
    r.connect();
    TEST_ASSERT_TRUE(r.adapter.commandReady(0));
  }
}
void safe_stop_modes() {
  {
    Rig r;
    r.connect();
    r.host.notify(0, r.host.status(900, 5, 3));
    r.pump();
    TEST_ASSERT_FALSE(r.adapter.begin(0, *r.manager.configuredCamera(0), Operation::Stop,
                                      r.manager.state(0)->token));
    r.host.notify(0, r.host.status(901, 1, 5));
    r.pump();
    TEST_ASSERT_FALSE(r.adapter.begin(0, *r.manager.configuredCamera(0), Operation::Stop,
                                      r.manager.state(0)->token));
    r.clock.value = 1600;
    r.host.status_enabled = false;
    r.pump();
    TEST_ASSERT_FALSE(r.adapter.begin(0, *r.manager.configuredCamera(0), Operation::Stop,
                                      r.manager.state(0)->token));
  }
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(pairing_and_status);
  RUN_TEST(missing_authorization_is_not_ready);
  RUN_TEST(denied_and_wrong_model);
  RUN_TEST(status_stale_and_cancel);
  RUN_TEST(ack_not_recording_and_explicit_stop);
  RUN_TEST(wrong_sequence_and_missing_grant);
  RUN_TEST(malformed_fragment_and_properties);
  RUN_TEST(duplicate_state_does_not_refresh_and_photo_blocks_rec);
  RUN_TEST(reconnect_has_no_replay);
  RUN_TEST(local_write_barrier_and_error);
  RUN_TEST(query_and_wrong_command_ack);
  RUN_TEST(missing_service_cccd_and_short_mtu);
  RUN_TEST(model_ids_and_camera_indexes);
  RUN_TEST(direct_approved_grant);
  RUN_TEST(safe_stop_modes);
  return UNITY_END();
}

#include "profiles/mixed_camera.h"
#include <cstring>
#include <unity.h>
#include <vector>
using namespace ridesync;
struct Time : Clock {
  uint32_t now() const override { return 0; }
};
struct Host : BleHost {
  std::vector<BleCommand> commands;
  std::vector<uint8_t> slots;
  BleHostState start(bool, bool) override { return BleHostState::Ready; }
  BleHostState state() const override { return BleHostState::Ready; }
  BleFault fault() const override { return BleFault::None; }
  void sealStartup() override {}
  BleBondAdmission bondAdmission(const BondIdentity &) override {
    BleBondAdmission b;
    b.stack_ready = b.restore_verified = b.refusal_installed = b.existing_verified_identity =
        b.identity_matches = b.persistence_allowed = true;
    b.capacity = 5;
    return b;
  }
  int submit(const BleCommand &c, BleContext &x) override {
    commands.push_back(c);
    slots.push_back(x.peer);
    return 0;
  }
  int retire(BleContext &x) override {
    x.terminal.store(true);
    BleEvent e;
    e.kind = BleEventKind::Disconnected;
    e.connection = x.connection.load();
    x.receiver->copied(x, e);
    return 0;
  }
  int cancelScan(BleContext &x) override {
    x.terminal.store(true);
    return 0;
  }
  bool quiescent(const BleContext &x) const override { return x.terminal.load(); }
  bool releaseContext(BleContext &) override { return true; }
  uint16_t mtu(uint16_t) const override { return 100; }
};
struct Peripheral : X5PeripheralPort {
  bool configure(const X5Qualification &) override { return true; }
  unsigned connects = 0, shutters = 0;
  bool connect(Token, uint32_t) override {
    ++connects;
    return true;
  }
  bool notify(const X5ShutterRequest &) override {
    ++shutters;
    return true;
  }
  void cancel(Token) override {}
  void close(uint32_t) override {}
  bool poll(X5Input &) override { return false; }
  bool takeLoss(uint32_t) override { return false; }
  bool released(uint32_t) const override { return true; }
  void service(uint32_t) override {}
};
struct MarkerSink : CameraMarkerAudit {
  unsigned unsupported = 0;
  void markerEvent(const CameraMarkerEvent &event) override {
    if (event.phase == CameraMarkerPhase::RequestRefused &&
        event.error == static_cast<uint8_t>(CameraError::Unsupported))
      ++unsupported;
  }
};
void test_mixed_connect_routes_and_advances_once() {
  Time t;
  Host h;
  Peripheral p;
  MixedCameraAdapter a(h, p, t);
  CameraManager m(t, a, MixedCameraAdapter::managerPolicy());
  RecordingManager g(m, t);
  a.attach(m, g);
  SourceConfig c;
  c.count = 3;
  MixedCameraQualifications q;
  for (unsigned i = 0; i < 2; ++i) {
    auto &x = c.cameras[i];
    x.enabled = true;
    x.name = i ? "go" : "rs";
    x.family = CameraFamily::Insta360;
    x.model = i ? CameraModel::GO3S : CameraModel::ONE_RS;
    x.address_type = AddressType::Public;
    x.identifier = i ? "01:02:03:04:05:07" : "01:02:03:04:05:06";
  }
  auto &rs = q.one_rs[0];
  rs.source_qualified = rs.core_one_rs = rs.ordinary_360_lens = rs.video_mode_declared = true;
  rs.identity.verified = true;
  rs.identity.type = IdentityType::Public;
  rs.identity.address = {{6, 5, 4, 3, 2, 1}};
  rs.firmware[0] = '1';
  rs.firmware_size = 1;
  auto &go = q.go3s[1];
  go.source_qualified = true;
  go.identity.verified = true;
  go.identity.type = IdentityType::Public;
  go.identity.address = {{7, 5, 4, 3, 2, 1}};
  std::memcpy(go.firmware.data(), "8.0.4.11", 8);
  go.firmware_size = 8;
  go.authorization_id[0] = 'a';
  go.authorization_size = 1;
  auto &camera = c.cameras[2];
  camera.name = "DJI synthetic";
  camera.family = CameraFamily::DJI;
  camera.model = CameraModel::DJI_ACTION4;
  camera.address_type = AddressType::Public;
  camera.identifier = "01:02:03:04:05:08";
  auto &dji = q.dji[2];
  dji.model = camera.model;
  dji.source_qualified = dji.identity.verified = dji.controller_mac_verified = true;
  dji.identity.type = IdentityType::Public;
  dji.identity.address = {{8, 5, 4, 3, 2, 1}};
  dji.firmware[0] = '1';
  dji.firmware_size = 1;
  dji.controller_mac = {{1, 2, 3, 4, 5, 6}};
  dji.connection_challenge = 0x1234;
  dji.unencrypted_protocol_reviewed = true;
  dji.controller_id = 0x12345678;
  dji.already_paired = true;
  TEST_ASSERT_TRUE(m.configure(c).ok());
  TEST_ASSERT_TRUE(a.configure(c, q));
  TEST_ASSERT_TRUE(a.start(true, true));
  MarkerSink marker_sink;
  TEST_ASSERT_TRUE(a.attachMarkerAudit(marker_sink));
  for (uint8_t i = 0; i < 3; ++i)
    TEST_ASSERT_EQUAL(CameraError::Unsupported, a.requestHilight(i, CameraMarkerKey(42, i + 1)));
  TEST_ASSERT_EQUAL_UINT(3, marker_sink.unsupported);
  TEST_ASSERT_EQUAL_UINT(0, h.commands.size());
  TEST_ASSERT_EQUAL(GroupError::None, g.request(RecordingState::Recording));
  auto ticks = m.ticks();
  auto advances = g.advancements();
  a.service();
  TEST_ASSERT_EQUAL(ticks + 1, m.ticks());
  TEST_ASSERT_EQUAL(advances + 1, g.advancements());
  TEST_ASSERT_EQUAL(3, h.commands.size());
  TEST_ASSERT_EQUAL(0, h.slots[0]);
  TEST_ASSERT_EQUAL(1, h.slots[1]);
  TEST_ASSERT_EQUAL(2, h.slots[2]);
  TEST_ASSERT_EQUAL(RecordingState::Unknown, m.state(0)->observed);
  TEST_ASSERT_EQUAL(RecordingState::Unknown, m.state(1)->observed);
  a.stop();
  for (unsigned i = 0; i < 10 && !a.canDestroy(); ++i)
    a.service();
  TEST_ASSERT_TRUE(a.canDestroy());
}
struct Radio : WakeRadio {
  unsigned starts = 0;
  WakeOperation op;
  WakeSubmit begin(const WakeOperation &o, const insta360::WakeEncoding &, uint32_t,
                   uint32_t) override {
    op = o;
    ++starts;
    return WakeSubmit::Accepted;
  }
  void cancel(const WakeOperation &) override {}
  WakeRadioResult poll(const WakeOperation &o, uint32_t) override {
    WakeRadioResult r;
    r.operation = o;
    r.submitted = r.terminal = r.released = true;
    return r;
  }
};
void test_wake_recovery_connects_without_shutter_and_cancel_releases() {
  Time t;
  Host h;
  Peripheral p;
  Radio radio;
  MixedCameraAdapter a(h, p, t, &radio);
  CameraManager m(t, a, MixedCameraAdapter::managerPolicy());
  RecordingManager g(m, t);
  a.attach(m, g);
  SourceConfig c;
  c.count = 1;
  auto &cam = c.cameras[0];
  cam.name = "x5";
  cam.family = CameraFamily::Insta360;
  cam.model = CameraModel::X5;
  cam.identifier = "01:02:03:04:05:06";
  cam.address_type = AddressType::Public;
  cam.wake_identifier = "ABCDEF";
  MixedCameraQualifications q;
  q.x5.enabled = true;
  q.x5.identity.verified = true;
  q.x5.identity.type = IdentityType::Public;
  q.x5.identity.address = {{6, 5, 4, 3, 2, 1}};
  q.x5.store.qualification_record = 1;
  q.x5.display.profile = insta360::Ce80DisplayProfile::X5CapturedDisplayV1;
  q.x5.firmware[0] = '1';
  q.x5.firmware_size = 1;
  q.x5_wake.enabled = q.x5_wake.source_qualified = true;
  q.x5_wake.profile = insta360::WakeProfile::M5WakeV1;
  std::copy(cam.wake_identifier.begin(), cam.wake_identifier.end(), q.x5_wake.identifier.begin());
  TEST_ASSERT_TRUE(m.configure(c).ok());
  SourceConfig mismatch = c;
  mismatch.cameras[0].identifier = "01:02:03:04:05:08";
  TEST_ASSERT_FALSE(a.configure(mismatch, q));
  TEST_ASSERT_TRUE(a.configure(c, q));
  TEST_ASSERT_TRUE(a.start(true, true));
  TEST_ASSERT_EQUAL(CameraError::None, a.requestRecovery(0, false));
  for (unsigned i = 0; i < 8; ++i)
    a.service();
  TEST_ASSERT_EQUAL(1, radio.starts);
  TEST_ASSERT_EQUAL(1, p.connects);
  TEST_ASSERT_EQUAL(0, p.shutters);
  TEST_ASSERT_FALSE(a.recoveryReady(0));
  TEST_ASSERT_EQUAL(RecordingState::Unknown, m.state(0)->observed);
  a.cancelRecovery(0);
  a.stop();
  for (unsigned i = 0; i < 8; ++i)
    a.service();
  TEST_ASSERT_TRUE(a.canDestroy());
}
void test_dji_mixed_routes_all_models_and_reconnects_without_wake() {
  Time t;
  Host h;
  Peripheral p;
  Radio radio;
  MixedCameraAdapter a(h, p, t, &radio);
  CameraManager m(t, a, MixedCameraAdapter::managerPolicy());
  RecordingManager g(m, t);
  a.attach(m, g);
  SourceConfig c;
  c.count = 4;
  MixedCameraQualifications q;
  for (unsigned i = 0; i < 4; ++i) {
    auto &cam = c.cameras[i];
    cam.name = "DJI synthetic";
    cam.family = CameraFamily::DJI;
    cam.model = static_cast<CameraModel>(5 + i);
    cam.address_type = AddressType::Public;
    cam.identifier = "01:02:03:04:05:06";
    cam.identifier.back() = "6789"[i];
    auto &qual = q.dji[i];
    qual.model = cam.model;
    qual.source_qualified = qual.identity.verified = qual.controller_mac_verified = true;
    qual.identity.type = IdentityType::Public;
    qual.identity.address = {{uint8_t(6 + i), 5, 4, 3, 2, 1}};
    qual.firmware[0] = '1';
    qual.firmware_size = 1;
    qual.controller_mac = {{1, 2, 3, 4, 5, 6}};
    qual.connection_challenge = 0x1234;
    qual.unencrypted_protocol_reviewed = true;
    qual.controller_id = 0x12345678;
    qual.already_paired = true;
  }
  TEST_ASSERT_TRUE(m.configure(c).ok());
  // Source settings retain eight slots; BLE runtimes explicitly limit active slots to four.
  {
    Host other_host;
    Peripheral other_port;
    MixedCameraAdapter other(other_host, other_port, t);
    CameraManager other_manager(t, other, MixedCameraAdapter::managerPolicy());
    RecordingManager other_group(other_manager, t);
    other.attach(other_manager, other_group);
    SourceConfig beyond = c;
    beyond.count = 5;
    beyond.cameras[4] = c.cameras[0];
    beyond.cameras[4].identifier = "01:02:03:04:05:0A";
    for (unsigned i = 0; i < 4; ++i)
      beyond.cameras[i].enabled = false;
    MixedCameraQualifications beyond_q;
    beyond_q.dji[4] = q.dji[0];
    beyond_q.dji[4].identity.address[0] = 10;
    TEST_ASSERT_TRUE(other_manager.configure(beyond).ok());
    TEST_ASSERT_FALSE(other.configure(beyond, beyond_q));
  }
  auto mismatch = q;
  mismatch.dji[0].model = CameraModel::DJI_ACTION6;
  TEST_ASSERT_FALSE(a.configure(c, mismatch));
  mismatch = q;
  mismatch.dji[0].identity.type = IdentityType::RandomStatic;
  TEST_ASSERT_FALSE(a.configure(c, mismatch));
  TEST_ASSERT_TRUE(a.configure(c, q));
  TEST_ASSERT_TRUE(a.start(true, true));
  for (unsigned i = 0; i < 4; ++i) {
    TEST_ASSERT_FALSE(a.wakeSupported(i));
    TEST_ASSERT_FALSE(a.commandReady(i));
    TEST_ASSERT_TRUE(a.linkReleased(i));
    TEST_ASSERT_EQUAL(CameraError::Unsupported, a.requestRecovery(i, true));
    TEST_ASSERT_EQUAL(CameraError::None, a.requestRecovery(i, false));
  }
  auto ticks = m.ticks();
  auto advances = g.advancements();
  a.service();
  TEST_ASSERT_EQUAL(ticks + 1, m.ticks());
  TEST_ASSERT_EQUAL(advances + 1, g.advancements());
  TEST_ASSERT_EQUAL(1, h.commands.size()); // One shared central initiates one link at a time.
  bool seen[4] = {};
  for (unsigned i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL(i + 1, h.commands.size());
    const auto peer = h.slots[i];
    TEST_ASSERT_LESS_THAN(4, peer);
    TEST_ASSERT_FALSE(seen[peer]);
    seen[peer] = true;
    TEST_ASSERT_EQUAL(RecordingState::Unknown, m.state(peer)->observed);
    TEST_ASSERT_EQUAL(CameraError::None, a.cancelRecovery(peer));
    for (unsigned pass = 0; pass < 3; ++pass)
      a.service();
  }
  for (bool routed : seen)
    TEST_ASSERT_TRUE(routed);
  TEST_ASSERT_EQUAL(0, radio.starts);
  TEST_ASSERT_EQUAL(0, p.connects);
  a.stop();
  for (unsigned i = 0; i < 10 && !a.canDestroy(); ++i)
    a.service();
  TEST_ASSERT_TRUE(a.canDestroy());
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dji_mixed_routes_all_models_and_reconnects_without_wake);
  RUN_TEST(test_mixed_connect_routes_and_advances_once);
  RUN_TEST(test_wake_recovery_connects_without_shutter_and_cancel_releases);
  return UNITY_END();
}

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
void test_mixed_connect_routes_and_advances_once() {
  Time t;
  Host h;
  Peripheral p;
  MixedCameraAdapter a(h, p, t);
  CameraManager m(t, a, MixedCameraAdapter::managerPolicy());
  RecordingManager g(m, t);
  a.attach(m, g);
  SourceConfig c;
  c.count = 2;
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
  TEST_ASSERT_TRUE(m.configure(c).ok());
  TEST_ASSERT_TRUE(a.configure(c, q));
  TEST_ASSERT_TRUE(a.start(true, true));
  TEST_ASSERT_EQUAL(GroupError::None, g.request(RecordingState::Recording));
  auto ticks = m.ticks();
  auto advances = g.advancements();
  a.service();
  TEST_ASSERT_EQUAL(ticks + 1, m.ticks());
  TEST_ASSERT_EQUAL(advances + 1, g.advancements());
  TEST_ASSERT_EQUAL(2, h.commands.size());
  TEST_ASSERT_EQUAL(0, h.slots[0]);
  TEST_ASSERT_EQUAL(1, h.slots[1]);
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
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_mixed_connect_routes_and_advances_once);
  RUN_TEST(test_wake_recovery_connects_without_shutter_and_cancel_releases);
  return UNITY_END();
}

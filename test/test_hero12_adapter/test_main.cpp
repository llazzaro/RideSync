#include "profiles/gopro_hero12.h"
#include <algorithm>
#include <cstring>
#include <unity.h>
#include <vector>
using namespace ridesync;

struct TestClock : Clock {
  uint32_t value = 0;
  uint32_t now() const override { return value; }
};
struct SilentHost : BleHost {
  unsigned starts = 0;
  BleHostState start(bool, bool) override {
    ++starts;
    return BleHostState::Ready;
  }
  BleHostState state() const override { return BleHostState::Ready; }
  BleFault fault() const override { return BleFault::None; }
  void sealStartup() override {}
  BleBondAdmission bondAdmission(const BondIdentity &) override { return {}; }
  int submit(const BleCommand &, BleContext &) override { return 0; }
  int retire(BleContext &) override { return 0; }
  int cancelScan(BleContext &) override { return 0; }
  bool quiescent(const BleContext &) const override { return true; }
  bool releaseContext(BleContext &) override { return true; }
  uint16_t mtu(uint16_t) const override { return 67; }
};
void default_disabled_never_starts_host_or_admits_connect() {
  SilentHost host;
  TestClock clock;
  Hero12Adapter adapter(host, clock);
  CameraManager manager(clock, adapter, Hero12Adapter::managerPolicy());
  adapter.attach(manager);
  TEST_ASSERT_FALSE(adapter.start(false, true));
  CameraConfig camera;
  camera.family = CameraFamily::GoPro;
  camera.model = CameraModel::HERO12_BLACK;
  Token token;
  token.connection = token.operation = 1;
  TEST_ASSERT_FALSE(adapter.begin(0, camera, Operation::Connect, token));
  adapter.service();
  TEST_ASSERT_EQUAL_UINT(0, host.starts);
}
void required_management_and_classic_routes_are_declared() {
  const auto spec = Hero12Adapter::profileSpec();
  TEST_ASSERT_EQUAL_UINT8(2, spec.service_count);
  TEST_ASSERT_EQUAL_UINT8(8, spec.endpoint_count);
  TEST_ASSERT_EQUAL_UINT8(0xa6, spec.services[0].bytes[0]);
  TEST_ASSERT_EQUAL_UINT8(0xfe, spec.services[0].bytes[1]);
  TEST_ASSERT_EQUAL_UINT8(0x90, spec.services[1].bytes[12]);
  TEST_ASSERT_EQUAL_UINT8(0x00, spec.services[1].bytes[13]);
  TEST_ASSERT_EQUAL_UINT8(0x72, spec.endpoints[0].uuid.bytes[12]);
  TEST_ASSERT_EQUAL_UINT8(0x77, spec.endpoints[5].uuid.bytes[12]);
  TEST_ASSERT_EQUAL_UINT8(0x91, spec.endpoints[6].uuid.bytes[12]);
  TEST_ASSERT_EQUAL_UINT8(0x92, spec.endpoints[7].uuid.bytes[12]);
  TEST_ASSERT_EQUAL_UINT8(1, spec.endpoints[7].subscribe);
  TEST_ASSERT_EQUAL_UINT8(8, spec.endpoints[6].properties);
}
struct ScriptHost : SilentHost {
  std::vector<BleCommand> commands;
  std::vector<BleContext *> contexts;
  std::vector<BleContext *> quiet;
  bool encoding = false;
  bool busy = false, ready = true, ignore_shutter_effect = false, drop_shutter_ack = false;
  unsigned hardware_not_ready = 0;
  uint8_t hardware_model = 62, api_major = 1;
  bool reject_pair = false, reject_claim = false, wrong_pair_route = false,
       wrong_query_status = false;
  bool fragment_hardware = false;
  bool fail_scan_submit = false;
  uint8_t empty_register_target = 0, wrong_register_target = 0;
  BleBondAdmission bond;
  ScriptHost() {
    bond.stack_ready = bond.restore_verified = bond.refusal_installed = true;
    bond.existing_verified_identity = bond.identity_matches = bond.persistence_allowed = true;
    bond.used = bond.capacity = 4;
  }
  BleBondAdmission bondAdmission(const BondIdentity &) override { return bond; }
  int submit(const BleCommand &c, BleContext &ctx) override {
    commands.push_back(c);
    contexts.push_back(&ctx);
    quiet.erase(std::remove(quiet.begin(), quiet.end(), &ctx), quiet.end());
    if (c.phase == BlePhase::Scan && fail_scan_submit) {
      ctx.terminal.store(true);
      quiet.push_back(&ctx);
      return 77;
    }
    return 0;
  }
  int retire(BleContext &ctx) override {
    BleEvent e;
    e.kind = BleEventKind::Disconnected;
    e.connection = ctx.connection.load();
    deliver(ctx, e, true);
    return 0;
  }
  int cancelScan(BleContext &ctx) override {
    ctx.terminal.store(true);
    quiet.push_back(&ctx);
    return 0;
  }
  bool quiescent(const BleContext &ctx) const override {
    return ctx.terminal.load() && std::find(quiet.begin(), quiet.end(), &ctx) != quiet.end();
  }
  void deliver(BleContext &ctx, BleEvent e, bool terminal = false) {
    if (terminal) {
      ctx.terminal.store(true);
      quiet.push_back(&ctx);
    }
    ctx.receiver->copied(ctx, e);
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
    e.size = static_cast<uint8_t>(body.size() + 1);
    e.bytes[0] = static_cast<uint8_t>(body.size());
    std::copy(body.begin(), body.end(), e.bytes.begin() + 1);
    deliver(link, e);
  }
};
struct Rig {
  ScriptHost host;
  TestClock clock;
  Hero12Adapter adapter;
  CameraManager manager;
  size_t handled = 0;
  BleContext *link = nullptr;
  bool auto_camera = true;
  bool omit_management = false;
  bool omit_cccd = false;
  unsigned peer_count = 1;
  BleContext *links[4] = {};
  explicit Rig(RetryPolicy policy = Hero12Adapter::managerPolicy(), unsigned peers = 1)
      : adapter(host, clock), manager(clock, adapter, policy), peer_count(peers) {
    adapter.attach(manager);
    SourceConfig config;
    config.count = peers;
    for (unsigned i = 0; i < peers; ++i) {
      config.cameras[i].name = "hero";
      config.cameras[i].family = CameraFamily::GoPro;
      config.cameras[i].model = CameraModel::HERO12_BLACK;
      config.cameras[i].identifier = "01:02:03:04:05:0" + std::to_string(i + 1);
      config.cameras[i].address_type = AddressType::Public;
    }
    TEST_ASSERT_TRUE(manager.configure(config).ok());
    Hero12Qualification q;
    q.identity.verified = true;
    q.identity.type = IdentityType::Public;
    const char fw[] = "H23.01.10.00";
    q.firmware_size = sizeof(fw) - 1;
    std::memcpy(q.firmware.data(), fw, q.firmware_size);
    q.api_major = 1;
    q.api_minor = 0;
    q.source_qualified = q.classic_profile_confirmed = true;
    for (unsigned i = 0; i < peers; ++i) {
      q.identity.address[0] = i + 1;
      TEST_ASSERT_TRUE(adapter.configurePeer(i, q));
    }
    TEST_ASSERT_TRUE(adapter.start(true, true));
  }
  ~Rig() {
    adapter.stop();
    for (unsigned n = 0; n < 8 && !adapter.canDestroy(); ++n)
      adapter.service();
  }
  void process(size_t n) {
    const auto &c = host.commands[n];
    auto &ctx = *host.contexts[n];
    BleEvent e;
    e.connection = c.connection;
    if (c.phase == BlePhase::Connect) {
      links[ctx.peer] = &ctx;
      if (ctx.peer == 0)
        link = &ctx;
      e.kind = BleEventKind::Connected;
      e.connection = 10 + ctx.peer;
      ctx.connection.store(e.connection);
      host.deliver(ctx, e);
    } else if (c.phase == BlePhase::Security) {
      e.kind = BleEventKind::Security;
      e.encrypted = e.bonded = true;
      e.authenticated = false; // Just Works bonding does not assert MITM.
      e.identity.verified = true;
      e.identity.type = IdentityType::Public;
      e.identity.address[0] = ctx.peer + 1;
      host.deliver(ctx, e);
    } else if (c.phase == BlePhase::Services) {
      if (!(omit_management && c.uuid == Hero12Adapter::profileSpec().services[1])) {
        e.kind = BleEventKind::Service;
        e.uuid = c.uuid;
        e.start = c.uuid == Hero12Adapter::profileSpec().services[0] ? 1 : 41;
        e.end = c.uuid == Hero12Adapter::profileSpec().services[0] ? 40 : 60;
        host.deliver(ctx, e);
      }
      host.complete(n);
    } else if (c.phase == BlePhase::Characteristics) {
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
    } else if (c.phase == BlePhase::Descriptors) {
      if (!omit_cccd) {
        e.kind = BleEventKind::Descriptor;
        e.uuid = BleUuid::shortUuid(0x2902);
        e.handle = c.start + 1;
        host.deliver(ctx, e);
      }
      host.complete(n);
    } else if (c.phase == BlePhase::Subscribe || c.phase == BlePhase::VerifySubscription) {
      host.complete(n);
    } else if (c.phase == BlePhase::Write) {
      host.complete(n);
      if (!auto_camera)
        return;
      const uint8_t *payload = c.bytes.data() + 2;
      std::vector<uint8_t> reply;
      uint16_t notify_handle = 0;
      if (c.handle == 43) {
        reply = {3, 0x81, 8, static_cast<uint8_t>(host.reject_pair ? 3 : 1)};
        notify_handle = host.wrong_pair_route ? 6 : 46;
      } else if (c.handle == 3) {
        notify_handle = 6;
        if (payload[0] == 0xf1)
          reply = {0xf1, 0xe9, 8, static_cast<uint8_t>(host.reject_claim ? 6 : 1)};
        else if (payload[0] == 0x3c && host.hardware_not_ready) {
          --host.hardware_not_ready;
          reply = {0x3c, 1};
        } else if (payload[0] == 0x3c) {
          reply = {0x3c, 0, 1, host.hardware_model, 1, 'x', 0, 12};
          const char fw[] = "H23.01.10.00";
          reply.insert(reply.end(), fw, fw + 12);
          reply.insert(reply.end(), {0, 0, 0});
        } else if (payload[0] == 0x51)
          reply = {0x51, 0, 1, host.api_major, 1, 0};
        else {
          reply = {payload[0], 0};
          if (payload[0] == 1) {
            if (!host.ignore_shutter_effect)
              host.encoding = payload[2] == 1;
            if (host.drop_shutter_ack)
              return;
          }
        }
      } else if (c.handle == 9) {
        reply = {0x5b, 0};
        notify_handle = 12;
      } else if (c.handle == 15) {
        notify_handle = 18;
        if (payload[0] == 0x53)
          reply = host.empty_register_target == payload[1]
                      ? std::vector<uint8_t>{0x53, 0}
                      : std::vector<uint8_t>{
                            0x53, 0,
                            static_cast<uint8_t>(host.wrong_register_target == payload[1]
                                                     ? (payload[1] == 8 ? 10 : 8)
                                                     : payload[1]),
                            1, 1};
        else
          reply = {0x13, 0, static_cast<uint8_t>(host.wrong_query_status ? 82 : payload[1]), 1,
                   static_cast<uint8_t>(payload[1] == 10   ? host.encoding
                                        : payload[1] == 82 ? host.ready
                                                           : host.busy)};
      }
      TEST_ASSERT_TRUE(reply.size() < 63);
      if (host.fragment_hardware && c.handle == 3 && payload[0] == 0x3c && reply.size() > 8) {
        BleEvent part;
        part.kind = BleEventKind::Notification;
        part.connection = 10 + ctx.peer;
        part.handle = notify_handle;
        part.size = 9;
        part.bytes[0] = static_cast<uint8_t>(reply.size());
        std::copy(reply.begin(), reply.begin() + 8, part.bytes.begin() + 1);
        host.deliver(*links[ctx.peer], part);
        part.size = static_cast<uint8_t>(reply.size() - 8 + 1);
        part.bytes[0] = 0x80;
        std::copy(reply.begin() + 8, reply.end(), part.bytes.begin() + 1);
        host.deliver(*links[ctx.peer], part);
        return;
      }
      host.notification(*links[ctx.peer], notify_handle, reply);
    }
  }
  void pump(unsigned max = 200) {
    for (unsigned n = 0; n < max; ++n) {
      adapter.service();
      while (handled < host.commands.size())
        process(handled++);
      ++clock.value;
      bool all_ready = true;
      for (unsigned p = 0; p < peer_count; ++p)
        all_ready &= adapter.central().phase(p) == BlePhase::ReadyForProfile &&
                     manager.state(p)->lifecycle == Lifecycle::Ready;
      if (handled == host.commands.size() && all_ready)
        return;
    }
  }
};
void full_pairing_observes_state_and_shutter_requires_encoding_query() {
  Rig r;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Ready, (int)r.manager.state(0)->lifecycle);
  TEST_ASSERT_EQUAL_INT((int)RecordingState::Stopped, (int)r.manager.state(0)->observed);
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Start));
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)RecordingState::Recording, (int)r.manager.state(0)->observed);
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Stop));
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)RecordingState::Stopped, (int)r.manager.state(0)->observed);
}
void missing_management_service_refuses_pairing() {
  Rig r;
  r.omit_management = true;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  TEST_ASSERT_TRUE(r.manager.state(0)->lifecycle != Lifecycle::Ready);
  TEST_ASSERT_EQUAL_INT((int)CapabilityState::Unknown, (int)r.manager.state(0)->capabilities.start);
  TEST_ASSERT_EQUAL_INT((int)Hero12Fault::MissingService, (int)r.adapter.fault(0));
}
void missing_cccd_refuses_pairing_without_capabilities() {
  Rig r;
  r.omit_cccd = true;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  TEST_ASSERT_TRUE(r.manager.state(0)->lifecycle != Lifecycle::Ready);
  TEST_ASSERT_EQUAL_INT((int)CapabilityState::Unknown, (int)r.manager.state(0)->capabilities.start);
  TEST_ASSERT_EQUAL_INT((int)Hero12Fault::MissingCccd, (int)r.adapter.fault(0));
}
void busy_camera_never_receives_video_or_shutter() {
  Rig r;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  r.host.busy = true;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Start));
  r.pump();
  for (const auto &c : r.host.commands)
    if (c.phase == BlePhase::Write && c.handle == 3)
      TEST_ASSERT_TRUE(c.bytes[2] != 0x3e && c.bytes[2] != 1);
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Idle, (int)r.manager.state(0)->lifecycle);
}
void shutter_ack_without_encoding_change_does_not_complete_start() {
  Rig r;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  r.host.ignore_shutter_effect = true;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Start));
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Idle, (int)r.manager.state(0)->lifecycle);
  TEST_ASSERT_EQUAL_INT((int)RecordingState::Unknown, (int)r.manager.state(0)->observed);
}
void lost_shutter_ack_retires_before_manager_retry() {
  RetryPolicy policy = Hero12Adapter::managerPolicy();
  policy.max_attempts = 3;
  Rig r(policy);
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  r.host.drop_shutter_ack = true;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Start));
  r.pump(100);
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Operating, (int)r.manager.state(0)->lifecycle);
  const size_t sent = r.host.commands.size();
  r.clock.value += 2001;
  r.adapter.service();
  r.clock.value += 1000;
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Idle, (int)r.manager.state(0)->lifecycle);
  TEST_ASSERT_EQUAL_UINT(sent, r.host.commands.size());
}
void silent_fragment_expiry_retires_link() {
  Rig r;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  r.auto_camera = false;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Query));
  r.pump(5);
  BleEvent fragment;
  fragment.kind = BleEventKind::Notification;
  fragment.connection = 10;
  fragment.handle = 18;
  fragment.size = 3;
  fragment.bytes[0] = 5;
  fragment.bytes[1] = 0x13;
  fragment.bytes[2] = 0;
  r.host.deliver(*r.link, fragment);
  r.adapter.service();
  r.clock.value += 1001;
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Idle, (int)r.manager.state(0)->lifecycle);
}
void oversized_notification_seals_link_before_query_can_complete() {
  Rig r;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  r.auto_camera = false;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Query));
  r.pump(5);
  BleEvent oversized;
  oversized.kind = BleEventKind::Notification;
  oversized.connection = 10;
  oversized.handle = 18;
  oversized.size = 65;
  r.host.deliver(*r.link, oversized);
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Idle, (int)r.manager.state(0)->lifecycle);
  TEST_ASSERT_EQUAL_INT((int)RecordingState::Unknown, (int)r.manager.state(0)->observed);
}
void retired_connection_can_reconnect_without_replaying_shutter() {
  Rig r;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  const uint32_t old_generation = r.manager.state(0)->token.connection;
  r.host.drop_shutter_ack = true;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Start));
  r.pump(100);
  r.clock.value += 2001;
  r.adapter.service();
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Idle, (int)r.manager.state(0)->lifecycle);
  TEST_ASSERT_EQUAL_INT((int)RecordingState::Unknown, (int)r.manager.state(0)->observed);
  r.host.drop_shutter_ack = false;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Ready, (int)r.manager.state(0)->lifecycle);
  TEST_ASSERT_TRUE(r.manager.state(0)->token.connection > old_generation);
  BleResult stale;
  stale.kind = BleResultKind::Notification;
  stale.event.peer = 0;
  stale.event.generation = old_generation;
  stale.endpoint = 5;
  stale.event.size = 6;
  stale.event.bytes = {{5, 0x93, 0, 10, 1, 0}};
  r.adapter.result(stale);
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)RecordingState::Recording, (int)r.manager.state(0)->observed);
}
void due_keepalive_is_bounded_without_catchup_burst() {
  Rig r;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  r.clock.value += 30000;
  r.pump(20);
  unsigned keepalives = 0;
  for (const auto &c : r.host.commands)
    if (c.phase == BlePhase::Write && c.handle == 9 && c.bytes[2] == 0x5b)
      ++keepalives;
  TEST_ASSERT_EQUAL_UINT(1, keepalives);
}
void two_peers_get_independent_keepalive_without_starvation() {
  Rig r(Hero12Adapter::managerPolicy(), 2);
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(1, Operation::Connect));
  r.pump(600);
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Ready, (int)r.manager.state(0)->lifecycle);
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Ready, (int)r.manager.state(1)->lifecycle);
  r.clock.value += 3000;
  r.pump(100);
  unsigned counts[2] = {};
  for (size_t n = 0; n < r.host.commands.size(); ++n)
    if (r.host.commands[n].phase == BlePhase::Write && r.host.commands[n].handle == 9 &&
        r.host.commands[n].bytes[2] == 0x5b)
      ++counts[r.host.contexts[n]->peer];
  TEST_ASSERT_EQUAL_UINT(1, counts[0]);
  TEST_ASSERT_EQUAL_UINT(1, counts[1]);
}
void initial_hardware_readiness_polls_before_identity_qualification() {
  Rig r;
  r.host.hardware_not_ready = 2;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump(2000);
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Ready, (int)r.manager.state(0)->lifecycle);
  TEST_ASSERT_EQUAL_INT((int)CapabilityState::Supported,
                        (int)r.manager.state(0)->capabilities.start);
}
void fragmented_hardware_identity_completes_only_after_second_packet() {
  Rig r;
  r.host.fragment_hardware = true;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump(300);
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Ready, (int)r.manager.state(0)->lifecycle);
  TEST_ASSERT_EQUAL_INT((int)CapabilityState::Supported,
                        (int)r.manager.state(0)->capabilities.start);
}
void pairing_rejection_is_distinct_from_classic_ack() {
  Rig r;
  r.host.reject_pair = true;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)Hero12Fault::SetupRejected, (int)r.adapter.fault(0));
  TEST_ASSERT_EQUAL_INT((int)CapabilityState::Unknown, (int)r.manager.state(0)->capabilities.start);
}
void external_control_refusal_retires_before_identity_or_intent() {
  RetryPolicy policy = Hero12Adapter::managerPolicy();
  policy.max_attempts = 3;
  Rig r(policy);
  r.host.reject_claim = true; // Resource unavailable is not proof of another client's ownership.
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  unsigned pair = 0, claim = 0;
  for (const auto &c : r.host.commands) {
    if (c.phase != BlePhase::Write)
      continue;
    const uint8_t id = c.bytes[2];
    if (c.handle == 43 && id == 3)
      ++pair;
    else if (c.handle == 3 && id == 0xf1)
      ++claim;
    else
      TEST_FAIL_MESSAGE("identity, API, status, shutter or other write followed claim refusal");
  }
  TEST_ASSERT_EQUAL_UINT(1, pair);
  TEST_ASSERT_EQUAL_UINT(1, claim);
  TEST_ASSERT_EQUAL_INT((int)Hero12Fault::SetupRejected, (int)r.adapter.fault(0));
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Idle, (int)r.manager.state(0)->lifecycle);
  TEST_ASSERT_EQUAL_INT((int)CapabilityState::Unknown, (int)r.manager.state(0)->capabilities.start);
  TEST_ASSERT_EQUAL_INT((int)RecordingState::Unknown, (int)r.manager.state(0)->observed);
  TEST_ASSERT_EQUAL_INT((int)BlePhase::Closed, (int)r.adapter.central().phase(0));
  TEST_ASSERT_TRUE(r.adapter.canDestroy());
  const size_t sent = r.host.commands.size();
  r.clock.value += 120001;
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Idle, (int)r.manager.state(0)->lifecycle);
  TEST_ASSERT_EQUAL_UINT(sent, r.host.commands.size()); // No Backoff replay with three attempts.
}
void returned_model_and_api_must_match_independent_qualification() {
  {
    Rig r;
    r.host.hardware_model = 63;
    TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
    r.pump();
    TEST_ASSERT_EQUAL_INT((int)Hero12Fault::IdentityMismatch, (int)r.adapter.fault(0));
  }
  {
    Rig r;
    r.host.api_major = 2;
    TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
    r.pump();
    TEST_ASSERT_EQUAL_INT((int)Hero12Fault::IdentityMismatch, (int)r.adapter.fault(0));
  }
}
void missing_source_qualification_refuses_transport_admission() {
  Rig r;
  Hero12Qualification missing;
  missing.identity.verified = true;
  missing.identity.type = IdentityType::Public;
  missing.identity.address[0] = 1;
  missing.firmware[0] = 'x';
  missing.firmware_size = 1;
  TEST_ASSERT_FALSE(r.adapter.configurePeer(0, missing));
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.adapter.service();
  TEST_ASSERT_EQUAL_UINT(0, r.host.commands.size());
  TEST_ASSERT_EQUAL_INT((int)Hero12Fault::Qualification, (int)r.adapter.fault(0));
}
void fifth_registry_peer_is_explicitly_refused_by_four_slot_transport() {
  SilentHost host;
  TestClock clock;
  Hero12Adapter adapter(host, clock);
  CameraManager manager(clock, adapter, Hero12Adapter::managerPolicy());
  adapter.attach(manager);
  SourceConfig config;
  config.count = 5;
  for (unsigned i = 0; i < 5; ++i) {
    auto &camera = config.cameras[i];
    camera.name = "hero";
    camera.family = CameraFamily::GoPro;
    camera.model = CameraModel::HERO12_BLACK;
    camera.identifier = "01:02:03:04:05:0" + std::to_string(i + 1);
    camera.address_type = AddressType::Public;
  }
  TEST_ASSERT_TRUE(manager.configure(config).ok());
  TEST_ASSERT_TRUE(adapter.start(true, true));
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)manager.request(4, Operation::Connect));
  TEST_ASSERT_EQUAL_INT((int)Hero12Fault::Capacity, (int)adapter.fault(4));
}
void cancellation_retires_query_and_discards_late_same_id_reply() {
  Rig r;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  r.auto_camera = false;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Query));
  r.pump(5);
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.cancel(0));
  r.host.notification(*r.link, 18, {0x13, 0, 10, 1, 1});
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Idle, (int)r.manager.state(0)->lifecycle);
  TEST_ASSERT_EQUAL_INT((int)RecordingState::Unknown, (int)r.manager.state(0)->observed);
}
void setup_and_keepalive_deadlines_survive_millis_rollover() {
  Rig r;
  r.clock.value = UINT32_MAX - 20;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump(200);
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Ready, (int)r.manager.state(0)->lifecycle);
  r.clock.value += 3000;
  r.pump(20);
  unsigned keepalives = 0;
  for (const auto &c : r.host.commands)
    if (c.phase == BlePhase::Write && c.handle == 9 && c.bytes[2] == 0x5b)
      ++keepalives;
  TEST_ASSERT_EQUAL_UINT(1, keepalives);
}
void wrong_route_or_missing_requested_status_cannot_complete_transaction() {
  {
    Rig r;
    r.host.wrong_pair_route = true;
    TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
    r.pump(100);
    r.clock.value += 2001;
    r.adapter.service();
    TEST_ASSERT_EQUAL_INT((int)Lifecycle::Idle, (int)r.manager.state(0)->lifecycle);
  }
  {
    Rig r;
    TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
    r.pump();
    r.host.wrong_query_status = true;
    TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Query));
    r.pump(100);
    TEST_ASSERT_EQUAL_INT((int)Lifecycle::Operating, (int)r.manager.state(0)->lifecycle);
    r.clock.value += 2001;
    r.adapter.service();
    TEST_ASSERT_EQUAL_INT((int)Lifecycle::Idle, (int)r.manager.state(0)->lifecycle);
  }
}
void empty_register_ack_cannot_complete_setup() {
  for (uint8_t target : {uint8_t(8), uint8_t(10), uint8_t(82)}) {
    RetryPolicy policy = Hero12Adapter::managerPolicy();
    policy.max_attempts = 3;
    Rig r(policy);
    r.host.empty_register_target = target;
    TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
    r.pump(100);
    TEST_ASSERT_EQUAL_INT((int)Lifecycle::Connecting, (int)r.manager.state(0)->lifecycle);
    const size_t sent = r.host.commands.size();
    r.clock.value += 2001;
    r.adapter.service();
    r.clock.value += 1000;
    r.adapter.service();
    TEST_ASSERT_EQUAL_INT((int)Lifecycle::Idle, (int)r.manager.state(0)->lifecycle);
    TEST_ASSERT_EQUAL_INT((int)CapabilityState::Unknown,
                          (int)r.manager.state(0)->capabilities.start);
    TEST_ASSERT_EQUAL_UINT(sent, r.host.commands.size());
  }
}
void wrong_register_element_cannot_complete_setup() {
  for (uint8_t target : {uint8_t(8), uint8_t(10), uint8_t(82)}) {
    RetryPolicy policy = Hero12Adapter::managerPolicy();
    policy.max_attempts = 3;
    Rig r(policy);
    r.host.wrong_register_target = target;
    TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
    r.pump(100);
    TEST_ASSERT_EQUAL_INT((int)Lifecycle::Connecting, (int)r.manager.state(0)->lifecycle);
    const size_t sent = r.host.commands.size();
    r.clock.value += 2001;
    r.adapter.service();
    r.clock.value += 1000;
    r.adapter.service();
    TEST_ASSERT_EQUAL_INT((int)Lifecycle::Idle, (int)r.manager.state(0)->lifecycle);
    TEST_ASSERT_EQUAL_INT((int)CapabilityState::Unknown,
                          (int)r.manager.state(0)->capabilities.start);
    TEST_ASSERT_EQUAL_UINT(sent, r.host.commands.size());
  }
}
void recovery_without_advertising_is_bounded_and_power_removal_is_unsupported() {
  Rig r;
  TEST_ASSERT_EQUAL_INT(
      (int)CameraError::Unsupported,
      (int)r.adapter.requestRecovery(0, true, Hero12PowerCondition::PowerRemoved));
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Unsupported,
                        (int)r.adapter.recoveryState(0).phase);
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.requestRecovery(0, true));
  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    r.adapter.service();
    auto &scan = *r.host.contexts.back();
    TEST_ASSERT_EQUAL_INT((int)BlePhase::Scan, (int)scan.phase);
    BleEvent done;
    done.kind = BleEventKind::ScanComplete;
    r.host.deliver(scan, done, true);
    r.adapter.service();
  }
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Unavailable,
                        (int)r.adapter.recoveryState(0).phase);
  for (const auto &c : r.host.commands)
    TEST_ASSERT_EQUAL_INT((int)BlePhase::Scan, (int)c.phase);
}
void recovery_connects_only_matching_fea6_and_records_after_observation() {
  Rig r;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.requestRecovery(0, true));
  r.adapter.service();
  auto &scan = *r.host.contexts.back();
  BleEvent unrelated;
  unrelated.kind = BleEventKind::Advertisement;
  unrelated.identity.type = IdentityType::Public;
  unrelated.identity.address[0] = 1;
  unrelated.size = 4;
  unrelated.bytes = {{3, 3, 0x0f, 0x18}};
  r.host.deliver(scan, unrelated);
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Scanning, (int)r.adapter.recoveryState(0).phase);
  unrelated.size = 9;
  unrelated.bytes = {{4, 3, 0xa6, 0xfe, 0xff, 3, 3, 0xa6, 0xfe}};
  r.host.deliver(scan, unrelated);
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Scanning, (int)r.adapter.recoveryState(0).phase);
  unrelated.size = 4;
  unrelated.bytes = {{3, 3, 0xa6, 0xfe}};
  for (auto type : {BleAdvertisementType::Unknown, BleAdvertisementType::NonConnectable,
                    BleAdvertisementType::ScanResponse, BleAdvertisementType::Scannable}) {
    unrelated.advertisement_type = type;
    r.host.deliver(scan, unrelated);
    r.adapter.service();
    TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Scanning,
                          (int)r.adapter.recoveryState(0).phase);
  }
  unrelated.identity.type = IdentityType::UnresolvedPrivate;
  r.host.deliver(scan, unrelated);
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Scanning, (int)r.adapter.recoveryState(0).phase);
  unrelated.identity.type = IdentityType::Public;
  unrelated.advertisement_type = BleAdvertisementType::ConnectableUndirected;
  r.host.deliver(scan, unrelated);
  r.adapter.service();
  r.pump();
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Recording, (int)r.adapter.recoveryState(0).phase);
  TEST_ASSERT_EQUAL_INT((int)RecordingState::Recording, (int)r.manager.state(0)->observed);
  unsigned shutter_on = 0;
  for (const auto &c : r.host.commands)
    if (c.phase == BlePhase::Write && c.handle == 3 && c.bytes[2] == 1 && c.bytes[4] == 1)
      ++shutter_on;
  TEST_ASSERT_EQUAL_UINT(1, shutter_on);
}
void recovery_on_existing_recording_link_queries_without_scan_or_shutter() {
  Rig r;
  r.host.encoding = true;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  const auto commands_before = r.host.commands.size();
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.requestRecovery(0, true));
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Recording, (int)r.adapter.recoveryState(0).phase);
  for (size_t i = commands_before; i < r.host.commands.size(); ++i)
    if (r.host.commands[i].phase == BlePhase::Write)
      TEST_ASSERT_FALSE(r.host.commands[i].handle == 3 && r.host.commands[i].bytes[2] == 1);
}
void cancelled_recovery_ignores_late_advertisement_and_preserves_other_link() {
  Rig r(Hero12Adapter::managerPolicy(), 2);
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(1, Operation::Connect));
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.requestRecovery(0, true));
  r.adapter.service();
  auto &scan = *r.host.contexts.back();
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.cancelRecovery(0));
  BleEvent late;
  late.kind = BleEventKind::Advertisement;
  late.advertisement_type = BleAdvertisementType::ConnectableUndirected;
  late.identity.type = IdentityType::Public;
  late.identity.address[0] = 1;
  late.size = 4;
  late.bytes = {{3, 3, 0xa6, 0xfe}};
  r.host.deliver(scan, late);
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Cancelled, (int)r.adapter.recoveryState(0).phase);
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Ready, (int)r.manager.state(1)->lifecycle);
  for (const auto &c : r.host.commands)
    if (c.phase == BlePhase::Connect)
      TEST_ASSERT_TRUE(c.identity.address[0] != 1);
}
void recovery_connection_readiness_timeout_is_reported_without_rec() {
  Rig r;
  r.auto_camera = false;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.requestRecovery(0, true));
  r.adapter.service();
  auto &scan = *r.host.contexts.back();
  BleEvent ad;
  ad.kind = BleEventKind::Advertisement;
  ad.advertisement_type = BleAdvertisementType::ConnectableUndirected;
  ad.identity.type = IdentityType::Public;
  ad.identity.address[0] = 1;
  ad.size = 4;
  ad.bytes = {{3, 3, 0xa6, 0xfe}};
  r.host.deliver(scan, ad);
  r.adapter.service();
  r.pump(100);
  r.clock.value += 2001;
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Timeout, (int)r.adapter.recoveryState(0).phase);
  for (const auto &c : r.host.commands)
    if (c.phase == BlePhase::Write && c.handle == 3)
      TEST_ASSERT_TRUE(c.bytes[2] != 1);
}
void recovery_claim_refusal_never_replays_rec() {
  Rig r;
  r.host.reject_claim = true;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.requestRecovery(0, true));
  r.adapter.service();
  auto &scan = *r.host.contexts.back();
  BleEvent ad;
  ad.kind = BleEventKind::Advertisement;
  ad.advertisement_type = BleAdvertisementType::ConnectableUndirected;
  ad.identity.type = IdentityType::Public;
  ad.identity.address[0] = 1;
  ad.size = 4;
  ad.bytes = {{3, 3, 0xa6, 0xfe}};
  r.host.deliver(scan, ad);
  r.adapter.service();
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Failed, (int)r.adapter.recoveryState(0).phase);
  TEST_ASSERT_EQUAL_INT((int)Hero12Fault::SetupRejected, (int)r.adapter.fault(0));
  for (const auto &c : r.host.commands)
    if (c.phase == BlePhase::Write && c.handle == 3)
      TEST_ASSERT_TRUE(c.bytes[2] != 1);
}
void recovery_scans_pending_peers_fairly_after_absence() {
  Rig r(Hero12Adapter::managerPolicy(), 2);
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.requestRecovery(0, false));
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.requestRecovery(1, false));
  r.adapter.service();
  BleEvent done;
  done.kind = BleEventKind::ScanComplete;
  r.host.deliver(*r.host.contexts.back(), done, true);
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Scanning, (int)r.adapter.recoveryState(1).phase);
  TEST_ASSERT_EQUAL_UINT8(1, r.adapter.recoveryState(0).scans);
  TEST_ASSERT_EQUAL_UINT8(1, r.adapter.recoveryState(1).scans);
}
void recovery_does_not_connect_after_manager_generation_reset() {
  Rig r;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.requestRecovery(0, true));
  r.adapter.service();
  auto &scan = *r.host.contexts.back();
  r.manager.reset();
  BleEvent late;
  late.kind = BleEventKind::Advertisement;
  late.advertisement_type = BleAdvertisementType::ConnectableUndirected;
  late.identity.type = IdentityType::Public;
  late.identity.address[0] = 1;
  late.size = 4;
  late.bytes = {{3, 3, 0xa6, 0xfe}};
  r.host.deliver(scan, late);
  r.adapter.service();
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Failed, (int)r.adapter.recoveryState(0).phase);
  for (const auto &c : r.host.commands)
    TEST_ASSERT_TRUE(c.phase != BlePhase::Connect);
}
void rejected_scan_submission_consumes_bounded_attempt_and_releases_lease() {
  Rig r;
  r.host.fail_scan_submit = true;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.requestRecovery(0, false));
  for (unsigned pass = 0; pass < 20; ++pass)
    r.adapter.service();
  unsigned submissions = 0;
  for (const auto &c : r.host.commands)
    submissions += c.phase == BlePhase::Scan;
  TEST_ASSERT_TRUE(submissions <= 2);
  TEST_ASSERT_EQUAL_UINT8(submissions, r.adapter.recoveryState(0).scans);
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Failed, (int)r.adapter.recoveryState(0).phase);
  r.adapter.stop();
  r.adapter.service();
  TEST_ASSERT_TRUE(r.adapter.canDestroy());
}
void recovery_waits_for_pending_keepalive_before_querying_live_link() {
  Rig r;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  r.clock.value += 3000;
  r.adapter.service(); // KeepAlive write is now pending at the real central.
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.requestRecovery(0, false));
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Ready, (int)r.manager.state(0)->lifecycle);
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Ready, (int)r.adapter.recoveryState(0).phase);
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Ready, (int)r.manager.state(0)->lifecycle);
}
void recovery_waits_when_keepalive_becomes_due_in_same_owner_pass() {
  Rig r;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.requestRecovery(0, false));
  r.clock.value += 3000;
  r.adapter.service(); // The normal profile starts KeepAlive before recovery advances.
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Ready, (int)r.manager.state(0)->lifecycle);
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Ready, (int)r.adapter.recoveryState(0).phase);
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Ready, (int)r.manager.state(0)->lifecycle);
}
void competing_query_cannot_own_or_orphan_recovery_start() {
  Rig r;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.requestRecovery(0, true));
  r.pump(); // Fresh Encoding query observed Stopped; Start is due next owner pass.
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Starting, (int)r.adapter.recoveryState(0).phase);
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Query));
  r.adapter.service(); // Recovery must not enqueue Start behind this Query.
  r.pump(300);
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Recording, (int)r.adapter.recoveryState(0).phase);
  unsigned shutter_on = 0;
  for (const auto &c : r.host.commands)
    if (c.phase == BlePhase::Write && c.handle == 3 && c.bytes[2] == 1 && c.bytes[4] == 1)
      ++shutter_on;
  TEST_ASSERT_EQUAL_UINT(1, shutter_on);
}
void cancelling_recovery_while_competing_query_runs_leaves_query_owned_by_caller() {
  Rig r;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.requestRecovery(0, true));
  r.pump();
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Starting, (int)r.adapter.recoveryState(0).phase);
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Query));
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.adapter.cancelRecovery(0));
  r.pump(300);
  TEST_ASSERT_EQUAL_INT((int)Hero12RecoveryPhase::Cancelled, (int)r.adapter.recoveryState(0).phase);
  TEST_ASSERT_EQUAL_INT((int)Lifecycle::Ready, (int)r.manager.state(0)->lifecycle);
  for (const auto &c : r.host.commands)
    if (c.phase == BlePhase::Write && c.handle == 3)
      TEST_ASSERT_TRUE(c.bytes[2] != 1);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(default_disabled_never_starts_host_or_admits_connect);
  RUN_TEST(required_management_and_classic_routes_are_declared);
  RUN_TEST(full_pairing_observes_state_and_shutter_requires_encoding_query);
  RUN_TEST(missing_management_service_refuses_pairing);
  RUN_TEST(missing_cccd_refuses_pairing_without_capabilities);
  RUN_TEST(busy_camera_never_receives_video_or_shutter);
  RUN_TEST(shutter_ack_without_encoding_change_does_not_complete_start);
  RUN_TEST(lost_shutter_ack_retires_before_manager_retry);
  RUN_TEST(silent_fragment_expiry_retires_link);
  RUN_TEST(oversized_notification_seals_link_before_query_can_complete);
  RUN_TEST(retired_connection_can_reconnect_without_replaying_shutter);
  RUN_TEST(due_keepalive_is_bounded_without_catchup_burst);
  RUN_TEST(two_peers_get_independent_keepalive_without_starvation);
  RUN_TEST(initial_hardware_readiness_polls_before_identity_qualification);
  RUN_TEST(fragmented_hardware_identity_completes_only_after_second_packet);
  RUN_TEST(pairing_rejection_is_distinct_from_classic_ack);
  RUN_TEST(external_control_refusal_retires_before_identity_or_intent);
  RUN_TEST(returned_model_and_api_must_match_independent_qualification);
  RUN_TEST(missing_source_qualification_refuses_transport_admission);
  RUN_TEST(fifth_registry_peer_is_explicitly_refused_by_four_slot_transport);
  RUN_TEST(cancellation_retires_query_and_discards_late_same_id_reply);
  RUN_TEST(setup_and_keepalive_deadlines_survive_millis_rollover);
  RUN_TEST(wrong_route_or_missing_requested_status_cannot_complete_transaction);
  RUN_TEST(empty_register_ack_cannot_complete_setup);
  RUN_TEST(wrong_register_element_cannot_complete_setup);
  RUN_TEST(recovery_without_advertising_is_bounded_and_power_removal_is_unsupported);
  RUN_TEST(recovery_connects_only_matching_fea6_and_records_after_observation);
  RUN_TEST(recovery_on_existing_recording_link_queries_without_scan_or_shutter);
  RUN_TEST(cancelled_recovery_ignores_late_advertisement_and_preserves_other_link);
  RUN_TEST(recovery_connection_readiness_timeout_is_reported_without_rec);
  RUN_TEST(recovery_claim_refusal_never_replays_rec);
  RUN_TEST(recovery_scans_pending_peers_fairly_after_absence);
  RUN_TEST(recovery_does_not_connect_after_manager_generation_reset);
  RUN_TEST(rejected_scan_submission_consumes_bounded_attempt_and_releases_lease);
  RUN_TEST(recovery_waits_for_pending_keepalive_before_querying_live_link);
  RUN_TEST(recovery_waits_when_keepalive_becomes_due_in_same_owner_pass);
  RUN_TEST(competing_query_cannot_own_or_orphan_recovery_start);
  RUN_TEST(cancelling_recovery_while_competing_query_runs_leaves_query_owned_by_caller);
  return UNITY_END();
}

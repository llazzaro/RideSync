#include "../fixtures/ble/adapter_host.h"
#include "../fixtures/telemetry/csv_rows.h"
#include "camera_event_logger.h"
#include "camera_event_session.h"
#include "handlebar_control.h"
#include "profiles/gopro_hero12.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>
#include <unity.h>
#include <vector>
using namespace ridesync;
struct CameraLogSink : StorageSink {
  std::string bytes;
  bool mount() override { return true; }
  bool openExclusive(const char *) override { return true; }
  size_t write(const char *p, size_t n) override {
    bytes.append(p, n);
    return n;
  }
  bool flush() override { return true; }
  void close() override {}
};
using telemetry_fixture::cameraRows;

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
  bool encoding = false, independent_encoding = false;
  std::array<bool, kBlePeers> encodings{};
  const BleContext *held = nullptr;
  bool busy = false, ready = true, ignore_shutter_effect = false, drop_shutter_ack = false;
  unsigned hardware_not_ready = 0;
  uint8_t hardware_model = 62, api_major = 1;
  bool reject_pair = false, reject_claim = false, unknown_claim = false, wrong_pair_route = false,
       wrong_query_status = false;
  bool fragment_hardware = false, drop_hilight_ack = false;
  uint32_t health_floor = 5000;
  bool health_reject = false, health_silent = false;
  bool battery_present = true;
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
    return &ctx != held && ctx.terminal.load() &&
           std::find(quiet.begin(), quiet.end(), &ctx) != quiet.end();
  }
  void deliver(BleContext &ctx, BleEvent e, bool terminal = false) {
    if (terminal) {
      ctx.terminal.store(true);
      quiet.push_back(&ctx);
    }
    ctx.receiver->copied(ctx, e);
  }
  void complete(size_t n) { adapter_fixture::complete(*this, n); }
  void notification(BleContext &link, uint16_t handle, const std::vector<uint8_t> &body) {
    BleEvent e;
    e.kind = BleEventKind::Notification;
    e.connection = link.connection.load();
    e.handle = handle;
    const size_t header = body.size() >= 32 ? 2 : 1;
    e.size = static_cast<uint8_t>(body.size() + header);
    e.bytes[0] = header == 2 ? 0x20 : static_cast<uint8_t>(body.size());
    if (header == 2)
      e.bytes[1] = static_cast<uint8_t>(body.size());
    std::copy(body.begin(), body.end(), e.bytes.begin() + header);
    deliver(link, e);
  }
};
namespace hero_fixture {
template <typename Rig> bool applyShutter(Rig &rig, const uint8_t *payload, BleContext &ctx) {

  if (!rig.host.ignore_shutter_effect) {
    if (rig.host.independent_encoding)
      rig.host.encodings[ctx.peer] = payload[2] == 1;
    else
      rig.host.encoding = payload[2] == 1;
  }
  return !rig.host.drop_shutter_ack;
}
template <typename Rig> void hardwareReply(Rig &rig, std::vector<uint8_t> &reply) {
  if (rig.host.hardware_not_ready) {
    --rig.host.hardware_not_ready;
    reply = {0x3c, 1};
    return;
  }
  reply = {0x3c, 0, 1, rig.host.hardware_model, 1, 'x', 0, 12};
  const char fw[] = "H23.01.10.00";
  reply.insert(reply.end(), fw, fw + 12);
  reply.insert(reply.end(), {0, 0, 0});
}
template <typename Rig>
bool commandReply(Rig &rig, const uint8_t *payload, BleContext &ctx, std::vector<uint8_t> &reply) {
  switch (payload[0]) {
  case 0xf1:
    reply = {0xf1, 0xe9, 8,
             static_cast<uint8_t>(rig.host.unknown_claim  ? 0
                                  : rig.host.reject_claim ? 6
                                                          : 1)};
    break;
  case 0x3c:
    hardwareReply(rig, reply);
    break;
  case 0x51:
    reply = {0x51, 0, 1, rig.host.api_major, 1, 0};
    break;
  default:
    reply = {payload[0], 0};
    if (payload[0] == 0x18 && rig.host.drop_hilight_ack)
      return false;
    if (payload[0] == 1 && !applyShutter(rig, payload, ctx))
      return false;
    break;
  }
  return true;
}
template <typename Rig> std::vector<uint8_t> registerReply(Rig &rig, uint8_t target) {
  if (rig.host.empty_register_target == target)
    return {0x53, 0};
  const uint8_t observed =
      rig.host.wrong_register_target == target ? (target == 8 ? 10 : 8) : target;
  return {0x53, 0, observed, 1, 1};
}
template <typename Rig> uint8_t queryValue(Rig &rig, uint8_t target, uint8_t peer) {
  if (target == 10)
    return rig.host.independent_encoding ? rig.host.encodings[peer] : rig.host.encoding;
  return target == 82 ? rig.host.ready : rig.host.busy;
}
template <typename Rig>
void queryReply(Rig &rig, const uint8_t *payload, BleContext &ctx, std::vector<uint8_t> &reply,
                size_t count) {
  if (payload[1] == 1 || count > 1) {
    reply = {payload[0], static_cast<uint8_t>(rig.host.health_reject ? 1 : 0)};
    if (rig.host.health_reject)
      return;
    for (size_t n = 0; n < count; ++n) {
      const uint8_t id = payload[n + 1];
      switch (id) {
      case 1:
        reply.insert(reply.end(), {1, 1, static_cast<uint8_t>(rig.host.battery_present)});
        break;
      case 6:
        reply.insert(reply.end(), {6, 1, 0});
        break;
      case 10:
        reply.insert(reply.end(), {10, 1, queryValue(rig, 10, ctx.peer)});
        break;
      case 35:
        reply.insert(reply.end(), {35, 4, 0, 0, 1, 44});
        break;
      case 54:
        reply.insert(reply.end(), {54, 8, 0, 0, 0, 0, 0, 0, 1, 0});
        break;
      case 60:
        reply.insert(reply.end(), {60, 4, static_cast<uint8_t>(rig.host.health_floor >> 24),
                                   static_cast<uint8_t>(rig.host.health_floor >> 16),
                                   static_cast<uint8_t>(rig.host.health_floor >> 8),
                                   static_cast<uint8_t>(rig.host.health_floor)});
        break;
      case 70:
        reply.insert(reply.end(), {70, 1, 20});
        break;
      }
    }
    return;
  }
  if (payload[0] == 0x53) {
    reply = registerReply(rig, payload[1]);
    return;
  }
  reply = {0x13, 0, static_cast<uint8_t>(rig.host.wrong_query_status ? 82 : payload[1]), 1,
           queryValue(rig, payload[1], ctx.peer)};
}
template <typename Rig>
bool fragmentReply(Rig &rig, const BleCommand &c, const uint8_t *payload, BleContext &ctx,
                   uint16_t notify_handle, const std::vector<uint8_t> &reply) {
  if (!(rig.host.fragment_hardware && c.handle == 3 && payload[0] == 0x3c && reply.size() > 8))
    return false;

  BleEvent part;
  part.kind = BleEventKind::Notification;
  part.connection = 10 + ctx.peer;
  part.handle = notify_handle;
  part.size = 9;
  part.bytes[0] = static_cast<uint8_t>(reply.size());
  std::copy(reply.begin(), reply.begin() + 8, part.bytes.begin() + 1);
  rig.host.deliver(*rig.links[ctx.peer], part);
  part.size = static_cast<uint8_t>(reply.size() - 8 + 1);
  part.bytes[0] = 0x80;
  std::copy(reply.begin() + 8, reply.end(), part.bytes.begin() + 1);
  rig.host.deliver(*rig.links[ctx.peer], part);
  return true;
}
template <typename Rig>
void connect(Rig &rig, size_t, const BleCommand &, BleContext &ctx, BleEvent e) {

  rig.links[ctx.peer] = &ctx;
  if (ctx.peer == 0)
    rig.link = &ctx;
  e.kind = BleEventKind::Connected;
  e.connection = 10 + ctx.peer;
  ctx.connection.store(e.connection);
  rig.host.deliver(ctx, e);
}
template <typename Rig>
void security(Rig &rig, size_t, const BleCommand &, BleContext &ctx, BleEvent e) {

  e.kind = BleEventKind::Security;
  e.encrypted = e.bonded = true;
  e.authenticated = false; // Just Works bonding does not assert MITM.
  e.identity.verified = true;
  e.identity.type = IdentityType::Public;
  e.identity.address[0] = ctx.peer + 1;
  rig.host.deliver(ctx, e);
}
template <typename Rig>
void services(Rig &rig, size_t n, const BleCommand &c, BleContext &ctx, BleEvent e) {

  if (!(rig.omit_management && c.uuid == Hero12Adapter::profileSpec().services[1])) {
    e.kind = BleEventKind::Service;
    e.uuid = c.uuid;
    e.start = c.uuid == Hero12Adapter::profileSpec().services[0] ? 1 : 41;
    e.end = c.uuid == Hero12Adapter::profileSpec().services[0] ? 40 : 60;
    rig.host.deliver(ctx, e);
  }
  rig.host.complete(n);
}
template <typename Rig>
void characteristics(Rig &rig, size_t n, const BleCommand &c, BleContext &ctx, BleEvent e) {

  const auto spec = Hero12Adapter::profileSpec();
  const bool management = c.start == 41;
  for (unsigned j = management ? 6 : 0; j < (management ? 8u : 6u); ++j) {
    e.kind = BleEventKind::Characteristic;
    e.uuid = spec.endpoints[j].uuid;
    e.start = (management ? 42 : 2) + (j - (management ? 6 : 0)) * 3;
    e.handle = e.start + 1;
    e.properties = spec.endpoints[j].properties;
    rig.host.deliver(ctx, e);
  }
  rig.host.complete(n);
}
template <typename Rig>
void descriptors(Rig &rig, size_t n, const BleCommand &c, BleContext &ctx, BleEvent e) {

  if (!rig.omit_cccd) {
    e.kind = BleEventKind::Descriptor;
    e.uuid = BleUuid::shortUuid(0x2902);
    e.handle = c.start + 1;
    rig.host.deliver(ctx, e);
  }
  rig.host.complete(n);
}
template <typename Rig> void write(Rig &rig, size_t n, const BleCommand &c, BleContext &ctx) {
  rig.host.complete(n);
  if (!rig.auto_camera)
    return;
  const uint8_t *payload = c.bytes.data() + 2;
  std::vector<uint8_t> reply;
  uint16_t notify_handle = 0;
  switch (c.handle) {
  case 43:
    reply = {3, 0x81, 8, static_cast<uint8_t>(rig.host.reject_pair ? 3 : 1)};
    notify_handle = rig.host.wrong_pair_route ? 6 : 46;
    break;
  case 3:
    notify_handle = 6;
    if (!commandReply(rig, payload, ctx, reply))
      return;
    break;
  case 9:
    reply = {0x5b, 0};
    notify_handle = 12;
    break;
  case 15:
    notify_handle = 18;
    if (rig.host.health_silent && (payload[1] == 1 || c.size > 4))
      return;
    queryReply(rig, payload, ctx, reply, c.size - 3);
    break;
  default:
    break;
  }
  TEST_ASSERT_TRUE(reply.size() < 63);
  if (!fragmentReply(rig, c, payload, ctx, notify_handle, reply))
    rig.host.notification(*rig.links[ctx.peer], notify_handle, reply);
}
template <typename Rig> void process(Rig &rig, size_t n) {
  const auto &c = rig.host.commands[n];
  auto &ctx = *rig.host.contexts[n];
  BleEvent e;
  e.connection = c.connection;
  switch (c.phase) {
  case BlePhase::Connect:
    connect(rig, n, c, ctx, e);
    break;
  case BlePhase::Security:
    security(rig, n, c, ctx, e);
    break;
  case BlePhase::Services:
    services(rig, n, c, ctx, e);
    break;
  case BlePhase::Characteristics:
    characteristics(rig, n, c, ctx, e);
    break;
  case BlePhase::Descriptors:
    descriptors(rig, n, c, ctx, e);
    break;
  case BlePhase::Subscribe:
  case BlePhase::VerifySubscription:
    rig.host.complete(n);
    break;
  case BlePhase::Write:
    write(rig, n, c, ctx);
    break;
  default:
    break;
  }
}
} // namespace hero_fixture
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
  explicit Rig(RetryPolicy policy = Hero12Adapter::managerPolicy(), unsigned peers = 1,
               bool hilight = false, bool health = false, bool video = false)
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
    q.hilight_enabled = hilight;
    q.health_enabled = health;
    q.ordinary_video_confirmed = video;
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
  void finishBound(RecordingManager &group, CameraEventLogger &logger) {
    adapter.stop();
    for (unsigned n = 0; n < 8 && !adapter.canDestroy(); ++n)
      adapter.service();
    TEST_ASSERT_TRUE(adapter.canDestroy());
    manager.detachAudit(&logger);
    adapter.detachGroup(&group);
  }
  void process(size_t n) { hero_fixture::process(*this, n); }
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
void duplicate_matching_response_records_one_wire_ack() {
  Rig r;
  RecordingManager group(r.manager, r.clock);
  r.adapter.attachGroup(group);
  CameraInbox camera;
  CameraEventLogger logger(camera, r.clock, 42, group);
  TEST_ASSERT_TRUE(logger.configurePeer(0, 301, CameraModel::HERO12_BLACK));
  r.manager.attachAudit(logger);
  CameraLogSink sink;
  Storage storage(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::CameraV3});
  SessionClock clock(r.clock, 42, 1000);
  ImuInbox imu;
  TelemetryAdmission admission(clock, storage, imu, &camera);
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  for (unsigned i = 0; i < 500; ++i) {
    admission.tick();
    storage.workerStep();
  }
  r.auto_camera = false;
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Query));
  r.pump(5);
  r.host.notification(*r.link, 18, {0x13, 0, 10, 1, 0});
  r.host.notification(*r.link, 18, {0x13, 0, 10, 1, 0});
  r.adapter.service();
  for (unsigned i = 0; i < 500; ++i) {
    admission.tick();
    storage.workerStep();
  }
  unsigned query_ack = 0;
  for (const auto &row : cameraRows(sink.bytes))
    if (row[20] == "4" && row[25] == "15")
      ++query_ack;
  TEST_ASSERT_EQUAL_UINT(1, query_ack);
  r.finishBound(group, logger);
}
static void profileReplyDeadline(uint32_t start, uint32_t delay) {
  Rig r;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump();
  RecordingManager group(r.manager, r.clock);
  r.adapter.attachGroup(group);
  CameraInbox camera;
  CameraEventLogger logger(camera, r.clock, 42, group);
  TEST_ASSERT_TRUE(logger.configurePeer(0, 301, CameraModel::HERO12_BLACK));
  TEST_ASSERT_TRUE(r.manager.attachAudit(logger));
  CameraLogSink sink;
  Storage storage(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::CameraV3});
  SessionClock clock(r.clock, 42, 1000);
  ImuInbox imu;
  TelemetryAdmission admission(clock, storage, imu, &camera);
  r.clock.value = start;
  r.auto_camera = false;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Query));
  r.pump(1);
  r.clock.value = start + delay;
  r.host.notification(*r.link, 18, {0x13, 0, 10, 1, 0});
  r.adapter.service();
  for (unsigned i = 0; i < 500; ++i) {
    admission.tick();
    storage.workerStep();
  }
  unsigned acknowledgements = 0;
  for (const auto &row : cameraRows(sink.bytes))
    if (row[20] == "4" && row[25] == "15")
      ++acknowledgements;
  TEST_ASSERT_EQUAL_UINT(delay < 2000 ? 1 : 0, acknowledgements);
  r.finishBound(group, logger);
}
void profile_reply_at_or_after_deadline_cannot_publish_ack() {
  for (uint32_t start : {uint32_t(100), uint32_t(UINT32_MAX - 100)}) {
    for (uint32_t delay : {uint32_t(1999), uint32_t(2000), uint32_t(2001)}) {
      profileReplyDeadline(start, delay);
    }
  }
}
void group_route_keeps_shutter_ack_distinct_from_recording() {
  Rig r;
  RecordingManager group(r.manager, r.clock);
  r.adapter.attachGroup(group);
  CameraInbox camera;
  CameraEventLogger logger(camera, r.clock, 42, group);
  TEST_ASSERT_TRUE(logger.configurePeer(0, 301, CameraModel::HERO12_BLACK));
  r.manager.attachAudit(logger);
  CameraLogSink sink;
  Storage storage(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::CameraV3});
  SessionClock clock(r.clock, 42, 1000);
  ImuInbox imu;
  TelemetryAdmission admission(clock, storage, imu, &camera);
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  for (unsigned i = 0; i < 500; ++i) {
    admission.tick();
    storage.workerStep();
  }
  r.host.ignore_shutter_effect = true;
  TEST_ASSERT_EQUAL_INT((int)GroupError::None, (int)group.request(RecordingState::Recording));
  r.pump();
  for (unsigned i = 0; i < 500; ++i) {
    admission.tick();
    storage.workerStep();
  }
  unsigned shutter_ack = 0, recording_observed = 0;
  for (const auto &row : cameraRows(sink.bytes)) {
    if (row[20] == "4" && row[25] == "12")
      ++shutter_ack;
    if (row[20] == "5" && row[23] == "2")
      ++recording_observed;
  }
  TEST_ASSERT_EQUAL_UINT(1, shutter_ack);
  TEST_ASSERT_EQUAL_UINT(0, recording_observed);
  TEST_ASSERT_EQUAL_UINT(0, group.status().recording);
  TEST_ASSERT_TRUE(group.status().peers[0].terminal_failure);
  r.finishBound(group, logger);
}
void identical_unsolicited_encoding_observations_are_each_preserved() {
  Rig r;
  RecordingManager group(r.manager, r.clock);
  r.adapter.attachGroup(group);
  CameraInbox camera;
  CameraEventLogger logger(camera, r.clock, 42, group);
  TEST_ASSERT_TRUE(logger.configurePeer(0, 301, CameraModel::HERO12_BLACK));
  r.manager.attachAudit(logger);
  CameraLogSink sink;
  Storage storage(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::CameraV3});
  SessionClock clock(r.clock, 42, 1000);
  ImuInbox imu;
  TelemetryAdmission admission(clock, storage, imu, &camera);
  TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
  r.pump();
  for (unsigned i = 0; i < 500; ++i) {
    admission.tick();
    storage.workerStep();
  }
  const auto before = cameraRows(sink.bytes).size();
  r.host.notification(*r.link, 18, {0x93, 0, 10, 1, 0});
  r.adapter.service();
  r.host.notification(*r.link, 18, {0x93, 0, 10, 1, 0});
  r.adapter.service();
  for (unsigned i = 0; i < 500; ++i) {
    admission.tick();
    storage.workerStep();
  }
  const auto rows = cameraRows(sink.bytes);
  unsigned observations = 0;
  for (size_t i = before; i < rows.size(); ++i)
    if (rows[i][20] == "5" && rows[i][23] == "1")
      ++observations;
  TEST_ASSERT_EQUAL_UINT(2, observations);
  r.finishBound(group, logger);
}
static void countGroupCallback(void *context, const RecordingStatus &) {
  ++*static_cast<unsigned *>(context);
}
void composed_session_finishes_camera_after_final_owner_access() {
  Rig r;
  unsigned group_callbacks = 0;
  RecordingManager group(r.manager, r.clock, countGroupCallback, &group_callbacks);
  CameraLogSink sink;
  {
    CameraEventSession session(r.clock, sink, r.adapter, r.manager, group, 42, "fw", "synthetic");
    TEST_ASSERT_FALSE(session.configurePeer(0, 301, CameraModel::X5));
    TEST_ASSERT_TRUE(session.configurePeer(0, 301, CameraModel::HERO12_BLACK));
    TEST_ASSERT_TRUE(session.activate());
    TEST_ASSERT_EQUAL_INT((int)CameraError::None, (int)r.manager.request(0, Operation::Connect));
    for (unsigned i = 0; i < 300; ++i) {
      session.service();
      while (r.handled < r.host.commands.size())
        r.process(r.handled++);
      ++r.clock.value;
      session.storage().workerStep();
    }
    session.requestStop();
    session.finishImu();
    for (unsigned i = 0; i < 500 && !session.stopped(); ++i) {
      session.service();
      session.storage().workerStep();
    }
    TEST_ASSERT_TRUE(session.stopped());
    TEST_ASSERT_TRUE(session.storage().health().stopped);
    TEST_ASSERT_TRUE(session.cameraInbox().stopRequested());
  }
  const unsigned stopped_callbacks = group_callbacks;
  TEST_ASSERT_TRUE(stopped_callbacks > 0);
  r.adapter.service();
  TEST_ASSERT_EQUAL_UINT(stopped_callbacks, group_callbacks);
}
void stopped_session_cannot_detach_later_same_group_binding() {
  Rig r;
  unsigned group_callbacks = 0;
  RecordingManager group(r.manager, r.clock, countGroupCallback, &group_callbacks);
  CameraLogSink first_sink, second_sink;
  std::unique_ptr<CameraEventSession> first(new CameraEventSession(
      r.clock, first_sink, r.adapter, r.manager, group, 42, "fw", "synthetic"));
  TEST_ASSERT_TRUE(first->configurePeer(0, 301, CameraModel::HERO12_BLACK));
  TEST_ASSERT_TRUE(first->activate());
  first->requestStop();
  first->finishImu();
  for (unsigned i = 0; i < 500 && !first->stopped(); ++i) {
    first->service();
    first->storage().workerStep();
  }
  TEST_ASSERT_TRUE(first->stopped());
  TEST_ASSERT_TRUE(r.adapter.canDestroy());

  CameraEventSession second(r.clock, second_sink, r.adapter, r.manager, group, 43, "fw",
                            "synthetic");
  TEST_ASSERT_TRUE(second.configurePeer(0, 302, CameraModel::HERO12_BLACK));
  TEST_ASSERT_TRUE(second.activate());
  const unsigned before_old_service = group_callbacks;
  first->service(); // Completed first session must not service second's adapter route.
  TEST_ASSERT_EQUAL_UINT(before_old_service, group_callbacks);
  first.reset(); // Old destructor must not detach the same group newly owned by second.
  const unsigned before = group_callbacks;
  second.service();
  TEST_ASSERT_TRUE(group_callbacks > before);
  second.requestStop();
  second.finishImu();
  for (unsigned i = 0; i < 500 && !second.stopped(); ++i) {
    second.service();
    second.storage().workerStep();
  }
  TEST_ASSERT_TRUE(second.stopped());
}
void inactive_or_refused_session_closes_storage_without_servicing_independent_adapter() {
  for (bool refused : {false, true}) {
    Rig r;
    RecordingManager group(r.manager, r.clock);
    unsigned independent_callbacks = 0;
    RecordingManager independent(r.manager, r.clock, countGroupCallback, &independent_callbacks);
    r.adapter.attachGroup(independent);
    CameraLogSink sink;
    CameraEventSession session(r.clock, sink, r.adapter, r.manager, group, 42, "fw", "synthetic");
    if (refused)
      TEST_ASSERT_FALSE(session.activate()); // No configured opaque peer identity.
    TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
    const size_t commands = r.host.commands.size();
    session.requestStop();
    session.finishImu();
    for (unsigned i = 0; i < 500 && !session.stopped(); ++i) {
      session.service();
      session.storage().workerStep();
    }
    TEST_ASSERT_TRUE(session.stopped());
    TEST_ASSERT_TRUE(session.storage().health().stopped);
    TEST_ASSERT_EQUAL_UINT(commands, r.host.commands.size());
    TEST_ASSERT_EQUAL_INT(Lifecycle::Connecting, r.manager.state(0)->lifecycle);
    r.adapter.service();
    TEST_ASSERT_TRUE(independent_callbacks > 0); // Refused session did not unbind this owner.
    r.adapter.stop();
    for (unsigned n = 0; n < 8 && !r.adapter.canDestroy(); ++n)
      r.adapter.service();
    TEST_ASSERT_TRUE(r.adapter.canDestroy());
    r.adapter.detachGroup(&independent);
  }
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
// Synthetic schema-derived responses reproduce observed outcomes, not raw captures.
void unknown_control_claim_cannot_grant_control_or_replay_recording() {
  RetryPolicy policy = Hero12Adapter::managerPolicy();
  policy.max_attempts = 3;
  Rig r(policy);
  r.host.unknown_claim = true;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump();
  TEST_ASSERT_EQUAL_INT(Hero12Fault::SetupRejected, r.adapter.fault(0));
  TEST_ASSERT_EQUAL_INT(CapabilityState::Unknown, r.manager.state(0)->capabilities.start);
  TEST_ASSERT_EQUAL_INT(RecordingState::Unknown, r.manager.state(0)->observed);
  unsigned claims = 0;
  for (const auto &c : r.host.commands) {
    if (c.phase != BlePhase::Write)
      continue;
    if (c.handle == 43 && c.bytes[2] == 3)
      continue;
    TEST_ASSERT_EQUAL_UINT16(3, c.handle);
    TEST_ASSERT_EQUAL_UINT8(0xf1, c.bytes[2]);
    ++claims;
  }
  TEST_ASSERT_EQUAL_UINT(1, claims);
  size_t sent = r.host.commands.size();
  r.clock.value += 120001;
  r.adapter.service();
  TEST_ASSERT_EQUAL_UINT(sent, r.host.commands.size());
}
void stop_ack_with_encoding_still_true_never_confirms_stopped_or_retries_shutter() {
  RetryPolicy policy = Hero12Adapter::managerPolicy();
  policy.max_attempts = 3;
  Rig r(policy);
  r.host.encoding = true;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump();
  TEST_ASSERT_EQUAL_INT(RecordingState::Recording, r.manager.state(0)->observed);
  r.host.ignore_shutter_effect = true; // Camera can ACK while Encoding remains true.
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Stop));
  r.pump();
  TEST_ASSERT_EQUAL_INT(RecordingState::Unknown, r.manager.state(0)->observed);
  TEST_ASSERT_EQUAL_INT(Hero12Fault::CameraRejected, r.adapter.fault(0));
  unsigned starts = 0, stops = 0;
  for (const auto &c : r.host.commands)
    if (c.phase == BlePhase::Write && c.handle == 3 && c.bytes[2] == 1) {
      if (c.bytes[4])
        ++starts;
      else
        ++stops;
    }
  TEST_ASSERT_EQUAL_UINT(0, starts);
  TEST_ASSERT_EQUAL_UINT(1, stops);
  size_t sent = r.host.commands.size();
  r.clock.value += 120001;
  r.adapter.service();
  TEST_ASSERT_EQUAL_UINT(sent, r.host.commands.size());
}
void wake_only_recovery_reaches_stopped_ready_without_video_or_shutter() {
  Rig r;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.adapter.requestRecovery(0, false));
  r.adapter.service();
  auto &scan = *r.host.contexts.back();
  BleEvent advertised;
  advertised.kind = BleEventKind::Advertisement;
  advertised.advertisement_type = BleAdvertisementType::ConnectableUndirected;
  advertised.identity.type = IdentityType::Public;
  advertised.identity.address[0] = 1;
  advertised.size = 4;
  advertised.bytes = {{3, 3, 0xa6, 0xfe}};
  r.host.deliver(scan, advertised);
  r.adapter.service();
  r.pump();
  r.pump();
  TEST_ASSERT_EQUAL_INT(Hero12RecoveryPhase::Ready, r.adapter.recoveryState(0).phase);
  TEST_ASSERT_EQUAL_INT(RecordingState::Stopped, r.manager.state(0)->observed);
  for (const auto &c : r.host.commands)
    if (c.phase == BlePhase::Write && c.handle == 3) {
      TEST_ASSERT_NOT_EQUAL(1, c.bytes[2]);
      TEST_ASSERT_NOT_EQUAL(0x3e, c.bytes[2]);
    }
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
struct HandlebarInput : ButtonInput {
  bool down = false;
  bool pressed() override { return down; }
};
struct HandlebarLed : LedSink {
  LedFrame frame;
  int failure = 0;
  int write(LedFrame f) override {
    frame = f;
    return failure;
  }
};
void first_recording_action_recovers_fresh_then_next_stops() {
  Rig r;
  RecordingManager group(r.manager, r.clock);
  r.adapter.attachGroup(group);
  HandlebarInput input;
  HandlebarLed sink;
  HandlebarControl control(r.clock, r.adapter, r.manager, group, input, sink);
  TEST_ASSERT_TRUE(control.begin({}));
  LocalTelemetryStatus local;
  local.phase = TelemetryPhase::Running;
  local.camera = CameraAdmission::Admitted;
  control.observe(local);
  const auto admission = control.submit(ButtonAction::RecordingIntent);
  r.adapter.service(&control);
  const auto intent = group.status().intent;
  const auto phase = r.adapter.recoveryState(0).phase;
  control.reset();
  r.adapter.detachGroup(&group);
  TEST_ASSERT_EQUAL_INT(ControlAdmission::Admitted, admission);
  TEST_ASSERT_EQUAL_INT(RecordingState::Recording, intent);
  TEST_ASSERT_EQUAL_INT(Hero12RecoveryPhase::Scanning, phase);
}
struct ControlRig {
  Rig radio;
  RecordingManager group;
  HandlebarInput input;
  HandlebarLed sink;
  HandlebarControl control;
  CameraLogSink storage;
  CameraEventSession session;
  LocalTelemetryStatus local;
  int absent = -1;
  explicit ControlRig(unsigned peers = 1, const ButtonConfig &buttons = ButtonConfig{})
      : radio(Hero12Adapter::managerPolicy(), peers), group(radio.manager, radio.clock),
        control(radio.clock, radio.adapter, radio.manager, group, input, sink),
        session(radio.clock, storage, radio.adapter, radio.manager, group, 42, "fw", "synthetic") {
    for (unsigned i = 0; i < peers; ++i)
      TEST_ASSERT_TRUE(session.configurePeer(i, 300 + i, CameraModel::HERO12_BLACK));
    TEST_ASSERT_TRUE(session.activate());
    TEST_ASSERT_TRUE(control.begin(buttons));
    local.phase = TelemetryPhase::Running;
    local.camera = CameraAdmission::Admitted;
    local.gps.validity = FixValidity::Valid;
    control.observe(local);
  }
  ~ControlRig() { control.reset(); }
  void respondRecoveryScan(size_t n) {
    auto &ctx = *radio.host.contexts[n];
    // The scan is global; synthesize only the selected recovery candidate.
    unsigned peer = 0;
    for (; peer < radio.peer_count; ++peer)
      if (radio.adapter.recoveryState(peer).phase == Hero12RecoveryPhase::Scanning)
        break;
    BleEvent e;
    if (int(peer) == absent) {
      e.kind = BleEventKind::ScanComplete;
      radio.host.deliver(ctx, e, true);
    } else {
      e.kind = BleEventKind::Advertisement;
      e.advertisement_type = BleAdvertisementType::ConnectableUndirected;
      e.identity.type = IdentityType::Public;
      e.identity.address[0] = peer + 1;
      e.size = 4;
      e.bytes = {{3, 3, 0xa6, 0xfe}};
      radio.host.deliver(ctx, e);
    }
  }
  void pass(bool responses = true) {
    const auto ticks = radio.manager.ticks(), advances = group.advancements();
    session.service(&control);
    control.observe(local);
    TEST_ASSERT_EQUAL_UINT32(ticks + 1, radio.manager.ticks());
    TEST_ASSERT_EQUAL_UINT32(advances + 1, group.advancements());
    if (responses)
      while (radio.handled < radio.host.commands.size()) {
        const size_t n = radio.handled++;
        if (radio.host.commands[n].phase == BlePhase::Scan) {
          respondRecoveryScan(n);
        } else {
          radio.process(n);
        }
      }
    ++radio.clock.value;
  }
  void pump(unsigned count = 200) {
    for (unsigned i = 0; i < count; ++i)
      pass();
  }
  unsigned shutters(bool on, unsigned peer = 0) {
    unsigned count = 0;
    for (size_t i = 0; i < radio.host.commands.size(); ++i) {
      const auto &c = radio.host.commands[i];
      if (radio.host.contexts[i]->peer == peer && c.phase == BlePhase::Write && c.handle == 3 &&
          c.bytes[2] == 1 && c.bytes[4] == unsigned(on))
        ++count;
    }
    return count;
  }
  void shortPress() {
    pass();
    radio.clock.value += 21;
    pass();
    input.down = true;
    pass();
    radio.clock.value += 21;
    pass();
    input.down = false;
    pass();
    radio.clock.value += 21;
    pass();
  }
};
void actual_button_default_first_rec_second_stop_and_copied_status() {
  ControlRig r;
  r.shortPress();
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.shutters(true));
  TEST_ASSERT_EQUAL_INT(RecordingState::Recording, r.control.status().group.intent);
  TEST_ASSERT_EQUAL_INT(LedState::Recording, r.control.status().led);
  TEST_ASSERT_EQUAL_INT(CameraError::Unsupported, r.control.status().wake[0]);
  const auto copied = r.control.status();
  r.shortPress();
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.shutters(false));
  TEST_ASSERT_EQUAL_INT(RecordingState::Stopped, r.control.status().group.intent);
  TEST_ASSERT_EQUAL_INT(LedState::Ready, r.control.status().led);
  TEST_ASSERT_EQUAL_INT(RecordingState::Recording, copied.group.intent);
}
void unavailable_peer_does_not_abort_available_rec_or_deadline() {
  ControlRig r(2);
  r.absent = 0;
  r.control.submit(ButtonAction::RecordingIntent);
  r.pump(400);
  TEST_ASSERT_EQUAL_UINT(1, r.shutters(true, 1));
  TEST_ASSERT_EQUAL_INT(RecordingState::Recording, r.radio.manager.state(1)->observed);
  TEST_ASSERT_EQUAL_INT(Hero12RecoveryPhase::Unavailable, r.control.status().recovery[0].phase);
  TEST_ASSERT_EQUAL_INT(CameraError::NotConnected, r.control.status().group.peers[0].error);
  TEST_ASSERT_EQUAL_INT(LedState::Error, r.control.status().led);
}
void already_recording_startup_queries_without_rec_then_explicit_stop() {
  ControlRig r;
  r.radio.host.encoding = true;
  r.control.submit(ButtonAction::RecordingIntent);
  r.pump();
  TEST_ASSERT_EQUAL_UINT(0, r.shutters(true));
  TEST_ASSERT_EQUAL_INT(RecordingState::Recording, r.control.status().group.intent);
  r.control.submit(ButtonAction::RecordingIntent);
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.shutters(false));
}
void stop_during_scan_cancels_late_advertisement_without_rec() {
  ControlRig r;
  r.control.submit(ButtonAction::RecordingIntent);
  r.pass(false);
  auto *scan = r.radio.host.contexts.back();
  r.radio.handled = r.radio.host.commands.size();
  r.control.submit(ButtonAction::RecordingIntent);
  BleEvent e;
  e.kind = BleEventKind::Advertisement;
  e.advertisement_type = BleAdvertisementType::ConnectableUndirected;
  e.identity.type = IdentityType::Public;
  e.identity.address[0] = 1;
  e.size = 4;
  e.bytes = {{3, 3, 0xa6, 0xfe}};
  r.radio.host.deliver(*scan, e);
  r.pump();
  TEST_ASSERT_EQUAL_UINT(0, r.shutters(true));
  TEST_ASSERT_EQUAL_INT(RecordingState::Stopped, r.control.status().group.intent);
}
void stop_cancels_pending_fresh_query_and_reset_never_replays_rec() {
  ControlRig r;
  r.radio.manager.request(0, Operation::Connect);
  r.pump();
  r.radio.auto_camera = false;
  r.control.submit(ButtonAction::RecordingIntent);
  r.pump(5);
  TEST_ASSERT_EQUAL_INT(Hero12RecoveryPhase::Observing, r.radio.adapter.recoveryState(0).phase);
  auto *old = r.radio.link;
  r.control.submit(ButtonAction::RecordingIntent);
  r.radio.host.notification(*old, 18, {0x13, 0, 10, 1, 0});
  r.radio.host.notification(*old, 18, {0x93, 0, 10, 1, 0});
  r.radio.auto_camera = true;
  r.pump();
  TEST_ASSERT_EQUAL_UINT(0, r.shutters(true));
  r.control.submit(ButtonAction::RecordingIntent);
  r.pass(false);
  const auto outstanding = r.radio.handled;
  r.control.reset();
  r.radio.manager.reset();
  for (size_t i = outstanding; i < r.radio.host.commands.size(); ++i)
    if (r.radio.host.commands[i].phase == BlePhase::Write)
      r.radio.host.complete(i);
  r.radio.handled = r.radio.host.commands.size();
  r.pump();
  TEST_ASSERT_EQUAL_UINT(0, r.shutters(true));
  TEST_ASSERT_EQUAL_INT(RecordingState::Unknown, r.control.status().group.intent);
}
void bounded_actions_overflow_refusal_and_batch_stop_have_no_phantom_rec() {
  ControlRig r;
  for (unsigned i = 0; i < 4; ++i)
    TEST_ASSERT_EQUAL_INT(ControlAdmission::Admitted,
                          r.control.submit(ButtonAction::RecordingIntent));
  TEST_ASSERT_EQUAL_INT(ControlAdmission::Overflow,
                        r.control.submit(ButtonAction::RecordingIntent));
  r.pump();
  TEST_ASSERT_EQUAL_UINT(0, r.shutters(true));
  TEST_ASSERT_EQUAL_UINT(4, r.control.status().accepted);
  TEST_ASSERT_EQUAL_UINT(1, r.control.status().overflow);
  TEST_ASSERT_EQUAL_INT(RecordingState::Stopped, r.control.status().group.intent);
  r.local.camera = CameraAdmission::Refused;
  r.control.observe(r.local);
  TEST_ASSERT_EQUAL_INT(ControlAdmission::Refused, r.control.submit(ButtonAction::RecordingIntent));
}
void custom_short_wake_long_record_and_double_resync_are_preserved() {
  ButtonConfig config;
  config.short_action = ButtonAction::WakeReconnect;
  config.long_action = ButtonAction::RecordingIntent;
  config.double_enabled = true;
  ControlRig r(1, config);
  r.shortPress();
  r.radio.clock.value += 301;
  r.pass();
  r.pump();
  TEST_ASSERT_EQUAL_UINT(0, r.shutters(true));
  TEST_ASSERT_EQUAL_INT(RecordingState::Unknown, r.control.status().group.intent);
  r.input.down = true;
  r.pass();
  r.radio.clock.value += 21;
  r.pass();
  r.radio.clock.value += 801;
  r.pass();
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.shutters(true));
  r.input.down = false;
  r.pass();
  r.radio.clock.value += 21;
  r.pass();
  const auto accepted = r.control.status().accepted;
  r.shortPress();
  r.shortPress();
  r.pump();
  TEST_ASSERT_EQUAL_UINT(accepted + 1, r.control.status().accepted);
  TEST_ASSERT_EQUAL_INT(RecordingState::Unknown, r.control.status().group.intent);
  TEST_ASSERT_EQUAL_UINT(1, r.shutters(true));
}
void resync_during_recording_preserves_next_button_stop() {
  for (auto action : {ButtonAction::Resync, ButtonAction::WakeReconnect}) {
    ControlRig r;
    r.shortPress();
    r.pump();
    TEST_ASSERT_EQUAL_UINT(1, r.shutters(true));
    r.control.submit(action);
    r.pump();
    TEST_ASSERT_EQUAL_INT(RecordingState::Unknown, r.group.status().intent);
    r.shortPress();
    r.pump();
    TEST_ASSERT_EQUAL_UINT(1, r.shutters(false));
    TEST_ASSERT_EQUAL_UINT(1, r.shutters(true));
    TEST_ASSERT_EQUAL_INT(RecordingState::Stopped, r.group.status().intent);
  }
}
void manager_reset_discards_previously_admitted_action_without_replay() {
  ControlRig r;
  r.control.submit(ButtonAction::RecordingIntent);
  r.radio.manager.reset();
  r.pump();
  TEST_ASSERT_EQUAL_UINT(0, r.shutters(true));
  TEST_ASSERT_EQUAL_UINT(0, r.radio.host.commands.size());
  TEST_ASSERT_EQUAL_INT(RecordingState::Unknown, r.group.status().intent);
}
void copied_logger_fault_and_led_backend_error_remain_visible() {
  ControlRig r;
  r.local.storage.terminal = true;
  r.local.storage_error = 17;
  r.control.observe(r.local);
  TEST_ASSERT_EQUAL_INT(LedState::Error, r.control.status().led);
  TEST_ASSERT_EQUAL_INT(17, r.control.status().local.storage_error);
  r.sink.failure = 23;
  r.radio.clock.value += 101;
  r.control.observe(r.local);
  TEST_ASSERT_EQUAL_INT(23, r.control.status().led_error);
  r.local.storage.terminal = false;
  r.local.storage_error = 0;
  r.control.observe(r.local);
  TEST_ASSERT_EQUAL_INT(LedState::Error, r.control.status().led);
}
void stop_waits_for_actual_host_release_and_rejects_late_query_without_blocking_peer() {
  ControlRig r(2);
  r.radio.host.independent_encoding = true;
  r.radio.host.encodings[0] = r.radio.host.encodings[1] = true;
  r.radio.manager.request(0, Operation::Connect);
  r.radio.manager.request(1, Operation::Connect);
  r.pump();
  r.radio.auto_camera = false;
  r.control.submit(ButtonAction::RecordingIntent);
  r.pump(5);
  TEST_ASSERT_EQUAL_INT(Hero12RecoveryPhase::Observing, r.radio.adapter.recoveryState(0).phase);
  TEST_ASSERT_EQUAL_INT(Hero12RecoveryPhase::Observing, r.radio.adapter.recoveryState(1).phase);
  auto *old_link = r.radio.links[0];
  const auto old_token = r.radio.manager.state(0)->token;
  r.radio.host.held = old_link;
  r.control.submit(ButtonAction::RecordingIntent);
  r.radio.auto_camera = true;
  r.pump(100);
  TEST_ASSERT_EQUAL_UINT(0, r.shutters(false, 0));
  TEST_ASSERT_EQUAL_UINT(1, r.shutters(false, 1));
  TEST_ASSERT_TRUE(r.radio.host.encodings[0]);
  TEST_ASSERT_FALSE(r.radio.host.encodings[1]);
  TEST_ASSERT_TRUE(r.group.status().peers[0].pending);
  TEST_ASSERT_EQUAL_INT(RecordingState::Unknown, r.radio.manager.state(0)->observed);
  TEST_ASSERT_TRUE(r.radio.adapter.central().phase(0) == BlePhase::Retiring ||
                   r.radio.adapter.central().phase(0) == BlePhase::Quarantined);
  // Valid owned old context, after STOP; neither late query nor old connection
  // evidence may complete the replacement request or release the host lease.
  r.radio.host.notification(*old_link, 18, {0x13, 0, 10, 1, 0});
  r.radio.host.notification(*old_link, 18, {0x93, 0, 10, 1, 1});
  r.pump(10);
  TEST_ASSERT_EQUAL_INT(RecordingState::Unknown, r.radio.manager.state(0)->observed);
  TEST_ASSERT_EQUAL_UINT(0, r.shutters(true, 0));
  r.radio.host.held = nullptr;
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.shutters(false, 0));
  TEST_ASSERT_EQUAL_UINT(0, r.shutters(true, 0));
  TEST_ASSERT_EQUAL_INT(RecordingState::Stopped, r.radio.manager.state(0)->observed);
  TEST_ASSERT_TRUE(r.radio.manager.state(0)->token.connection != old_token.connection);
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.group.status().peers[0].error);
}
void cancellation_retirement_wait_times_out_without_fabricated_stop_or_forced_release() {
  for (uint32_t start : {uint32_t(100), uint32_t(UINT32_MAX - 70000)}) {
    ControlRig r;
    r.radio.clock.value = start;
    r.radio.host.encoding = true;
    r.radio.manager.request(0, Operation::Connect);
    r.pump();
    r.radio.auto_camera = false;
    r.control.submit(ButtonAction::RecordingIntent);
    r.pump(5);
    r.radio.host.held = r.radio.link;
    r.control.submit(ButtonAction::RecordingIntent);
    r.radio.auto_camera = true;
    r.pump(5);
    r.radio.clock.value += 140000;
    r.pass();
    const auto status = r.group.status();
    const auto phase = r.radio.adapter.central().phase(0);
    const auto observed = r.radio.manager.state(0)->observed;
    r.radio.host.held = nullptr;
    r.pump();
    TEST_ASSERT_EQUAL_INT(CameraError::Timeout, status.peers[0].error);
    TEST_ASSERT_TRUE(status.peers[0].terminal_failure);
    TEST_ASSERT_TRUE(phase == BlePhase::Retiring || phase == BlePhase::Quarantined);
    TEST_ASSERT_EQUAL_INT(RecordingState::Unknown, observed);
    TEST_ASSERT_EQUAL_UINT(0, r.shutters(false));
    TEST_ASSERT_EQUAL_UINT(0, r.shutters(true));
    TEST_ASSERT_TRUE(r.radio.host.encoding);
  }
}
void reset_during_cancellation_retirement_discards_reconnect_and_rec() {
  ControlRig r;
  r.radio.host.encoding = true;
  r.radio.manager.request(0, Operation::Connect);
  r.pump();
  r.radio.auto_camera = false;
  r.control.submit(ButtonAction::RecordingIntent);
  r.pump(5);
  r.radio.host.held = r.radio.link;
  r.control.submit(ButtonAction::RecordingIntent);
  r.radio.auto_camera = true;
  r.pump(5);
  r.control.reset();
  r.radio.manager.reset();
  r.radio.host.held = nullptr;
  r.pump();
  TEST_ASSERT_EQUAL_INT(RecordingState::Unknown, r.group.status().intent);
  TEST_ASSERT_EQUAL_UINT(0, r.shutters(true));
  TEST_ASSERT_EQUAL_UINT(0, r.shutters(false));
}
void stop_during_unanswered_live_query_reconnects_and_observes_stopped() {
  unsigned stop_shutters = 0, start_shutters = 0;
  bool recording = true;
  RecordingState observed = RecordingState::Unknown;
  CameraError error = CameraError::None;
  {
    ControlRig r;
    r.radio.host.encoding = true;
    r.radio.manager.request(0, Operation::Connect);
    r.pump();
    r.radio.auto_camera = false;
    r.control.submit(ButtonAction::RecordingIntent);
    r.pump(5);
    TEST_ASSERT_EQUAL_INT(Hero12RecoveryPhase::Observing, r.radio.adapter.recoveryState(0).phase);
    TEST_ASSERT_EQUAL_INT(Lifecycle::Operating, r.radio.manager.state(0)->lifecycle);
    // ATT is complete, but no camera reply/notification is delivered before STOP.
    r.control.submit(ButtonAction::RecordingIntent);
    r.radio.auto_camera = true;
    r.pump();
    stop_shutters = r.shutters(false);
    start_shutters = r.shutters(true);
    recording = r.radio.host.encoding;
    observed = r.radio.manager.state(0)->observed;
    error = r.group.status().peers[0].error;
  }
  TEST_ASSERT_EQUAL_UINT(1, stop_shutters);
  TEST_ASSERT_EQUAL_UINT(0, start_shutters);
  TEST_ASSERT_FALSE(recording);
  TEST_ASSERT_EQUAL_INT(RecordingState::Stopped, observed);
  TEST_ASSERT_EQUAL_INT(CameraError::None, error);
}
void preparation_timeout_is_bounded_across_millis_wrap_without_rec() {
  for (uint32_t start : {uint32_t(100), uint32_t(UINT32_MAX - 70000)}) {
    ControlRig r;
    r.radio.clock.value = start;
    r.control.submit(ButtonAction::RecordingIntent);
    r.pass(false);
    r.radio.handled = r.radio.host.commands.size();
    r.radio.clock.value += 140000;
    r.pass(false);
    TEST_ASSERT_EQUAL_INT(CameraError::Timeout, r.group.status().peers[0].error);
    TEST_ASSERT_EQUAL_INT(Hero12RecoveryPhase::Cancelled, r.radio.adapter.recoveryState(0).phase);
    TEST_ASSERT_EQUAL_UINT(0, r.shutters(true));
  }
}
void keepalive_timeout_during_stop_remains_terminal_and_never_dispatches_rec() {
  ControlRig r;
  r.control.submit(ButtonAction::RecordingIntent);
  r.pump();
  const auto start_count = r.shutters(true);
  r.control.submit(ButtonAction::RecordingIntent);
  r.radio.clock.value += 3000;
  r.pass(false);
  r.radio.clock.value += 2001;
  r.pass(false);
  for (size_t i = r.radio.handled; i < r.radio.host.commands.size(); ++i)
    if (r.radio.host.commands[i].phase == BlePhase::Write)
      r.radio.host.complete(i);
  r.radio.handled = r.radio.host.commands.size();
  r.pump();
  TEST_ASSERT_EQUAL_UINT(start_count, r.shutters(true));
  TEST_ASSERT_TRUE(r.group.status().peers[0].terminal_failure);
  TEST_ASSERT_EQUAL_INT(LedState::Error, r.control.status().led);
}
void group_rec_waits_for_keepalive_due_after_fresh_recovery() {
  ControlRig r;
  r.radio.manager.request(0, Operation::Connect);
  r.pump();
  r.control.submit(ButtonAction::RecordingIntent);
  for (unsigned i = 0;
       i < 100 && r.radio.adapter.recoveryState(0).phase != Hero12RecoveryPhase::Ready; ++i)
    r.pass();
  TEST_ASSERT_EQUAL_INT(Hero12RecoveryPhase::Ready, r.radio.adapter.recoveryState(0).phase);
  r.radio.clock.value += 3000;
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.shutters(true));
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.group.status().peers[0].error);
}
void stop_action_waits_for_keepalive_due_in_same_composed_pass() {
  ControlRig r;
  r.control.submit(ButtonAction::RecordingIntent);
  r.pump();
  r.control.submit(ButtonAction::RecordingIntent);
  r.radio.clock.value += 3000;
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.shutters(false));
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.group.status().peers[0].error);
}
void custom_double_recording_intent_admits_rec_then_stop() {
  ButtonConfig config;
  config.double_enabled = true;
  config.short_action = ButtonAction::Resync;
  config.double_action = ButtonAction::RecordingIntent;
  ControlRig r(1, config);
  r.shortPress();
  r.shortPress();
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.control.status().accepted);
  TEST_ASSERT_EQUAL_UINT(1, r.shutters(true));
  TEST_ASSERT_EQUAL_INT(RecordingState::Recording, r.group.status().intent);
  r.shortPress();
  r.shortPress();
  r.pump();
  TEST_ASSERT_EQUAL_UINT(2, r.control.status().accepted);
  TEST_ASSERT_EQUAL_UINT(1, r.shutters(false));
  TEST_ASSERT_EQUAL_INT(RecordingState::Stopped, r.group.status().intent);
}
void camera_admission_revocation_cancels_queued_recovery_before_rec() {
  ControlRig r;
  r.control.submit(ButtonAction::RecordingIntent);
  r.pass(false);
  auto *scan = r.radio.host.contexts.back();
  r.radio.handled = r.radio.host.commands.size();
  BleEvent e;
  e.kind = BleEventKind::Advertisement;
  e.advertisement_type = BleAdvertisementType::ConnectableUndirected;
  e.identity.type = IdentityType::Public;
  e.identity.address[0] = 1;
  e.size = 4;
  e.bytes = {{3, 3, 0xa6, 0xfe}};
  r.radio.host.deliver(*scan, e);
  r.local.camera = CameraAdmission::Refused;
  r.control.observe(r.local);
  r.pump();
  TEST_ASSERT_EQUAL_UINT(0, r.shutters(true));
  TEST_ASSERT_EQUAL_INT(RecordingState::Unknown, r.group.status().intent);
}
void superseded_recovery_token_cannot_authorize_rec() {
  ControlRig r;
  r.radio.manager.request(0, Operation::Connect);
  r.pump();
  r.control.submit(ButtonAction::RecordingIntent);
  for (unsigned i = 0;
       i < 100 && r.radio.adapter.recoveryState(0).phase != Hero12RecoveryPhase::Ready; ++i)
    r.pass();
  TEST_ASSERT_EQUAL_INT(Hero12RecoveryPhase::Ready, r.radio.adapter.recoveryState(0).phase);
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.radio.manager.request(0, Operation::Query));
  r.pump();
  TEST_ASSERT_EQUAL_UINT(0, r.shutters(true));
  TEST_ASSERT_EQUAL_INT(CameraError::NotConnected, r.group.status().peers[0].error);
}
void unsupported_peer_retains_error_while_qualified_peer_records() {
  ControlRig r(2);
  TEST_ASSERT_FALSE(r.radio.adapter.configurePeer(1, {}));
  r.control.submit(ButtonAction::RecordingIntent);
  r.pump();
  TEST_ASSERT_EQUAL_UINT(1, r.shutters(true, 0));
  TEST_ASSERT_EQUAL_UINT(0, r.shutters(true, 1));
  TEST_ASSERT_EQUAL_INT(CameraError::Unsupported, r.group.status().peers[1].error);
}
void actual_transport_faults_precede_action_and_only_group_advancement() {
  Rig r;
  r.manager.request(0, Operation::Connect);
  r.pump();
  RecordingManager group(r.manager, r.clock);
  r.adapter.attachGroup(group);
  struct Action : CameraServiceAction {
    CameraManager &manager;
    RecordingManager &group;
    Lifecycle lifecycle = Lifecycle::Ready;
    uint32_t ticks = 0, advances = 0;
    Action(CameraManager &m, RecordingManager &g) : manager(m), group(g) {}
    void beforeAdvance() override {
      lifecycle = manager.state(0)->lifecycle;
      ticks = manager.ticks();
      advances = group.advancements();
      group.request(RecordingState::Recording);
    }
  } action(r.manager, group);
  const auto ticks = r.manager.ticks(), advances = group.advancements();
  BleEvent fault;
  fault.kind = BleEventKind::Disconnected;
  fault.connection = r.link->connection.load();
  r.host.deliver(*r.link, fault, true);
  r.adapter.service(&action);
  r.adapter.detachGroup(&group);
  TEST_ASSERT_EQUAL_INT(Lifecycle::Idle, action.lifecycle);
  TEST_ASSERT_EQUAL_UINT32(ticks, action.ticks);
  TEST_ASSERT_EQUAL_UINT32(advances, action.advances);
  TEST_ASSERT_EQUAL_UINT32(ticks + 1, r.manager.ticks());
  TEST_ASSERT_EQUAL_UINT32(advances + 1, group.advancements());
}
void composed_adapter_reentry_cannot_advance_managers_twice() {
  Rig r;
  RecordingManager group(r.manager, r.clock);
  r.adapter.attachGroup(group);
  struct Action : CameraServiceAction {
    Hero12Adapter &adapter;
    explicit Action(Hero12Adapter &a) : adapter(a) {}
    void beforeAdvance() override { adapter.service(); }
  } action(r.adapter);
  const auto ticks = r.manager.ticks(), advances = group.advancements();
  r.adapter.service(&action);
  r.adapter.detachGroup(&group);
  TEST_ASSERT_EQUAL_UINT32(ticks + 1, r.manager.ticks());
  TEST_ASSERT_EQUAL_UINT32(advances + 1, group.advancements());
}
void composed_pass_publishes_group_once_after_all_transport_events() {
  Rig r;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump();
  unsigned publications = 0;
  RecordingManager group(
      r.manager, r.clock, [](void *p, const RecordingStatus &) { ++*static_cast<unsigned *>(p); },
      &publications);
  r.adapter.attachGroup(group);
  r.host.notification(*r.link, 18, {0x93, 0, 10, 1, 1});
  r.host.notification(*r.link, 18, {0x93, 0, 10, 1, 0});
  const auto advances = group.advancements(), ticks = r.manager.ticks();
  r.adapter.service();
  r.adapter.detachGroup(&group);
  TEST_ASSERT_EQUAL_UINT(1, publications);
  TEST_ASSERT_EQUAL_UINT32(advances + 1, group.advancements());
  TEST_ASSERT_EQUAL_UINT32(ticks + 1, r.manager.ticks());
}
void maintenance_retires_actual_target_context_and_keeps_foreign_ready() {
  Rig r(Hero12Adapter::managerPolicy(), 2);
  r.manager.request(0, Operation::Connect);
  r.manager.request(1, Operation::Connect);
  r.pump();
  TEST_ASSERT_TRUE(r.adapter.commandReady(0));
  TEST_ASSERT_TRUE(r.adapter.commandReady(1));
  auto *held = r.links[0];
  r.host.held = held;
  TEST_ASSERT_TRUE(r.adapter.sealForMaintenance(0));
  TEST_ASSERT_FALSE_MESSAGE(r.adapter.commandReady(0),
                            "maintenance must retire actual adapter link");
  r.adapter.service();
  TEST_ASSERT_FALSE(r.adapter.linkReleased(0));
  TEST_ASSERT_TRUE(r.adapter.commandReady(1));
  TEST_ASSERT_EQUAL((int)CameraError::Cancelled, (int)r.adapter.requestRecovery(0, true));
  r.host.held = nullptr;
  r.adapter.service();
  TEST_ASSERT_TRUE(r.adapter.linkReleased(0));
  TEST_ASSERT_EQUAL((int)CameraError::None, (int)r.manager.request(1, Operation::Query));
  r.pump();
  TEST_ASSERT_TRUE(r.manager.state(1)->has_observation);
}
struct MarkerAuditSpy : CameraMarkerAudit {
  std::vector<CameraMarkerEvent> events;
  void markerEvent(const CameraMarkerEvent &event) override { events.push_back(event); }
  unsigned count(CameraMarkerPhase phase) const {
    unsigned n = 0;
    for (const auto &event : events)
      n += event.phase == phase;
    return n;
  }
};
void hilight_requires_opt_in_and_fresh_encoding_then_matches_ack() {
  CameraMarkerKey key;
  key.session_id = 42;
  key.bookmark_id = 7;
  {
    Rig disabled;
    MarkerAuditSpy audit;
    TEST_ASSERT_TRUE(disabled.adapter.attachMarkerAudit(audit));
    TEST_ASSERT_EQUAL_INT(CameraError::None, disabled.manager.request(0, Operation::Connect));
    disabled.host.encoding = true;
    disabled.pump();
    TEST_ASSERT_EQUAL_INT(CameraError::Unsupported, disabled.adapter.requestHilight(0, key));
    TEST_ASSERT_EQUAL_UINT(0, audit.count(CameraMarkerPhase::Attempt));
  }
  Rig r(Hero12Adapter::managerPolicy(), 1, true);
  MarkerAuditSpy audit;
  TEST_ASSERT_TRUE(r.adapter.attachMarkerAudit(audit));
  r.host.encoding = true;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump();
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.adapter.requestHilight(0, key));
  r.pump(20);
  unsigned marker_writes = 0;
  for (const auto &command : r.host.commands)
    if (command.phase == BlePhase::Write && command.handle == 3 && command.size >= 3 &&
        command.bytes[2] == 0x18)
      ++marker_writes;
  TEST_ASSERT_EQUAL_UINT(1, marker_writes);
  TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::Attempt));
  TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::WireAck));
  TEST_ASSERT_EQUAL_UINT64(42, audit.events.back().key.session_id);
  TEST_ASSERT_EQUAL_UINT32(7, audit.events.back().key.bookmark_id);
  TEST_ASSERT_TRUE(audit.events.back().connection_generation > 0);
  TEST_ASSERT_TRUE(audit.events.back().operation_generation > 0);
}
struct HealthSinkSpy : CameraHealthSampleSink {
  std::vector<QualifiedHealthSample> samples;
  std::vector<uint32_t> floors;
  void healthSample(uint8_t, const QualifiedHealthSample &sample) override {
    samples.push_back(sample);
  }
  void healthPollPeriod(uint8_t, uint32_t floor, uint32_t) override { floors.push_back(floor); }
};
void stale_health_status_cannot_authorize_pending_hilight_encoding_check() {
  HealthSinkSpy sink;
  Rig r(Hero12Adapter::managerPolicy(), 1, true, true, true);
  MarkerAuditSpy audit;
  TEST_ASSERT_TRUE(r.adapter.attachMarkerAudit(audit));
  TEST_ASSERT_TRUE(r.adapter.attachHealthSink(sink));
  r.host.encoding = true;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump();

  // Hold the requested single-field reply while a late background 0x13
  // response (also carrying Encoding=true) reaches the same query endpoint.
  r.auto_camera = false;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.adapter.requestHilight(0, CameraMarkerKey(42, 14)));
  r.pump(5);
  r.host.notification(*r.link, 18, {0x13, 0, 10, 1, 1, 60, 4, 0, 0, 0x13, 0x88});
  r.adapter.service();
  TEST_ASSERT_EQUAL_UINT(0, audit.count(CameraMarkerPhase::Attempt));
  for (const auto &command : r.host.commands)
    TEST_ASSERT_FALSE(command.phase == BlePhase::Write && command.handle == 3 &&
                      command.size >= 3 && command.bytes[2] == 0x18);

  // The actual foreground single-field result is still accepted.
  r.host.notification(*r.link, 18, {0x13, 0, 10, 1, 1});
  r.adapter.service();
  r.auto_camera = true;
  r.pump(20);
  TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::Attempt));
  TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::WireAck));
  r.adapter.detachHealthSink(&sink);
}
void optional_health_failure_keeps_control_link_and_shutter_operable() {
  HealthSinkSpy sink;
  Rig r(Hero12Adapter::managerPolicy(), 1, false, true, true);
  TEST_ASSERT_TRUE(r.adapter.attachHealthSink(sink));
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump();
  // A rejected optional update must not retire functioning recording control.
  r.host.notification(*r.link, 18, {0x93, 1});
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT(Lifecycle::Ready, r.manager.state(0)->lifecycle);
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Start));
  r.pump();
  TEST_ASSERT_EQUAL_INT(RecordingState::Recording, r.manager.state(0)->observed);
  r.adapter.detachHealthSink(&sink);
}
void hilight_rechecks_encoding_after_due_keepalive() {
  Rig r(Hero12Adapter::managerPolicy(), 1, true);
  MarkerAuditSpy audit;
  TEST_ASSERT_TRUE(r.adapter.attachMarkerAudit(audit));
  r.host.encoding = true;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump(80);
  r.auto_camera = false;
  r.clock.value += 2800;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.adapter.requestHilight(0, CameraMarkerKey(42, 15)));
  r.pump(5);            // Encoding query ATT completes, camera response remains pending.
  r.clock.value += 200; // Keepalive becomes due while the fresh query is pending.
  r.host.notification(*r.link, 18, {0x13, 0, 10, 1, 1});
  r.adapter.service();
  r.pump(5); // Keepalive takes precedence over the marker command.
  TEST_ASSERT_EQUAL_UINT(0, audit.count(CameraMarkerPhase::Attempt));
  r.host.notification(*r.link, 18, {0x93, 0, 10, 1, 0});
  r.host.notification(*r.link, 12, {0x5b, 0});
  r.adapter.service();
  r.pump(5);
  TEST_ASSERT_EQUAL_UINT(0, audit.count(CameraMarkerPhase::Attempt));
  for (const auto &command : r.host.commands)
    TEST_ASSERT_FALSE(command.phase == BlePhase::Write && command.handle == 3 &&
                      command.size >= 3 && command.bytes[2] == 0x18);
}
void hilight_missing_encoding_reply_times_out_without_tag_or_link_loss() {
  Rig r(Hero12Adapter::managerPolicy(), 1, true);
  MarkerAuditSpy audit;
  TEST_ASSERT_TRUE(r.adapter.attachMarkerAudit(audit));
  r.host.encoding = true;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump(80);
  r.auto_camera = false;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.adapter.requestHilight(0, CameraMarkerKey(42, 16)));
  TEST_ASSERT_EQUAL_INT(CameraError::Busy, r.adapter.requestHilight(0, CameraMarkerKey(42, 17)));
  r.pump(5);
  r.host.notification(*r.link, 18, {0x13, 0}); // No Encoding value; cached true is insufficient.
  r.adapter.service();
  TEST_ASSERT_EQUAL_UINT(0, audit.count(CameraMarkerPhase::Attempt));
  r.clock.value += 601;
  r.adapter.service();
  TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::Failed));
  TEST_ASSERT_EQUAL_UINT(0, audit.count(CameraMarkerPhase::WireAck));
  TEST_ASSERT_EQUAL_INT(Lifecycle::Ready, r.manager.state(0)->lifecycle);
}
void hilight_wire_ack_waits_for_att_completion() {
  Rig r(Hero12Adapter::managerPolicy(), 1, true);
  MarkerAuditSpy audit;
  TEST_ASSERT_TRUE(r.adapter.attachMarkerAudit(audit));
  r.host.encoding = true;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump(80);
  r.auto_camera = false;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.adapter.requestHilight(0, CameraMarkerKey(42, 18)));
  r.pump(5);
  r.host.notification(*r.link, 18, {0x13, 0, 10, 1, 1});
  r.adapter.service();
  TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::Attempt));
  const auto marker_write = r.host.commands.size() - 1;
  r.host.notification(*r.link, 6, {0x18, 0});
  r.adapter.service();
  TEST_ASSERT_EQUAL_UINT(0, audit.count(CameraMarkerPhase::WireAck));
  r.host.complete(marker_write);
  r.adapter.service();
  TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::WireAck));
  r.handled = r.host.commands.size();
}
void hilight_disconnect_reset_shutdown_and_audit_detach_cancel_without_replay() {
  for (unsigned cause = 0; cause < 4; ++cause) {
    Rig r(Hero12Adapter::managerPolicy(), 1, true);
    MarkerAuditSpy audit;
    TEST_ASSERT_TRUE(r.adapter.attachMarkerAudit(audit));
    r.host.encoding = true;
    TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
    r.pump(80);
    r.host.drop_hilight_ack = true;
    TEST_ASSERT_EQUAL_INT(CameraError::None, r.adapter.requestHilight(0, CameraMarkerKey(42, 19)));
    r.pump(10);
    TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::Attempt));
    if (cause == 0) {
      BleResult closed;
      closed.kind = BleResultKind::LinkClosed;
      closed.event.peer = 0;
      closed.event.generation = r.manager.state(0)->token.connection;
      r.adapter.result(closed);
    } else if (cause == 1) {
      r.manager.reset();
    } else if (cause == 2) {
      r.adapter.stop();
    } else {
      r.adapter.detachMarkerAudit(&audit);
    }
    TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::Cancelled));
    r.pump(40);
    unsigned marker_writes = 0;
    for (const auto &command : r.host.commands)
      marker_writes += command.phase == BlePhase::Write && command.handle == 3 &&
                       command.size >= 3 && command.bytes[2] == 0x18;
    TEST_ASSERT_EQUAL_UINT(1, marker_writes);
    TEST_ASSERT_EQUAL_UINT(0, audit.count(CameraMarkerPhase::WireAck));
    r.adapter.detachMarkerAudit(&audit);
  }
}
void hilight_refuses_stopped_and_never_writes_marker() {
  Rig r(Hero12Adapter::managerPolicy(), 1, true);
  MarkerAuditSpy audit;
  TEST_ASSERT_TRUE(r.adapter.attachMarkerAudit(audit));
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump();
  CameraMarkerKey key;
  key.session_id = 42;
  key.bookmark_id = 8;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.adapter.requestHilight(0, key));
  r.pump(20);
  TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::RequestRefused));
  TEST_ASSERT_EQUAL_UINT(0, audit.count(CameraMarkerPhase::Attempt));
  for (const auto &command : r.host.commands)
    TEST_ASSERT_FALSE(command.phase == BlePhase::Write && command.handle == 3 &&
                      command.size >= 3 && command.bytes[2] == 0x18);
}
void hilight_refuses_observed_busy_before_query() {
  Rig r(Hero12Adapter::managerPolicy(), 1, true);
  MarkerAuditSpy audit;
  TEST_ASSERT_TRUE(r.adapter.attachMarkerAudit(audit));
  r.host.encoding = true;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump();
  r.host.notification(*r.link, 18, {0x93, 0, 8, 1, 1});
  r.adapter.service();
  const auto before = r.host.commands.size();
  TEST_ASSERT_EQUAL_INT(CameraError::Busy, r.adapter.requestHilight(0, CameraMarkerKey(42, 13)));
  TEST_ASSERT_EQUAL_UINT(before, r.host.commands.size());
  TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::RequestRefused));
}
void unanswered_hilight_ack_times_out_without_retry_or_stale_credit() {
  Rig r(Hero12Adapter::managerPolicy(), 2, true);
  MarkerAuditSpy audit;
  TEST_ASSERT_TRUE(r.adapter.attachMarkerAudit(audit));
  r.host.encoding = true;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(1, Operation::Connect));
  r.pump();
  TEST_ASSERT_NOT_NULL(r.link);
  TEST_ASSERT_NOT_NULL(r.links[1]);
  r.host.drop_hilight_ack = true;
  CameraMarkerKey key;
  key.session_id = 42;
  key.bookmark_id = 9;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.adapter.requestHilight(0, key));
  r.pump(20);
  TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::Attempt));
  BleResult wrong_peer;
  wrong_peer.kind = BleResultKind::Notification;
  wrong_peer.event.peer = 1;
  wrong_peer.event.generation = r.manager.state(1)->token.connection;
  wrong_peer.endpoint = 1;
  wrong_peer.event.size = 3;
  wrong_peer.event.bytes[0] = 2;
  wrong_peer.event.bytes[1] = 0x18;
  wrong_peer.event.bytes[2] = 0;
  r.adapter.result(wrong_peer); // Correct ID on wrong peer.
  TEST_ASSERT_EQUAL_UINT(0, audit.count(CameraMarkerPhase::WireAck));
  BleResult stale = wrong_peer;
  stale.event.peer = 0;
  stale.event.generation = r.manager.state(0)->token.connection - 1;
  r.adapter.result(stale); // Wrong connection generation.
  TEST_ASSERT_EQUAL_UINT(0, audit.count(CameraMarkerPhase::WireAck));
  r.host.notification(*r.link, 6, {1, 0}); // Wrong command ID.
  r.adapter.service();
  TEST_ASSERT_EQUAL_UINT(0, audit.count(CameraMarkerPhase::WireAck));
  r.clock.value += 601;
  r.adapter.service();
  TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::Failed));
  r.host.notification(*r.link, 6, {0x18, 0}); // Late success cannot credit key.
  r.adapter.service();
  TEST_ASSERT_EQUAL_UINT(0, audit.count(CameraMarkerPhase::WireAck));
  key.bookmark_id = 10;
  TEST_ASSERT_EQUAL_INT(CameraError::Busy, r.adapter.requestHilight(0, key));
  TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::Attempt));
}
void stop_preempts_unanswered_marker_without_replaying_it() {
  Rig r(Hero12Adapter::managerPolicy(), 1, true);
  MarkerAuditSpy audit;
  TEST_ASSERT_TRUE(r.adapter.attachMarkerAudit(audit));
  r.host.encoding = true;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump();
  r.host.drop_hilight_ack = true;
  CameraMarkerKey key;
  key.session_id = 42;
  key.bookmark_id = 11;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.adapter.requestHilight(0, key));
  r.pump(20);
  TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::Attempt));
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Stop));
  r.pump(40);
  TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::Cancelled));
  TEST_ASSERT_EQUAL_UINT(0, audit.count(CameraMarkerPhase::WireAck));
  TEST_ASSERT_EQUAL_INT(RecordingState::Stopped, r.manager.state(0)->observed);
  unsigned marker_writes = 0;
  for (const auto &command : r.host.commands)
    if (command.phase == BlePhase::Write && command.handle == 3 && command.size >= 3 &&
        command.bytes[2] == 0x18)
      ++marker_writes;
  TEST_ASSERT_EQUAL_UINT(1, marker_writes);
}
unsigned backgroundWrites(const Rig &r) {
  unsigned n = 0;
  for (const auto &c : r.host.commands)
    n += c.phase == BlePhase::Write && c.handle == 15 && c.size > 4;
  return n;
}
void health_requires_both_opt_in_and_owner_attachment() {
  HealthSinkSpy sink;
  Rig r;
  r.adapter.attachHealthSink(sink);
  r.manager.request(0, Operation::Connect);
  r.pump();
  r.clock.value += 20000;
  r.pump();
  TEST_ASSERT_EQUAL_UINT(0, backgroundWrites(r));
  TEST_ASSERT_TRUE(sink.samples.empty());
  r.adapter.detachHealthSink(&sink);
}
void health_registers_once_and_uses_qualified_floor_and_video_guard() {
  HealthSinkSpy sink;
  Rig r(Hero12Adapter::managerPolicy(), 1, false, true, false);
  r.adapter.attachHealthSink(sink);
  r.manager.request(0, Operation::Connect);
  r.pump(240);
  r.pump(240);
  TEST_ASSERT_EQUAL_UINT(1, backgroundWrites(r));
  TEST_ASSERT_EQUAL_UINT(1, sink.floors.size());
  TEST_ASSERT_EQUAL_UINT32(5000, sink.floors.back());
  bool saw_battery = false, saw_time = false;
  for (const auto &sample : sink.samples) {
    TEST_ASSERT_EQUAL_INT(HealthProfile::Hero12V1, sample.profile);
    TEST_ASSERT_EQUAL_UINT32(r.manager.state(0)->token.connection, sample.connection);
    TEST_ASSERT_TRUE(sample.sequence > 0);
    if (sample.field == HealthField::BatteryPercent) {
      saw_battery = true;
      TEST_ASSERT_EQUAL_INT(HealthValidity::Valid, sample.validity);
      TEST_ASSERT_EQUAL_UINT64(20, sample.value);
    }
    if (sample.field == HealthField::RemainingVideoSeconds) {
      saw_time = true;
      TEST_ASSERT_EQUAL_INT(HealthValidity::Unavailable, sample.validity);
    }
  }
  TEST_ASSERT_TRUE(saw_battery && saw_time);
  r.adapter.detachHealthSink(&sink);
}
void invalid_health_floor_stays_unavailable_without_invented_poll_period() {
  for (uint32_t floor : {uint32_t(0), uint32_t(30001)}) {
    HealthSinkSpy sink;
    Rig r(Hero12Adapter::managerPolicy(), 1, false, true, true);
    r.host.health_floor = floor;
    r.adapter.attachHealthSink(sink);
    r.manager.request(0, Operation::Connect);
    r.pump(240);
    r.pump(240);
    TEST_ASSERT_FALSE(sink.floors.empty());
    TEST_ASSERT_EQUAL_UINT32(0, sink.floors.back());
    const unsigned writes = backgroundWrites(r);
    r.clock.value += 40000;
    r.pump(240);
    TEST_ASSERT_EQUAL_UINT(writes, backgroundWrites(r));
    TEST_ASSERT_EQUAL_INT(Lifecycle::Ready, r.manager.state(0)->lifecycle);
    r.adapter.detachHealthSink(&sink);
  }
}
void health_poll_timeout_isolated_and_next_period_has_no_catchup_burst() {
  HealthSinkSpy sink;
  Rig r(Hero12Adapter::managerPolicy(), 1, false, true, true);
  r.adapter.attachHealthSink(sink);
  r.manager.request(0, Operation::Connect);
  r.pump(240);
  r.pump(240);
  r.host.health_silent = true;
  r.clock.value += 15000;
  r.pump(40);
  const unsigned writes = backgroundWrites(r);
  r.clock.value += 1000;
  r.pump(40);
  TEST_ASSERT_TRUE(r.adapter.healthFailures() > 0);
  TEST_ASSERT_EQUAL_INT(Lifecycle::Ready, r.manager.state(0)->lifecycle);
  TEST_ASSERT_EQUAL_UINT(writes, backgroundWrites(r));
  r.host.health_silent = false;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Stop));
  r.pump(240);
  TEST_ASSERT_EQUAL_INT(RecordingState::Stopped, r.manager.state(0)->observed);
  r.adapter.detachHealthSink(&sink);
}
void admitted_hilight_attempt_and_ack_are_separate_v6_audit_rows() {
  Rig r(Hero12Adapter::managerPolicy(), 1, true);
  r.host.encoding = true;
  RecordingManager group(r.manager, r.clock);
  r.adapter.attachGroup(group);
  CameraInbox inbox;
  CameraEventLogger logger(inbox, r.clock, 42, group);
  TEST_ASSERT_TRUE(logger.configurePeer(0, 301, CameraModel::HERO12_BLACK));
  TEST_ASSERT_TRUE(r.adapter.attachMarkerAudit(logger));
  CameraLogSink sink;
  Storage storage(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::BookmarkV6});
  SessionClock session_clock(r.clock, 42, 1000);
  ImuInbox imu;
  TelemetryAdmission admission(session_clock, storage, imu, &inbox);
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump();
  CameraMarkerKey key(42, 12);
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.adapter.requestHilight(0, key));
  r.pump(20);
  for (unsigned i = 0; i < 100; ++i) {
    admission.tick();
    storage.workerStep();
  }
  size_t rows = 0;
  size_t offset = 0;
  while ((offset = sink.bytes.find("marker,", offset)) != std::string::npos) {
    ++rows;
    offset += 7;
  }
  TEST_ASSERT_EQUAL_UINT(2, rows);
  TEST_ASSERT_TRUE(sink.bytes.find(",12,0,301,") != std::string::npos);
  r.adapter.detachMarkerAudit(&logger);
  r.adapter.detachGroup(&group);
}
void admitted_session_bookmark_dispatches_one_native_marker_and_retains_local_row() {
  Rig r(Hero12Adapter::managerPolicy(), 1, true);
  r.host.encoding = true;
  RecordingManager group(r.manager, r.clock);
  CameraLogSink sink;
  CameraEventSession session(r.clock, sink, r.adapter, r.manager, group, 42, "fw", "synthetic",
                             MotionAdmissionConfig{}, nullptr, true, true);
  TEST_ASSERT_TRUE(session.configurePeer(0, 301, CameraModel::HERO12_BLACK));
  TEST_ASSERT_TRUE(session.activate());
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump();
  const auto before_refusal = r.host.commands.size();
  TEST_ASSERT_EQUAL_INT(
      BookmarkOutcome::Inactive,
      session.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial, 43).outcome);
  TEST_ASSERT_EQUAL_UINT(before_refusal, r.host.commands.size());
  const auto receipt = session.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
  TEST_ASSERT_EQUAL_INT(BookmarkOutcome::Admitted, receipt.outcome);
  TEST_ASSERT_EQUAL_UINT64(42, receipt.session_id);
  TEST_ASSERT_EQUAL_UINT32(1, receipt.bookmark_id);
  r.pump(20);
  for (unsigned i = 0; i < 100; ++i) {
    session.service();
    session.storage().workerStep();
  }
  TEST_ASSERT_TRUE(sink.bytes.find("bookmark,") != std::string::npos);
  TEST_ASSERT_TRUE(sink.bytes.find("marker,") != std::string::npos);
  unsigned marker_writes = 0;
  for (const auto &command : r.host.commands)
    if (command.phase == BlePhase::Write && command.handle == 3 && command.size >= 3 &&
        command.bytes[2] == 0x18)
      ++marker_writes;
  TEST_ASSERT_EQUAL_UINT(1, marker_writes);
  BleResult closed;
  closed.kind = BleResultKind::LinkClosed;
  closed.event.peer = 0;
  closed.event.generation = r.manager.state(0)->token.connection;
  r.adapter.result(closed);
  const auto after_loss = session.bookmark(BookmarkSource::Serial, BookmarkGesture::Serial);
  TEST_ASSERT_EQUAL_INT(BookmarkOutcome::Admitted, after_loss.outcome);
  TEST_ASSERT_EQUAL_UINT32(2, after_loss.bookmark_id);
  for (unsigned i = 0; i < 100; ++i) {
    session.service();
    session.storage().workerStep();
  }
  unsigned local_rows = 0;
  size_t row_offset = 0;
  while ((row_offset = sink.bytes.find("bookmark,", row_offset)) != std::string::npos) {
    ++local_rows;
    row_offset += 9;
  }
  TEST_ASSERT_EQUAL_UINT(2, local_rows);
  marker_writes = 0;
  for (const auto &command : r.host.commands)
    marker_writes += command.phase == BlePhase::Write && command.handle == 3 && command.size >= 3 &&
                     command.bytes[2] == 0x18;
  TEST_ASSERT_EQUAL_UINT(1, marker_writes);
  session.requestStop();
  session.finishImu();
  for (unsigned i = 0; i < 500 && !session.stopped(); ++i) {
    session.service();
    session.storage().workerStep();
  }
  TEST_ASSERT_TRUE(session.stopped());
}
void health_control_preempts_polling_floor_and_late_health_reply_cannot_complete_query() {
  HealthSinkSpy sink;
  Rig r(Hero12Adapter::managerPolicy(), 1, false, true, true);
  r.host.health_floor = 30000;
  r.adapter.attachHealthSink(sink);
  r.manager.request(0, Operation::Connect);
  r.pump();
  r.pump();
  const uint32_t before = r.clock.value;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Start));
  r.pump();
  TEST_ASSERT_EQUAL_INT(RecordingState::Recording, r.manager.state(0)->observed);
  TEST_ASSERT_TRUE(r.clock.value - before < 1000);
  r.auto_camera = false;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Query));
  r.pump(5);
  r.host.notification(*r.link, 18, {0x13, 1});
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT(Lifecycle::Operating, r.manager.state(0)->lifecycle);
  r.host.notification(*r.link, 18, {0x13, 0, 10, 1, 0, 60, 4, 0, 0, 0x75, 0x30});
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT(Lifecycle::Operating, r.manager.state(0)->lifecycle);
  TEST_ASSERT_EQUAL_INT(RecordingState::Recording, r.manager.state(0)->observed);
  r.host.notification(*r.link, 18, {0x13, 0, 10, 1, 1});
  r.adapter.service();
  TEST_ASSERT_EQUAL_INT(Lifecycle::Ready, r.manager.state(0)->lifecycle);
  r.adapter.detachHealthSink(&sink);
}
void health_active_query_yields_to_admitted_stop_at_bounded_deadline() {
  HealthSinkSpy sink;
  Rig r(Hero12Adapter::managerPolicy(), 1, false, true, true);
  r.adapter.attachHealthSink(sink);
  r.manager.request(0, Operation::Connect);
  r.pump();
  r.pump();
  r.host.encoding = true;
  r.host.health_silent = true;
  r.clock.value += 5000;
  r.pump(10);
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Stop));
  r.clock.value += 1000;
  r.host.health_silent = false;
  r.pump();
  TEST_ASSERT_EQUAL_INT(RecordingState::Stopped, r.manager.state(0)->observed);
  TEST_ASSERT_EQUAL_INT(Lifecycle::Ready, r.manager.state(0)->lifecycle);
  r.adapter.detachHealthSink(&sink);
}
void health_resources_without_battery_and_unqualified_video_remain_unavailable() {
  HealthSinkSpy sink;
  Rig r(Hero12Adapter::managerPolicy(), 1, false, true, false);
  r.host.battery_present = false;
  r.adapter.attachHealthSink(sink);
  r.manager.request(0, Operation::Connect);
  r.pump();
  r.pump();
  unsigned unavailable = 0;
  for (const auto &sample : sink.samples)
    if (sample.field == HealthField::BatteryPercent ||
        sample.field == HealthField::RemainingVideoSeconds) {
      ++unavailable;
      TEST_ASSERT_EQUAL_INT(HealthValidity::Unavailable, sample.validity);
    }
  TEST_ASSERT_EQUAL_UINT(2, unavailable);
  r.adapter.detachHealthSink(&sink);
}
void health_notifications_coalesce_with_visible_loss_and_preserve_old_receipt() {
  HealthSinkSpy sink;
  Rig r(Hero12Adapter::managerPolicy(), 1, false, true, true);
  r.adapter.attachHealthSink(sink);
  r.manager.request(0, Operation::Connect);
  r.pump();
  r.pump();
  const uint32_t received = r.clock.value;
  r.host.notification(*r.link, 18, {0x93, 0, 10, 1, 1});
  r.host.notification(*r.link, 18, {0x93, 0, 10, 1, 0});
  r.host.notification(*r.link, 18, {0x93, 0, 10, 1, 1});
  r.clock.value += 12000;
  r.adapter.service();
  TEST_ASSERT_TRUE(r.adapter.healthCoalesced() >= 2);
  TEST_ASSERT_EQUAL_UINT32(received, r.manager.state(0)->last_observed_ms);
  TEST_ASSERT_TRUE(r.manager.state(0)->has_qualified_observation);
  r.pump();
  r.adapter.detachHealthSink(&sink);
}
void health_four_peers_rotate_under_one_aggregate_admission_per_500ms() {
  HealthSinkSpy sink;
  Rig r(Hero12Adapter::managerPolicy(), 4, false, true, true);
  r.adapter.attachHealthSink(sink);
  for (uint8_t i = 0; i < 4; ++i)
    r.manager.request(i, Operation::Connect);
  r.pump();
  r.pump();
  unsigned seen = backgroundWrites(r);
  for (unsigned n = 0; n < 20; ++n) {
    r.clock.value += 100;
    r.pump();
    r.pump();
    const unsigned current = backgroundWrites(r);
    TEST_ASSERT_TRUE(current - seen <= 1);
    seen = current;
  }
  TEST_ASSERT_EQUAL_UINT(4, seen);
  TEST_ASSERT_EQUAL_UINT(4, sink.floors.size());
  for (uint8_t i = 0; i < 4; ++i)
    TEST_ASSERT_EQUAL_INT(Lifecycle::Ready, r.manager.state(i)->lifecycle);
  r.adapter.detachHealthSink(&sink);
}
void health_reset_disconnect_and_owner_detach_never_replay_background_or_shutter() {
  HealthSinkSpy sink;
  Rig r(Hero12Adapter::managerPolicy(), 1, false, true, true);
  r.adapter.attachHealthSink(sink);
  r.manager.request(0, Operation::Connect);
  r.pump();
  r.pump();
  const unsigned before = backgroundWrites(r);
  r.adapter.detachHealthSink(&sink);
  r.clock.value += 20000;
  r.pump();
  TEST_ASSERT_EQUAL_UINT(before, backgroundWrites(r));
  r.manager.reset();
  r.pump();
  r.adapter.attachHealthSink(sink);
  r.clock.value += 20000;
  r.pump();
  TEST_ASSERT_EQUAL_UINT(before, backgroundWrites(r));
  TEST_ASSERT_EQUAL_INT(RecordingState::Unknown, r.manager.state(0)->observed);
  r.adapter.detachHealthSink(&sink);
}
void changed_qualified_floor_reschedules_normal_period_without_immediate_burst() {
  HealthSinkSpy sink;
  Rig r(Hero12Adapter::managerPolicy(), 1, false, true, true);
  r.host.health_floor = 30000;
  r.adapter.attachHealthSink(sink);
  r.manager.request(0, Operation::Connect);
  r.pump();
  r.pump();
  r.host.health_floor = 5000;
  r.host.notification(*r.link, 18, {0x93, 0, 60, 4, 0, 0, 0x13, 0x88});
  r.adapter.service();
  const unsigned before = backgroundWrites(r);
  r.pump();
  r.pump();
  TEST_ASSERT_EQUAL_UINT(before, backgroundWrites(r));
  r.clock.value += 5001;
  r.pump();
  r.pump();
  r.pump();
  TEST_ASSERT_EQUAL_UINT(before + 1, backgroundWrites(r));
  r.adapter.detachHealthSink(&sink);
}
void scheduled_health_requests_fit_four_per_15seconds_and_refresh_resources() {
  HealthSinkSpy sink;
  Rig r(Hero12Adapter::managerPolicy(), 1, false, true, true);
  r.adapter.attachHealthSink(sink);
  r.manager.request(0, Operation::Connect);
  r.pump();
  r.pump();
  const unsigned initial = backgroundWrites(r);
  const size_t initial_samples = sink.samples.size();
  for (unsigned n = 0; n < 30; ++n) {
    r.clock.value += 500;
    r.pump();
    r.pump();
  }
  const unsigned scheduled = backgroundWrites(r) - initial;
  TEST_ASSERT_TRUE(scheduled >= 3 && scheduled <= 4);
  TEST_ASSERT_TRUE(sink.samples.size() > initial_samples);
  bool batched = false;
  for (const auto &c : r.host.commands) {
    if (c.phase != BlePhase::Write || c.handle != 15 || c.bytes[2] != 0x13)
      continue;
    bool encoding = false, seconds = false;
    for (size_t i = 3; i < c.size; ++i) {
      encoding |= c.bytes[i] == 10;
      seconds |= c.bytes[i] == 35;
    }
    batched |= encoding && seconds;
  }
  TEST_ASSERT_TRUE(batched);
  r.adapter.detachHealthSink(&sink);
}
void late_full_health_reply_cannot_authorize_hilight_encoding_check() {
  HealthSinkSpy sink;
  MarkerAuditSpy audit;
  Rig r(Hero12Adapter::managerPolicy(), 1, true, true, true);
  r.host.encoding = true;
  TEST_ASSERT_TRUE(r.adapter.attachHealthSink(sink));
  TEST_ASSERT_TRUE(r.adapter.attachMarkerAudit(audit));
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.manager.request(0, Operation::Connect));
  r.pump();
  r.pump();
  r.auto_camera = false;
  TEST_ASSERT_EQUAL_INT(CameraError::None, r.adapter.requestHilight(0, CameraMarkerKey(42, 7)));
  r.pump(5);
  r.host.notification(*r.link, 18, {0x13, 0, 10, 1, 1, 60, 4, 0, 0, 0x13, 0x88});
  r.pump(2);
  unsigned markers = 0;
  for (const auto &c : r.host.commands)
    markers += c.phase == BlePhase::Write && c.handle == 3 && c.size >= 3 && c.bytes[2] == 0x18;
  TEST_ASSERT_EQUAL_UINT(0, markers);
  TEST_ASSERT_EQUAL_UINT(0, audit.count(CameraMarkerPhase::Attempt));
  r.host.notification(*r.link, 18, {0x13, 0, 10, 1, 1});
  r.auto_camera = true;
  r.pump(20);
  markers = 0;
  for (const auto &c : r.host.commands)
    markers += c.phase == BlePhase::Write && c.handle == 3 && c.size >= 3 && c.bytes[2] == 0x18;
  TEST_ASSERT_EQUAL_UINT(1, markers);
  TEST_ASSERT_EQUAL_UINT(1, audit.count(CameraMarkerPhase::WireAck));
  r.adapter.detachMarkerAudit(&audit);
  r.adapter.detachHealthSink(&sink);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(hilight_rechecks_encoding_after_due_keepalive);
  RUN_TEST(hilight_missing_encoding_reply_times_out_without_tag_or_link_loss);
  RUN_TEST(hilight_wire_ack_waits_for_att_completion);
  RUN_TEST(hilight_disconnect_reset_shutdown_and_audit_detach_cancel_without_replay);
  RUN_TEST(late_full_health_reply_cannot_authorize_hilight_encoding_check);
  RUN_TEST(changed_qualified_floor_reschedules_normal_period_without_immediate_burst);
  RUN_TEST(scheduled_health_requests_fit_four_per_15seconds_and_refresh_resources);
  RUN_TEST(health_control_preempts_polling_floor_and_late_health_reply_cannot_complete_query);
  RUN_TEST(health_active_query_yields_to_admitted_stop_at_bounded_deadline);
  RUN_TEST(health_resources_without_battery_and_unqualified_video_remain_unavailable);
  RUN_TEST(health_notifications_coalesce_with_visible_loss_and_preserve_old_receipt);
  RUN_TEST(health_four_peers_rotate_under_one_aggregate_admission_per_500ms);
  RUN_TEST(health_reset_disconnect_and_owner_detach_never_replay_background_or_shutter);
  RUN_TEST(admitted_session_bookmark_dispatches_one_native_marker_and_retains_local_row);
  RUN_TEST(admitted_hilight_attempt_and_ack_are_separate_v6_audit_rows);
  RUN_TEST(stop_preempts_unanswered_marker_without_replaying_it);
  RUN_TEST(unanswered_hilight_ack_times_out_without_retry_or_stale_credit);
  RUN_TEST(hilight_refuses_stopped_and_never_writes_marker);
  RUN_TEST(hilight_refuses_observed_busy_before_query);
  RUN_TEST(hilight_requires_opt_in_and_fresh_encoding_then_matches_ack);
  RUN_TEST(stale_health_status_cannot_authorize_pending_hilight_encoding_check);
  RUN_TEST(health_requires_both_opt_in_and_owner_attachment);
  RUN_TEST(health_registers_once_and_uses_qualified_floor_and_video_guard);
  RUN_TEST(invalid_health_floor_stays_unavailable_without_invented_poll_period);
  RUN_TEST(health_poll_timeout_isolated_and_next_period_has_no_catchup_burst);
  RUN_TEST(optional_health_failure_keeps_control_link_and_shutter_operable);
  RUN_TEST(maintenance_retires_actual_target_context_and_keeps_foreign_ready);
  RUN_TEST(stop_waits_for_actual_host_release_and_rejects_late_query_without_blocking_peer);
  RUN_TEST(cancellation_retirement_wait_times_out_without_fabricated_stop_or_forced_release);
  RUN_TEST(reset_during_cancellation_retirement_discards_reconnect_and_rec);
  RUN_TEST(stop_during_unanswered_live_query_reconnects_and_observes_stopped);
  RUN_TEST(preparation_timeout_is_bounded_across_millis_wrap_without_rec);
  RUN_TEST(keepalive_timeout_during_stop_remains_terminal_and_never_dispatches_rec);
  RUN_TEST(group_rec_waits_for_keepalive_due_after_fresh_recovery);
  RUN_TEST(stop_action_waits_for_keepalive_due_in_same_composed_pass);
  RUN_TEST(custom_double_recording_intent_admits_rec_then_stop);
  RUN_TEST(camera_admission_revocation_cancels_queued_recovery_before_rec);
  RUN_TEST(superseded_recovery_token_cannot_authorize_rec);
  RUN_TEST(unsupported_peer_retains_error_while_qualified_peer_records);
  RUN_TEST(actual_transport_faults_precede_action_and_only_group_advancement);
  RUN_TEST(composed_adapter_reentry_cannot_advance_managers_twice);
  RUN_TEST(resync_during_recording_preserves_next_button_stop);
  RUN_TEST(manager_reset_discards_previously_admitted_action_without_replay);
  RUN_TEST(actual_button_default_first_rec_second_stop_and_copied_status);
  RUN_TEST(unavailable_peer_does_not_abort_available_rec_or_deadline);
  RUN_TEST(already_recording_startup_queries_without_rec_then_explicit_stop);
  RUN_TEST(stop_during_scan_cancels_late_advertisement_without_rec);
  RUN_TEST(stop_cancels_pending_fresh_query_and_reset_never_replays_rec);
  RUN_TEST(bounded_actions_overflow_refusal_and_batch_stop_have_no_phantom_rec);
  RUN_TEST(custom_short_wake_long_record_and_double_resync_are_preserved);
  RUN_TEST(copied_logger_fault_and_led_backend_error_remain_visible);
  RUN_TEST(first_recording_action_recovers_fresh_then_next_stops);
  RUN_TEST(composed_pass_publishes_group_once_after_all_transport_events);
  RUN_TEST(default_disabled_never_starts_host_or_admits_connect);
  RUN_TEST(required_management_and_classic_routes_are_declared);
  RUN_TEST(full_pairing_observes_state_and_shutter_requires_encoding_query);
  RUN_TEST(duplicate_matching_response_records_one_wire_ack);
  RUN_TEST(profile_reply_at_or_after_deadline_cannot_publish_ack);
  RUN_TEST(group_route_keeps_shutter_ack_distinct_from_recording);
  RUN_TEST(identical_unsolicited_encoding_observations_are_each_preserved);
  RUN_TEST(composed_session_finishes_camera_after_final_owner_access);
  RUN_TEST(stopped_session_cannot_detach_later_same_group_binding);
  RUN_TEST(inactive_or_refused_session_closes_storage_without_servicing_independent_adapter);
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
  RUN_TEST(unknown_control_claim_cannot_grant_control_or_replay_recording);
  RUN_TEST(stop_ack_with_encoding_still_true_never_confirms_stopped_or_retries_shutter);
  RUN_TEST(wake_only_recovery_reaches_stopped_ready_without_video_or_shutter);
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

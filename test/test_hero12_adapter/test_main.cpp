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
static std::vector<std::vector<std::string>> cameraRows(const std::string &text) {
  std::vector<std::vector<std::string>> rows;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.compare(0, 7, "camera,") != 0)
      continue;
    std::vector<std::string> row;
    std::istringstream fields(line);
    std::string field;
    while (std::getline(fields, field, ','))
      row.push_back(field);
    rows.push_back(row);
  }
  return rows;
}

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
  void finishBound(RecordingManager &group, CameraEventLogger &logger) {
    adapter.stop();
    for (unsigned n = 0; n < 8 && !adapter.canDestroy(); ++n)
      adapter.service();
    TEST_ASSERT_TRUE(adapter.canDestroy());
    manager.detachAudit(&logger);
    adapter.detachGroup(&group);
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
            if (!host.ignore_shutter_effect) {
              if (host.independent_encoding)
                host.encodings[ctx.peer] = payload[2] == 1;
              else
                host.encoding = payload[2] == 1;
            }
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
                   static_cast<uint8_t>(
                       payload[1] == 10
                           ? (host.independent_encoding ? host.encodings[ctx.peer] : host.encoding)
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
void profile_reply_at_or_after_deadline_cannot_publish_ack() {
  for (uint32_t start : {uint32_t(100), uint32_t(UINT32_MAX - 100)}) {
    for (uint32_t delay : {uint32_t(1999), uint32_t(2000), uint32_t(2001)}) {
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
  explicit ControlRig(unsigned peers = 1, const ButtonConfig &buttons = {})
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
int main() {
  UNITY_BEGIN();
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

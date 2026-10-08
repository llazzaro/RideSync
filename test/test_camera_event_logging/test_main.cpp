#include "camera_event_logger.h"
#include "telemetry_admission.h"
#include <sstream>
#include <string>
#include <unity.h>
#include <vector>
using namespace ridesync;

struct FakeClock : Clock {
  uint32_t value = 0;
  uint32_t now() const override { return value; }
};
struct FakeTransport : CameraTransport {
  bool begin(size_t, const CameraConfig &, Operation, Token) override { return true; }
  void cancel(size_t, Token) override {}
  void close(size_t, Token) override {}
};
struct MemorySink : StorageSink {
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
static void drain(Storage &storage) {
  for (unsigned i = 0; i < 500; ++i)
    storage.workerStep();
}
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
void request_ack_without_observation_and_queued_intent_identity() {
  FakeClock raw;
  FakeTransport transport;
  CameraManager manager(raw, transport);
  SourceConfig config;
  config.count = 1;
  config.cameras[0].name = "hero";
  config.cameras[0].family = CameraFamily::GoPro;
  config.cameras[0].model = CameraModel::HERO12_BLACK;
  config.cameras[0].identifier = "01:02:03:04:05:06";
  config.cameras[0].address_type = AddressType::Public;
  TEST_ASSERT_TRUE(manager.configure(config).ok());
  RecordingManager group(manager, raw);
  CameraInbox camera;
  CameraEventLogger logger(camera, raw, 42, group);
  TEST_ASSERT_TRUE(logger.configurePeer(0, 301, CameraModel::HERO12_BLACK));
  manager.attachAudit(logger);
  MemorySink sink;
  Storage storage(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::CameraV3});
  SessionClock clock(raw, 42, 1000);
  ImuInbox imu;
  TelemetryAdmission admission(clock, storage, imu, &camera);
  TEST_ASSERT_EQUAL_INT(CameraError::None, manager.request(0, Operation::Connect));
  Token connect = manager.state(0)->token;
  Event ready(0, connect, EventKind::Completed);
  ready.capabilities.start = ready.capabilities.stop = ready.capabilities.query =
      CapabilityState::Supported;
  TEST_ASSERT_TRUE(group.event(ready));
  for (unsigned i = 0; i < 500; ++i) {
    admission.tick();
    storage.workerStep();
  }
  TEST_ASSERT_EQUAL_INT(CameraError::None, manager.request(0, Operation::Start));
  Token active = manager.state(0)->token;
  TEST_ASSERT_EQUAL_INT(CameraError::None, manager.request(0, Operation::Stop));
  TEST_ASSERT_TRUE(
      manager.wireAck(0, active, CameraAckDomain::Classic, CameraAckAction::ShutterOn));
  TEST_ASSERT_FALSE(
      manager.wireAck(0, active, CameraAckDomain::Classic, CameraAckAction::ShutterOn));
  TEST_ASSERT_FALSE(
      manager.wireAck(0, connect, CameraAckDomain::Classic, CameraAckAction::ShutterOn));
  TEST_ASSERT_EQUAL_INT(CameraError::Unsupported, manager.request(0, Operation::Wake));
  for (unsigned i = 0; i < 500; ++i) {
    admission.tick();
    storage.workerStep();
  }
  auto rows = cameraRows(sink.bytes);
  bool queued = false, start_attempt = false, start_ack = false, stop_attempt = false,
       refused = false;
  for (const auto &row : rows) {
    TEST_ASSERT_EQUAL_STRING("0", row[13].c_str());
    TEST_ASSERT_EQUAL_STRING("301", row[14].c_str());
    if (row[20] == "1" && row[21] == "2")
      queued = row[17] == "3" && row[19] == "0";
    if (row[20] == "3" && row[21] == "1")
      start_attempt = row[17] == "2" && row[19] == std::to_string(active.operation);
    if (row[20] == "4" && row[25] == "12")
      start_ack = row[17] == "2" && row[19] == std::to_string(active.operation);
    if (row[20] == "3" && row[21] == "2")
      stop_attempt = true;
    if (row[20] == "2" && row[21] == "4")
      refused = row[17] == "0" && row[22] == std::to_string((int)CameraError::Unsupported);
  }
  TEST_ASSERT_TRUE(queued);
  TEST_ASSERT_TRUE(start_attempt);
  TEST_ASSERT_TRUE(start_ack);
  TEST_ASSERT_FALSE(stop_attempt);
  TEST_ASSERT_TRUE(refused);
  TEST_ASSERT_EQUAL_UINT32(1, storage.kindHealth(RecordKind::Camera).flushed > 0);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, sink.bytes.find("#ridesync_telemetry,3"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, sink.bytes.find(",301,"));
  TEST_ASSERT_EQUAL(std::string::npos, sink.bytes.find("01:02:03:04:05:06"));
  TEST_ASSERT_EQUAL(std::string::npos, sink.bytes.find("observed,recording"));
}
void camera_pressure_preserves_gps_and_final_publication() {
  FakeClock raw;
  SessionClock clock(raw, 42, 1000);
  MemorySink sink;
  Storage storage(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::CameraV3});
  CameraInbox camera;
  ImuInbox imu;
  TelemetryAdmission admission(clock, storage, imu, &camera);
  CameraEvidence e;
  e.session_id = 42;
  e.peer_id = 301;
  e.model = CameraModel::HERO12_BLACK;
  e.kind = CameraEventKind::RequestAccepted;
  e.operation = Operation::Start;
  e.intent_id = 1;
  for (unsigned i = 0; i < CameraInbox::kCapacity; ++i)
    TEST_ASSERT_TRUE(camera.publish(e));
  TEST_ASSERT_FALSE(camera.publish(e));
  ImuBatch batch;
  batch.count = 1;
  batch.records[0].session_id = 42;
  batch.records[0].kind = RecordKind::ImuHealth;
  TEST_ASSERT_TRUE(imu.publish(batch));
  ModemSnapshot gps;
  gps.session_id = 42;
  TEST_ASSERT_TRUE(admission.gps(gps));
  admission.requestStop();
  camera.finish();
  imu.finish();
  while (!admission.stopped())
    admission.tick();
  drain(storage);
  TEST_ASSERT_TRUE(storage.health().stopped);
  TEST_ASSERT_EQUAL_UINT32(1, storage.kindHealth(RecordKind::Gps).flushed);
  TEST_ASSERT_EQUAL_UINT32(1, storage.kindHealth(RecordKind::ImuHealth).flushed);
  TEST_ASSERT_TRUE(storage.kindHealth(RecordKind::Camera).dropped >= 1);
}
void camera_final_publication_after_empty_owner_pass_is_drained() {
  FakeClock raw;
  SessionClock clock(raw, 42, 1000);
  MemorySink sink;
  Storage storage(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::CameraV3});
  CameraInbox camera;
  ImuInbox imu;
  TelemetryAdmission admission(clock, storage, imu, &camera);
  admission.requestStop();
  TEST_ASSERT_TRUE(camera.stopRequested());
  TEST_ASSERT_EQUAL_UINT8(0, admission.tick());
  TEST_ASSERT_FALSE(admission.stopped());
  CameraEvidence final;
  final.session_id = 42;
  final.peer_id = 301;
  final.model = CameraModel::HERO12_BLACK;
  final.kind = CameraEventKind::Failed;
  final.operation = Operation::Start;
  final.intent_id = 7;
  final.connection_generation = 2;
  final.operation_generation = 9;
  final.error = CameraError::Timeout;
  TEST_ASSERT_TRUE(camera.publish(final));
  camera.finish();
  imu.finish();
  TEST_ASSERT_EQUAL_UINT8(1, admission.tick());
  TEST_ASSERT_TRUE(admission.stopped());
  drain(storage);
  TEST_ASSERT_EQUAL_UINT32(1, storage.kindHealth(RecordKind::Camera).flushed);
  TEST_ASSERT_TRUE(storage.health().stopped);
}
void cancelled_operation_rejects_late_command_observation() {
  FakeClock raw;
  FakeTransport transport;
  CameraManager manager(raw, transport);
  SourceConfig config;
  config.count = 1;
  config.cameras[0].name = "hero";
  config.cameras[0].family = CameraFamily::GoPro;
  config.cameras[0].model = CameraModel::HERO12_BLACK;
  config.cameras[0].identifier = "01:02:03:04:05:06";
  config.cameras[0].address_type = AddressType::Public;
  TEST_ASSERT_TRUE(manager.configure(config).ok());
  TEST_ASSERT_EQUAL_INT(CameraError::None, manager.request(0, Operation::Connect));
  Event ready(0, manager.state(0)->token, EventKind::Completed);
  ready.capabilities.start = CapabilityState::Supported;
  TEST_ASSERT_TRUE(manager.event(ready));
  TEST_ASSERT_EQUAL_INT(CameraError::None, manager.request(0, Operation::Start));
  const Token token = manager.state(0)->token;
  TEST_ASSERT_EQUAL_INT(CameraError::None, manager.cancel(0));
  Event late(0, token, EventKind::CommandRecordingObserved);
  late.recording = RecordingState::Recording;
  TEST_ASSERT_FALSE(manager.event(late));
  TEST_ASSERT_EQUAL_INT(RecordingState::Unknown, manager.state(0)->observed);
}
void group_confirm_observation_after_completion_keeps_original_intent() {
  FakeClock raw;
  FakeTransport transport;
  CameraManager manager(raw, transport);
  SourceConfig config;
  config.count = 1;
  config.cameras[0].name = "hero";
  config.cameras[0].family = CameraFamily::GoPro;
  config.cameras[0].model = CameraModel::HERO12_BLACK;
  config.cameras[0].identifier = "01:02:03:04:05:06";
  config.cameras[0].address_type = AddressType::Public;
  TEST_ASSERT_TRUE(manager.configure(config).ok());
  RecordingManager group(manager, raw);
  CameraInbox camera;
  CameraEventLogger logger(camera, raw, 42, group);
  TEST_ASSERT_TRUE(logger.configurePeer(0, 301, CameraModel::HERO12_BLACK));
  TEST_ASSERT_TRUE(manager.attachAudit(logger));
  MemorySink sink;
  Storage storage(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::CameraV3});
  SessionClock clock(raw, 42, 1000);
  ImuInbox imu;
  TelemetryAdmission admission(clock, storage, imu, &camera);
  TEST_ASSERT_EQUAL_INT(CameraError::None, manager.request(0, Operation::Connect));
  Event ready(0, manager.state(0)->token, EventKind::Completed);
  ready.capabilities.start = CapabilityState::Supported;
  TEST_ASSERT_TRUE(group.event(ready));
  Event initial(0, manager.state(0)->token.connection, EventKind::RecordingObserved);
  initial.recording = RecordingState::Stopped;
  TEST_ASSERT_TRUE(group.event(initial));
  for (unsigned i = 0; i < 500; ++i) {
    admission.tick();
    storage.workerStep();
  }
  TEST_ASSERT_EQUAL_INT(GroupError::None, group.request(RecordingState::Recording));
  const Token start = manager.state(0)->token;
  TEST_ASSERT_TRUE(group.event(Event(0, start, EventKind::Completed)));
  Event observed(0, start, EventKind::CommandRecordingObserved);
  observed.recording = RecordingState::Recording;
  TEST_ASSERT_TRUE(group.event(observed));
  TEST_ASSERT_EQUAL_UINT32(1, group.status().recording);
  TEST_ASSERT_FALSE(group.status().peers[0].pending);
  for (unsigned i = 0; i < 500; ++i) {
    admission.tick();
    storage.workerStep();
  }
  bool correlated = false;
  for (const auto &row : cameraRows(sink.bytes))
    if (row[20] == "5" && row[23] == "2")
      correlated = row[17] == "2" && row[19] == std::to_string(start.operation);
  TEST_ASSERT_TRUE(correlated);
}
void camera_rows_reject_unqualified_identity_and_mismatched_ack_domain() {
  MemorySink sink;
  Storage storage(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::CameraV3});
  RecordTimestamp t;
  t.session_id = 42;
  t.monotonic_quality = MonotonicQuality::Valid;
  CameraEvidence e;
  e.session_id = 42;
  e.peer_id = 301;
  e.model = CameraModel::HERO12_BLACK;
  e.kind = CameraEventKind::WireAck;
  e.operation = Operation::Start;
  e.intent_id = 1;
  e.connection_generation = 2;
  e.operation_generation = 3;
  e.ack_domain = CameraAckDomain::Classic;
  e.ack_action = CameraAckAction::ShutterOn;
  TEST_ASSERT_TRUE(storage.enqueueCamera(t, e));
  e.model = static_cast<CameraModel>(99);
  TEST_ASSERT_FALSE(storage.enqueueCamera(t, e));
  e.model = CameraModel::HERO12_BLACK;
  e.ack_domain = CameraAckDomain::Protobuf;
  TEST_ASSERT_FALSE(storage.enqueueCamera(t, e));
  e.ack_domain = CameraAckDomain::Classic;
  e.kind = CameraEventKind::RequestQueued;
  e.ack_action = CameraAckAction::None;
  e.ack_domain = CameraAckDomain::None;
  TEST_ASSERT_FALSE(storage.enqueueCamera(t, e));
  e.kind = CameraEventKind::Attempt;
  e.operation_generation = 0;
  TEST_ASSERT_FALSE(storage.enqueueCamera(t, e));
  TEST_ASSERT_EQUAL_UINT32(4, storage.kindHealth(RecordKind::Camera).rejected);
}
void audit_receipt_is_distinct_from_owner_admission_and_wraps() {
  FakeClock raw;
  raw.value = 0xfffffff0u;
  FakeTransport transport;
  CameraManager manager(raw, transport);
  SourceConfig config;
  config.count = 1;
  config.cameras[0].name = "hero";
  config.cameras[0].family = CameraFamily::GoPro;
  config.cameras[0].model = CameraModel::HERO12_BLACK;
  config.cameras[0].identifier = "01:02:03:04:05:06";
  config.cameras[0].address_type = AddressType::Public;
  TEST_ASSERT_TRUE(manager.configure(config).ok());
  RecordingManager group(manager, raw);
  CameraInbox camera;
  CameraEventLogger logger(camera, raw, 42, group);
  TEST_ASSERT_TRUE(logger.configurePeer(0, 301, CameraModel::HERO12_BLACK));
  TEST_ASSERT_TRUE(manager.attachAudit(logger));
  MemorySink sink;
  Storage storage(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::CameraV3});
  SessionClock clock(raw, 42, 1000);
  ImuInbox imu;
  TelemetryAdmission admission(clock, storage, imu, &camera);
  raw.value = 0xfffffff8u;
  TEST_ASSERT_EQUAL_INT(CameraError::None, manager.request(0, Operation::Connect));
  raw.value = 0x10;
  TEST_ASSERT_EQUAL_UINT8(2, admission.tick());
  drain(storage);
  auto rows = cameraRows(sink.bytes);
  TEST_ASSERT_EQUAL_UINT(2, rows.size());
  for (const auto &row : rows) {
    TEST_ASSERT_EQUAL_STRING("32", row[2].c_str());
    TEST_ASSERT_EQUAL_STRING("owner_admission", row[27].c_str());
    TEST_ASSERT_EQUAL_STRING("1", row[28].c_str());
    TEST_ASSERT_EQUAL_STRING("8", row[29].c_str());
    TEST_ASSERT_EQUAL_STRING("24", row[30].c_str());
    TEST_ASSERT_EQUAL_STRING("0", row[31].c_str());
  }
}
void stale_audit_receipt_remains_explicitly_unknown() {
  FakeClock raw;
  SessionClock clock(raw, 42, 1000);
  MemorySink sink;
  Storage storage(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::CameraV3});
  CameraInbox camera;
  ImuInbox imu;
  TelemetryAdmission admission(clock, storage, imu, &camera);
  CameraEvidence e;
  e.session_id = 42;
  e.peer_id = 301;
  e.model = CameraModel::HERO12_BLACK;
  e.kind = CameraEventKind::RequestAccepted;
  e.operation = Operation::Connect;
  e.intent_id = 1;
  e.event_receipt_known = true;
  TEST_ASSERT_TRUE(camera.publish(e));
  raw.value = 60001;
  TEST_ASSERT_EQUAL_UINT8(1, admission.tick());
  drain(storage);
  auto rows = cameraRows(sink.bytes);
  TEST_ASSERT_EQUAL_UINT(1, rows.size());
  TEST_ASSERT_EQUAL_STRING("0", rows[0][28].c_str());
  TEST_ASSERT_EQUAL_STRING("", rows[0][29].c_str());
  TEST_ASSERT_EQUAL_STRING("", rows[0][30].c_str());
}
void generation_exhaustion_never_reuses_a_token() {
  Token token;
  token.connection = UINT32_MAX - Token::kHeadroom;
  token.operation = UINT32_MAX - Token::kHeadroom;
  TEST_ASSERT_FALSE(token.hasRoom());
  for (unsigned i = 0; i < Token::kHeadroom + 2; ++i) {
    Token::advance(token.connection);
    Token::advance(token.operation);
  }
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, token.connection);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, token.operation);
  TEST_ASSERT_FALSE(token.valid());
  TEST_ASSERT_FALSE(token.hasRoom());
}
void pre_session_event_receipt_is_not_translated() {
  FakeClock raw;
  raw.value = 100;
  SessionClock clock(raw, 42, 1000);
  MemorySink sink;
  Storage storage(sink, {42, "fw", "synthetic", 2, 4, StorageFormat::CameraV3});
  CameraInbox camera;
  ImuInbox imu;
  TelemetryAdmission admission(clock, storage, imu, &camera);
  CameraEvidence e;
  e.session_id = 42;
  e.peer_id = 301;
  e.model = CameraModel::HERO12_BLACK;
  e.kind = CameraEventKind::RequestAccepted;
  e.operation = Operation::Connect;
  e.intent_id = 1;
  e.event_receipt_known = true;
  e.event_receipt_raw32 = 90;
  TEST_ASSERT_TRUE(camera.publish(e));
  raw.value = 110;
  TEST_ASSERT_EQUAL_UINT8(1, admission.tick());
  drain(storage);
  const auto rows = cameraRows(sink.bytes);
  TEST_ASSERT_EQUAL_UINT(1, rows.size());
  TEST_ASSERT_EQUAL_STRING("0", rows[0][28].c_str());
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(request_ack_without_observation_and_queued_intent_identity);
  RUN_TEST(camera_pressure_preserves_gps_and_final_publication);
  RUN_TEST(camera_final_publication_after_empty_owner_pass_is_drained);
  RUN_TEST(cancelled_operation_rejects_late_command_observation);
  RUN_TEST(group_confirm_observation_after_completion_keeps_original_intent);
  RUN_TEST(camera_rows_reject_unqualified_identity_and_mismatched_ack_domain);
  RUN_TEST(audit_receipt_is_distinct_from_owner_admission_and_wraps);
  RUN_TEST(stale_audit_receipt_remains_explicitly_unknown);
  RUN_TEST(generation_exhaustion_never_reuses_a_token);
  RUN_TEST(pre_session_event_receipt_is_not_translated);
  return UNITY_END();
}

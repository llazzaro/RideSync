#include "handlebar_control.h"
#include "local_telemetry_runtime.h"
#include "session_storage_owner.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <memory>
#include <string>
#include <unity.h>
#include <vector>
using namespace ridesync;
struct RawClock : Clock {
  uint32_t value = 100;
  uint32_t now() const override { return value; }
};
struct Host : BleHost {
  unsigned calls = 0;
  BleHostState start(bool, bool) override {
    ++calls;
    return BleHostState::Ready;
  }
  BleHostState state() const override { return BleHostState::Ready; }
  BleFault fault() const override { return BleFault::None; }
  void sealStartup() override {}
  BleBondAdmission bondAdmission(const BondIdentity &) override { return {}; }
  int submit(const BleCommand &, BleContext &) override {
    ++calls;
    return 0;
  }
  int retire(BleContext &) override {
    ++calls;
    return 0;
  }
  int cancelScan(BleContext &) override {
    ++calls;
    return 0;
  }
  bool quiescent(const BleContext &) const override { return true; }
  bool releaseContext(BleContext &) override { return true; }
  uint16_t mtu(uint16_t) const override { return 67; }
};
struct Sink : StorageSink {
  std::string bytes;
  bool collision = false, write_failure = false, close_blocked = false;
  unsigned closes = 0;
  bool mount() override { return true; }
  bool openExclusive(const char *) override { return !collision; }
  size_t write(const char *p, size_t n) override {
    if (write_failure)
      return 0;
    bytes.append(p, n);
    return n;
  }
  bool flush() override { return true; }
  void close() override { ++closes; }
  int ioError() const override { return collision ? EEXIST : write_failure ? EIO : 0; }
};
struct Ledger : IdentityLedgerIO {
  std::array<std::vector<uint8_t>, 2> slots;
  unsigned selected = 0;
  size_t offset = 0;
  int err = 0;
  bool open(unsigned s, bool, bool create) override {
    selected = s;
    offset = 0;
    if (create ? !slots[s].empty() : slots[s].empty()) {
      err = create ? EEXIST : ENOENT;
      return false;
    }
    return true;
  }
  int read(uint8_t *b, size_t n) override {
    n = std::min(n, slots[selected].size() - offset);
    if (n)
      memcpy(b, slots[selected].data() + offset, n);
    offset += n;
    return int(n);
  }
  int write(const uint8_t *b, size_t n) override {
    slots[selected].assign(b, b + n);
    return int(n);
  }
  bool sync() override { return true; }
  bool close() override { return true; }
  int error() const override { return err; }
};
struct SdWorker : TelemetryStorageWorker {
  Sink fs;
  Ledger ledger;
  SessionStorageOwner owner;
  bool refuse_task = false, launched = false, finished = false, invalid_id = false,
       hold_finished = false;
  unsigned starts = 0;
  explicit SdWorker(uint32_t ns = 7) : owner(fs, ledger, ns) {
    TEST_ASSERT_EQUAL_INT(IdentityStatus::Committed,
                          SessionIdentityAllocator::commission(ledger, 7).status);
  }
  bool start() override {
    ++starts;
    launched = !refuse_task;
    return launched;
  }
  IdentityAllocation allocation() const override {
    auto a = owner.allocation();
    if (invalid_id && a.status == IdentityStatus::Committed)
      a.id = 0;
    return a;
  }
  StorageSink &sink() override { return fs; }
  bool bind(Storage &s) override { return owner.bind(s); }
  void cancel() override { owner.cancel(); }
  bool workerFinished() const override { return finished && !hold_finished; }
  int ioError() const override { return fs.ioError(); }
  void step() {
    if (launched && !finished)
      finished = owner.workerStep();
  }
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
// Only the acquisition/task boundary is synthetic. Runtime still uses the real
// inbox, admission, clock, Storage and commissioned SD owner.
struct ImuWorker : TelemetryImuWorker {
  ImuInbox *inbox = nullptr;
  uint64_t id = 0;
  bool refuse = false, stopping = false, finished = false, safe_seen = false;
  unsigned starts = 0;
  bool start(ImuInbox &i, uint64_t s, bool safe) override {
    ++starts;
    safe_seen = safe;
    if (refuse || safe)
      return false;
    inbox = &i;
    id = s;
    return true;
  }
  void requestStop() override { stopping = true; }
  ImuWorkerObservation observation() const override {
    ImuWorkerObservation o;
    o.finished = finished;
    o.outcome = DeviceHealth::Ok;
    return o;
  }
  bool publish(uint32_t raw) {
    ImuBatch b;
    b.count = 1;
    b.records[0].session_id = id;
    b.records[0].kind = RecordKind::ImuHealth;
    b.records[0].event_code = 2;
    b.records[0].receipt_known = true;
    b.records[0].receipt_millis32 = raw;
    return inbox->publish(b);
  }
  void finalPublication(uint32_t raw) {
    TEST_ASSERT_TRUE(stopping);
    TEST_ASSERT_TRUE(publish(raw));
    finished = true;
  }
};
LocalTelemetryConfig qualified() {
  LocalTelemetryConfig c;
  c.opt_in = c.gps_qualified = c.imu_enabled = c.imu_qualified = true;
  c.power_timing.qualified = true;
  c.modem.documentary_profile_opt_in = true;
  c.modem.terminal_retires_transaction = true;
  c.gps_record_ms = 10;
  c.modem.poll_ms = 10;
  c.firmware = "test";
  c.provenance = "synthetic";
  return c;
}
struct Rig {
  RawClock raw;
  Host host;
  Hero12Adapter adapter;
  CameraManager manager;
  RecordingManager group;
  Uart uart;
  SdWorker sd;
  ImuWorker imu;
  Rig()
      : adapter(host, raw), manager(raw, adapter, Hero12Adapter::managerPolicy()),
        group(manager, raw) {
    adapter.attach(manager);
  }
  void pass(LocalTelemetryRuntime &r, bool disk = true) {
    r.service();
    if (disk)
      sd.step();
    ++raw.value;
  }
  void finish(LocalTelemetryRuntime &r) {
    r.requestStop();
    if (imu.inbox && !imu.finished)
      imu.finalPublication(raw.value);
    for (unsigned n = 0; n < 1000 && !r.canRelease(); ++n)
      pass(r);
    TEST_ASSERT_TRUE(r.canRelease());
  }
};
void camera_free_session_admits_and_stops_without_camera_service() {
  Rig f;
  CameraEventSession s(f.raw, f.sd.fs, f.adapter, f.manager, f.group, 42, "test", "synthetic");
  TEST_ASSERT_TRUE(s.activateLocal());
  TEST_ASSERT_FALSE(s.activate());
  ImuBatch b;
  b.count = 1;
  b.records[0].session_id = 42;
  b.records[0].kind = RecordKind::ImuHealth;
  TEST_ASSERT_TRUE(s.imuInbox().publish(b));
  s.service();
  TEST_ASSERT_EQUAL_UINT32(1, s.storage().kindHealth(RecordKind::ImuHealth).accepted);
  s.requestStop();
  s.finishImu();
  for (unsigned n = 0; n < 1000 && !s.stopped(); ++n) {
    s.service();
    s.storage().workerStep();
  }
  TEST_ASSERT_TRUE(s.stopped());
  TEST_ASSERT_EQUAL_UINT(0, f.host.calls);
}
void runtime_commits_before_constructing_and_camera_free_logging_reaches_same_worker() {
  Rig f;
  auto c = qualified();
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
  TEST_ASSERT_TRUE(r.start());
  TEST_ASSERT_NULL(r.session());
  f.pass(r);
  f.pass(r);
  TEST_ASSERT_EQUAL_INT(TelemetryPhase::Running, r.status().phase);
  TEST_ASSERT_EQUAL_UINT64(0x700000001ULL, r.status().identity.id);
  TEST_ASSERT_TRUE(r.session()->storage().usesSink(f.sd.fs));
  TEST_ASSERT_TRUE(f.imu.publish(f.raw.value));
  for (unsigned n = 0; n < 300; ++n)
    f.pass(r);
  TEST_ASSERT_TRUE(f.sd.fs.bytes.find("gps,") != std::string::npos);
  TEST_ASSERT_TRUE(f.sd.fs.bytes.find("health,") != std::string::npos);
  TEST_ASSERT_EQUAL_UINT(0, f.host.calls);
  f.finish(r);
  TEST_ASSERT_TRUE(f.sd.finished);
  TEST_ASSERT_EQUAL_UINT(1, f.sd.fs.closes);
}
void failed_storage_task_has_no_pending_worker_lifetime() {
  Rig f;
  f.sd.refuse_task = true;
  auto c = qualified();
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
  TEST_ASSERT_FALSE(r.start());
  TEST_ASSERT_EQUAL_INT(TelemetryFault::StorageTask, r.status().fault);
  TEST_ASSERT_TRUE(r.canRelease());
  TEST_ASSERT_NULL(r.session());
  TEST_ASSERT_EQUAL_UINT(0, f.imu.starts);
}
void safe_mode_reaches_real_policy_and_refused_imu_does_not_pause_gps() {
  Rig f;
  auto c = qualified();
  c.safe_mode = true;
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
  TEST_ASSERT_TRUE(r.start());
  for (unsigned n = 0; n < 300; ++n)
    f.pass(r);
  TEST_ASSERT_TRUE(f.imu.safe_seen);
  TEST_ASSERT_EQUAL_INT(SensorAdmission::SafeModeRefused, r.status().imu);
  TEST_ASSERT_TRUE(f.sd.fs.bytes.find("gps,") != std::string::npos);
  f.finish(r);
}
void stop_waits_for_imu_final_publication_before_storage_close() {
  Rig f;
  auto c = qualified();
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
  TEST_ASSERT_TRUE(r.start());
  for (unsigned n = 0; n < 50; ++n)
    f.pass(r);
  r.requestStop();
  for (unsigned n = 0; n < 100; ++n)
    f.pass(r);
  TEST_ASSERT_FALSE(r.canRelease());
  TEST_ASSERT_EQUAL_UINT(0, f.sd.fs.closes);
  f.imu.finalPublication(f.raw.value);
  for (unsigned n = 0; n < 1000 && !r.canRelease(); ++n)
    f.pass(r);
  TEST_ASSERT_TRUE(r.canRelease());
  TEST_ASSERT_TRUE(f.sd.fs.bytes.find("health,") != std::string::npos);
}
void utc_anchor_uses_source_receipt_not_later_owner_time() {
  RawClock raw;
  SessionClock clock(raw, 42, 1000);
  raw.value = 130;
  UtcDateTime date{2026, 10, 8, 12, 0, 0, 0};
  TEST_ASSERT_TRUE(clock.anchorAtReceipt(date, 10));
  auto t = clock.snapshot();
  TEST_ASSERT_EQUAL_UINT64(10, t.anchor.receipt_ms);
  TEST_ASSERT_EQUAL_UINT64(20, t.anchor_age_ms);
  TEST_ASSERT_EQUAL_INT64(1791460800020LL, t.utc_estimate_ms);
  TEST_ASSERT_FALSE(t.anchor.uncertainty_known);
  TEST_ASSERT_FALSE(clock.anchorAtReceipt(date, 31));
  TEST_ASSERT_EQUAL_UINT32(1, clock.snapshot().anchor.sequence);
}
void qualification_refuses_without_creating_or_polling_workers() {
  for (unsigned missing = 0; missing < 5; ++missing) {
    Rig f;
    auto c = qualified();
    if (missing == 0)
      c.opt_in = false;
    if (missing == 1)
      c.gps_qualified = false;
    if (missing == 2)
      c.imu_qualified = false;
    if (missing == 3)
      c.power_timing.qualified = false;
    if (missing == 4)
      c.gps_record_ms = 0;
    LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
    TEST_ASSERT_FALSE(r.start());
    r.service();
    r.requestStop();
    TEST_ASSERT_EQUAL_INT(TelemetryFault::Qualification, r.status().fault);
    TEST_ASSERT_EQUAL_UINT(0, f.sd.starts);
    TEST_ASSERT_TRUE(r.canRelease());
    TEST_ASSERT_TRUE(f.uart.tx.empty());
  }
}
void imu_task_refusal_leaves_gps_and_stop_eligible() {
  Rig f;
  f.imu.refuse = true;
  auto c = qualified();
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
  TEST_ASSERT_TRUE(r.start());
  for (unsigned n = 0; n < 300; ++n)
    f.pass(r);
  TEST_ASSERT_EQUAL_INT(SensorAdmission::TaskRefused, r.status().imu);
  TEST_ASSERT_TRUE(r.status().kinds[0].accepted > 0);
  f.finish(r);
}
void nvs_refusal_and_empty_camera_configuration_still_log_local_data() {
  Rig f;
  auto c = qualified();
  SettingsSnapshot snapshot;
  snapshot.completed = true;
  snapshot.load = PersistStatus::Refused;
  SettingsQualification q;
  q.local_telemetry = true;
  q.nvs_allowed = false;
  const auto admitted = admitSettings(snapshot, q);
  TEST_ASSERT_TRUE(admitted.local_telemetry);
  TEST_ASSERT_FALSE(admitted.cameras);
  c.cameras_qualified = admitted.cameras;
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
  TEST_ASSERT_TRUE(r.start());
  for (unsigned n = 0; n < 300; ++n)
    f.pass(r);
  TEST_ASSERT_EQUAL_INT(CameraAdmission::Disabled, r.status().camera);
  TEST_ASSERT_TRUE(r.status().kinds[0].written > 0);
  f.finish(r);
}
void identity_failure_and_invalid_committed_id_never_construct_session() {
  for (bool invalid : {false, true}) {
    Rig f;
    auto c = qualified();
    f.sd.invalid_id = invalid;
    if (!invalid)
      f.sd.ledger.slots[1].clear();
    LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
    TEST_ASSERT_TRUE(r.start());
    for (unsigned n = 0; n < 20; ++n)
      f.pass(r);
    TEST_ASSERT_EQUAL_INT(TelemetryFault::Identity, r.status().fault);
    TEST_ASSERT_TRUE(r.canRelease());
    TEST_ASSERT_NULL(r.session());
    TEST_ASSERT_TRUE(f.uart.tx.empty());
    TEST_ASSERT_EQUAL_UINT(0, f.imu.starts);
  }
}
void pending_stop_and_final_sd_barrier_retain_resources() {
  Rig f;
  auto c = qualified();
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
  TEST_ASSERT_TRUE(r.start());
  r.requestStop();
  r.service();
  TEST_ASSERT_FALSE(r.canRelease());
  TEST_ASSERT_NULL(r.session());
  f.sd.hold_finished = true;
  f.sd.step();
  r.service();
  TEST_ASSERT_FALSE(r.canRelease());
  TEST_ASSERT_TRUE(f.sd.finished);
  f.sd.hold_finished = false;
  r.service();
  TEST_ASSERT_TRUE(r.canRelease());
}
void media_collision_is_terminal_and_never_retries_storage_or_identity() {
  for (bool collision : {false, true}) {
    Rig f;
    auto c = qualified();
    f.sd.fs.collision = collision;
    f.sd.fs.write_failure = !collision;
    LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
    TEST_ASSERT_TRUE(r.start());
    for (unsigned n = 0; n < 50; ++n)
      f.pass(r);
    TEST_ASSERT_TRUE(r.status().storage.terminal);
    TEST_ASSERT_EQUAL_INT(collision ? EEXIST : EIO, r.status().storage_error);
    TEST_ASSERT_EQUAL_INT(TelemetryPhase::Stopping, r.status().phase);
    TEST_ASSERT_FALSE(r.start());
    f.finish(r);
    TEST_ASSERT_EQUAL_UINT(1, f.sd.starts);
  }
}
void camera_bursts_share_quota_and_preserve_gps_reservation() {
  Rig f;
  auto c = qualified();
  c.cameras_qualified = true;
  SourceConfig source;
  source.count = 1;
  source.cameras[0].name = "hero";
  source.cameras[0].family = CameraFamily::GoPro;
  source.cameras[0].model = CameraModel::HERO12_BLACK;
  source.cameras[0].identifier = "01:02:03:04:05:06";
  source.cameras[0].address_type = AddressType::Public;
  TEST_ASSERT_TRUE(f.manager.configure(source).ok());
  c.peers.count = 1;
  c.peers.entries[0].id = 301;
  c.peers.entries[0].model = CameraModel::HERO12_BLACK;
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
  TEST_ASSERT_TRUE(r.start());
  for (unsigned n = 0; n < 2; ++n)
    f.pass(r);
  TEST_ASSERT_EQUAL_INT(CameraAdmission::Admitted, r.status().camera);
  auto *session = r.session();
  CameraEvidence e;
  e.session_id = r.status().identity.id;
  e.peer_id = 301;
  e.model = CameraModel::HERO12_BLACK;
  e.kind = CameraEventKind::Disconnected;
  for (unsigned n = 0; n < 40; ++n)
    session->cameraInbox().publish(e);
  for (unsigned n = 0; n < 12; ++n)
    f.imu.publish(f.raw.value);
  const auto before = r.status();
  f.raw.value += 10;
  f.pass(r, false);
  auto after = r.status();
  TEST_ASSERT_EQUAL_UINT32(before.kinds[0].accepted + 1, after.kinds[0].accepted);
  TEST_ASSERT_EQUAL_UINT32(1, after.kinds[static_cast<unsigned>(RecordKind::Camera)].accepted);
  TEST_ASSERT_EQUAL_UINT32(1, after.kinds[static_cast<unsigned>(RecordKind::ImuHealth)].accepted);
  TEST_ASSERT_TRUE(after.kinds[static_cast<unsigned>(RecordKind::Camera)].dropped >= 24);
  TEST_ASSERT_TRUE(after.imu_dropped[3] >= 8);
  for (unsigned n = 0; n < 300; ++n) {
    f.manager.request(0, Operation::Connect);
    f.pass(r);
  }
  f.finish(r);
  TEST_ASSERT_TRUE(f.sd.fs.bytes.find("camera,") != std::string::npos);
  TEST_ASSERT_TRUE(f.sd.fs.bytes.find("gps,") != std::string::npos);
  TEST_ASSERT_TRUE(f.sd.fs.bytes.find("health,") != std::string::npos);
}
void actual_modem_no_fix_fix_staleness_and_errors_keep_provenance() {
  Rig f;
  auto c = qualified();
  c.imu_enabled = false;
  c.modem.stale_ms = 50;
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
  TEST_ASSERT_TRUE(r.start());
  f.pass(r);
  f.pass(r);
  TEST_ASSERT_EQUAL_STRING("AT\r", f.uart.tx.c_str());
  f.uart.tx.clear();
  f.uart.rx = "OK\r\n";
  f.pass(r);
  TEST_ASSERT_EQUAL_STRING("AT+CGNSSPWR=1\r", f.uart.tx.c_str());
  f.uart.tx.clear();
  f.uart.rx = "OK\r\n+CGNSSPWR: READY!\r\n";
  f.pass(r);
  TEST_ASSERT_EQUAL_STRING("AT+CGPSINFO\r", f.uart.tx.c_str());
  f.uart.tx.clear();
  f.uart.rx = "+CGPSINFO: ,,,,,,,,\r\nOK\r\n";
  f.pass(r);
  TEST_ASSERT_EQUAL_INT(FixValidity::NoFix, r.status().gps.validity);
  TEST_ASSERT_EQUAL_INT(AnchorQuality::Missing, r.status().timestamp.anchor_quality);
  TEST_ASSERT_FALSE(r.status().gps.fix.valid);
  f.raw.value += 10;
  f.pass(r);
  f.uart.rx = "+CGPSINFO: 3723.247500,N,12158.341600,W,081026,120000.00,10.0,0.0,0.0\r\nOK\r\n";
  for (unsigned n = 0; n < 4; ++n)
    f.pass(r);
  auto fixed = r.status();
  TEST_ASSERT_EQUAL_INT(FixValidity::Valid, fixed.gps.validity);
  TEST_ASSERT_TRUE(fixed.timestamp.anchor.sequence > 0);
  TEST_ASSERT_EQUAL_UINT64(fixed.gps.fix.receipt_monotonic_ms, fixed.timestamp.anchor.receipt_ms);
  const auto sequence = fixed.timestamp.anchor.sequence;
  f.raw.value += 51;
  f.pass(r);
  TEST_ASSERT_EQUAL_INT(FixValidity::Stale, r.status().gps.validity);
  TEST_ASSERT_EQUAL_UINT32(sequence, r.status().timestamp.anchor.sequence);
  f.uart.rx = "ERROR\r\n";
  f.pass(r);
  TEST_ASSERT_EQUAL_INT(UartHealth::ProtocolError, r.status().gps.health);
  TEST_ASSERT_EQUAL_INT(FixValidity::Invalid, r.status().gps.validity);
  f.finish(r);
}
struct FailedSensor : ImuPort {
  bool begin(ImuConfig &) override { return false; }
  bool read(uint8_t *, uint16_t, uint16_t &, uint8_t &) override { return false; }
  bool flush() override { return false; }
};
struct ManagedImu : TelemetryImuWorker {
  FailedSensor sensor;
  HealthProgress progress;
  std::unique_ptr<ImuManager> manager;
  bool finished = false;
  bool start(ImuInbox &inbox, uint64_t id, bool safe) override {
    if (safe)
      return false;
    manager.reset(new ImuManager(sensor, inbox, progress, id, ImuConfig{}));
    return true;
  }
  void requestStop() override { manager->stop(); }
  ImuWorkerObservation observation() const override {
    ImuWorkerObservation o;
    o.outcome = progress.outcome();
    o.completed = progress.generation();
    o.finished = finished;
    if (finished) {
      o.manager = manager->health();
      o.codec = manager->codecHealth();
    }
    return o;
  }
  void step(uint32_t raw) {
    manager->step(raw);
    finished = progress.isFinished();
  }
};
void actual_sensor_terminal_outcome_is_copied_and_gps_continues() {
  Rig f;
  ManagedImu imu;
  auto c = qualified();
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, imu, f.adapter, f.manager, f.group, c);
  TEST_ASSERT_TRUE(r.start());
  f.pass(r);
  f.pass(r);
  imu.step(f.raw.value);
  f.pass(r);
  TEST_ASSERT_EQUAL_INT(DeviceHealth::Missing, r.status().imu_worker.outcome);
  for (unsigned n = 0; n < 300; ++n) {
    imu.step(f.raw.value);
    f.pass(r);
  }
  TEST_ASSERT_EQUAL_INT(DeviceHealth::RetryExhausted, r.status().imu_worker.outcome);
  TEST_ASSERT_TRUE(r.status().kinds[0].written > 0);
  r.requestStop();
  imu.step(f.raw.value);
  for (unsigned n = 0; n < 1000 && !r.canRelease(); ++n)
    f.pass(r);
  TEST_ASSERT_TRUE(r.canRelease());
  TEST_ASSERT_EQUAL_UINT32(1, r.status().imu_worker.manager.init_errors);
  TEST_ASSERT_TRUE(f.sd.fs.bytes.find("health,") != std::string::npos);
}
struct TrackingPower : GnssPowerControl {
  bool active = false;
  unsigned enabled = 0, asserted = 0, released = 0;
  void enableSupply() override { ++enabled; }
  void key(bool value) override {
    active = value;
    value ? ++asserted : ++released;
  }
};
void runtime_stop_before_gps_startup_never_touches_power_or_uart() {
  Rig f;
  TrackingPower power;
  auto c = qualified();
  c.power_timing.key_active_ms = c.power_timing.settle_ms = 1000;
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c, &power);
  TEST_ASSERT_TRUE(r.start());
  r.requestStop();
  for (unsigned i = 0; i < 100 && !r.canRelease(); ++i)
    f.pass(r);
  TEST_ASSERT_TRUE(r.canRelease());
  TEST_ASSERT_NULL(r.session());
  for (unsigned i = 0; i < 20; ++i) {
    r.requestStop();
    f.pass(r);
  }
  TEST_ASSERT_EQUAL_UINT(0, power.enabled);
  TEST_ASSERT_EQUAL_UINT(0, power.asserted);
  TEST_ASSERT_EQUAL_UINT(0, power.released);
  TEST_ASSERT_TRUE(f.uart.tx.empty());
}
// A stop during the pulse must cancel it immediately, while final publication
// and actual SD last-access still retain all resources. Each media variant
// reaches the same production requestStop path without a manual stop trigger.
static void powerStop(unsigned media) {
  Rig f;
  TrackingPower power;
  auto c = qualified();
  c.power_timing.key_active_ms = c.power_timing.settle_ms = 1000;
  f.sd.fs.collision = media == 1;
  f.sd.fs.write_failure = media == 2;
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c, &power);
  TEST_ASSERT_TRUE(r.start());
  f.pass(r);
  f.pass(r);
  TEST_ASSERT_TRUE(power.active);
  TEST_ASSERT_TRUE(f.uart.tx.empty());
  if (!media)
    r.requestStop();
  else
    for (unsigned i = 0; i < 50 && r.status().phase == TelemetryPhase::Running; ++i)
      f.pass(r);
  TEST_ASSERT_EQUAL_INT(TelemetryPhase::Stopping, r.status().phase);
  TEST_ASSERT_FALSE(power.active);
  TEST_ASSERT_EQUAL_UINT(1, power.released);
  TEST_ASSERT_FALSE(r.canRelease());
  for (unsigned i = 0; i < 20; ++i) {
    r.requestStop();
    f.pass(r);
  }
  TEST_ASSERT_FALSE(r.canRelease());
  TEST_ASSERT_EQUAL_UINT(1, power.enabled);
  TEST_ASSERT_EQUAL_UINT(1, power.asserted);
  TEST_ASSERT_EQUAL_UINT(1, power.released);
  TEST_ASSERT_TRUE(f.uart.tx.empty());
  f.sd.hold_finished = true;
  f.imu.finalPublication(f.raw.value);
  for (unsigned i = 0; i < 100; ++i)
    f.pass(r);
  TEST_ASSERT_FALSE(r.canRelease());
  TEST_ASSERT_TRUE(f.sd.finished);
  f.sd.hold_finished = false;
  f.pass(r);
  TEST_ASSERT_TRUE(r.canRelease());
  for (unsigned i = 0; i < 20; ++i) {
    r.requestStop();
    f.raw.value += 2000;
    f.pass(r);
  }
  TEST_ASSERT_FALSE(power.active);
  TEST_ASSERT_EQUAL_UINT(1, power.released);
  TEST_ASSERT_TRUE(f.uart.tx.empty());
}
void manual_stop_releases_key_before_imu_and_sd_barriers() { powerStop(0); }
void collision_stop_releases_key_before_imu_and_sd_barriers() { powerStop(1); }
void write_failure_stop_releases_key_before_imu_and_sd_barriers() { powerStop(2); }
void runtime_stop_during_settling_or_ready_does_not_restart_or_repeat_gpio() {
  for (bool ready : {false, true}) {
    Rig f;
    TrackingPower power;
    auto c = qualified();
    c.power_timing.key_active_ms = c.power_timing.settle_ms = 20;
    LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c, &power);
    TEST_ASSERT_TRUE(r.start());
    f.pass(r);
    f.pass(r);
    f.raw.value += 20;
    f.pass(r);
    TEST_ASSERT_FALSE(power.active);
    if (ready) {
      f.raw.value += 20;
      f.pass(r);
      for (const char *reply : {"OK\r\n", "OK\r\n+CGNSSPWR: READY!\r\n"}) {
        f.uart.rx = reply;
        f.pass(r);
      }
      TEST_ASSERT_TRUE(r.status().gps.receiver_ready);
    }
    const auto sent = f.uart.tx;
    f.finish(r);
    for (unsigned i = 0; i < 20; ++i) {
      r.requestStop();
      f.raw.value += 1000;
      f.pass(r);
    }
    TEST_ASSERT_EQUAL_UINT(1, power.enabled);
    TEST_ASSERT_EQUAL_UINT(1, power.asserted);
    TEST_ASSERT_EQUAL_UINT(1, power.released);
    TEST_ASSERT_FALSE(power.active);
    TEST_ASSERT_EQUAL_STRING(sent.c_str(), f.uart.tx.c_str());
  }
}
void setUp() {}
void tearDown() {}
struct ControlInput : ButtonInput {
  bool pressed() override { return false; }
};
struct ControlLed : LedSink {
  unsigned writes = 0;
  int write(LedFrame) override {
    ++writes;
    return 0;
  }
};
void runtime_control_reuses_one_camera_pass_and_copied_local_observations() {
  Rig f;
  SourceConfig cameras;
  cameras.count = 1;
  cameras.cameras[0].name = "hero";
  cameras.cameras[0].family = CameraFamily::GoPro;
  cameras.cameras[0].model = CameraModel::HERO12_BLACK;
  cameras.cameras[0].identifier = "01:02:03:04:05:06";
  cameras.cameras[0].address_type = AddressType::Public;
  TEST_ASSERT_TRUE(f.manager.configure(cameras).ok());
  auto c = qualified();
  c.cameras_qualified = true;
  c.peers.count = 1;
  c.peers.entries[0].slot = 0;
  c.peers.entries[0].id = 300;
  c.peers.entries[0].model = CameraModel::HERO12_BLACK;
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
  ControlInput input;
  ControlLed sink;
  HandlebarControl control(f.raw, f.adapter, f.manager, f.group, input, sink);
  TEST_ASSERT_TRUE(control.begin({}));
  TEST_ASSERT_TRUE(r.attachControl(control));
  TEST_ASSERT_EQUAL_INT(ControlAdmission::Inactive, control.submit(ButtonAction::RecordingIntent));
  TEST_ASSERT_TRUE(r.start());
  f.pass(r);
  f.pass(r);
  TEST_ASSERT_EQUAL_INT(CameraAdmission::Admitted, r.status().camera);
  TEST_ASSERT_EQUAL_INT(ControlAdmission::Admitted, control.submit(ButtonAction::RecordingIntent));
  const auto ticks = f.manager.ticks(), advances = f.group.advancements();
  f.pass(r);
  TEST_ASSERT_EQUAL_UINT32(ticks + 1, f.manager.ticks());
  TEST_ASSERT_EQUAL_UINT32(advances + 1, f.group.advancements());
  TEST_ASSERT_EQUAL_INT(RecordingState::Recording, control.status().group.intent);
  TEST_ASSERT_EQUAL_INT(CameraError::Disabled, control.status().group.peers[0].error);
  TEST_ASSERT_EQUAL_UINT64(r.status().timestamp.session_id,
                           control.status().local.timestamp.session_id);
  TEST_ASSERT_EQUAL_UINT32(r.status().storage.accepted, control.status().local.storage.accepted);
  TEST_ASSERT_TRUE(sink.writes > 0);
  const auto logged = r.status().storage.accepted;
  for (unsigned n = 0; n < 40; ++n)
    f.pass(r);
  TEST_ASSERT_TRUE(r.status().storage.accepted > logged);
  control.submit(ButtonAction::RecordingIntent);
  f.finish(r);
  TEST_ASSERT_EQUAL_INT(ControlAdmission::Inactive, control.submit(ButtonAction::RecordingIntent));
  TEST_ASSERT_EQUAL_INT(TelemetryPhase::Finished, control.status().local.phase);
  r.detachControl(control);
}
void local_camera_refusal_keeps_control_ineligible_and_local_storage_running() {
  Rig f;
  auto c = qualified();
  c.safe_mode = true;
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
  ControlInput input;
  ControlLed sink;
  HandlebarControl control(f.raw, f.adapter, f.manager, f.group, input, sink);
  TEST_ASSERT_TRUE(control.begin({}));
  TEST_ASSERT_TRUE(r.attachControl(control));
  TEST_ASSERT_TRUE(r.start());
  f.pass(r);
  f.pass(r);
  TEST_ASSERT_EQUAL_INT(ControlAdmission::Refused, control.submit(ButtonAction::RecordingIntent));
  const auto ticks = f.manager.ticks();
  for (unsigned n = 0; n < 40; ++n)
    f.pass(r);
  TEST_ASSERT_EQUAL_UINT32(ticks, f.manager.ticks());
  TEST_ASSERT_TRUE(r.status().storage.accepted > 0);
  TEST_ASSERT_EQUAL_INT(SensorAdmission::SafeModeRefused, control.status().local.imu);
  f.finish(r);
  r.detachControl(control);
}
void control_copies_terminal_startup_refusal_even_without_worker() {
  Rig f;
  auto c = qualified();
  c.gps_qualified = false;
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
  ControlInput input;
  ControlLed sink;
  HandlebarControl control(f.raw, f.adapter, f.manager, f.group, input, sink);
  TEST_ASSERT_TRUE(control.begin({}));
  TEST_ASSERT_TRUE(r.attachControl(control));
  TEST_ASSERT_FALSE(r.start());
  r.service();
  const auto status = control.status();
  r.detachControl(control);
  TEST_ASSERT_EQUAL_INT(TelemetryPhase::Refused, status.local.phase);
  TEST_ASSERT_EQUAL_INT(TelemetryFault::Qualification, status.local.fault);
  TEST_ASSERT_EQUAL_INT(LedState::Error, status.led);
}
void blocked_identity_startup_cancels_at_deadline_without_releasing_worker() {
  Rig f;
  auto c = qualified();
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
  TEST_ASSERT_TRUE(r.start());
  f.raw.value += 999;
  r.service();
  TEST_ASSERT_EQUAL_INT(TelemetryPhase::Allocating, r.status().phase);
  ++f.raw.value;
  r.service();
  TEST_ASSERT_EQUAL_INT(TelemetryFault::StartupTimeout, r.status().fault);
  TEST_ASSERT_EQUAL_INT(TelemetryPhase::Stopping, r.status().phase);
  TEST_ASSERT_FALSE(r.canRelease());
  TEST_ASSERT_NULL(r.session());
  TEST_ASSERT_EQUAL_UINT(0, f.imu.starts);
  TEST_ASSERT_TRUE(f.uart.tx.empty());
  f.finish(r);
}
void late_identity_completion_cannot_bypass_owner_startup_deadline() {
  Rig f;
  auto c = qualified();
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
  TEST_ASSERT_TRUE(r.start());
  f.sd.step(); // committed, but not yet admitted by the application owner
  TEST_ASSERT_EQUAL_INT(IdentityStatus::Committed, f.sd.allocation().status);
  f.raw.value += 1000;
  r.service();
  TEST_ASSERT_EQUAL_INT(TelemetryFault::StartupTimeout, r.status().fault);
  TEST_ASSERT_NULL(r.session());
  TEST_ASSERT_EQUAL_UINT(0, f.imu.starts);
  TEST_ASSERT_TRUE(f.uart.tx.empty());
  TEST_ASSERT_FALSE(r.canRelease());
  f.finish(r);
}
void invalid_clock_records_the_completed_terminal_at_pass_without_restarting() {
  Rig f;
  auto c = qualified();
  LocalTelemetryRuntime r(f.raw, f.uart, f.sd, f.imu, f.adapter, f.manager, f.group, c);
  TEST_ASSERT_TRUE(r.start());
  f.pass(r);
  f.pass(r);
  const auto before = r.status().at_completed;
  TEST_ASSERT_TRUE(r.session()->clock().reset(42));
  r.service();
  TEST_ASSERT_EQUAL_INT(UartHealth::InvalidClock, r.status().gps.health);
  TEST_ASSERT_EQUAL_UINT32(before + 1, r.status().at_completed);
  const auto bytes = f.uart.tx.size();
  r.service();
  TEST_ASSERT_EQUAL_UINT32(before + 1, r.status().at_completed);
  TEST_ASSERT_EQUAL_UINT(bytes, f.uart.tx.size());
  f.finish(r);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(invalid_clock_records_the_completed_terminal_at_pass_without_restarting);
  RUN_TEST(late_identity_completion_cannot_bypass_owner_startup_deadline);
  RUN_TEST(blocked_identity_startup_cancels_at_deadline_without_releasing_worker);
  RUN_TEST(control_copies_terminal_startup_refusal_even_without_worker);
  RUN_TEST(runtime_control_reuses_one_camera_pass_and_copied_local_observations);
  RUN_TEST(local_camera_refusal_keeps_control_ineligible_and_local_storage_running);
  RUN_TEST(runtime_stop_before_gps_startup_never_touches_power_or_uart);
  RUN_TEST(manual_stop_releases_key_before_imu_and_sd_barriers);
  RUN_TEST(collision_stop_releases_key_before_imu_and_sd_barriers);
  RUN_TEST(write_failure_stop_releases_key_before_imu_and_sd_barriers);
  RUN_TEST(runtime_stop_during_settling_or_ready_does_not_restart_or_repeat_gpio);
  RUN_TEST(utc_anchor_uses_source_receipt_not_later_owner_time);
  RUN_TEST(camera_free_session_admits_and_stops_without_camera_service);
  RUN_TEST(runtime_commits_before_constructing_and_camera_free_logging_reaches_same_worker);
  RUN_TEST(failed_storage_task_has_no_pending_worker_lifetime);
  RUN_TEST(safe_mode_reaches_real_policy_and_refused_imu_does_not_pause_gps);
  RUN_TEST(stop_waits_for_imu_final_publication_before_storage_close);
  RUN_TEST(qualification_refuses_without_creating_or_polling_workers);
  RUN_TEST(imu_task_refusal_leaves_gps_and_stop_eligible);
  RUN_TEST(nvs_refusal_and_empty_camera_configuration_still_log_local_data);
  RUN_TEST(identity_failure_and_invalid_committed_id_never_construct_session);
  RUN_TEST(pending_stop_and_final_sd_barrier_retain_resources);
  RUN_TEST(media_collision_is_terminal_and_never_retries_storage_or_identity);
  RUN_TEST(camera_bursts_share_quota_and_preserve_gps_reservation);
  RUN_TEST(actual_modem_no_fix_fix_staleness_and_errors_keep_provenance);
  RUN_TEST(actual_sensor_terminal_outcome_is_copied_and_gps_continues);
  return UNITY_END();
}

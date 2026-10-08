#include "local_telemetry_esp32.h"
#include "profiles/gopro_hero12_esp32.h"
#include <array>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>
using namespace ridesync;
std::atomic<uint32_t> raw_time{100};
std::vector<std::thread> workers;
bool fail_sd = false, fail_imu = false;
std::atomic<bool> block_close{false}, close_entered{false}, release_close{false};
std::array<std::vector<uint8_t>, 2> ledger;
std::string csv;
int selected = 0;
size_t offset = 0;
unsigned mounts = 0, unmounts = 0, sd_tasks = 0, imu_tasks = 0;
int spawn(void (*fn)(void *), const char *name, void *arg) {
  bool sd = strcmp(name, "gps_sd") == 0;
  assert(sd || strcmp(name, "imu") == 0);
  if (sd) {
    ++sd_tasks;
    if (fail_sd)
      return 0;
  } else {
    ++imu_tasks;
    if (fail_imu)
      return 0;
  }
  workers.emplace_back([=] { fn(arg); });
  return 1;
}
int test_open(const char *path, int flags, unsigned) {
  assert(!(flags & O_TRUNC));
  if (strstr(path, ".session-id-")) {
    selected = strstr(path, "-a") ? 0 : 1;
    offset = 0;
    return 8;
  }
  assert((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL));
  return 7;
}
ssize_t test_read(int fd, void *b, size_t n) {
  assert(fd == 8);
  n = std::min(n, ledger[selected].size() - offset);
  if (n)
    memcpy(b, ledger[selected].data() + offset, n);
  offset += n;
  return n;
}
ssize_t test_write(int fd, const void *b, size_t n) {
  if (fd == 8) {
    ledger[selected].assign(static_cast<const uint8_t *>(b), static_cast<const uint8_t *>(b) + n);
  } else {
    assert(fd == 7);
    csv.append(static_cast<const char *>(b), n);
  }
  return n;
}
int test_fsync(int) { return 0; }
int test_close(int fd) {
  if (fd == 7 && block_close.load()) {
    close_entered.store(true);
    while (!release_close.load())
      std::this_thread::yield();
  }
  return 0;
}
void put(uint8_t *b, uint32_t v) {
  for (unsigned j = 0; j < 4; ++j)
    b[j] = uint8_t(v >> (8 * j));
}
void commission() {
  uint8_t b[40]{};
  put(b, 0x44495352);
  put(b + 4, 1);
  put(b + 8, 7);
  put(b + 20, ~uint32_t(7));
  put(b + 24, ~uint32_t(0));
  put(b + 28, ~uint32_t(0));
  uint32_t crc = 0xffffffff;
  for (unsigned i = 0; i < 36; ++i) {
    crc ^= b[i];
    for (unsigned j = 0; j < 8; ++j)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  put(b + 36, ~crc);
  for (auto &s : ledger)
    s.assign(b, b + 40);
}
QualifiedLocalTelemetry qualified() {
  QualifiedLocalTelemetry q;
  q.runtime.opt_in = q.runtime.gps_qualified = true;
  q.runtime.imu_enabled = q.runtime.imu_qualified = true;
  q.runtime.power_timing.qualified = true;
  q.runtime.modem.documentary_profile_opt_in = q.runtime.modem.terminal_retires_transaction = true;
  q.runtime.firmware = "test";
  q.runtime.provenance = "synthetic";
  q.runtime.gps_record_ms = 10;
  q.sd.opt_in = q.sd.wiring_card_qualified = q.sd.exclusive_volume = q.sd.namespace_commissioned =
      true;
  q.sd.commissioned_namespace = 7;
  q.sd.chip_select = 5;
  q.sd.frequency_hz = 1000000;
  q.modem.pins_qualified = q.modem.documentary_profile_opt_in = true;
  q.modem.tx = 18;
  q.modem.rx = 19;
  q.modem.baud = 115200;
  q.modem_already_powered = true;
  q.imu.enabled = q.imu.dedicated_bus = q.imu.electrically_qualified = true;
  q.imu.address = 0x68;
  q.imu.sensor_id = 9;
  return q;
}
void pass(Esp32LocalTelemetry &r) {
  r.service();
  raw_time.fetch_add(1);
  std::this_thread::sleep_for(std::chrono::microseconds(100));
}
int main(int argc, char **argv) {
  assert(argc == 2);
  std::string mode = argv[1];
  HardwareSerial uart;
  SPIClass spi;
  TwoWire wire;
  auto q = qualified();
  commission();
  wire.payload[0] = 0x8c;
  wire.payload[1] = 7;
  wire.payload[7] = 6;
  if (mode == "camera") {
    auto camera = hero12Runtime();
    SourceConfig source;
    source.count = 1;
    source.cameras[0].name = "hero";
    source.cameras[0].family = CameraFamily::GoPro;
    source.cameras[0].model = CameraModel::HERO12_BLACK;
    source.cameras[0].identifier = "01:02:03:04:05:06";
    source.cameras[0].address_type = AddressType::Public;
    assert(camera.manager.configure(source).ok());
    q.runtime.cameras_qualified = true;
    q.runtime.peers.count = 1;
    q.runtime.peers.entries[0].id = 301;
    q.runtime.peers.entries[0].model = CameraModel::HERO12_BLACK;
  }
  bool refuse = false;
  if (mode == "default") {
    q = QualifiedLocalTelemetry{};
    refuse = true;
  }
  if (mode == "uart-unqualified") {
    q.modem.pins_qualified = false;
    refuse = true;
  }
  if (mode == "imu-unqualified") {
    q.imu.dedicated_bus = false;
    refuse = true;
  }
  if (mode == "power-unqualified") {
    q.modem_already_powered = false;
    refuse = true;
  }
  if (mode == "sd-task") {
    fail_sd = true;
    refuse = true;
  }
  if (mode == "imu-task")
    fail_imu = true;
  if (mode == "safe-mode")
    q.runtime.safe_mode = true;
  if (mode == "close-barrier")
    block_close.store(true);
  auto runtime = std::unique_ptr<Esp32LocalTelemetry>(new Esp32LocalTelemetry(uart, spi, wire, q));
  assert(ridesync_local_telemetry_start(*runtime) == !refuse);
  if (refuse) {
    assert(runtime->status().phase == TelemetryPhase::Refused);
    assert(runtime->status().fault != TelemetryFault::None);
    assert(runtime->canRelease() && workers.empty());
    assert(hero12BindTelemetry(runtime->owner()));
    assert(hero12UnbindTelemetry(runtime->owner()));
    return 0;
  }
  for (unsigned i = 0; i < 400; ++i) {
    if (mode == "camera" && runtime->owner().session())
      hero12Runtime().manager.request(0, Operation::Wake);
    pass(*runtime);
  }
  auto s = runtime->status();
  assert(s.phase == TelemetryPhase::Running);
  assert(s.identity.id == 0x700000001ULL && s.kinds[0].accepted > 0);
  if (mode == "safe-mode")
    assert(s.imu == SensorAdmission::SafeModeRefused && imu_tasks == 0);
  else if (mode == "imu-task")
    assert(s.imu == SensorAdmission::TaskRefused && imu_tasks == 1);
  else
    assert(s.imu == SensorAdmission::Admitted && imu_tasks == 1);
  assert(uart.begins == 1 && uart.writes > 0 && sd_tasks == 1);
  HardwareSerial second_uart;
  {
    Esp32LocalTelemetry competing(second_uart, spi, wire, q);
    assert(!competing.start() && competing.canRelease());
    assert(competing.status().fault == TelemetryFault::CameraRoute && second_uart.begins == 0);
  }
  assert(!hero12BindSession(*runtime->owner().session())); // Mutually exclusive routing.
  runtime->requestStop();
  for (unsigned i = 0; i < 1000 && !runtime->canRelease() && !close_entered.load(); ++i)
    pass(*runtime);
  if (block_close.load()) {
    assert(close_entered.load() && !runtime->canRelease());
    for (unsigned i = 0; i < 100; ++i)
      runtime->service(); // Control never waits on final close.
    assert(!runtime->canRelease());
    release_close.store(true);
  }
  for (unsigned i = 0; i < 1000 && !runtime->canRelease(); ++i)
    pass(*runtime);
  assert(runtime->canRelease() && runtime->status().storage_worker_finished);
  assert(mounts == 1 && unmounts == 1);
  runtime.reset();
  for (auto &t : workers)
    t.join();
  assert(csv.find("gps,") != std::string::npos);
  if (mode != "imu-task" && mode != "safe-mode") {
    assert(csv.find("config,") != std::string::npos);
    assert(csv.find("imu,") != std::string::npos);
  }
  if (mode == "camera")
    assert(csv.find("camera,") != std::string::npos);
}

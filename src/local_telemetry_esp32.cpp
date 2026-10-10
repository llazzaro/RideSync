#include "local_telemetry_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "profiles/gopro_hero12_esp32.h"
#if defined(RIDESYNC_BENCH_APPLICATION) || defined(RIDESYNC_MIXED_COMPOSITION)
#define hero12Runtime mixedCameraRuntime
#endif
#include <new>
#if defined(RIDESYNC_MIXED_COMPOSITION)
#include <cstdlib>
#include <esp_heap_caps.h>
#endif
namespace ridesync {
uint32_t Esp32LocalTelemetry::RawClock::now() const { return millis(); }
Esp32LocalTelemetry::ImuWorker::Active::Active(ImuPort &port, ImuInbox &inbox, uint64_t id,
                                               const ImuConfig &metadata)
    : manager(port, inbox, progress, id, metadata), worker(manager, progress) {}
Esp32LocalTelemetry::ImuWorker::ImuWorker(TwoWire &wire, const Bmi270Qualification &q,
                                          const ImuConfig &metadata)
    : port_(wire, q), qualification_(q), metadata_(metadata) {}
Esp32LocalTelemetry::ImuWorker::~ImuWorker() {
  if (active_)
    active_->~Active();
}
bool Esp32LocalTelemetry::ImuWorker::start(ImuInbox &inbox, uint64_t id, bool safe_mode) {
  if (attempted_)
    return false;
  attempted_ = true;
  active_ = new (&memory_) Active(port_, inbox, id, metadata_);
  const bool qualified = qualification_.enabled && qualification_.dedicated_bus &&
                         qualification_.electrically_qualified && qualification_.sensor_id &&
                         (qualification_.address == 0x68 || qualification_.address == 0x69);
  started_ = active_->worker.start(qualified, safe_mode);
  return started_;
}
void Esp32LocalTelemetry::ImuWorker::requestStop() {
  if (started_)
    active_->manager.stop();
}
ImuWorkerObservation Esp32LocalTelemetry::ImuWorker::observation() const {
  ImuWorkerObservation result;
  if (!started_)
    return result;
  result.finished = active_->worker.workerFinished();
  result.completed = active_->progress.generation();
  result.outcome = active_->progress.outcome();
  if (result.finished) {
    result.manager = active_->manager.health();
    result.codec = active_->manager.codecHealth();
  }
  return result;
}
Esp32LocalTelemetry::Esp32LocalTelemetry(HardwareSerial &serial, SPIClass &spi, TwoWire &wire,
                                         const QualifiedLocalTelemetry &q,
                                         StaticMotionReferenceSource *source,
                                         MixedCameraRuntime *cameras)
    : sd_(spi, q.sd), imu_(wire, q.imu, q.metadata), uart_(serial), power_(q.modem),
      qualification_(q),
      runtime_(clock_, uart_, sd_, imu_,
               cameras ? static_cast<CameraRuntimePort &>(cameras->adapter)
                       : static_cast<CameraRuntimePort &>(hero12Runtime().adapter),
               cameras ? cameras->manager : hero12Runtime().manager,
               cameras ? cameras->group : hero12Runtime().group, q.runtime,
               q.modem_power_sequence ? &power_ : nullptr, source),
      cameras_(cameras) {}
Esp32LocalTelemetry::~Esp32LocalTelemetry() {
  // Caller has observed final IMU/SD access. No bus or filesystem cleanup here.
#if !defined(RIDESYNC_BENCH_APPLICATION) && !defined(RIDESYNC_MIXED_COMPOSITION)
  if (!cameras_ && route_bound_ && runtime_.canRelease())
    hero12UnbindTelemetry(runtime_);
#endif
}
bool Esp32LocalTelemetry::start() {
  if (attempted_)
    return false;
  attempted_ = true;
  // Validate local sensor qualification before UART startup. The private SD
  // owner independently validates volume qualification before task creation.
  // NVS and camera readiness deliberately do not participate in local admission.
  const auto &q = qualification_;
  const bool imu_ok = !q.runtime.imu_enabled ||
                      (q.imu.enabled && q.imu.dedicated_bus && q.imu.electrically_qualified &&
                       q.imu.sensor_id && (q.imu.address == 0x68 || q.imu.address == 0x69));
  if (!q.runtime.opt_in || !q.runtime.gps_qualified ||
      (q.runtime.imu_enabled && !q.runtime.imu_qualified) || !imu_ok ||
      (!q.modem_already_powered && !q.modem_power_sequence) || !q.runtime.power_timing.qualified) {
    runtime_.refuseStart(TelemetryFault::Qualification);
    return false;
  }
  if (q.modem_power_sequence &&
      (q.modem_already_powered || !q.runtime.power_timing.key_active_ms ||
       !q.runtime.power_timing.settle_ms || q.runtime.power_timing.key_active_ms > 60000 ||
       q.runtime.power_timing.settle_ms > 60000 || q.runtime.power_timing.pre_key_ms > 60000 ||
       !power_.begin(true))) {
    runtime_.refuseStart(TelemetryFault::Qualification);
    return false;
  }
#if !defined(RIDESYNC_BENCH_APPLICATION) && !defined(RIDESYNC_MIXED_COMPOSITION)
  if (!cameras_ && !hero12BindTelemetry(runtime_)) {
    runtime_.refuseStart(TelemetryFault::CameraRoute);
    return false;
  }
#endif
  route_bound_ = true;
  if (!uart_.begin(q.modem)) {
    runtime_.refuseStart(TelemetryFault::Qualification);
#if !defined(RIDESYNC_BENCH_APPLICATION) && !defined(RIDESYNC_MIXED_COMPOSITION)
    if (!cameras_)
      hero12UnbindTelemetry(runtime_);
#endif
    route_bound_ = false;
    return false;
  }
  if (!runtime_.start()) {
#if !defined(RIDESYNC_BENCH_APPLICATION) && !defined(RIDESYNC_MIXED_COMPOSITION)
    if (!cameras_)
      hero12UnbindTelemetry(runtime_);
#endif
    route_bound_ = false;
    return false;
  }
  return true;
}
void Esp32LocalTelemetry::service() {
  if (route_bound_) {
#if defined(RIDESYNC_BENCH_APPLICATION) || defined(RIDESYNC_MIXED_COMPOSITION)
    runtime_.service();
#else
    if (cameras_)
      runtime_.service();
    else
      ridesync_hero12_service();
#endif
    if (runtime_.canRelease()) {
#if !defined(RIDESYNC_BENCH_APPLICATION) && !defined(RIDESYNC_MIXED_COMPOSITION)
      if (!cameras_)
        hero12UnbindTelemetry(runtime_);
#endif
      route_bound_ = false;
    }
  } else if (runtime_.canRelease()) {
    // Inactive/refused/finished owner has no producers to advance. Publish its
    // own copied status; the global route may belong to an unrelated owner.
    runtime_.service();
  }
}
} // namespace ridesync
extern "C" ridesync::Esp32LocalTelemetry &
ridesync_local_telemetry_runtime(HardwareSerial &uart, SPIClass &spi, TwoWire &wire,
                                 const ridesync::QualifiedLocalTelemetry &q,
                                 ridesync::StaticMotionReferenceSource *source) {
  // One boot-lifetime owner. First call fixes caller resource references/config;
  // repeated calls cannot replace an active or terminal session with another ID.
#if defined(RIDESYNC_MIXED_COMPOSITION)
  // Compatibility factory remains a fixed boot-lifetime owner. Keep its storage
  // in internal RAM and allocate only on explicit invocation of this standalone
  // route; the supervised application owns a separate in-place telemetry member.
  static auto *runtime = [&]() {
    void *memory = heap_caps_malloc(sizeof(ridesync::Esp32LocalTelemetry),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!memory)
      std::abort();
    return new (memory) ridesync::Esp32LocalTelemetry(uart, spi, wire, q, source);
  }();
  return *runtime;
#else
  static ridesync::Esp32LocalTelemetry runtime(uart, spi, wire, q, source);
  return runtime;
#endif
}
extern "C" bool ridesync_local_telemetry_start(ridesync::Esp32LocalTelemetry &runtime) {
  return runtime.start();
}
extern "C" void ridesync_local_telemetry_service(ridesync::Esp32LocalTelemetry &runtime) {
  runtime.service();
}
#endif

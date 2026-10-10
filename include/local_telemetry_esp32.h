#pragma once
#if defined(ARDUINO_ARCH_ESP32)
#include "bmi270_imu.h"
#include "local_telemetry_runtime.h"
#include "modem_arduino.h"
#include "profiles/mixed_camera_esp32.h"
#include "storage_sd.h"
namespace ridesync {
struct QualifiedLocalTelemetry {
  LocalTelemetryConfig runtime;
  QualifiedSdConfig sd;
  QualifiedModemPins modem;
  Bmi270Qualification imu;
  ImuConfig metadata;
  // Caller asserts qualified startup/receive barrier is already complete.
  // No PWRKEY/supply operations are performed by this composition.
  bool modem_already_powered = false;
  // Alternative finite key/settle startup on the existing GPS owner. Supply
  // remains asserted; caller supplies real routing, polarity and timing.
  bool modem_power_sequence = false;
};
// Default inactive, callable by the supervised commissioning composition. Caller owns UART,
// initialized dedicated SPI/I2C and qualified camera singleton; all outlive canRelease. Construct
// in static/owned storage, NOT a task stack (~16 KiB target fixed state).
// start/service/requestStop/status have ONE application owner. service invokes
// the bound ridesync_hero12_service route exactly once, including GPS/admission.
class Esp32LocalTelemetry {
public:
  Esp32LocalTelemetry(HardwareSerial &, SPIClass &, TwoWire &, const QualifiedLocalTelemetry &,
                      StaticMotionReferenceSource *source = nullptr,
                      MixedCameraRuntime *cameras = nullptr);
  ~Esp32LocalTelemetry();
  Esp32LocalTelemetry(const Esp32LocalTelemetry &) = delete;
  Esp32LocalTelemetry &operator=(const Esp32LocalTelemetry &) = delete;
  bool start();
  void service();
  void requestStop() { runtime_.requestStop(); }
  LocalTelemetryStatus status() const { return runtime_.status(); }
  bool canRelease() const { return runtime_.canRelease(); }
  LocalTelemetryRuntime &owner() { return runtime_; }

private:
  class RawClock : public Clock {
    uint32_t now() const override;
  } clock_;
  class SdWorker : public TelemetryStorageWorker {
  public:
    SdWorker(SPIClass &spi, const QualifiedSdConfig &q) : sd_(spi, q) {}
    bool start() override { return sd_.start(); }
    IdentityAllocation allocation() const override { return sd_.allocation(); }
    StorageSink &sink() override { return sd_.sink(); }
    bool bind(Storage &s) override { return sd_.bind(s); }
    void cancel() override { sd_.cancel(); }
    bool workerFinished() const override { return sd_.workerFinished(); }
    int ioError() const override { return sd_.ioError(); }
    uint32_t completed() const override { return sd_.progress().generation(); }

  private:
    ArduinoSdStorage sd_;
  } sd_;
  class ImuWorker : public TelemetryImuWorker {
  public:
    ImuWorker(TwoWire &, const Bmi270Qualification &, const ImuConfig &);
    ~ImuWorker();
    bool start(ImuInbox &, uint64_t, bool safe_mode) override;
    void requestStop() override;
    ImuWorkerObservation observation() const override;

  private:
    struct Active {
      HealthProgress progress;
      ImuManager manager;
      Bmi270Worker worker;
      Active(ImuPort &, ImuInbox &, uint64_t, const ImuConfig &);
    };
    Bmi270Imu port_;
    const Bmi270Qualification qualification_;
    const ImuConfig metadata_;
    typename std::aligned_storage<sizeof(Active), alignof(Active)>::type memory_;
    Active *active_ = nullptr;
    bool attempted_ = false, started_ = false;
  } imu_;
  ArduinoModemUart uart_;
  ArduinoGnssPower power_;
  const QualifiedLocalTelemetry qualification_;
  LocalTelemetryRuntime runtime_;
  MixedCameraRuntime *cameras_ = nullptr;
  bool attempted_ = false, route_bound_ = false;
};
} // namespace ridesync
extern "C" ridesync::Esp32LocalTelemetry &
ridesync_local_telemetry_runtime(HardwareSerial &, SPIClass &, TwoWire &,
                                 const ridesync::QualifiedLocalTelemetry &,
                                 ridesync::StaticMotionReferenceSource *source = nullptr);
extern "C" bool ridesync_local_telemetry_start(ridesync::Esp32LocalTelemetry &);
extern "C" void ridesync_local_telemetry_service(ridesync::Esp32LocalTelemetry &);
#endif

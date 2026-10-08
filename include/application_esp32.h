#pragma once
#if defined(ARDUINO_ARCH_ESP32)
#include "application_startup.h"
#include "handlebar_control_esp32.h"
#include "profiles/gopro_hero12_esp32.h"
namespace ridesync {
// Commissioning supplies initialized dedicated buses and actual camera evidence.
// Boot-lifetime owner; no implicit pins, namespace, peripheral begin or retry.
class SupervisedEsp32Application final : public ApplicationWorkers {
public:
  SupervisedEsp32Application(HardwareSerial &, SPIClass &, TwoWire &,
                             const QualifiedLocalTelemetry &, const QualifiedHandlebar & = {},
                             const std::array<Hero12Qualification, kMaxCameras> & = {});
  CameraPeers peers() const override { return qualification_.runtime.peers; }
  ~SupervisedEsp32Application(); // caller must first observe telemetry()->canRelease()
  std::array<WorkerPolicy, 4> prepare(const SettingsSnapshot &, bool, bool,
                                      HealthSupervisor &) override;
  void launch() override;
  void current(const SettingsSnapshot &, bool, bool) override;
  void service(uint8_t, uint8_t, bool) override;
  Esp32LocalTelemetry *telemetry() { return telemetry_; }
  const Esp32HandlebarControl *control() const { return control_; }

private:
  HardwareSerial &uart_;
  SPIClass &spi_;
  TwoWire &wire_;
  QualifiedLocalTelemetry qualification_;
  QualifiedHandlebar handlebar_;
  const std::array<Hero12Qualification, kMaxCameras> cameras_;
  HealthSupervisor *supervisor_ = nullptr;
  std::array<WorkerPolicy, 4> policy_{};
  StartupState configuration_ = StartupState::WaitingConfig;
  uint32_t epoch_ = 0;
  uint64_t generation_ = 0;
  bool prepared_ = false, launched_ = false, camera_allowed_ = false, button_allowed_ = false;
  bool ble_started_ = false, safe_mode_ = false;
  typename std::aligned_storage<sizeof(Esp32LocalTelemetry), alignof(Esp32LocalTelemetry)>::type
      telemetry_memory_;
  typename std::aligned_storage<sizeof(Esp32HandlebarControl), alignof(Esp32HandlebarControl)>::type
      control_memory_;
  Esp32LocalTelemetry *telemetry_ = nullptr;
  Esp32HandlebarControl *control_ = nullptr;
  void publish(Worker, uint32_t, DeviceHealth, bool finished = false, bool refused = false);
};
} // namespace ridesync
#endif

#if defined(ARDUINO_ARCH_ESP32)
extern "C" ridesync::SupervisedEsp32Application &ridesync_supervised_application(
    HardwareSerial &, SPIClass &, TwoWire &, const ridesync::QualifiedLocalTelemetry &,
    const ridesync::QualifiedHandlebar &,
    const std::array<ridesync::Hero12Qualification, ridesync::kMaxCameras> &);
#endif

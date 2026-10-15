#pragma once
#if defined(ARDUINO_ARCH_ESP32)
#include "application_startup.h"
#include "ble_pairing_reset.h"
#include "handlebar_control_esp32.h"
#include "power_shutdown_esp32.h"
#include "profiles/gopro_hero12_esp32.h"
#include "profiles/mixed_camera_esp32.h"
namespace ridesync {
enum class ApplicationResetPhase {
  Idle,
  Retiring,
  RevokingProof,
  Submitting,
  WaitingHost,
  Finished
};
struct ApplicationPairingResetStatus {
  uint32_t operation = 0, peer_id = 0, epoch = 0, qualification_record = 0;
  uint64_t generation = 0;
  BondIdentity identity;
  uint8_t slot = 0;
  ApplicationResetPhase phase = ApplicationResetPhase::Idle;
  BondOutcome outcome = BondOutcome::Busy;
  int error = 0;
  bool finished = false, releasable = true, mutation = false, requalification_required = false;
  bool proof_denied = false, proof_write_attempted = false, cancelled = false, timed_out = false;
};
struct ApplicationPowerStatus {
  PowerShutdownStatus policy;
  PowerBackendStatus backend;
  PowerObservation observation;
  int32_t storage_error = 0;
  StorageHealth storage{};
  TelemetryFault telemetry_fault = TelemetryFault::None;
  uint32_t uncertain_rows = 0, report_generation = 0;
  int32_t reset_reason = 0;
  bool startup_refused = false;
};
// Commissioning supplies initialized dedicated buses and actual camera evidence.
// Boot-lifetime owner; no implicit pins, namespace, peripheral begin or retry.
class SupervisedEsp32Application final : public ApplicationWorkers {
public:
  SupervisedEsp32Application(HardwareSerial &, SPIClass &, TwoWire &,
                             const QualifiedLocalTelemetry &,
                             const QualifiedHandlebar &handlebar = QualifiedHandlebar{},
                             const std::array<Hero12Qualification, kMaxCameras> &hero12 =
                                 std::array<Hero12Qualification, kMaxCameras>{},
                             StaticMotionReferenceSource *source = nullptr,
                             const PowerShutdownConfig &power = PowerShutdownConfig{},
                             PowerEventBackend *power_backend = nullptr);
  SupervisedEsp32Application(HardwareSerial &, SPIClass &, TwoWire &,
                             const QualifiedLocalTelemetry &, const QualifiedHandlebar &,
                             const MixedCameraQualifications &,
                             StaticMotionReferenceSource *source = nullptr,
                             const PowerShutdownConfig &power = PowerShutdownConfig{},
                             PowerEventBackend *power_backend = nullptr);
  bool startupReady() override;
  bool requestShutdown();
  ApplicationPowerStatus powerStatus() const;
  CameraPeers peers() const override { return qualification_.runtime.peers; }
  ~SupervisedEsp32Application(); // caller must first observe canRelease()
  std::array<WorkerPolicy, 4> prepare(const SettingsSnapshot &, bool, bool,
                                      HealthSupervisor &) override;
  void launch() override;
  void current(const SettingsSnapshot &, bool, bool) override;
  void service(uint8_t, uint8_t, bool) override;
  Esp32LocalTelemetry *telemetry() { return telemetry_; }
  const Esp32HandlebarControl *control() const { return control_; }
  // Serialized explicit operator entry; no authentication-failure or button hook.
  // Copied fixed lease, 1..5000ms original finite deadline; never reopens target.
  BondResetSubmission requestPairingReset(uint8_t slot, uint32_t peer_id, uint32_t epoch,
                                          uint64_t generation, uint32_t operation,
                                          uint32_t timeout_ms);
  bool cancelPairingReset(uint32_t operation);
  ApplicationPairingResetStatus pairingResetStatus() const { return reset_; }
  bool canRelease() const { return (!telemetry_ || telemetry_->canRelease()) && reset_.releasable; }

private:
  PowerShutdownConfig power_config_;
  PowerShutdown power_policy_;
  PowerEventBackend *power_backend_ = nullptr;
  bool power_initialized_ = false, power_startup_refused_ = false;
  uint32_t power_report_generation_ = 0;
  int32_t power_reset_reason_ = 0;
  PowerShutdownStatus last_power_status_;
  PowerObservation power_observation_;
  void servicePower(bool manual = false);
  HardwareSerial &uart_;
  SPIClass &spi_;
  TwoWire &wire_;
  QualifiedLocalTelemetry qualification_;
  QualifiedHandlebar handlebar_;
  const MixedCameraQualifications cameras_;
  MixedCameraRuntime cameras_runtime_;
  HealthSupervisor *supervisor_ = nullptr;
  std::array<WorkerPolicy, 4> policy_{};
  StartupState configuration_ = StartupState::WaitingConfig;
  uint32_t epoch_ = 0;
  uint64_t generation_ = 0;
  bool prepared_ = false, launched_ = false, camera_allowed_ = false, button_allowed_ = false;
  bool ble_started_ = false, safe_mode_ = false;
  bool mark_allowed_ = false;
  typename std::aligned_storage<sizeof(Esp32LocalTelemetry), alignof(Esp32LocalTelemetry)>::type
      telemetry_memory_;
  typename std::aligned_storage<sizeof(Esp32HandlebarControl), alignof(Esp32HandlebarControl)>::type
      control_memory_;
  StaticMotionReferenceSource *motion_source_ = nullptr;
  Esp32LocalTelemetry *telemetry_ = nullptr;
  Esp32HandlebarControl *control_ = nullptr;
  ApplicationPairingResetStatus reset_;
  BondIdentity reset_identity_;
  BleStoreProof reset_proof_;
  uint32_t reset_deadline_ = 0, last_reset_ = 0, host_operation_ = 0, reset_host_operation_ = 0,
           retry_at_ = 0;
  bool proof_pending_ = false, host_pending_ = false, retried_ = false, retry_wait_ = false;
  bool reset_current_ = true, host_cancelled_ = false;
  bool resetNow(uint32_t &);
  bool resetAdmitted() const;
  void configureWorkerPolicies(bool, bool);
  void configureCameras(const SettingsSnapshot &);
  bool resetContextCurrent(uint8_t, uint32_t, uint32_t, uint64_t, uint32_t, uint32_t &);
  bool resetPeerMatches(uint8_t, uint32_t) const;
  void submitProofReset(uint32_t);
  void submitHostReset(uint32_t);
  void revokeReset(bool timeout = false);
  void serviceReset();
  bool serviceResetProof();
  void launchControl();
  void observeHostReset(const BleBondResetResult &);
  void publishWorkers(const LocalTelemetryStatus &);
  bool serviceResetHost(uint32_t now);
  void serviceResetPhase(uint32_t now);
  void finishReset();
  void publish(Worker, uint32_t, DeviceHealth, bool finished = false, bool refused = false);
};
} // namespace ridesync
#endif

#if defined(ARDUINO_ARCH_ESP32)
extern "C" ridesync::SupervisedEsp32Application &ridesync_supervised_application(
    HardwareSerial &, SPIClass &, TwoWire &, const ridesync::QualifiedLocalTelemetry &,
    const ridesync::QualifiedHandlebar &,
    const std::array<ridesync::Hero12Qualification, ridesync::kMaxCameras> &,
    ridesync::StaticMotionReferenceSource *source = nullptr);
#endif

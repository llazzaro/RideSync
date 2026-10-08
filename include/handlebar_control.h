#pragma once
#include "button_manager.h"
#include "local_telemetry_runtime.h"
#include "status_led.h"
namespace ridesync {
enum class ControlAdmission { Admitted, Inactive, Refused, Overflow };
struct ControlStatus {
  RecordingStatus group;
  std::array<Lifecycle, kMaxCameras> lifecycle{};
  std::array<Hero12RecoveryState, kMaxCameras> recovery{};
  std::array<CameraError, kMaxCameras> wake{};
  std::array<Hero12Fault, kMaxCameras> adapter_fault{};
  LocalTelemetryStatus local;
  LedHealth health;
  LedState led = LedState::Off;
  int led_error = 0;
  ControlAdmission admission = ControlAdmission::Inactive;
  uint32_t accepted = 0, refused = 0, overflow = 0, discarded = 0;
};
// Same serialized application owner as LocalTelemetryRuntime. No tasks, clocks,
// storage or additional manager ticks. Input/sink references outlive this owner.
class HandlebarControl : public CameraServiceAction, public RecordingPreparation {
public:
  HandlebarControl(Clock &, Hero12Adapter &, CameraManager &, RecordingManager &, ButtonInput &,
                   LedSink &);
  ~HandlebarControl();
  bool begin(const ButtonConfig &);
  CameraError prepare(size_t) override;
  RecordingPreparationResult prepared(size_t) override;
  void retire(size_t) override;
  bool commandReady(size_t) const override;
  bool retiring(size_t) const override;
  bool released(size_t) const override;
  ControlAdmission submit(ButtonAction);
  void beforeAdvance() override;
  void admission(const LocalTelemetryStatus &);
  bool binds(const Hero12Adapter &, const CameraManager &, const RecordingManager &) const;
  void observe(const LocalTelemetryStatus &);
  void revoke();
  void reset();
  ControlStatus status() const { return status_; }

private:
  Clock &clock_;
  Hero12Adapter &adapter_;
  CameraManager &manager_;
  RecordingManager &group_;
  ButtonInput &input_;
  StatusLed led_;
  ButtonManager button_;
  ButtonConfig config_;
  ControlStatus status_;
  std::array<ButtonAction, 4> actions_{};
  size_t count_ = 0;
  uint32_t reset_generation_ = 0;
  bool begun_ = false, recording_ = false;
  static void increment(uint32_t &value) {
    if (value != UINT32_MAX)
      ++value;
  }
};
} // namespace ridesync

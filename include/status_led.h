#pragma once
#include "health_supervisor.h"
#include "recording_manager.h"
namespace ridesync {
enum class LedState { Off, Ready, Recording, Partial, Recovery, Error };
enum class LedSeverity { Ignore, Partial, Error };
struct LedDeviceHealth {
  bool enabled = false, qualified = false, current = false;
  DeviceHealth outcome = DeviceHealth::Ok;
  LedSeverity severity = LedSeverity::Ignore;
};
struct LedHealth {
  bool application_fault = false, safe_mode = false, required_worker_stall = false;
  bool adapter_recovery = false;
  std::array<LedDeviceHealth, 4> devices{};
};
// Owner copies status/lifecycle in the same serialized context as manager service.
// No calls to managers, transports or watchdog policy occur here.
LedState selectLedState(const RecordingStatus *group,
                        const std::array<Lifecycle, kMaxCameras> &lifecycle,
                        const LedHealth &health = {});
struct LedFrame {
  bool red = false, green = false, blue = false;
  LedFrame(bool r = false, bool g = false, bool b = false) : red(r), green(g), blue(b) {}
  bool operator==(const LedFrame &other) const {
    return red == other.red && green == other.green && blue == other.blue;
  }
  bool lit() const { return red || green || blue; }
};
class LedSink {
public:
  virtual ~LedSink() = default;
  // Must return promptly; no reentry, manager calls, waits or recursive callbacks.
  virtual int write(LedFrame frame) = 0;
};
// One application owner. Forward uint32 ms; service gaps <2^31 ms. Full unseen
// wraps cannot be recovered. An observed gap >=2^31 restarts phase at this sample.
// Only the present frame is emitted; no missed-edge replay. State changes restart
// even if the color is unchanged; refreshed generations alone do not restart.
class StatusLed {
public:
  explicit StatusLed(LedSink &sink) : sink_(sink) {}
  int service(LedState state, uint32_t now);
  LedState state() const { return state_; }

private:
  LedSink &sink_;
  LedState state_ = LedState::Off;
  LedFrame last_frame_;
  uint32_t previous_ = 0, phase_ = 0;
  bool started_ = false, emitted_ = false;
};
enum class LedMode { Disabled, Mono, Rgb };
enum class LedPolarity { Unspecified, ActiveHigh, ActiveLow };
struct LedWiring {
  LedMode mode = LedMode::Disabled;
  bool board_qualified = false, reservations_complete = false;
  std::array<int, 3> pins{{-1, -1, -1}}; // Mono uses first; RGB red, green, blue.
  std::array<LedPolarity, 3> polarity{
      {LedPolarity::Unspecified, LedPolarity::Unspecified, LedPolarity::Unspecified}};
  uint64_t reserved_pins = 0; // ALL active board/button/SD/IMU/bus reservations.
};
class LedGpioPort {
public:
  virtual ~LedGpioPort() = default;
  virtual int configureOutputs(uint64_t mask) = 0;
  virtual int setLevel(int pin, bool level) = 0;
};
enum class LedBackendState { Disabled, Refused, Ready, ConfigFailed, WriteFailed };
class GpioLedSink : public LedSink {
public:
  explicit GpioLedSink(LedGpioPort &port) : port_(port) {}
  // Startup only, before output service. A repeated begin returns the latched state without IO.
  // Enabled saved button pin is checked separately, never grants qualification.
  LedBackendState begin(const LedWiring &, bool acknowledge_qualification,
                        const ButtonGpioConfig &button = {});
  int write(LedFrame) override;
  LedBackendState state() const { return state_; }
  int error() const { return error_; }

private:
  LedGpioPort &port_;
  LedWiring wiring_;
  LedBackendState state_ = LedBackendState::Disabled;
  int error_ = 0;
  bool begun_ = false;
  size_t channels_ = 0;
  void fail(LedBackendState, int);
};
#if defined(ARDUINO_ARCH_ESP32)
class Esp32LedGpio : public LedGpioPort {
public:
  int configureOutputs(uint64_t mask) override;
  int setLevel(int pin, bool level) override;
};
#endif
} // namespace ridesync

#include "status_led.h"
namespace ridesync {
LedState selectLedState(const RecordingStatus *group,
                        const std::array<Lifecycle, kMaxCameras> &lifecycle,
                        const LedHealth &health) {
  bool error = health.application_fault || health.safe_mode || health.required_worker_stall;
  bool partial = false, recovery = health.adapter_recovery;
  for (const auto &device : health.devices) {
    if (!device.enabled || !device.qualified || !device.current ||
        device.outcome == DeviceHealth::Ok)
      continue;
    error |= device.severity == LedSeverity::Error;
    partial |= device.severity == LedSeverity::Partial;
  }
  if (group) {
    for (size_t i = 0; i < kMaxCameras; ++i) {
      const auto &peer = group->peers[i];
      if (!peer.enabled)
        continue;
      error |= (peer.terminal_failure || lifecycle[i] == Lifecycle::Failed) &&
               peer.error != CameraError::Cancelled;
      recovery |= lifecycle[i] == Lifecycle::Connecting || lifecycle[i] == Lifecycle::Backoff;
      partial |= peer.error != CameraError::None && peer.error != CameraError::Cancelled;
    }
  }
  if (error)
    return LedState::Error;
  if (recovery)
    return LedState::Recovery;
  if (partial)
    return LedState::Partial;
  if (!group || !group->enabled)
    return LedState::Off;
  if (group->ready != group->enabled || group->pending || group->unknown)
    return LedState::Partial;
  if (group->recording == group->enabled)
    return LedState::Recording;
  if (group->stopped == group->enabled)
    return LedState::Ready;
  return LedState::Partial;
}
namespace {
uint32_t period(LedState state) { return state == LedState::Partial ? 2000U : 1000U; }
LedFrame frame(LedState state, uint32_t phase) {
  switch (state) {
  case LedState::Ready:
    return {false, true};
  case LedState::Recording:
    return {phase < 500};
  case LedState::Partial:
    return {phase < 100 || (phase >= 200 && phase < 300),
            phase < 100 || (phase >= 200 && phase < 300)};
  case LedState::Recovery:
    return {false, false, phase % 200 < 100};
  case LedState::Error:
    return {phase < 100 || (phase >= 200 && phase < 300) || (phase >= 400 && phase < 500)};
  default:
    return {};
  }
}
} // namespace
int StatusLed::service(LedState state, uint32_t now) {
  const uint32_t gap = now - previous_;
  if (!started_ || state != state_ || gap >= 0x80000000U)
    phase_ = 0;
  else
    phase_ = (phase_ + gap % period(state)) % period(state);
  state_ = state;
  previous_ = now;
  started_ = true;
  const auto current = frame(state, phase_);
  if (emitted_ && current == last_frame_)
    return 0;
  const int result = sink_.write(current);
  if (result == 0) {
    last_frame_ = current;
    emitted_ = true;
  } else {
    emitted_ = false;
  }
  return result;
}
namespace {
bool allowedOutput(int pin) {
  return pin == 18 || pin == 19 || pin == 21 || pin == 22 || pin == 23 || pin == 32;
}
bool validWiring(const LedWiring &w, bool ack, const ButtonGpioConfig &button) {
  if (!w.board_qualified || !w.reservations_complete || !ack ||
      (w.mode != LedMode::Mono && w.mode != LedMode::Rgb))
    return false;
  if (button.enabled && (button.pin < 0 || button.pin > 39))
    return false;
  const size_t channels = w.mode == LedMode::Mono ? 1 : 3;
  uint64_t used = w.reserved_pins;
  if (button.enabled)
    used |= uint64_t(1) << button.pin;
  for (size_t i = 0; i < channels; ++i) {
    if (!allowedOutput(w.pins[i]) ||
        (w.polarity[i] != LedPolarity::ActiveHigh && w.polarity[i] != LedPolarity::ActiveLow))
      return false;
    const uint64_t pin = uint64_t(1) << w.pins[i];
    if (used & pin)
      return false;
    used |= pin;
  }
  // Mono may not hide an accidentally populated RGB configuration.
  for (size_t i = channels; i < 3; ++i)
    if (w.pins[i] != -1 || w.polarity[i] != LedPolarity::Unspecified)
      return false;
  return true;
}
} // namespace
LedBackendState GpioLedSink::begin(const LedWiring &w, bool ack, const ButtonGpioConfig &button) {
  if (begun_)
    return state_;
  begun_ = true;
  if (w.mode == LedMode::Disabled)
    return state_;
  if (!validWiring(w, ack, button)) {
    state_ = LedBackendState::Refused;
    return state_;
  }
  wiring_ = w;
  channels_ = w.mode == LedMode::Mono ? 1 : 3;
  uint64_t mask = 0;
  for (size_t i = 0; i < channels_; ++i)
    mask |= uint64_t(1) << w.pins[i];
  const int result = port_.configureOutputs(mask);
  if (result) {
    fail(LedBackendState::ConfigFailed, result);
    return state_;
  }
  state_ = LedBackendState::Ready;
  write({});
  return state_;
}
void GpioLedSink::fail(LedBackendState state, int error) {
  state_ = state;
  error_ = error;
  // Bounded best effort off; preserve the original error even if cleanup fails.
  // Electrical off cannot be guaranteed after an SDK fault.
  for (size_t i = 0; i < channels_; ++i)
    port_.setLevel(wiring_.pins[i], wiring_.polarity[i] == LedPolarity::ActiveLow);
}
int GpioLedSink::write(LedFrame frame) {
  if (state_ != LedBackendState::Ready)
    return error_;
  const bool lit[3] = {wiring_.mode == LedMode::Mono ? frame.lit() : frame.red, frame.green,
                       frame.blue};
  for (size_t i = 0; i < channels_; ++i) {
    const int result =
        port_.setLevel(wiring_.pins[i], lit[i] != (wiring_.polarity[i] == LedPolarity::ActiveLow));
    if (result) {
      fail(LedBackendState::WriteFailed, result);
      return result;
    }
  }
  return 0;
}
} // namespace ridesync
#if defined(ARDUINO_ARCH_ESP32)
#include <driver/gpio.h>
namespace ridesync {
int Esp32LedGpio::configureOutputs(uint64_t mask) {
  gpio_config_t config{};
  config.pin_bit_mask = mask;
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  return gpio_config(&config);
}
int Esp32LedGpio::setLevel(int pin, bool level) {
  return gpio_set_level(static_cast<gpio_num_t>(pin), level ? 1 : 0);
}
} // namespace ridesync
#endif

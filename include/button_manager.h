#pragma once
#include <cstdint>
namespace ridesync {
enum class ButtonAction { RecordingIntent, WakeReconnect, Resync };
struct ButtonConfig {
  uint32_t debounce_ms = 20;
  uint32_t long_ms = 800;
  uint32_t double_ms = 300;
  bool double_enabled = false;
  ButtonAction short_action = ButtonAction::RecordingIntent;
  ButtonAction long_action = ButtonAction::WakeReconnect;
  ButtonAction double_action = ButtonAction::Resync;
};
bool validateButtonConfig(const ButtonConfig &config);
class ButtonInput {
public:
  virtual ~ButtonInput() = default;
  virtual bool pressed() = 0;
};
using ButtonCallback = void (*)(void *, ButtonAction);
// Single scheduler context only; input/callback must be prompt and may not
// reenter begin/poll or mutate this manager. Clock gaps must be < 2^31 ms.
class ButtonManager {
public:
  bool begin(const ButtonConfig &, ButtonCallback, void *);
  void poll(ButtonInput &, uint32_t now);

private:
  ButtonConfig config_;
  ButtonCallback callback_ = nullptr;
  void *context_ = nullptr;
  bool enabled_ = false, sampled_ = false, raw_ = false, stable_ = false;
  bool armed_ = false, long_sent_ = false, pending_ = false, second_ = false;
  uint32_t changed_ = 0, pressed_ = 0, released_ = 0;
  void emit(ButtonAction action);
};
enum class ButtonPull { External, Up, Down };
struct ButtonGpioConfig {
  bool enabled = false;
  bool board_qualified = false;
  int pin = -1;
  ButtonPull pull = ButtonPull::External;
  bool active_low = true;
};
bool validateButtonGpioConfig(const ButtonGpioConfig &);
#if defined(ARDUINO_ARCH_ESP32)
class ArduinoButtonInput : public ButtonInput {
public:
  bool begin(const ButtonGpioConfig &, bool acknowledge_qualification);
  bool pressed() override;

private:
  ButtonGpioConfig config_;
  bool ready_ = false;
};
#endif
} // namespace ridesync

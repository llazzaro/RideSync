#include "button_manager.h"
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#endif
namespace ridesync {
namespace {
bool validAction(ButtonAction a) {
  return a == ButtonAction::RecordingIntent || a == ButtonAction::WakeReconnect ||
         a == ButtonAction::Resync;
}
bool interval(uint32_t n) { return n > 0 && n < 0x80000000u; }
} // namespace
bool validateButtonConfig(const ButtonConfig &c) {
  return interval(c.debounce_ms) && interval(c.long_ms) && interval(c.double_ms) &&
         c.long_ms > c.debounce_ms && c.double_ms > c.debounce_ms && validAction(c.short_action) &&
         validAction(c.long_action) && validAction(c.double_action);
}
bool ButtonManager::begin(const ButtonConfig &c, ButtonCallback cb, void *context) {
  *this = ButtonManager();
  if (!validateButtonConfig(c) || !cb)
    return false;
  config_ = c;
  callback_ = cb;
  context_ = context;
  enabled_ = true;
  return true;
}
void ButtonManager::emit(ButtonAction action) { callback_(context_, action); }
void ButtonManager::poll(ButtonInput &input, uint32_t now) {
  if (!enabled_)
    return;
  bool raw = input.pressed();
  if (!sampled_) {
    sampled_ = true;
    raw_ = raw;
    stable_ = raw;
    changed_ = now;
  }
  if (raw != raw_) {
    raw_ = raw;
    changed_ = now;
  }
  // Expiry wins at equality, including when a second press debounces then.
  if (pending_ && !second_ && uint32_t(now - released_) >= config_.double_ms) {
    pending_ = false;
    emit(config_.short_action);
  }
  if (!armed_) {
    if (!raw_ && uint32_t(now - changed_) >= config_.debounce_ms) {
      stable_ = false;
      armed_ = true;
    }
    return;
  }
  if (raw_ != stable_ && uint32_t(now - changed_) >= config_.debounce_ms) {
    stable_ = raw_;
    if (stable_) {
      pressed_ = now;
      long_sent_ = false;
      second_ = pending_;
    } else {
      // Release duration is measured between debounced transitions.
      if (!long_sent_ && uint32_t(now - pressed_) >= config_.long_ms) {
        pending_ = false;
        long_sent_ = true;
        emit(config_.long_action);
      }
      if (!long_sent_) {
        if (second_) {
          pending_ = false;
          emit(config_.double_action);
        } else if (config_.double_enabled) {
          pending_ = true;
          released_ = now;
        } else
          emit(config_.short_action);
      }
      second_ = false;
    }
  }
  if (stable_ && !long_sent_ && uint32_t(now - pressed_) >= config_.long_ms) {
    pending_ = false;
    long_sent_ = true;
    emit(config_.long_action);
  }
}
bool validateButtonGpioConfig(const ButtonGpioConfig &c) {
  if (!c.enabled || !c.board_qualified)
    return false;
  if (c.pull != ButtonPull::External && c.pull != ButtonPull::Up && c.pull != ButtonPull::Down)
    return false;
  // Silicon-valid inputs after rejecting straps, flash/PSRAM and known vendor peripherals.
  switch (c.pin) {
  case 18:
  case 19:
  case 21:
  case 22:
  case 23:
  case 32:
    return true;
  case 34:
  case 35:
  case 36:
  case 39:
    return c.pull == ButtonPull::External;
  default:
    return false;
  }
}
#if defined(ARDUINO_ARCH_ESP32)
bool ArduinoButtonInput::begin(const ButtonGpioConfig &c, bool acknowledge) {
  ready_ = false;
  if (!acknowledge || !validateButtonGpioConfig(c))
    return false;
  config_ = c;
  pinMode(c.pin, c.pull == ButtonPull::Up     ? INPUT_PULLUP
                 : c.pull == ButtonPull::Down ? INPUT_PULLDOWN
                                              : INPUT);
  ready_ = true;
  return true;
}
bool ArduinoButtonInput::pressed() {
  if (!ready_)
    return false;
  return (digitalRead(config_.pin) == LOW) == config_.active_low;
}
#endif
} // namespace ridesync

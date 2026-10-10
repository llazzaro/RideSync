#pragma once
#ifdef ARDUINO
#include "gps_manager.h"
#include <Arduino.h>
namespace ridesync {
// No board defaults: qualification includes schematic/continuity, boot straps,
// peripheral conflicts, voltage, polarity, baud, and the exact modem profile.
struct QualifiedModemPins {
  bool pins_qualified = false, documentary_profile_opt_in = false;
  int tx = -1, rx = -1, supply = -1, key = -1;
  uint32_t baud = 0;
  bool supply_active_high = false, key_active_high = false;
};
class ArduinoModemUart : public ModemUart {
public:
  explicit ArduinoModemUart(HardwareSerial &serial) : serial_(serial) {}
  bool begin(const QualifiedModemPins &pins) {
    if (!pins.pins_qualified || !pins.documentary_profile_opt_in || pins.baud == 0 || pins.tx < 0 ||
        pins.tx > 33 || pins.rx < 0 || pins.rx > 39 || pins.tx == pins.rx)
      return false;
    serial_.begin(pins.baud, SERIAL_8N1, pins.rx, pins.tx);
    enabled_ = true;
    return true;
  }
  size_t available() override { return enabled_ ? serial_.available() : 0; }
  int read() override { return enabled_ ? serial_.read() : -1; }
  size_t writable() override { return enabled_ ? serial_.availableForWrite() : 0; }
  size_t write(const char *data, size_t length) override {
    // ESP32 HardwareSerial has a sole writer. Prechecking ensures this bounded
    // write fits the hardware's currently free TX space. No flush or busy wait.
    if (!enabled_ || length > writable())
      return 0;
    return serial_.write(reinterpret_cast<const uint8_t *>(data), length);
  }

private:
  HardwareSerial &serial_;
  bool enabled_ = false;
};
class ArduinoGnssPower : public GnssPowerControl {
public:
  explicit ArduinoGnssPower(const QualifiedModemPins &pins) : pins_(pins) {}
  bool begin(bool keep_supply_enabled = false) {
    if (!pins_.pins_qualified || !pins_.documentary_profile_opt_in || pins_.supply < 0 ||
        pins_.supply > 33 || pins_.key < 0 || pins_.key > 33 || pins_.key == pins_.supply ||
        pins_.key == pins_.tx || pins_.key == pins_.rx || pins_.supply == pins_.tx ||
        pins_.supply == pins_.rx)
      return false;
    digitalWrite(pins_.key, !pins_.key_active_high);
    pinMode(pins_.key, OUTPUT);
    // Shared modem/SD rails must stay asserted throughout a logging session.
    // Explicit retained-supply mode never introduces an inactive pulse.
    digitalWrite(pins_.supply,
                 keep_supply_enabled ? pins_.supply_active_high : !pins_.supply_active_high);
    pinMode(pins_.supply, OUTPUT);
    enabled_ = true;
    return true;
  }
  void enableSupply() override {
    if (enabled_)
      digitalWrite(pins_.supply, pins_.supply_active_high);
  }
  void key(bool active) override {
    if (enabled_)
      digitalWrite(pins_.key, active == pins_.key_active_high);
  }

private:
  QualifiedModemPins pins_;
  bool enabled_ = false;
};
} // namespace ridesync
#endif

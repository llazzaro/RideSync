#pragma once
#if defined(ARDUINO_ARCH_ESP32)
#include "handlebar_control.h"
#include "local_telemetry_esp32.h"
namespace ridesync {
struct QualifiedHandlebar {
  bool opt_in = false, acknowledge_qualification = false;
  ButtonConfig actions;
  ButtonGpioConfig button;
  LedWiring led;
};
struct HandlebarGpioStatus {
  ControlStatus control;
  bool input_ready = false, attached = false;
  LedBackendState backend = LedBackendState::Disabled;
  int backend_error = 0;
};
// Retained, callable composition on the SAME telemetry/global service route.
// Caller owns telemetry and its resources through canRelease(). Construct before
// telemetry.start(); all entry points run on the one application owner. Default
// configuration refuses IO/admission. Supervised startup supplies qualified activation.
class Esp32HandlebarControl {
public:
  Esp32HandlebarControl(Esp32LocalTelemetry &, const QualifiedHandlebar &);
  ~Esp32HandlebarControl();
  bool begin();
  void service();
  HandlebarGpioStatus status() const;

private:
  class RawClock : public Clock {
    uint32_t now() const override;
  } clock_;
  Esp32LocalTelemetry &telemetry_;
  const QualifiedHandlebar qualification_;
  ArduinoButtonInput input_;
  Esp32LedGpio gpio_;
  GpioLedSink sink_;
  HandlebarControl control_;
  bool attempted_ = false, input_ready_ = false, attached_ = false;
};
} // namespace ridesync
extern "C" ridesync::Esp32HandlebarControl &
ridesync_handlebar_runtime(ridesync::Esp32LocalTelemetry &, const ridesync::QualifiedHandlebar &);
extern "C" bool ridesync_handlebar_begin(ridesync::Esp32HandlebarControl &);
extern "C" void ridesync_handlebar_service(ridesync::Esp32HandlebarControl &);
#endif

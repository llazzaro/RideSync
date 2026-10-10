#include "handlebar_control_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "profiles/gopro_hero12_esp32.h"
#if defined(RIDESYNC_BENCH_APPLICATION) || defined(RIDESYNC_MIXED_COMPOSITION)
#define hero12Runtime mixedCameraRuntime
#endif
namespace ridesync {
uint32_t Esp32HandlebarControl::RawClock::now() const { return millis(); }
Esp32HandlebarControl::Esp32HandlebarControl(Esp32LocalTelemetry &t, const QualifiedHandlebar &q,
                                             MixedCameraRuntime *cameras)
    : telemetry_(t), qualification_(q), sink_(gpio_),
      control_(clock_,
               cameras ? static_cast<CameraRuntimePort &>(cameras->adapter)
                       : static_cast<CameraRuntimePort &>(hero12Runtime().adapter),
               cameras ? cameras->manager : hero12Runtime().manager,
               cameras ? cameras->group : hero12Runtime().group, input_, sink_),
      group_(cameras ? cameras->group : hero12Runtime().group) {}
Esp32HandlebarControl::~Esp32HandlebarControl() {
  if (attached_)
    telemetry_.owner().detachControl(control_);
}
bool Esp32HandlebarControl::begin() {
  if (attempted_)
    return false;
  attempted_ = true;
  const auto &q = qualification_;
  if (!q.opt_in || !q.acknowledge_qualification || !validateButtonConfig(q.actions) ||
      !validateButtonGpioConfig(q.button))
    return false;
  if (!telemetry_.owner().attachControl(control_))
    return false;
  attached_ = true;
  if (!control_.begin(q.actions)) {
    telemetry_.owner().detachControl(control_);
    attached_ = false;
    return false;
  }
  const auto backend = sink_.begin(q.led, q.acknowledge_qualification, q.button);
  if (backend != LedBackendState::Ready && backend != LedBackendState::Disabled) {
    telemetry_.owner().detachControl(control_);
    attached_ = false;
    group_.detachPreparation(control_);
    return false;
  }
  input_ready_ = input_.begin(q.button, q.acknowledge_qualification);
  if (!input_ready_) {
    telemetry_.owner().detachControl(control_);
    attached_ = false;
    group_.detachPreparation(control_);
  }
  return input_ready_;
}
void Esp32HandlebarControl::service() {
  telemetry_.service(); // Existing exclusive global route, exactly once.
}
HandlebarGpioStatus Esp32HandlebarControl::status() const {
  HandlebarGpioStatus s;
  s.control = control_.status();
  s.input_ready = input_ready_;
  s.attached = attached_;
  s.backend = sink_.state();
  s.backend_error = sink_.error();
  return s;
}
} // namespace ridesync
extern "C" ridesync::Esp32HandlebarControl &
ridesync_handlebar_runtime(ridesync::Esp32LocalTelemetry &telemetry,
                           const ridesync::QualifiedHandlebar &q) {
  static ridesync::Esp32HandlebarControl control(telemetry, q);
  return control;
}
extern "C" bool ridesync_handlebar_begin(ridesync::Esp32HandlebarControl &control) {
  return control.begin();
}
extern "C" void ridesync_handlebar_service(ridesync::Esp32HandlebarControl &control) {
  control.service();
}
#endif

// Synthetic SDK boundary only: real mixed adapter/manager/group still execute.
#include "ble_esp32.h"
#include "profiles/mixed_camera_esp32.h"
namespace ridesync {
namespace {
struct FixtureClock final : Clock {
  uint32_t now() const override { return millis(); }
};
struct InactiveX5Port final : X5PeripheralPort {
  bool configure(const X5Qualification &) override { return false; }
  bool connect(Token, uint32_t) override { return false; }
  bool notify(const X5ShutterRequest &) override { return false; }
  void cancel(Token) override {}
  void close(uint32_t) override {}
  bool poll(X5Input &) override { return false; }
  bool takeLoss(uint32_t) override { return false; }
  bool released(uint32_t) const override { return true; }
  void service(uint32_t) override {}
};
} // namespace
MixedCameraRuntime mixedCameraRuntime() {
  static FixtureClock clock;
  static InactiveX5Port peripheral;
  static MixedCameraAdapter adapter(Esp32BleHost::instance(), peripheral, clock);
  static CameraManager manager(clock, adapter, MixedCameraAdapter::managerPolicy());
  static RecordingManager group(manager, clock);
  static bool attached = (adapter.attach(manager, group), true);
  (void)attached;
  return {adapter, manager, group};
}
} // namespace ridesync

#include "profiles/gopro_hero12_esp32.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "ble_esp32.h"
#include <Arduino.h>

namespace ridesync {
namespace {
struct MillisClock final : Clock {
  uint32_t now() const override { return millis(); }
};
CameraEventSession *active_session = nullptr;
} // namespace
Hero12Runtime hero12Runtime() {
  static MillisClock clock;
  static Hero12Adapter adapter(Esp32BleHost::instance(), clock);
  static CameraManager manager(clock, adapter, Hero12Adapter::managerPolicy());
  static RecordingManager group(manager, clock);
  static bool attached = (adapter.attach(manager), adapter.attachGroup(group), true);
  (void)attached;
  return {adapter, manager, group};
}
bool hero12BindSession(CameraEventSession &session) {
  auto runtime = hero12Runtime();
  if (active_session || !session.active() ||
      !session.binds(runtime.adapter, runtime.manager, runtime.group))
    return false;
  active_session = &session;
  return true;
}
bool hero12UnbindStoppedSession() {
  if (!active_session || !active_session->stopped())
    return false;
  active_session = nullptr;
  return true;
}
} // namespace ridesync
extern "C" ridesync::Hero12Runtime ridesync_hero12_runtime() { return ridesync::hero12Runtime(); }
extern "C" void ridesync_hero12_service() {
  auto runtime = ridesync::hero12Runtime();
  if (ridesync::active_session)
    ridesync::active_session->service();
  else
    runtime.adapter.service();
}
#endif

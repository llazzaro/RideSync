#pragma once
#include "wake_manager.h"
namespace ridesync {
// Pure serialized policy. The ESP32 host guards access with a nonblocking gate;
// callback publications that cannot be copied quarantine the boot lease.
class WakeRadioPolicy {
public:
  bool reserve(const WakeOperation &, uint32_t deadline, uint32_t now);
  bool admitStart(const WakeOperation &, uint32_t now);
  void seal(const WakeOperation &);
  void returned(const WakeOperation &, int sdk_error);
  bool incoming(const WakeOperation &, uint16_t connection);
  void disconnected(const WakeOperation &, uint16_t connection);
  void terminal(const WakeOperation &);
  void callbackEnter();
  void callbackExit();
  void barrierReleased(const WakeOperation &);
  bool reusable() const;
  WakeRadioResult status() const;

private:
  WakeRadioResult result_;
  std::array<uint32_t, kWakePeers> last_ids_{}, generations_{};
  uint32_t deadline_ = 0;
  unsigned callbacks_ = 0;
  uint16_t connection_ = 0xffff;
  bool active_ = false, sealed_ = false, admitted_ = false, inflight_ = false;
  bool barrier_ = false, quarantined_ = false;
  bool matches(const WakeOperation &) const;
};
} // namespace ridesync

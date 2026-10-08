#pragma once
#include "modem_gnss.h"
#include "session_clock.h"
namespace ridesync {
class GnssPowerControl {
public:
  virtual ~GnssPowerControl() = default;
  // Nonblocking GPIO operations; caller-qualified polarities and pin routing.
  virtual void enableSupply() = 0;
  virtual void key(bool active) = 0;
};
struct QualifiedPowerTiming {
  bool qualified = false;
  uint32_t key_active_ms = 0, settle_ms = 0;
};
enum class PowerStage { Disabled, KeyActive, Settling, Complete, InvalidClock, Cancelled };
class GpsManager {
public:
  GpsManager(SessionClock &clock, ModemGnss &modem, GnssPowerControl *power = nullptr,
             const QualifiedPowerTiming &timing = QualifiedPowerTiming{});
  void tick();
  // Terminal owner cancellation: nonblocking release of an asserted key only.
  // Supply ownership remains with the caller. Later tick/restart cannot start IO.
  void cancel();
  // Same verified physical/receive barrier contract as ModemGnss. Caller also
  // asserts the qualified supply/reset sequence is complete. Does not replay
  // supply enable/PWRKEY; deasserts an active key before restarting AT startup.
  // Refuses after cancel(); a fresh owner/session is required after terminal stop.
  bool restartAfterVerifiedBarrier();
  ModemSnapshot snapshot();
  uint32_t completed() const { return completed_; }
  PowerStage powerStage() const { return stage_; }

private:
  SessionClock &clock_;
  ModemGnss &modem_;
  GnssPowerControl *power_;
  QualifiedPowerTiming timing_;
  PowerStage stage_;
  uint32_t completed_ = 0;
  bool started_ = false;
  uint64_t start_ms_ = 0, session_ = 0;
};
} // namespace ridesync

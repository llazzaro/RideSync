#include "gps_manager.h"
namespace ridesync {
GpsManager::GpsManager(SessionClock &clock, ModemGnss &modem, GnssPowerControl *power,
                       const QualifiedPowerTiming &timing)
    : clock_(clock), modem_(modem), power_(power), timing_(timing), stage_(PowerStage::Disabled) {}
void GpsManager::tick() {
  const auto now = clock_.snapshot();
  if (now.monotonic_quality != MonotonicQuality::Valid ||
      (started_ && now.session_id != session_)) {
    if (power_ && stage_ == PowerStage::KeyActive)
      power_->key(false);
    stage_ = PowerStage::InvalidClock;
    modem_.tick(now);
    return;
  }
  if (stage_ == PowerStage::InvalidClock)
    return;
  if (!started_) {
    if (!timing_.qualified || modem_.snapshot(now).state == ModemState::Disabled ||
        (power_ && (timing_.key_active_ms == 0 || timing_.settle_ms == 0 ||
                    timing_.key_active_ms > 60000 || timing_.settle_ms > 60000)))
      return;
    started_ = true;
    session_ = now.session_id;
    start_ms_ = now.monotonic_ms;
    if (power_) {
      power_->enableSupply();
      power_->key(true);
      stage_ = PowerStage::KeyActive;
    } else
      stage_ = PowerStage::Complete;
  }
  if (stage_ == PowerStage::KeyActive && now.monotonic_ms - start_ms_ >= timing_.key_active_ms) {
    power_->key(false);
    stage_ = PowerStage::Settling;
    start_ms_ = now.monotonic_ms;
  }
  if (stage_ == PowerStage::Settling && now.monotonic_ms - start_ms_ >= timing_.settle_ms)
    stage_ = PowerStage::Complete;
  if (stage_ == PowerStage::Complete)
    modem_.tick(now);
}
ModemSnapshot GpsManager::snapshot() { return modem_.snapshot(clock_.snapshot()); }
} // namespace ridesync

#include "gps_manager.h"
namespace ridesync {
GpsManager::GpsManager(SessionClock &clock, ModemGnss &modem, GnssPowerControl *power,
                       const QualifiedPowerTiming &timing, GpsSnapshotConsumer *consumer)
    : clock_(clock), modem_(modem), power_(power), timing_(timing), consumer_(consumer),
      stage_(PowerStage::Disabled) {}
void GpsManager::cancel() {
  if (stage_ == PowerStage::Cancelled)
    return;
  const bool release_key = power_ && stage_ == PowerStage::KeyActive;
  stage_ = PowerStage::Cancelled;
  if (consumer_)
    consumer_->cancel();
  if (release_key)
    power_->key(false);
}
void GpsManager::tick() {
  if (stage_ == PowerStage::Cancelled || stage_ == PowerStage::InvalidClock)
    return;
  const auto now = clock_.snapshot();
  if (now.monotonic_quality != MonotonicQuality::Valid ||
      (started_ && now.session_id != session_)) {
    if (power_ && stage_ == PowerStage::KeyActive)
      power_->key(false);
    stage_ = PowerStage::InvalidClock;
    // A session reset during the power sequence must invalidate the modem
    // even when it has not yet sampled any session; never start AT here.
    auto invalid = now;
    invalid.monotonic_quality = MonotonicQuality::InvalidSession;
    modem_.tick(invalid);
    if (consumer_)
      consumer_->cancel();
    if (completed_ != UINT32_MAX)
      ++completed_; // Returned terminal AT service, no UART command/restart.
    return;
  }
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
  if (stage_ == PowerStage::Complete) {
    modem_.tick(now);
    if (consumer_)
      consumer_->offer(now, modem_.snapshot(now));
    if (completed_ != UINT32_MAX)
      ++completed_;
  }
}
bool GpsManager::restartAfterVerifiedBarrier() {
  if (stage_ == PowerStage::Cancelled)
    return false;
  const auto now = clock_.snapshot();
  if (!timing_.qualified || now.monotonic_quality != MonotonicQuality::Valid ||
      now.session_id == 0 ||
      (power_ && (timing_.key_active_ms == 0 || timing_.settle_ms == 0 ||
                  timing_.key_active_ms > 60000 || timing_.settle_ms > 60000)))
    return false;
  // Never send an AT command while our previously asserted PWRKEY is active.
  // Even a rejected restart leaves that interrupted power pulse fail-closed.
  if (power_ && stage_ == PowerStage::KeyActive) {
    power_->key(false);
    stage_ = PowerStage::InvalidClock;
  }
  if (!modem_.restartAfterVerifiedBarrier(now))
    return false;
  // The caller's barrier includes qualified physical startup. Replaying a key
  // pulse here could power down/restart an already ready modem.
  started_ = true;
  session_ = now.session_id;
  start_ms_ = now.monotonic_ms;
  stage_ = PowerStage::Complete;
  return true;
}
ModemSnapshot GpsManager::snapshot() { return modem_.snapshot(clock_.snapshot()); }
} // namespace ridesync

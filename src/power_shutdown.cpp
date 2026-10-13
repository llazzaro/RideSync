#include "power_shutdown.h"
namespace ridesync {
namespace {
constexpr uint32_t kHalfRange = 0x80000000u;
bool intervalValid(uint32_t interval) { return interval != 0 && interval < kHalfRange; }
bool observationsValid(const PowerObservation &o, bool voltage) {
  return (o.acc == PowerSignal::Inactive || o.acc == PowerSignal::Active) &&
         (o.power_fail == PowerSignal::Inactive || o.power_fail == PowerSignal::Active) &&
         (o.supply == PowerSupply::Unusable || o.supply == PowerSupply::Usable) &&
         (o.reserve == PowerReserve::NotReady || o.reserve == PowerReserve::Ready) &&
         (!voltage || o.voltage_valid);
}
} // namespace

PowerShutdown::PowerShutdown(const PowerShutdownConfig &config) : config_(config) {
  status_.enabled = config.enabled;
  status_.qualified = config.qualified;
  status_.config_valid =
      intervalValid(config.startup_ms) && intervalValid(config.acc_off_ms) &&
      intervalValid(config.undervoltage_ms) && intervalValid(config.drain_ms) &&
      (!config.voltage_enabled ||
       (config.voltage_enter_mv != 0 && config.voltage_enter_mv < config.voltage_clear_mv));
  status_.automatic_active = config.enabled && config.qualified && status_.config_valid;
  // Startup has no prior healthy voltage episode: require a clear-threshold
  // sample before a diagnostic voltage profile can claim usable protection.
  voltage_low_ = config.voltage_enabled;
  if (status_.automatic_active)
    status_.state = PowerShutdownState::BootGuard;
}

void PowerShutdown::seal(uint32_t now, PowerShutdownCause cause, PowerShutdownActions &actions) {
  status_.sealed = true;
  status_.cause = cause;
  status_.state = PowerShutdownState::Draining;
  status_.protected_ready = status_.admission_allowed = false;
  sealed_at_ = now;
  actions.seal = actions.request_drain = true;
}

void PowerShutdown::finish(PowerShutdownOutcome outcome, PowerShutdownActions &actions) {
  status_.outcome = outcome;
  switch (outcome) {
  case PowerShutdownOutcome::Clean:
    status_.state = PowerShutdownState::Clean;
    break;
  case PowerShutdownOutcome::Failed:
    status_.state = PowerShutdownState::Failed;
    break;
  default:
    status_.state = PowerShutdownState::TimedOut;
    break;
  }
  actions.report = true;
  if (status_.automatic_active && config_.off_qualified) {
    actions.request_off = true;
    off_requested_ = true;
  }
}

void PowerShutdown::drain(uint32_t now, const PowerObservation &o, bool discontinuity,
                          PowerShutdownActions &actions) {
  // Completion and failures are independent monotonic observations. Never
  // manufacture a lifetime barrier because of a deadline or a hardware request.
  status_.teardown_complete = status_.teardown_complete || o.teardown_complete;
  status_.io_error = status_.io_error || o.io_error;
  status_.terminal_loss = status_.terminal_loss || o.terminal_loss;
  if (status_.outcome != PowerShutdownOutcome::None) {
    if (status_.outcome == PowerShutdownOutcome::Clean &&
        (status_.io_error || status_.terminal_loss)) {
      status_.outcome = PowerShutdownOutcome::Failed;
      status_.state = PowerShutdownState::Failed;
    }
    if (off_requested_)
      status_.state = PowerShutdownState::OffPending;
    return;
  }
  status_.elapsed_ms = discontinuity ? kHalfRange - 1 : now - sealed_at_;
  if (status_.io_error || status_.terminal_loss)
    finish(PowerShutdownOutcome::Failed, actions);
  else if (discontinuity || status_.elapsed_ms >= config_.drain_ms)
    finish(PowerShutdownOutcome::TimedOut, actions);
  else if (status_.teardown_complete)
    finish(PowerShutdownOutcome::Clean, actions);
}

PowerShutdownActions PowerShutdown::service(uint32_t now, const PowerObservation &o,
                                            bool manual_shutdown) {
  PowerShutdownActions actions;
  if (!status_.config_valid)
    return actions;
  const bool discontinuity = serviced_ && now - last_service_ >= kHalfRange;
  serviced_ = true;
  last_service_ = now;
  if (status_.sealed) {
    drain(now, o, discontinuity, actions);
    return actions;
  }

  // A real upstream failure dominates debounce, manual intent and invalid
  // diagnostic samples, but disabled automatic sensing remains entirely inert.
  if (status_.automatic_active && o.power_fail == PowerSignal::Active)
    seal(now, PowerShutdownCause::SupplyFail, actions);
  else if (manual_shutdown)
    seal(now, PowerShutdownCause::Manual, actions);
  else if (!status_.automatic_active)
    return actions;
  else if (discontinuity && status_.state != PowerShutdownState::BootGuard)
    seal(now, PowerShutdownCause::ClockDiscontinuity, actions);

  if (status_.sealed) {
    // A discontinuity before sealing starts a fresh conservative drain budget.
    drain(now, o, false, actions);
    return actions;
  }

  const bool valid = observationsValid(o, config_.voltage_enabled);
  if (config_.voltage_enabled && (!o.voltage_valid || discontinuity))
    voltage_low_ = true;
  if (config_.voltage_enabled && o.voltage_valid) {
    if (o.supply_mv <= config_.voltage_enter_mv)
      voltage_low_ = true;
    else if (o.supply_mv >= config_.voltage_clear_mv)
      voltage_low_ = false;
  }
  const bool ready = valid && o.acc == PowerSignal::Active &&
                     o.power_fail == PowerSignal::Inactive && o.supply == PowerSupply::Usable &&
                     o.reserve == PowerReserve::Ready && !voltage_low_;
  status_.protected_ready = false;
  if (status_.state == PowerShutdownState::BootGuard) {
    if (discontinuity)
      startup_started_ = false;
    if (!ready) {
      startup_started_ = false;
      return actions;
    }
    if (!startup_started_) {
      startup_at_ = now;
      startup_started_ = true;
    }
    if (now - startup_at_ >= config_.startup_ms) {
      status_.state = PowerShutdownState::Running;
      status_.protected_ready = status_.admission_allowed = true;
      actions.admit_session = true;
    }
    return actions;
  }

  if (!valid)
    seal(now, PowerShutdownCause::ObservationInvalid, actions);
  else if (o.supply == PowerSupply::Unusable)
    seal(now, PowerShutdownCause::SupplyUnusable, actions);
  else if (o.reserve != PowerReserve::Ready)
    seal(now, PowerShutdownCause::ReserveLost, actions);
  // Equality commits the old suspicion before this pass can clear it. Neither
  // duplicate events nor a supply reading inside the hysteresis band rebase it.
  else if (acc_suspect_ && now - acc_off_at_ >= config_.acc_off_ms)
    seal(now, PowerShutdownCause::AccOff, actions);
  else if (voltage_suspect_ && now - voltage_low_at_ >= config_.undervoltage_ms)
    seal(now, PowerShutdownCause::Undervoltage, actions);
  if (status_.sealed) {
    drain(now, o, false, actions);
    return actions;
  }

  if (o.acc == PowerSignal::Inactive && !acc_suspect_) {
    acc_suspect_ = true;
    acc_off_at_ = now;
  } else if (o.acc == PowerSignal::Active && !voltage_low_)
    acc_suspect_ = false;
  if (voltage_low_ && !voltage_suspect_) {
    voltage_suspect_ = true;
    voltage_low_at_ = now;
  } else if (!voltage_low_)
    voltage_suspect_ = false;
  status_.state =
      acc_suspect_ || voltage_suspect_ ? PowerShutdownState::Suspect : PowerShutdownState::Running;
  status_.protected_ready = ready && status_.state == PowerShutdownState::Running;
  return actions;
}

PowerShutdownStatus PowerShutdown::status(uint32_t now) const {
  auto result = status_;
  if (status_.outcome != PowerShutdownOutcome::None)
    return result;
  if (serviced_ && now - last_service_ >= kHalfRange) {
    result.protected_ready = result.admission_allowed = false;
    result.elapsed_ms = kHalfRange - 1;
  } else if (status_.sealed)
    result.elapsed_ms = now - sealed_at_;
  else if (status_.state == PowerShutdownState::BootGuard && startup_started_)
    result.elapsed_ms = now - startup_at_;
  else if (acc_suspect_)
    result.elapsed_ms = now - acc_off_at_;
  else if (voltage_suspect_)
    result.elapsed_ms = now - voltage_low_at_;
  return result;
}
} // namespace ridesync

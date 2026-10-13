#pragma once
#include <stdint.h>

namespace ridesync {
enum class PowerSignal : uint8_t { Unknown, Inactive, Active };
enum class PowerSupply : uint8_t { Unknown, Unusable, Usable };
enum class PowerReserve : uint8_t { Unknown, NotReady, Ready };
enum class PowerShutdownState : uint8_t {
  Disabled,
  BootGuard,
  Running,
  Suspect,
  Draining,
  Clean,
  Failed,
  TimedOut,
  OffPending
};
enum class PowerShutdownCause : uint8_t {
  None,
  AccOff,
  SupplyFail,
  SupplyUnusable,
  Undervoltage,
  ReserveLost,
  ObservationInvalid,
  ClockDiscontinuity,
  Manual
};
enum class PowerShutdownOutcome : uint8_t { None, Clean, Failed, TimedOut };

// Fixed owner snapshot. Active ACC means on; Active power_fail means asserted.
// Unknown is unavailable/invalid, never permission to claim protection ready.
// Completion means ALL real lifetime barriers, independent of IO/loss status.
struct PowerObservation {
  PowerSignal acc = PowerSignal::Unknown;
  PowerSignal power_fail = PowerSignal::Unknown;
  PowerSupply supply = PowerSupply::Unknown;
  PowerReserve reserve = PowerReserve::Unknown;
  bool voltage_valid = false;
  uint32_t supply_mv = 0;
  bool teardown_complete = false;
  bool io_error = false;
  bool terminal_loss = false;
};
struct PowerShutdownConfig {
  bool enabled = false;
  bool qualified = false;
  bool off_qualified = false;
  // Synthetic initial fixtures, NOT measured electrical hold-up guarantees.
  uint32_t startup_ms = 2000;
  uint32_t acc_off_ms = 500;
  uint32_t undervoltage_ms = 50;
  uint32_t drain_ms = 5000;
  bool voltage_enabled = false;
  // A separately qualified voltage profile must supply enter < clear.
  uint32_t voltage_enter_mv = 0;
  uint32_t voltage_clear_mv = 0;
};
struct PowerShutdownActions {
  // All actions are edge outputs from service; consume in the same owner pass.
  // Admit a new protected session, never start a camera or replay intent.
  bool admit_session = false;
  bool seal = false;
  bool request_drain = false;
  bool report = false;
  // Request to separately qualified hardware, never proof of power removal.
  bool request_off = false;
};
struct PowerShutdownStatus {
  PowerShutdownState state = PowerShutdownState::Disabled;
  PowerShutdownCause cause = PowerShutdownCause::None;
  PowerShutdownOutcome outcome = PowerShutdownOutcome::None;
  bool enabled = false;
  bool qualified = false;
  bool config_valid = false;
  bool automatic_active = false;
  bool protected_ready = false;
  bool admission_allowed = false;
  bool sealed = false;
  bool teardown_complete = false;
  bool io_error = false;
  bool terminal_loss = false;
  // Current startup/suspicion/drain age; frozen at terminal publication. A
  // discontinuous drain reports 2^31-1, not an invented elapsed duration.
  uint32_t elapsed_ms = 0;
};

// Single serialized owner only; no IO, allocation, waits or cross-context use.
// All configured intervals AND successive service gaps must be <2^31 ms.
// One instance represents one boot/session episode; sealed sessions never rearm.
// Invalid config is wholly inert. Valid disabled/unqualified config still
// accepts manual shutdown, but never emits automatic admission or hardware OFF.
// Late failure revokes Clean in status without repeating report/OFF actions.
class PowerShutdown {
public:
  explicit PowerShutdown(const PowerShutdownConfig &config = PowerShutdownConfig{});
  PowerShutdownActions service(uint32_t now, const PowerObservation &observation,
                               bool manual_shutdown = false);
  PowerShutdownStatus status(uint32_t now) const;

private:
  void seal(uint32_t now, PowerShutdownCause cause, PowerShutdownActions &actions);
  void finish(PowerShutdownOutcome outcome, PowerShutdownActions &actions);
  void drain(uint32_t now, const PowerObservation &observation, bool discontinuity,
             PowerShutdownActions &actions);
  PowerShutdownConfig config_;
  PowerShutdownStatus status_;
  uint32_t last_service_ = 0, startup_at_ = 0, acc_off_at_ = 0, voltage_low_at_ = 0;
  uint32_t sealed_at_ = 0;
  bool serviced_ = false, startup_started_ = false, acc_suspect_ = false;
  bool voltage_suspect_ = false, voltage_low_ = false, off_requested_ = false;
};
} // namespace ridesync

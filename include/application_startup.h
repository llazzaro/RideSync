#pragma once
#include "config_bootstrap.h"
#include "health_supervisor.h"
namespace ridesync {
enum class StartupState : uint8_t {
  WaitingConfig,
  ConfigReady,
  ConfigUnavailable,
  ConfigTimedOut,
  WaitingSupervisor,
  Running,
  SupervisionFailed
};
// Application-owner seam. prepare fixes policy without device IO; launch occurs
// only after the dedicated supervisor has acquired its own SDK subscription.
// Supplied commissioning owner/resources remain alive for the entire boot.
class ApplicationWorkers {
public:
  virtual ~ApplicationWorkers() = default;
  virtual CameraPeers peers() const { return {}; }
  virtual std::array<WorkerPolicy, 4> prepare(const SettingsSnapshot &, bool safe_mode,
                                              bool nvs_allowed, HealthSupervisor &) = 0;
  virtual void launch() = 0;
  virtual void current(const SettingsSnapshot &, bool safe_mode, bool nvs_allowed) = 0;
  virtual void service(uint8_t stalled, uint8_t refused, bool supervision_fault) = 0;
};
// Weak default returns nullptr. A commissioned board supplies a strong provider;
// no default buses, pin mapping, protocol or namespace is inferred here.
ApplicationWorkers *commissionedApplication();
} // namespace ridesync

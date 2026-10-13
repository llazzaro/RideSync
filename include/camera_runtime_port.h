#pragma once
#include "camera_manager.h"
#include "camera_marker.h"
namespace ridesync {
class RecordingManager;
// Serialized owner delivery; callbacks never touch evaluator or storage.
class CameraHealthSampleSink {
public:
  virtual ~CameraHealthSampleSink() = default;
  virtual void healthSample(uint8_t peer, const QualifiedHealthSample &) = 0;
  virtual void healthPollPeriod(uint8_t peer, uint32_t floor_ms, uint32_t now) = 0;
};
class CameraServiceAction {
public:
  virtual ~CameraServiceAction() = default;
  virtual void beforeAdvance() = 0;
};
enum class Hero12Fault : uint8_t {
  None,
  Disabled,
  Qualification,
  Capacity,
  Transport,
  SetupRejected,
  IdentityMismatch,
  CameraRejected,
  Protocol,
  Timeout,
  DeliveryUncertain,
  Store,
  MissingService,
  MissingProperty,
  MissingCccd,
  Att
};
// These are caller-supplied conditions, not classifications inferred from a scan.
enum class Hero12PowerCondition : uint8_t { Unknown, BleConnectable, PowerRemoved };
enum class Hero12RecoveryPhase : uint8_t {
  Idle,
  Pending,
  Scanning,
  Connecting,
  Observing,
  Starting,
  Ready,
  Recording,
  Unavailable,
  Timeout,
  Cancelled,
  Unsupported,
  Failed
};
struct Hero12RecoveryState {
  Hero12RecoveryPhase phase = Hero12RecoveryPhase::Idle;
  uint8_t scans = 0;
};
// A serialized, model-dispatched camera owner. Existing HERO12 status types
// are retained as compatibility vocabulary; unsupported capabilities stay explicit.
class CameraRuntimePort {
public:
  virtual ~CameraRuntimePort() = default;
  virtual bool attachHealthSink(CameraHealthSampleSink &) { return false; }
  virtual void detachHealthSink(const CameraHealthSampleSink *) {}
  virtual uint32_t healthCoalesced() const { return 0; }
  virtual uint32_t healthFailures() const { return 0; }
  virtual bool attachMarkerAudit(CameraMarkerAudit &) { return false; }
  virtual void detachMarkerAudit(const CameraMarkerAudit *) {}
  virtual CameraError requestHilight(uint8_t, CameraMarkerKey) { return CameraError::Unsupported; }
  virtual void attachGroup(RecordingManager &) = 0;
  virtual void detachGroup(const RecordingManager *) = 0;
  virtual void service(CameraServiceAction *action = nullptr) = 0;
  virtual void stop() = 0;
  virtual bool canDestroy() const = 0;
  virtual Hero12Fault fault(uint8_t) const = 0;
  virtual CameraError requestRecovery(uint8_t, bool,
                                      Hero12PowerCondition = Hero12PowerCondition::Unknown) = 0;
  virtual CameraError cancelRecovery(uint8_t) = 0;
  virtual Hero12RecoveryState recoveryState(uint8_t) const = 0;
  virtual bool recoveryReady(uint8_t) const = 0;
  virtual bool commandReady(uint8_t) const = 0;
  virtual bool wakeSupported(uint8_t) const { return false; }
  virtual bool linkRetiring(uint8_t) const = 0;
  virtual bool linkReleased(uint8_t) const = 0;
};
} // namespace ridesync

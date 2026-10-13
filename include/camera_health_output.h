#pragma once
#include "camera_health_logger.h"
namespace ridesync {
struct CameraHealthSummary {
  uint32_t dropped = 0, coalesced = 0, wire_failures = 0, wire_coalesced = 0;
  HealthReason reason = HealthReason::UnexpectedStop;
  HealthSeverity severity = HealthSeverity::None;
  uint8_t peer = 0;
  bool enabled = false, critical = false, warning = false;
};
CameraHealthSummary summarizeCameraHealth(const CameraHealthEvaluator &, bool enabled);
const char *cameraHealthReasonName(HealthReason);
class CameraHealthSerialPort {
public:
  virtual ~CameraHealthSerialPort() = default;
  virtual size_t writable() const = 0;
  virtual size_t write(const char *, size_t) = 0;
};
class CameraHealthSerialSink final : public CameraHealthTransitionSink {
public:
  explicit CameraHealthSerialSink(CameraHealthSerialPort &port) : port_(port) {}
  void transition(const CameraHealthTransition &, uint32_t, CameraModel, uint32_t,
                  uint32_t) override;
  uint32_t dropped() const { return dropped_; }

private:
  CameraHealthSerialPort &port_;
  uint32_t dropped_ = 0;
};
} // namespace ridesync

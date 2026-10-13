#pragma once
#include "camera_health.h"
#include "telemetry_admission.h"
namespace ridesync {
// Owner sink runs once per drained transition even if Storage refuses it.
// It must be bounded and must not retain references or generate camera IO.
class CameraHealthTransitionSink {
public:
  virtual ~CameraHealthTransitionSink() = default;
  virtual void transition(const CameraHealthTransition &, uint32_t opaque_id, CameraModel,
                          uint32_t dropped, uint32_t coalesced) = 0;
};
// Drains the evaluator's ONE 8-entry ring; no additional health inbox exists.
// All calls share the serialized application/GPS/SessionClock owner context.
class CameraHealthLogger final : public HealthLogSource {
public:
  static constexpr uint8_t kQuota = 2;
  CameraHealthLogger(CameraHealthEvaluator &, TelemetryAdmission &,
                     CameraHealthTransitionSink *sink = nullptr);
  bool configurePeer(size_t peer, uint32_t opaque_id, CameraModel model);
  bool attached() const { return attached_; }
  void drain(TelemetryAdmission &) override;
  void requestStop() override { stop_ = true; }
  bool stopRequested() const { return stop_; }
  // Only after evaluator final retirement; no later evaluator publications.
  void finish() { finished_ = true; }
  bool drained() const override;

private:
  struct Identity {
    uint32_t id = 0;
    CameraModel model = CameraModel::Unknown;
  };
  CameraHealthEvaluator &evaluator_;
  CameraHealthTransitionSink *sink_;
  std::array<Identity, kMaxCameras> identities_{};
  uint32_t dropped_seen_ = 0;
  bool attached_ = false, sealed_ = false, stop_ = false, finished_ = false;
};
static_assert(sizeof(CameraHealthLogger) <= 112, "Health logger fixed memory budget");
} // namespace ridesync

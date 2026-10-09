#pragma once
#include "motion_types.h"
#include "telemetry_record.h"

namespace ridesync {
// Fixed-size static estimator; no integration, averaging, allocation or clock.
// Call update for every evidence record in order and reset on terminal/lost
// reference. Output is replaced each time; no prior attitude bridges a gap.
// No clock/expiry: caller retains input identity/time and resets on no-new-data.
// estimate() is a snapshot, never an indefinitely live-valid reading.
class MotionEstimator {
public:
  static bool configValid(const MotionEstimatorConfig &);
  explicit MotionEstimator(const MotionEstimatorConfig &config) : config_(config) {}
  MotionEstimate update(const ImuEvidence &e, const StaticMotionReference &reference);
  void reset() { estimate_ = {}; }
  const MotionEstimate &estimate() const { return estimate_; }

private:
  MotionEstimatorConfig config_;
  MotionEstimate estimate_;
  uint32_t last_declaration_ = 0;
};
} // namespace ridesync

#pragma once
#include "motion_estimator.h"
namespace ridesync {
enum class MotionInputRoute : uint8_t { Direct, Inbox };
// Serialized admission owner calls this retained, nonblocking source. Each new
// declaration must independently attest stationary initialization for this sample.
class DynamicMotionReferenceSource {
public:
  virtual ~DynamicMotionReferenceSource() = default;
  virtual DynamicMotionReference referenceFor(const ImuEvidence &) = 0;
};
struct MotionAdmissionConfig {
  bool requested = false, imu_qualified = false;
  MotionEstimatorConfig estimator;
  uint32_t snapshot_max_age_ms = 0;
  MotionInputRoute route = MotionInputRoute::Direct;
  DynamicMotionConfig dynamic;
  uint32_t dynamic_cadence_us = 0;
  DynamicMotionReferenceSource *dynamic_reference = nullptr;
};
inline bool motionAdmissionQualified(const MotionAdmissionConfig &m) {
  return m.imu_qualified && MotionEstimator::configValid(m.estimator) && m.snapshot_max_age_ms &&
         m.snapshot_max_age_ms <= 60000 &&
         (!m.dynamic.enabled ||
          (DynamicMotionEstimator::configValid(m.dynamic) && m.dynamic_cadence_us &&
           m.dynamic_cadence_us <= m.dynamic.max_step_us && m.dynamic_reference));
}
// Caller-owned external qualification; only the serialized admission owner calls.
// No IO/allocation/wait/lock/throw/reentry; outlives runtime canRelease().
class StaticMotionReferenceSource {
public:
  virtual ~StaticMotionReferenceSource() = default;
  virtual StaticMotionReference referenceFor(const ImuEvidence &) = 0;
};
struct MotionSampleIdentity {
  uint64_t session_id = 0;
  uint32_t generation = 0, batch = 0, frame = 0, byte_position = 0;
  uint32_t sensor_epoch = 0, receipt_millis32 = 0;
  bool receipt_known = false;
  uint8_t timing_flags = 0;
};
struct MotionCurrentSnapshot {
  bool current = false;
  MotionEstimate estimate;
  MotionSampleIdentity source;
  RecordTimestamp admitted_at;
};
} // namespace ridesync

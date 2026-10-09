#pragma once
#include "motion_estimator.h"
namespace ridesync {
enum class MotionInputRoute : uint8_t { Direct, Inbox };
struct MotionAdmissionConfig {
  bool requested = false, imu_qualified = false;
  MotionEstimatorConfig estimator;
  uint32_t snapshot_max_age_ms = 0;
  MotionInputRoute route = MotionInputRoute::Direct;
};
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

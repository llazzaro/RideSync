#pragma once
#include "camera_manager.h"
#include "modem_gnss.h"
#include <cstdint>
#include <type_traits>
namespace ridesync {
enum class RecordKind : uint8_t { Gps, ImuSample, ImuConfig, ImuHealth, ImuControl, Camera, Count };
enum class StorageFormat : uint8_t { GpsV1, MixedV2, CameraV3 };
enum class Qualification : uint8_t { Unknown, Unqualified, Qualified };
// IDs are caller assigned opaque uint32 values; zero = unknown. This schema
// does not establish identity uniqueness, a mount transform, or calibration.
struct ImuConfig {
  uint32_t generation = 0, sensor_id = 0, mount_id = 0, calibration_id = 0;
  Qualification sensor_state = Qualification::Unknown, mount_state = Qualification::Unknown,
                calibration_state = Qualification::Unknown;
  uint32_t accel_range_mg = 0, gyro_range_mdps = 0;
  // Exact rational g/count and dps/count. Both zero = unknown; denominator
  // zero with numerator nonzero is invalid. No converted measurements logged.
  uint32_t accel_scale_numerator = 0, accel_scale_denominator = 0;
  uint32_t gyro_scale_numerator = 0, gyro_scale_denominator = 0;
  uint32_t accel_odr_millihz = 0, gyro_odr_millihz = 0;
  uint32_t accel_filter = 0, gyro_filter = 0;
  // 0 unknown, 1 disabled, 2 enabled; host cross-axis/remap must be disabled
  // for sensor-frame raw counts. Device compensation is separately described.
  uint8_t accel_offset_compensation = 0, gyro_offset_compensation = 0;
  uint32_t calibration_method = 0;
  bool calibration_time_known = false, calibration_temperature_known = false;
  int64_t calibration_utc_ms = 0;
  int32_t calibration_temperature_millic = 0;
  bool calibration_offsets_known = false, calibration_gains_known = false;
  int16_t accel_offset[3]{}, gyro_offset[3]{};
  // Optional dimensionless per-axis calibration gains, retained but never applied.
  uint32_t accel_gain_numerator[3]{}, accel_gain_denominator[3]{};
  uint32_t gyro_gain_numerator[3]{}, gyro_gain_denominator[3]{};
};
// Transport worker evidence: millis32 is the raw modulo 2^32 host Clock domain,
// NOT session milliseconds. No host timestamp is a frame acquisition time.
struct ImuEvidence {
  uint64_t session_id = 0;
  RecordKind kind = RecordKind::ImuSample;
  ImuConfig config;
  uint32_t batch_sequence = 0, frame_sequence = 0, byte_position = 0, sensor_epoch = 0;
  bool receipt_known = false, drain_known = false;
  uint32_t receipt_millis32 = 0, drain_start_millis32 = 0, drain_end_millis32 = 0;
  // Explicit transport-observed flags (bit0 discontinuity, bit1 stale receipt,
  // bit2 wrap/reset ambiguous, bit3 software endpoint indication).
  uint8_t timing_flags = 0;
  int16_t accel[3]{}, gyro[3]{};
  // 0 unknown, 1 unsupported, 2 transport error, 3 partial frame, 4 skipped,
  // 5 sensor time at FIFO read boundary, 6 input config, 7 full indication,
  // 8 reset, 9 flush. Payload retains control bytes in source order.
  uint8_t event_code = 0, event_length = 0, event_bytes[4]{};
  bool sensor_time_present = false;
  uint32_t sensor_time_ticks24 = 0;
  // Event counters are transport evidence; 255 skip count is a lower bound.
  uint32_t event_count = 0;
  bool event_count_lower_bound = false;
};
enum class CameraEventKind : uint8_t {
  RequestAccepted,
  RequestQueued,
  RequestRefused,
  Attempt,
  WireAck,
  RecordingObserved,
  ManagerCompleted,
  Failed,
  Cancelled,
  Disconnected
};
// Owner-admission timestamp is in TelemetryRecord::timestamp. The event receipt
// is the manager audit hook's raw host clock sample, translated by the telemetry
// owner. It is not BLE radio receipt or camera acquisition. IDs are opaque.
struct CameraEvidence {
  uint64_t session_id = 0;
  uint64_t event_receipt_ms = 0;
  uint32_t event_receipt_raw32 = 0, event_receipt_age_ms = 0;
  bool event_receipt_known = false;
  uint8_t peer_slot = 0;
  uint32_t peer_id = 0, group_generation = 0, intent_id = 0;
  uint32_t connection_generation = 0, operation_generation = 0;
  CameraModel model = CameraModel::Unknown;
  CameraEventKind kind = CameraEventKind::RequestRefused;
  Operation operation = Operation::Connect;
  CameraError error = CameraError::None;
  RecordingState recording = RecordingState::Unknown;
  CameraAckDomain ack_domain = CameraAckDomain::None;
  CameraAckAction ack_action = CameraAckAction::None;
  bool delivery_admitted = false;
};
struct TelemetryRecord {
  RecordKind kind = RecordKind::Gps;
  RecordTimestamp timestamp;
  ModemSnapshot gps;
  ImuEvidence imu;
  CameraEvidence camera;
};
static_assert(std::is_trivially_copyable<TelemetryRecord>::value,
              "Telemetry records must own copied fixed data");
static_assert(sizeof(ImuEvidence) <= 240 && sizeof(TelemetryRecord) <= 544,
              "Review telemetry RAM bounds when changing the envelope");
struct KindHealth {
  uint32_t accepted, dropped, rejected, written, flushed, lost;
};
} // namespace ridesync

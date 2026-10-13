#include "camera_health_output.h"
#include <cstdio>
namespace ridesync {
const char *cameraHealthReasonName(HealthReason r) {
  static const char *const names[] = {
      "UnexpectedStop", "RecordingUnconfirmed", "RecordingEvidenceLost", "BatteryLow",
      "StorageLow",     "Overheating",          "MonitoringUnavailable", "ResourceUnavailable"};
  const auto i = static_cast<size_t>(r);
  return i < static_cast<size_t>(HealthReason::Count) ? names[i] : "Unknown";
}
CameraHealthSummary summarizeCameraHealth(const CameraHealthEvaluator &e, bool enabled) {
  CameraHealthSummary s;
  s.enabled = enabled;
  if (!enabled)
    return s;
  s.dropped = e.dropped();
  s.coalesced = e.coalesced();
  s.wire_coalesced = e.historyCoalesced();
  for (size_t i = 0; i < kMaxCameras; ++i) {
    const auto *p = e.snapshot(i);
    if (!p || p->model == CameraModel::Unknown)
      continue;
    for (size_t j = 0; j < static_cast<size_t>(HealthReason::Count); ++j) {
      const auto severity = e.severity(i, static_cast<HealthReason>(j));
      s.critical |= severity == HealthSeverity::Critical;
      s.warning |= severity == HealthSeverity::Warning ||
                   (severity == HealthSeverity::Status &&
                    j == static_cast<size_t>(HealthReason::MonitoringUnavailable));
      if (severity > s.severity || (severity == s.severity && severity != HealthSeverity::None &&
                                    (j < static_cast<size_t>(s.reason) ||
                                     (j == static_cast<size_t>(s.reason) && i < s.peer)))) {
        s.severity = severity;
        s.reason = static_cast<HealthReason>(j);
        s.peer = static_cast<uint8_t>(i);
      }
    }
  }
  return s;
}
void CameraHealthSerialSink::transition(const CameraHealthTransition &t, uint32_t id,
                                        CameraModel model, uint32_t dropped, uint32_t coalesced) {
  char line[128]; // Longest qualified line is 126 bytes; fits the default UART FIFO.
  const int n = std::snprintf(
      line, sizeof line, "HEALTH p=%u id=%lu m=%u e=%lu s=%lu k=%u %s/%u v=%u a=%lu d=%lu c=%lu\n",
      unsigned(t.peer), static_cast<unsigned long>(id), unsigned(model),
      static_cast<unsigned long>(t.episode), static_cast<unsigned long>(t.sequence),
      unsigned(t.kind), cameraHealthReasonName(t.reason), unsigned(t.severity),
      unsigned(t.validity), static_cast<unsigned long>(t.age_ms),
      static_cast<unsigned long>(dropped), static_cast<unsigned long>(coalesced));
  if (n <= 0 || static_cast<size_t>(n) >= sizeof line ||
      port_.writable() < static_cast<size_t>(n) ||
      port_.write(line, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
    if (dropped_ != UINT32_MAX)
      ++dropped_;
  }
}
} // namespace ridesync

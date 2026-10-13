#pragma once
#include <stdint.h>
namespace ridesync {
// A key from a locally admitted bookmark. The camera does not receive it.
struct CameraMarkerKey {
  uint64_t session_id = 0;
  uint32_t bookmark_id = 0;
  CameraMarkerKey() = default;
  CameraMarkerKey(uint64_t session, uint32_t bookmark)
      : session_id(session), bookmark_id(bookmark) {}
};
enum class CameraMarkerPhase : uint8_t { RequestRefused, Attempt, WireAck, Failed, Cancelled };
struct CameraMarkerEvent {
  CameraMarkerKey key;
  uint32_t connection_generation = 0, operation_generation = 0;
  uint8_t peer = 0;
  CameraMarkerPhase phase = CameraMarkerPhase::RequestRefused;
  uint8_t error = 0; // CameraError value; kept independent of manager headers.
  bool delivery_admitted = false;
};
class CameraMarkerAudit {
public:
  virtual ~CameraMarkerAudit() = default;
  virtual void markerEvent(const CameraMarkerEvent &) = 0;
};
} // namespace ridesync

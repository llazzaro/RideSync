#pragma once
#include "camera_profile.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace ridesync {
namespace dji {
constexpr size_t kReceiveCapacity = 256;
struct Packet {
  std::array<uint8_t, 64> bytes{};
  size_t size = 0;
};
// DJI model IDs, independent of RideSync's model enumeration.
constexpr uint32_t modelWireId(unsigned model) {
  return model == 4     ? 0xFF33
         : model == 5   ? 0xFF44
         : model == 6   ? 0xFF55
         : model == 360 ? 0xFF66
                        : 0;
}
Packet encodeConnection(uint16_t sequence, uint32_t controller_id,
                        const std::array<uint8_t, 6> &mac, uint8_t verify_mode,
                        uint16_t verify_data);
Packet encodeAuthorizationResponse(uint16_t sequence, uint32_t controller_id,
                                   uint8_t return_code = 0, uint32_t camera_index = 0);
Packet encodeRecord(uint16_t sequence, uint32_t device_id, bool start);
Packet encodeStatusSubscription(uint16_t sequence, bool periodic);
Packet encodeVideoMode(uint16_t sequence, uint32_t device_id);
enum class Kind : uint8_t {
  Invalid,
  Pending,
  Unknown,
  ConnectionResponse,
  ConnectionRequest,
  RecordResponse,
  Status,
  ModeResponse
};
struct Decoded {
  Kind kind = Kind::Invalid;
  uint16_t sequence = 0;
  uint8_t cmd_set = 0, cmd_id = 0, cmd_type = 0, reply_type = 0;
  bool response = false;
  uint32_t device_id = 0;
  uint8_t verify_mode = 0, return_code = 0;
  uint16_t verify_data = 0;
  uint8_t camera_mode = 0, camera_status = 0, power_mode = 0, temperature = 0;
  RecordingState recording = RecordingState::Unknown;
};
Decoded decode(const uint8_t *, size_t);
class Receiver {
public:
  Decoded push(uint8_t);
  bool pending() const { return used_ != 0; }
  void reset() { used_ = expected_ = 0; }

private:
  std::array<uint8_t, kReceiveCapacity> bytes_{};
  size_t used_ = 0, expected_ = 0;
};
} // namespace dji
} // namespace ridesync

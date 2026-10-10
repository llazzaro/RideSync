#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace ridesync {
namespace go3s {
// Independently authored from published GO 3S FFFrame wire descriptions;
// provenance/limitations in test/fixtures/go3s/README.md. No upstream code reused.
constexpr size_t kReceiveCapacity = 256;
enum class Command : uint8_t { Authorize, VideoMode, StartVideo, Stop };
struct Packet {
  size_t size = 0;
  std::array<uint8_t, 64> bytes{};
};
// Sequence 1..254; no wrapping/retry. Authorization ID is caller-commissioned,
// printable ASCII, 1..32 bytes; it is not derived from a BLE address.
Packet encode(Command, uint8_t sequence, const char *auth = nullptr, size_t auth_size = 0);
Packet encodeSync();
enum class Kind : uint8_t { Invalid, Pending, Sync, Response };
struct Decoded {
  Kind kind = Kind::Invalid;
  uint8_t sequence = 0;
  uint16_t status = 0; // Opaque command response, never a recording observation.
};
// One complete camera->app FFFrame: direction, subtype, exact outer/inner
// lengths, CRC, message/protobuf header and complete inner-fragment admission.
// Body fields and unrecognized status values stay opaque. Invalid exposes zeros.
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
} // namespace go3s
} // namespace ridesync

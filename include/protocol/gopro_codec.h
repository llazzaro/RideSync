#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ridesync {
namespace gopro {
enum class Channel { Command, Settings, Query, Management };
enum class Request {
  Video,
  ShutterOn,
  ShutterOff,
  KeepAlive,
  HardwareInfo,
  ApiVersion,
  GetBusy,
  GetEncoding,
  GetReady,
  RegisterBusy,
  RegisterEncoding,
  RegisterReady
};
enum class Outcome {
  Complete,
  Pending,
  Invalid,
  Oversize,
  MissingStart,
  Interrupted,
  Timeout,
  Capacity,
  Unknown
};
struct Packet {
  Channel channel;
  uint8_t id;
  uint8_t bytes[8];
  size_t size;
};
struct Message {
  uint8_t bytes[256];
  size_t size = 0;
};
struct Response {
  Outcome outcome = Outcome::Invalid;
  uint8_t id = 0, result = 0;
  bool busy_known = false, busy = false;
  bool encoding_known = false, encoding = false;
  bool ready_known = false, ready = false;
  Message raw;
};
bool encode(Request request, Packet &packet, bool extended = true);
Response decode(Channel channel, const Message &message);
class Reassembler {
public:
  // Four active streams; 256 bytes/message, 64 bytes/GATT packet, 32 packets,
  // and a 1000ms absolute deadline (monotonic uint32 milliseconds).
  Outcome feed(uint32_t peer, Channel channel, const uint8_t *packet, size_t size, uint32_t now,
               Message &message);
  void reset(uint32_t peer, Channel channel);
  void resetPeer(uint32_t peer);

private:
  struct Stream {
    bool active = false;
    uint32_t peer = 0, started = 0;
    Channel channel = Channel::Command;
    size_t declared = 0, packets = 0;
    Message message;
  } streams_[4];
};
} // namespace gopro
} // namespace ridesync

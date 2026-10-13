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
  RegisterReady,
  GetHealth,
  RegisterHealth
};
// Classic one-byte IDs, qualified against the pinned Open GoPro SDK. Resource
// semantics still require the adapter's exact-model and ordinary-video profile.
enum class StatusId : uint8_t {
  BatteryPresent = 1,
  Overheating = 6,
  Encoding = 10,
  RemainingVideoSeconds = 35,
  SdRemainingKilobytes = 54,
  MinimumPollPeriodMs = 60,
  BatteryPercentage = 70
};
enum class StatusOperation { Get, Register };
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
  uint8_t bytes[10];
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
  // known means a validated field was supplied, not protocol success, battery
  // availability or valid recording mode. Missing fields/rejections stay unknown.
  bool battery_present_known = false, battery_present = false;
  bool overheating_known = false, overheating = false;
  bool remaining_video_seconds_known = false;
  uint32_t remaining_video_seconds = 0;
  bool sd_remaining_kilobytes_known = false;
  uint64_t sd_remaining_kilobytes = 0;
  bool minimum_poll_period_ms_known = false;
  uint32_t minimum_poll_period_ms = 0;
  bool battery_percentage_known = false;
  uint8_t battery_percentage = 0;
  Message raw;
};
bool encode(Request request, Packet &packet, bool extended = true);
// Nonempty, unique list of at most seven qualified IDs. Never queries all fields.
bool encodeStatuses(StatusOperation operation, const StatusId *ids, size_t count, Packet &packet,
                    bool extended = true);
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
  Outcome startStream(Stream *&, uint32_t, Channel, const uint8_t *, size_t, uint32_t, size_t &);
  Stream *findStream(uint32_t peer, Channel channel);
  void expireStreams(uint32_t now);
  static Outcome startHeader(const uint8_t *, size_t, size_t &, size_t &);
  Stream *allocateStream(uint32_t peer, Channel channel, uint32_t now, size_t declared);
  static Outcome appendPacket(Stream &, const uint8_t *, size_t, size_t, Message &);
};
} // namespace gopro
} // namespace ridesync

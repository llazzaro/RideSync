#include "protocol/gopro_codec.h"
#include <string.h>
namespace ridesync {
namespace gopro {
bool encode(Request request, Packet &packet, bool extended) {
  packet = Packet();
  uint8_t payload[4] = {0};
  size_t n = 0;
  packet.channel = Channel::Command;
  switch (request) {
  case Request::Video:
    payload[0] = 0x3e;
    payload[1] = 2;
    payload[2] = 3;
    payload[3] = 0xe8;
    n = 4;
    break;
  case Request::ShutterOn:
  case Request::ShutterOff:
    payload[0] = 1;
    payload[1] = 1;
    payload[2] = request == Request::ShutterOn ? 1 : 0;
    n = 3;
    break;
  case Request::KeepAlive:
    packet.channel = Channel::Settings;
    payload[0] = 0x5b;
    payload[1] = 1;
    payload[2] = 0x42;
    n = 3;
    break;
  case Request::HardwareInfo:
    payload[0] = 0x3c;
    n = 1;
    break;
  case Request::ApiVersion:
    payload[0] = 0x51;
    n = 1;
    break;
  case Request::GetBusy:
  case Request::GetEncoding:
  case Request::GetReady:
  case Request::RegisterBusy:
  case Request::RegisterEncoding:
  case Request::RegisterReady:
    packet.channel = Channel::Query;
    payload[0] = (request == Request::GetBusy || request == Request::GetEncoding ||
                  request == Request::GetReady)
                     ? 0x13
                     : 0x53;
    payload[1] = (request == Request::GetBusy || request == Request::RegisterBusy)           ? 8
                 : (request == Request::GetEncoding || request == Request::RegisterEncoding) ? 10
                                                                                             : 82;
    n = 2;
    break;
  default:
    return false;
  }
  packet.id = payload[0];
  const size_t header = extended ? 2 : 1;
  packet.bytes[0] = extended ? 0x20 : static_cast<uint8_t>(n);
  if (extended)
    packet.bytes[1] = static_cast<uint8_t>(n);
  memcpy(packet.bytes + header, payload, n);
  packet.size = header + n;
  return true;
}
Response decode(Channel channel, const Message &message) {
  Response r;
  if (message.size > sizeof(message.bytes))
    return r;
  r.raw = message;
  if (message.size < 2)
    return r;
  r.id = message.bytes[0];
  r.result = message.bytes[1];
  const bool query = channel == Channel::Query && (r.id == 0x13 || r.id == 0x53 || r.id == 0x93);
  const bool command =
      channel == Channel::Command && (r.id == 1 || r.id == 0x3e || r.id == 0x3c || r.id == 0x51);
  const bool setting = channel == Channel::Settings && r.id == 0x5b;
  if (!query && !command && !setting) {
    r.outcome = Outcome::Unknown;
    return r;
  }
  // A nonzero result is an explicit camera rejection, with raw details retained.
  if (r.result != 0) {
    r.outcome = Outcome::Complete;
    return r;
  }
  if (query) {
    bool unknown = false;
    // Validate the complete message before publishing any observed status.
    for (size_t i = 2; i < message.size;) {
      if (message.size - i < 2)
        return r;
      const uint8_t id = message.bytes[i++], length = message.bytes[i++];
      if (length > message.size - i)
        return r;
      if (id == 8 || id == 10 || id == 82) {
        if (length != 1 || message.bytes[i] > 1)
          return r;
      } else
        unknown = true;
      i += length;
    }
    for (size_t i = 2; i < message.size;) {
      const uint8_t id = message.bytes[i++], length = message.bytes[i++];
      if (id == 8) {
        r.busy_known = true;
        r.busy = message.bytes[i] != 0;
      }
      if (id == 10) {
        r.encoding_known = true;
        r.encoding = message.bytes[i] != 0;
      }
      if (id == 82) {
        r.ready_known = true;
        r.ready = message.bytes[i] != 0;
      }
      i += length;
    }
    r.outcome = unknown ? Outcome::Unknown : Outcome::Complete;
    return r;
  }
  if (r.id == 0x51 || r.id == 0x3c) {
    // Version: two length-prefixed unsigned fields. Hardware: seven fields
    // (model number/name, deprecated, firmware, serial, AP SSID/MAC),
    // followed by the documented eleven reserved bytes. No invented widths.
    const size_t fields = r.id == 0x51 ? 2 : 7;
    size_t i = 2;
    for (size_t f = 0; f < fields; ++f) {
      if (i >= message.size)
        return r;
      const size_t length = message.bytes[i++];
      if (length > message.size - i || (r.id == 0x51 && length == 0))
        return r;
      i += length;
    }
    if (r.id == 0x3c) {
      if (message.size - i != 0 && message.size - i != 11)
        return r;
    } else if (i != message.size) {
      r.outcome = Outcome::Unknown;
      return r;
    }
  } else if (message.size != 2)
    return r;
  r.outcome = Outcome::Complete;
  return r;
}
void Reassembler::reset(uint32_t peer, Channel channel) {
  for (auto &s : streams_)
    if (s.active && s.peer == peer && s.channel == channel)
      s = Stream();
}
void Reassembler::resetPeer(uint32_t peer) {
  for (auto &s : streams_)
    if (s.active && s.peer == peer)
      s = Stream();
}
Outcome Reassembler::feed(uint32_t peer, Channel channel, const uint8_t *packet, size_t size,
                          uint32_t now, Message &message) {
  message.size = 0;
  Stream *stream = nullptr;
  for (auto &s : streams_)
    if (s.active && s.peer == peer && s.channel == channel)
      stream = &s;
  if (stream && static_cast<uint32_t>(now - stream->started) >= 1000) {
    *stream = Stream();
    return Outcome::Timeout;
  }
  // Reclaim expired unrelated streams without allowing cross-stream completion.
  for (auto &s : streams_)
    if (s.active && static_cast<uint32_t>(now - s.started) >= 1000)
      s = Stream();
  auto fail = [&](Outcome outcome) {
    if (stream)
      *stream = Stream();
    return outcome;
  };
  if (!packet || size == 0)
    return fail(Outcome::Invalid);
  if (size > 64)
    return fail(Outcome::Oversize);
  size_t header = 1;
  if (packet[0] & 0x80) {
    if (packet[0] & 0x70)
      return fail(Outcome::Invalid);
    if (!stream)
      return Outcome::MissingStart;
    // Counter values 0..15 accepted; no hardware-qualified sequence policy.
  } else {
    if (stream)
      return fail(Outcome::Interrupted);
    size_t declared = 0;
    switch (packet[0] & 0x60) {
    case 0:
      declared = packet[0] & 0x1f;
      break;
    case 0x20:
      if (size < 2)
        return Outcome::Invalid;
      header = 2;
      declared = ((packet[0] & 0x1f) << 8) | packet[1];
      break;
    case 0x40:
      if (size < 3 || (packet[0] & 0x1f))
        return Outcome::Invalid;
      header = 3;
      declared = (static_cast<size_t>(packet[1]) << 8) | packet[2];
      break;
    default:
      return Outcome::Invalid;
    }
    if (declared == 0)
      return Outcome::Invalid;
    if (declared > 256)
      return Outcome::Oversize;
    if (size - header > declared)
      return Outcome::Invalid;
    for (auto &s : streams_)
      if (!s.active) {
        stream = &s;
        break;
      }
    if (!stream)
      return Outcome::Capacity;
    stream->active = true;
    stream->peer = peer;
    stream->channel = channel;
    stream->started = now;
    stream->declared = declared;
  }
  if (size == header)
    return fail(Outcome::Invalid); // reject no-progress packets
  if (++stream->packets > 32)
    return fail(Outcome::Oversize);
  const size_t bytes = size - header;
  if (bytes > stream->declared - stream->message.size)
    return fail(Outcome::Invalid);
  memcpy(stream->message.bytes + stream->message.size, packet + header, bytes);
  stream->message.size += bytes;
  if (stream->message.size != stream->declared)
    return Outcome::Pending;
  message = stream->message;
  *stream = Stream();
  return Outcome::Complete;
}
} // namespace gopro
} // namespace ridesync

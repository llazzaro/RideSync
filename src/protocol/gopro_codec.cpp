#include "protocol/gopro_codec.h"
#include <string.h>
namespace ridesync {
namespace gopro {
bool encode(Request request, Packet &packet, bool extended) {
  packet = Packet();
  struct WireRequest {
    Request request;
    Channel channel;
    uint8_t payload[4];
    size_t size;
  };
  static const WireRequest requests[] = {
      {Request::Video, Channel::Command, {0x3e, 0x2, 0x3, 0xe8}, 4},
      {Request::ShutterOn, Channel::Command, {0x1, 0x1, 0x1}, 3},
      {Request::ShutterOff, Channel::Command, {0x1, 0x1, 0x0}, 3},
      {Request::KeepAlive, Channel::Settings, {0x5b, 0x1, 0x42}, 3},
      {Request::HardwareInfo, Channel::Command, {0x3c}, 1},
      {Request::ApiVersion, Channel::Command, {0x51}, 1},
      {Request::GetBusy, Channel::Query, {0x13, 0x8}, 2},
      {Request::GetEncoding, Channel::Query, {0x13, 0xa}, 2},
      {Request::GetReady, Channel::Query, {0x13, 0x52}, 2},
      {Request::RegisterBusy, Channel::Query, {0x53, 0x8}, 2},
      {Request::RegisterEncoding, Channel::Query, {0x53, 0xa}, 2},
      {Request::RegisterReady, Channel::Query, {0x53, 0x52}, 2},
  };
  const WireRequest *spec = nullptr;
  for (const auto &candidate : requests)
    if (candidate.request == request) {
      spec = &candidate;
      break;
    }
  packet.channel = Channel::Command;
  if (!spec)
    return false;
  packet.channel = spec->channel;
  const auto *payload = spec->payload;
  const auto n = spec->size;
  packet.id = payload[0];
  const size_t header = extended ? 2 : 1;
  packet.bytes[0] = extended ? 0x20 : static_cast<uint8_t>(n);
  if (extended)
    packet.bytes[1] = static_cast<uint8_t>(n);
  memcpy(packet.bytes + header, payload, n);
  packet.size = header + n;
  return true;
}
namespace {
bool queryResponse(Channel channel, uint8_t id) {
  return channel == Channel::Query && (id == 0x13 || id == 0x53 || id == 0x93);
}
bool recognizedResponse(Channel channel, uint8_t id) {
  switch (channel) {
  case Channel::Query:
    return queryResponse(channel, id);
  case Channel::Command:
    return id == 1 || id == 0x3e || id == 0x3c || id == 0x51;
  case Channel::Settings:
    return id == 0x5b;
  default:
    return false;
  }
}
bool knownStatus(uint8_t id) { return id == 8 || id == 10 || id == 82; }
bool validateQuery(const Message &message, bool &unknown) {
  for (size_t i = 2; i < message.size;) {
    if (message.size - i < 2)
      return false;
    const uint8_t id = message.bytes[i++], length = message.bytes[i++];
    if (length > message.size - i)
      return false;
    if (knownStatus(id)) {
      if (length != 1 || message.bytes[i] > 1)
        return false;
    } else
      unknown = true;
    i += length;
  }
  return true;
}
void applyQuery(Response &r, const Message &message) {
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
}
Response decodeQuery(Response r, const Message &message) {
  bool unknown = false;
  // Publish only after every status element passes validation.
  if (!validateQuery(message, unknown))
    return r;
  applyQuery(r, message);
  r.outcome = unknown ? Outcome::Unknown : Outcome::Complete;
  return r;
}
bool identityFieldsEnd(uint8_t id, const Message &message, size_t &i) {
  const size_t fields = id == 0x51 ? 2 : 7;
  for (size_t f = 0; f < fields; ++f) {
    if (i >= message.size)
      return false;
    const size_t length = message.bytes[i++];
    if (length > message.size - i || (id == 0x51 && length == 0))
      return false;
    i += length;
  }
  return true;
}
Response decodeIdentityShape(Response r, const Message &message) {

  // Version: two length-prefixed unsigned fields. Hardware: seven fields
  // (model number/name, deprecated, firmware, serial, AP SSID/MAC),
  // followed by the documented eleven reserved bytes. No invented widths.
  size_t i = 2;
  if (!identityFieldsEnd(r.id, message, i))
    return r;
  if (r.id == 0x3c) {
    if (message.size - i != 0 && message.size - i != 11)
      return r;
  } else if (i != message.size) {
    r.outcome = Outcome::Unknown;
    return r;
  }
  r.outcome = Outcome::Complete;
  return r;
}
} // namespace
Response decode(Channel channel, const Message &message) {
  Response r;
  if (message.size > sizeof(message.bytes))
    return r;
  r.raw = message;
  if (message.size < 2)
    return r;
  r.id = message.bytes[0];
  r.result = message.bytes[1];
  const bool query = queryResponse(channel, r.id);
  if (!recognizedResponse(channel, r.id)) {
    r.outcome = Outcome::Unknown;
    return r;
  }
  // A nonzero result is an explicit camera rejection, with raw details retained.
  if (r.result != 0) {
    r.outcome = Outcome::Complete;
    return r;
  }
  if (query)
    return decodeQuery(r, message);
  if (r.id == 0x51 || r.id == 0x3c)
    return decodeIdentityShape(r, message);
  if (message.size != 2)
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
Reassembler::Stream *Reassembler::findStream(uint32_t peer, Channel channel) {
  Stream *found = nullptr;
  for (auto &stream : streams_)
    if (stream.active && stream.peer == peer && stream.channel == channel)
      found = &stream;
  return found;
}
void Reassembler::expireStreams(uint32_t now) {
  for (auto &stream : streams_)
    if (stream.active && static_cast<uint32_t>(now - stream.started) >= 1000)
      stream = Stream();
}
Outcome Reassembler::startHeader(const uint8_t *packet, size_t size, size_t &header,
                                 size_t &declared) {
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
  return Outcome::Complete;
}
Reassembler::Stream *Reassembler::allocateStream(uint32_t peer, Channel channel, uint32_t now,
                                                 size_t declared) {
  for (auto &stream : streams_)
    if (!stream.active) {
      stream.active = true;
      stream.peer = peer;
      stream.channel = channel;
      stream.started = now;
      stream.declared = declared;
      return &stream;
    }
  return nullptr;
}
Outcome Reassembler::appendPacket(Stream &stream, const uint8_t *packet, size_t size, size_t header,
                                  Message &message) {
  auto fail = [&](Outcome outcome) {
    stream = Stream();
    return outcome;
  };
  if (size == header)
    return fail(Outcome::Invalid); // reject no-progress packets
  if (++stream.packets > 32)
    return fail(Outcome::Oversize);
  const size_t bytes = size - header;
  if (bytes > stream.declared - stream.message.size)
    return fail(Outcome::Invalid);
  memcpy(stream.message.bytes + stream.message.size, packet + header, bytes);
  stream.message.size += bytes;
  if (stream.message.size != stream.declared)
    return Outcome::Pending;
  message = stream.message;
  stream = Stream();
  return Outcome::Complete;
}
Outcome Reassembler::startStream(Stream *&stream, uint32_t peer, Channel channel,
                                 const uint8_t *packet, size_t size, uint32_t now, size_t &header) {
  if (stream)
    return Outcome::Interrupted;
  size_t declared = 0;
  const auto outcome = startHeader(packet, size, header, declared);
  if (outcome != Outcome::Complete)
    return outcome;
  stream = allocateStream(peer, channel, now, declared);
  if (!stream)
    return Outcome::Capacity;
  return Outcome::Complete;
}
Outcome Reassembler::feed(uint32_t peer, Channel channel, const uint8_t *packet, size_t size,
                          uint32_t now, Message &message) {
  message.size = 0;
  Stream *stream = findStream(peer, channel);
  if (stream && static_cast<uint32_t>(now - stream->started) >= 1000) {
    *stream = Stream();
    return Outcome::Timeout;
  }
  // Reclaim expired unrelated streams without allowing cross-stream completion.
  expireStreams(now);
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
    const auto outcome = startStream(stream, peer, channel, packet, size, now, header);
    if (outcome != Outcome::Complete)
      return fail(outcome);
  }
  return appendPacket(*stream, packet, size, header, message);
}

} // namespace gopro
} // namespace ridesync

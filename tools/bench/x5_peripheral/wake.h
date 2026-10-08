#pragma once
#include "capture.h"
#include <array>

namespace x5_probe {
constexpr char kRemoteName[] = "Insta360 GPS Remote";
enum class WakeInput : uint8_t { Command, Consumed, Accepted, Refused };
// One private RAM-only option. W consumes a complete line even when malformed
// or too late; identifier bytes can never become serial control commands.
class WakeOption {
public:
  WakeInput feed(uint8_t byte, bool used) {
    if (!reading_) {
      if (byte != 'W')
        return WakeInput::Command;
      reading_ = true;
      accepted_idle_ = !used;
      count_ = 0;
      malformed_ = false;
      return WakeInput::Consumed;
    }
    if (byte != '\n') {
      if (count_ < sizeof(pending_) && byte >= 32 && byte <= 126)
        pending_[count_] = byte;
      else
        malformed_ = true;
      if (count_ < sizeof(pending_) + 1)
        ++count_;
      return WakeInput::Consumed;
    }
    reading_ = false;
    const bool accepted = accepted_idle_ && !used && !malformed_ && count_ == sizeof(pending_);
    if (accepted_idle_ && !used) {
      enabled_ = accepted;
      blocked_ = !accepted;
      std::memset(identifier_, 0, sizeof(identifier_));
      if (accepted)
        std::memcpy(identifier_, pending_, sizeof(identifier_));
    }
    std::memset(pending_, 0, sizeof(pending_));
    return accepted ? WakeInput::Accepted : WakeInput::Refused;
  }
  bool allowBegin() const { return !reading_ && !blocked_; }
  bool enabled() const { return enabled_; }
  bool shutterAllowed() const { return !enabled_; }
  uint32_t duration(uint32_t remaining) const {
    return enabled_ && remaining > 3000 ? 3000 : remaining;
  }
  std::array<uint8_t, 31> advertisement() const {
    // Manufacturer value facts from pinned MIT M5/ESP32 sources. AD framing
    // is this diagnostic's explicit legacy layout, not an observed X5 packet.
    std::array<uint8_t, 31> bytes{{2,    1,    6,    27,   255, 0x4c, 0,    2,    0x15, 9, 0x4f,
                                   0x52, 0x42, 0x49, 0x54, 9,   0xff, 0x0f, 0,    0,    0, 0,
                                   0,    0,    0,    0,    0,   0,    0,    0xe4, 1}};
    std::memcpy(bytes.data() + 19, identifier_, sizeof(identifier_));
    return bytes;
  }
  std::array<uint8_t, 25> scanResponse() const {
    std::array<uint8_t, 25> bytes{{3, 3, 0x80, 0xce, 20, 9}};
    static_assert(sizeof(kRemoteName) - 1 == 19, "Exact legacy scan response size");
    std::memcpy(bytes.data() + 6, kRemoteName, sizeof(kRemoteName) - 1);
    return bytes;
  }
  void advertisingEnded(Capture &capture, uint16_t connection) const {
    if (enabled_ && connection == 0xffff)
      capture.stop(StopReason::Deadline);
  }

private:
  uint8_t identifier_[6]{}, pending_[6]{};
  unsigned count_ = 0;
  bool reading_ = false, accepted_idle_ = false, malformed_ = false;
  bool enabled_ = false, blocked_ = false;
};
} // namespace x5_probe

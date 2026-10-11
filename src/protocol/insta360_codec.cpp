#include "protocol/insta360_codec.h"
#include <cstring>
namespace ridesync {
namespace insta360 {
ShutterEvent encodeShutterEvent() {
  // Independently authored from pinned MIT community wire facts; see fixtures.
  return {{0xfc, 0xef, 0xfe, 0x86, 0x00, 0x03, 0x01, 0x02, 0x00}};
}
namespace {
Ce80DisplayResult displayError(Ce80DisplayError error) {
  Ce80DisplayResult result;
  result.error = error;
  return result;
}
bool digit(uint8_t value) { return value >= '0' && value <= '9'; }
// No numeric conversion/overflow: remaining runtime/count never proves state.
bool remainingText(const uint8_t *text, size_t size) {
  size_t at = 0;
  while (at < size && text[at] == ' ')
    ++at;
  const bool padded = at != 0;
  const size_t begin = at;
  while (at < size && digit(text[at]))
    ++at;
  if (at == begin)
    return false;
  if (at == size)
    return padded;
  if (text[at++] != 'h')
    return false;
  const size_t minutes = at;
  while (at < size && digit(text[at]))
    ++at;
  return at > minutes && at + 1 == size && text[at] == 'm';
}
Ce80DisplayResult decodeSettings(const uint8_t *text, size_t text_size) {
  Ce80DisplayResult result;
  result.error = Ce80DisplayError::None;
  // Exact observed X5 settings. Other resolutions/submodes remain Unknown.
  const char video[] = "5.7K|30";
  const char photo[] = "72MP|MEGA";
  if (text_size == sizeof(video) - 1 && std::memcmp(text, video, text_size) == 0)
    result.mode = Ce80CameraMode::Video;
  else if (text_size == sizeof(photo) - 1 && std::memcmp(text, photo, text_size) == 0)
    result.mode = Ce80CameraMode::Photo;
  else
    return displayError(Ce80DisplayError::UnsupportedDisplay);
  result.kind = Ce80DisplayKind::Settings;
  result.recording = RecordingState::Stopped;
  return result;
}
bool elapsedText(const uint8_t *text, size_t size) {
  if (size != 9 || text[0] != '.' || text[3] != ':' || text[6] != ':')
    return false;
  constexpr size_t positions[] = {1, 2, 4, 5, 7, 8};
  for (auto position : positions)
    if (!digit(text[position]))
      return false;
  return true;
}
Ce80DisplayResult decodeElapsed(const uint8_t *text) {
  Ce80DisplayResult result;
  result.error = Ce80DisplayError::None;
  const uint32_t hours = (text[1] - '0') * 10 + text[2] - '0';
  const uint32_t minutes = (text[4] - '0') * 10 + text[5] - '0';
  const uint32_t seconds = (text[7] - '0') * 10 + text[8] - '0';
  if (minutes >= 60 || seconds >= 60)
    return displayError(Ce80DisplayError::UnsupportedDisplay);
  result.kind = Ce80DisplayKind::Elapsed;
  result.recording = RecordingState::Recording;
  result.elapsed_seconds = hours * 3600 + minutes * 60 + seconds;
  return result;
}
Ce80DisplayError validateDisplayEnvelope(const Ce80DisplayConfig &config, Ce80Direction direction,
                                         const uint8_t *data, size_t size) {
  if (config.profile == Ce80DisplayProfile::Disabled)
    return Ce80DisplayError::Disabled;
  if (config.profile != Ce80DisplayProfile::X5CapturedDisplayV1)
    return Ce80DisplayError::UnsupportedProfile;
  if (direction != Ce80Direction::CameraToRemote)
    return Ce80DisplayError::WrongDirection;
  if (size < 6 || size > 256)
    return Ce80DisplayError::InvalidSize;
  if (!data)
    return Ce80DisplayError::InvalidBuffer;
  if (data[0] != 0xfe || data[1] != 0xef || data[2] != 0xfe)
    return Ce80DisplayError::UnsupportedHeader;
  if (size != size_t(data[5]) + 6)
    return Ce80DisplayError::LengthMismatch;
  return Ce80DisplayError::None;
}
} // namespace
Ce80DisplayResult decodeCe80Display(const Ce80DisplayConfig &config, Ce80Direction direction,
                                    const uint8_t *data, size_t size) {
  const auto error = validateDisplayEnvelope(config, direction, data, size);
  if (error != Ce80DisplayError::None)
    return displayError(error);
  Ce80DisplayResult result;
  result.error = Ce80DisplayError::None;
  // Opaque status/handshake/other bodies are neither ACK nor state evidence.
  if (data[3] != 0x10)
    return result;
  if (size < 10 || (data[4] != 0x80 && data[4] != 0x81) || data[6] != 1)
    return displayError(Ce80DisplayError::UnsupportedDisplay);
  // Byte7 is display layout, not a state/sequence/ACK field.
  const uint8_t *text = data + 10;
  const size_t text_size = size - 10;
  if (data[8] == 0x5e && data[9] == 0)
    return decodeSettings(text, text_size);
  if (data[8] != 0x46 || data[9] != 1)
    return displayError(Ce80DisplayError::UnsupportedDisplay);
  if (elapsedText(text, text_size))
    return decodeElapsed(text);
  if (remainingText(text, text_size)) {
    result.kind = Ce80DisplayKind::Remaining;
    return result;
  }
  return displayError(Ce80DisplayError::UnsupportedDisplay);
}
} // namespace insta360
} // namespace ridesync

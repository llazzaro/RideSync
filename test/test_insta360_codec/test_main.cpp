#include "../fixtures/insta360/be80_envelope.h"
#include "../fixtures/insta360/be80_recording.h"
#include "../fixtures/insta360/ce80_display.h"
#include "../fixtures/insta360/ce80_shutter.h"
#include "protocol/insta360_be80_codec.h"
#include "protocol/insta360_codec.h"
#include <cstring>
#include <unity.h>
void shutter_button_event_matches_independent_pinned_literal() {
  const auto event = ridesync::insta360::encodeShutterEvent();
  TEST_ASSERT_EQUAL_UINT(9, event.size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(insta360_fixture::kShutterEvent, event.data(), 9);
}
using namespace ridesync::insta360;
Be80ControlConfig enabledConfig() {
  Be80ControlConfig config;
  config.profile = Be80ControlProfile::GarminBe80ControlV1;
  return config;
}
void expectFailure(const Be80ControlConfig &config, Be80RecordingCommand command, uint8_t sequence,
                   Be80CommandError error) {
  const auto result = encodeRecordingCommand(config, command, sequence);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(error), static_cast<int>(result.error));
  TEST_ASSERT_EQUAL_UINT(0, result.size);
  const uint8_t zero[18] = {};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, result.bytes.data(), 18);
}
void explicit_recording_commands_match_full_independent_literals() {
  const auto config = enabledConfig();
  const auto start = encodeRecordingCommand(config, Be80RecordingCommand::StartVideo, 1);
  const auto stop = encodeRecordingCommand(config, Be80RecordingCommand::Stop, 254);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Be80CommandError::None), static_cast<int>(start.error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Be80CommandError::None), static_cast<int>(stop.error));
  TEST_ASSERT_EQUAL_UINT(18, start.size);
  TEST_ASSERT_EQUAL_UINT(18, stop.size);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(insta360_fixture::kBe80StartVideo, start.bytes.data(), 18);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(insta360_fixture::kBe80Stop, stop.bytes.data(), 18);
}
void caller_sequence_changes_only_byte_ten_without_internal_state() {
  const auto config = enabledConfig();
  for (auto command : {Be80RecordingCommand::StartVideo, Be80RecordingCommand::Stop}) {
    for (unsigned sequence = 1; sequence <= 254; ++sequence) {
      uint8_t expected[18];
      std::memcpy(expected,
                  command == Be80RecordingCommand::StartVideo ? insta360_fixture::kBe80StartVideo
                                                              : insta360_fixture::kBe80Stop,
                  18);
      expected[10] = static_cast<uint8_t>(sequence);
      const auto first = encodeRecordingCommand(config, command, static_cast<uint8_t>(sequence));
      auto copy = first;
      copy.bytes.fill(0);
      const auto second = encodeRecordingCommand(config, command, static_cast<uint8_t>(sequence));
      TEST_ASSERT_EQUAL_UINT(18, first.size);
      TEST_ASSERT_EQUAL_INT(static_cast<int>(Be80CommandError::None),
                            static_cast<int>(first.error));
      TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, first.bytes.data(), 18);
      TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, second.bytes.data(), 18);
    }
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Be80ControlProfile::GarminBe80ControlV1),
                        static_cast<int>(config.profile));
}
void disabled_unknown_profiles_and_invalid_commands_fail_atomically() {
  auto config = Be80ControlConfig{};
  expectFailure(config, static_cast<Be80RecordingCommand>(255), 0, Be80CommandError::Disabled);
  for (unsigned profile = 2; profile <= 255; ++profile) {
    config.profile = static_cast<Be80ControlProfile>(profile);
    expectFailure(config, Be80RecordingCommand::StartVideo, 1,
                  Be80CommandError::UnsupportedProfile);
  }
  config = enabledConfig();
  for (unsigned command = 2; command <= 255; ++command)
    expectFailure(config, static_cast<Be80RecordingCommand>(command), 1,
                  Be80CommandError::InvalidCommand);
}
void invalid_sequences_and_error_precedence_expose_no_packet() {
  const auto config = enabledConfig();
  for (auto command : {Be80RecordingCommand::StartVideo, Be80RecordingCommand::Stop}) {
    expectFailure(config, command, 0, Be80CommandError::InvalidSequence);
    expectFailure(config, command, 255, Be80CommandError::InvalidSequence);
  }
  auto unknown = config;
  unknown.profile = static_cast<Be80ControlProfile>(255);
  expectFailure(unknown, static_cast<Be80RecordingCommand>(255), 0,
                Be80CommandError::UnsupportedProfile);
  expectFailure(config, static_cast<Be80RecordingCommand>(255), 0,
                Be80CommandError::InvalidCommand);
}
// Removing a decoder guard must either expose bytes on failure or accept the
// malformed case below; interpreting byte7/17 as state must fail Unknown checks.
void envelopeFailure(const Be80ControlConfig &config, Be80Direction direction, const uint8_t *data,
                     size_t size, Be80EnvelopeError error) {
  const auto result = decodeBe80Envelope(config, direction, data, size);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(error), static_cast<int>(result.error));
  TEST_ASSERT_EQUAL_UINT(0, result.size);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ridesync::RecordingState::Unknown),
                        static_cast<int>(result.recording));
  const uint8_t zeros[256] = {};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(zeros, result.bytes.data(), sizeof zeros);
}
void complete_source_envelope_is_copied_without_state_inference() {
  uint8_t input[18];
  std::memcpy(input, insta360_fixture::kBe80Envelope, sizeof input);
  const auto result =
      decodeBe80Envelope(enabledConfig(), Be80Direction::CameraToRemote, input, sizeof input);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Be80EnvelopeError::None), static_cast<int>(result.error));
  TEST_ASSERT_EQUAL_UINT(18, result.size);
  input[7] = 0;
  TEST_ASSERT_EQUAL_HEX8_ARRAY(insta360_fixture::kBe80Envelope, result.bytes.data(), 18);
  const uint8_t zeros[238] = {};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(zeros, result.bytes.data() + 18, sizeof zeros);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ridesync::RecordingState::Unknown),
                        static_cast<int>(result.recording));
}
void envelope_admission_rejects_profiles_directions_and_null_without_reading() {
  const auto inbound = Be80Direction::CameraToRemote;
  const auto config = enabledConfig();
  envelopeFailure({}, inbound, nullptr, 18, Be80EnvelopeError::Disabled);
  for (unsigned value = 2; value <= 255; ++value) {
    auto unsupported = config;
    unsupported.profile = static_cast<Be80ControlProfile>(value);
    envelopeFailure(unsupported, inbound, nullptr, 18, Be80EnvelopeError::UnsupportedProfile);
  }
  for (unsigned value = 1; value <= 255; ++value)
    envelopeFailure(config, static_cast<Be80Direction>(value), nullptr, 18,
                    Be80EnvelopeError::WrongDirection);
  envelopeFailure(config, inbound, nullptr, 18, Be80EnvelopeError::InvalidBuffer);
}
void envelope_size_limits_precede_pointer_access_and_accept_full_capacity() {
  const auto config = enabledConfig();
  const auto inbound = Be80Direction::CameraToRemote;
  for (size_t size = 0; size < 18; ++size)
    envelopeFailure(config, inbound, nullptr, size, Be80EnvelopeError::InvalidSize);
  for (size_t size : {size_t(257), size_t(65535), size_t(-1)})
    envelopeFailure(config, inbound, nullptr, size, Be80EnvelopeError::InvalidSize);
  uint8_t full[256];
  std::memset(full, 0xa5, sizeof full);
  const uint8_t header[7] = {0, 1, 0, 0, 4, 0, 0};
  std::memcpy(full, header, sizeof header);
  const auto result = decodeBe80Envelope(config, inbound, full, sizeof full);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Be80EnvelopeError::None), static_cast<int>(result.error));
  TEST_ASSERT_EQUAL_UINT(256, result.size);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(full, result.bytes.data(), sizeof full);
}
void declared_total_length_rejects_truncated_extra_and_byte_swapped_frames() {
  const auto config = enabledConfig();
  uint8_t data[19];
  std::memcpy(data, insta360_fixture::kBe80Envelope, 18);
  data[18] = 0;
  envelopeFailure(config, Be80Direction::CameraToRemote, data, 19,
                  Be80EnvelopeError::LengthMismatch);
  for (uint16_t size : {uint16_t(0), uint16_t(17), uint16_t(19), uint16_t(256), uint16_t(0x1200),
                        uint16_t(65535)}) {
    data[0] = static_cast<uint8_t>(size);
    data[1] = static_cast<uint8_t>(size >> 8);
    envelopeFailure(config, Be80Direction::CameraToRemote, data, 18,
                    Be80EnvelopeError::LengthMismatch);
  }
}
void altered_response_signatures_do_not_expose_partial_envelopes() {
  uint8_t data[18];
  for (size_t index = 2; index <= 6; ++index) {
    std::memcpy(data, insta360_fixture::kBe80Envelope, sizeof data);
    data[index] ^= 0xff;
    envelopeFailure(enabledConfig(), Be80Direction::CameraToRemote, data, sizeof data,
                    Be80EnvelopeError::UnsupportedHeader);
  }
}
void every_opaque_command_and_status_byte_remains_unknown() {
  uint8_t data[18];
  std::memcpy(data, insta360_fixture::kBe80Envelope, sizeof data);
  for (unsigned code = 0; code <= 255; ++code) {
    data[7] = static_cast<uint8_t>(code);
    for (unsigned status : {0u, 1u, 255u}) {
      data[17] = static_cast<uint8_t>(status);
      const auto result =
          decodeBe80Envelope(enabledConfig(), Be80Direction::CameraToRemote, data, sizeof data);
      TEST_ASSERT_EQUAL_INT(static_cast<int>(Be80EnvelopeError::None),
                            static_cast<int>(result.error));
      TEST_ASSERT_EQUAL_HEX8_ARRAY(data, result.bytes.data(), sizeof data);
      TEST_ASSERT_EQUAL_INT(static_cast<int>(ridesync::RecordingState::Unknown),
                            static_cast<int>(result.recording));
    }
  }
}
void finite_receive_corpus_handles_each_complete_size_and_rejected_prefix() {
  uint8_t data[256] = {0, 0, 0, 0, 4, 0, 0};
  for (size_t total = 18; total <= sizeof data; ++total) {
    data[0] = static_cast<uint8_t>(total);
    data[1] = static_cast<uint8_t>(total >> 8);
    const auto result =
        decodeBe80Envelope(enabledConfig(), Be80Direction::CameraToRemote, data, total);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Be80EnvelopeError::None),
                          static_cast<int>(result.error));
    TEST_ASSERT_EQUAL_UINT(total, result.size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, result.bytes.data(), total);
    // Every proper prefix is rejected; no retained assembly or previous result.
    for (size_t prefix = 0; prefix < total; ++prefix)
      envelopeFailure(enabledConfig(), Be80Direction::CameraToRemote, data, prefix,
                      prefix < 18 ? Be80EnvelopeError::InvalidSize
                                  : Be80EnvelopeError::LengthMismatch);
  }
}

Ce80DisplayConfig displayConfig() {
  Ce80DisplayConfig c;
  c.profile = Ce80DisplayProfile::X5CapturedDisplayV1;
  return c;
}
Ce80DisplayResult display(const uint8_t *p, size_t n) {
  return decodeCe80Display(displayConfig(), Ce80Direction::CameraToRemote, p, n);
}
void unknownDisplay(const Ce80DisplayResult &r, Ce80DisplayError error) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(error), static_cast<int>(r.error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Ce80DisplayKind::Unknown), static_cast<int>(r.kind));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Ce80CameraMode::Unknown), static_cast<int>(r.mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ridesync::RecordingState::Unknown),
                        static_cast<int>(r.recording));
  TEST_ASSERT_EQUAL_UINT32(0, r.elapsed_seconds);
}
// Omitting type/layout validation or accepting any colon must break these tests.
void captured_elapsed_display_reports_recording_with_owned_seconds() {
  uint8_t input[sizeof insta360_fixture::kCe80Timer];
  std::memcpy(input, insta360_fixture::kCe80Timer, sizeof input);
  const auto r = display(input, sizeof input);
  std::memset(input, 0, sizeof input);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Ce80DisplayError::None), static_cast<int>(r.error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Ce80DisplayKind::Elapsed), static_cast<int>(r.kind));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ridesync::RecordingState::Recording),
                        static_cast<int>(r.recording));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Ce80CameraMode::Unknown), static_cast<int>(r.mode));
  TEST_ASSERT_EQUAL_UINT32(5, r.elapsed_seconds);
}
void captured_settings_report_stopped_with_distinct_modes() {
  const uint8_t *inputs[] = {insta360_fixture::kCe80Video, insta360_fixture::kCe80VideoUpdate,
                             insta360_fixture::kCe80Photo};
  const size_t sizes[] = {sizeof insta360_fixture::kCe80Video,
                          sizeof insta360_fixture::kCe80VideoUpdate,
                          sizeof insta360_fixture::kCe80Photo};
  for (size_t i = 0; i < 3; ++i) {
    const auto r = display(inputs[i], sizes[i]);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Ce80DisplayError::None), static_cast<int>(r.error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Ce80DisplayKind::Settings), static_cast<int>(r.kind));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ridesync::RecordingState::Stopped),
                          static_cast<int>(r.recording));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(i == 2 ? Ce80CameraMode::Photo : Ce80CameraMode::Video),
                          static_cast<int>(r.mode));
    TEST_ASSERT_EQUAL_UINT32(0, r.elapsed_seconds);
  }
}
void remaining_runtime_and_count_do_not_prove_stopped_or_mode() {
  const auto runtime =
      display(insta360_fixture::kCe80Runtime, sizeof insta360_fixture::kCe80Runtime);
  const auto count = display(insta360_fixture::kCe80Count, sizeof insta360_fixture::kCe80Count);
  for (const auto &r : {runtime, count}) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Ce80DisplayError::None), static_cast<int>(r.error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Ce80DisplayKind::Remaining), static_cast<int>(r.kind));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ridesync::RecordingState::Unknown),
                          static_cast<int>(r.recording));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Ce80CameraMode::Unknown), static_cast<int>(r.mode));
    TEST_ASSERT_EQUAL_UINT32(0, r.elapsed_seconds);
  }
}
void display_admission_checks_profile_direction_bounds_and_pointer() {
  unknownDisplay(decodeCe80Display({}, Ce80Direction::CameraToRemote, nullptr, 19),
                 Ce80DisplayError::Disabled);
  for (unsigned value = 2; value <= 255; ++value) {
    auto c = displayConfig();
    c.profile = static_cast<Ce80DisplayProfile>(value);
    unknownDisplay(decodeCe80Display(c, Ce80Direction::CameraToRemote, nullptr, 19),
                   Ce80DisplayError::UnsupportedProfile);
  }
  for (unsigned value = 1; value <= 255; ++value)
    unknownDisplay(
        decodeCe80Display(displayConfig(), static_cast<Ce80Direction>(value), nullptr, 19),
        Ce80DisplayError::WrongDirection);
  for (size_t size : {size_t(0), size_t(5), size_t(257), size_t(-1)})
    unknownDisplay(display(nullptr, size), Ce80DisplayError::InvalidSize);
  unknownDisplay(display(nullptr, 6), Ce80DisplayError::InvalidBuffer);
}
void incomplete_extra_and_wrong_magic_frames_never_expose_state() {
  uint8_t data[40] = {};
  std::memcpy(data, insta360_fixture::kCe80Timer, sizeof insta360_fixture::kCe80Timer);
  for (size_t n = 0; n < sizeof insta360_fixture::kCe80Timer; ++n)
    unknownDisplay(display(data, n),
                   n < 6 ? Ce80DisplayError::InvalidSize : Ce80DisplayError::LengthMismatch);
  unknownDisplay(display(data, 20), Ce80DisplayError::LengthMismatch);
  for (size_t i = 0; i < 3; ++i) {
    data[i] ^= 1;
    unknownDisplay(display(data, 19), Ce80DisplayError::UnsupportedHeader);
    data[i] ^= 1;
  }
  for (unsigned value = 0; value <= 255; ++value) {
    if (value == 13)
      continue;
    data[5] = static_cast<uint8_t>(value);
    unknownDisplay(display(data, 19), Ce80DisplayError::LengthMismatch);
  }
}
void unknown_types_and_full_capacity_do_not_inherit_previous_recording() {
  uint8_t data[256] = {};
  std::memcpy(data, insta360_fixture::kCe80Timer, 19);
  for (unsigned type = 0; type <= 255; ++type) {
    if (type == 0x10)
      continue;
    data[3] = static_cast<uint8_t>(type);
    unknownDisplay(display(data, 19), Ce80DisplayError::None);
  }
  data[3] = 2;
  data[5] = 250;
  unknownDisplay(display(data, sizeof data), Ce80DisplayError::None);
  unknownDisplay(display(data, 255), Ce80DisplayError::LengthMismatch);
}
void altered_display_controls_and_unrecognized_settings_remain_unknown() {
  uint8_t data[19];
  for (size_t index : {size_t(4), size_t(6), size_t(8), size_t(9)}) {
    std::memcpy(data, insta360_fixture::kCe80Timer, 19);
    for (unsigned value = 0; value <= 255; ++value) {
      if (value == insta360_fixture::kCe80Timer[index] || (index == 4 && value == 0x81))
        continue;
      data[index] = static_cast<uint8_t>(value);
      unknownDisplay(display(data, 19), Ce80DisplayError::UnsupportedDisplay);
    }
  }
  std::memcpy(data, insta360_fixture::kCe80Video, sizeof insta360_fixture::kCe80Video);
  data[10] = '9';
  unknownDisplay(display(data, sizeof insta360_fixture::kCe80Video),
                 Ce80DisplayError::UnsupportedDisplay);
  data[10] = '5';
  data[9] = 1;
  unknownDisplay(display(data, sizeof insta360_fixture::kCe80Video),
                 Ce80DisplayError::UnsupportedDisplay);
}
void elapsed_text_is_exact_and_checks_ranges_without_colon_heuristic() {
  uint8_t data[20] = {};
  const char *invalid[] = {"x00:00:05", ".00:60:05", ".00:00:60", ".0x:00:05",
                           ".00;00:05", ".00:00:0x", "........."};
  for (const auto text : invalid) {
    std::memcpy(data, insta360_fixture::kCe80Timer, 19);
    std::memcpy(data + 10, text, 9);
    unknownDisplay(display(data, 19), Ce80DisplayError::UnsupportedDisplay);
  }
  std::memcpy(data, insta360_fixture::kCe80Timer, 19);
  data[15] = 0;
  unknownDisplay(display(data, 19), Ce80DisplayError::UnsupportedDisplay);
  data[15] = '0';
  data[19] = ':';
  data[5] = 14;
  unknownDisplay(display(data, 20), Ce80DisplayError::UnsupportedDisplay);
  const char *valid[] = {".00:00:00", ".01:02:03", ".99:59:59"};
  const uint32_t seconds[] = {0, 3723, 359999};
  for (size_t i = 0; i < 3; ++i) {
    std::memcpy(data, insta360_fixture::kCe80Timer, 19);
    std::memcpy(data + 10, valid[i], 9);
    const auto r = display(data, 19);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Ce80DisplayError::None), static_cast<int>(r.error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ridesync::RecordingState::Recording),
                          static_cast<int>(r.recording));
    TEST_ASSERT_EQUAL_UINT32(seconds[i], r.elapsed_seconds);
  }
}

void finite_ce80_display_corpus_rejects_every_proper_prefix_and_opaque_text() {
  uint8_t data[256];
  std::memset(data, 0xa5, sizeof data);
  data[0] = 0xfe;
  data[1] = 0xef;
  data[2] = 0xfe;
  data[3] = 0x10;
  data[4] = 0x80;
  data[6] = 1;
  data[8] = 0x46;
  data[9] = 1;
  for (size_t total = 6; total <= sizeof data; ++total) {
    data[5] = static_cast<uint8_t>(total - 6);
    unknownDisplay(display(data, total), Ce80DisplayError::UnsupportedDisplay);
    for (size_t prefix = 0; prefix < total; ++prefix)
      unknownDisplay(display(data, prefix),
                     prefix < 6 ? Ce80DisplayError::InvalidSize : Ce80DisplayError::LengthMismatch);
  }
  // A valid timer cannot carry state into subsequent malformed/other traffic.
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(ridesync::RecordingState::Recording),
      static_cast<int>(
          display(insta360_fixture::kCe80Timer, sizeof insta360_fixture::kCe80Timer).recording));
  unknownDisplay(display(data, 256), Ce80DisplayError::UnsupportedDisplay);
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(shutter_button_event_matches_independent_pinned_literal);
  RUN_TEST(explicit_recording_commands_match_full_independent_literals);
  RUN_TEST(caller_sequence_changes_only_byte_ten_without_internal_state);
  RUN_TEST(disabled_unknown_profiles_and_invalid_commands_fail_atomically);
  RUN_TEST(invalid_sequences_and_error_precedence_expose_no_packet);
  RUN_TEST(complete_source_envelope_is_copied_without_state_inference);
  RUN_TEST(envelope_admission_rejects_profiles_directions_and_null_without_reading);
  RUN_TEST(envelope_size_limits_precede_pointer_access_and_accept_full_capacity);
  RUN_TEST(declared_total_length_rejects_truncated_extra_and_byte_swapped_frames);
  RUN_TEST(altered_response_signatures_do_not_expose_partial_envelopes);
  RUN_TEST(every_opaque_command_and_status_byte_remains_unknown);
  RUN_TEST(finite_receive_corpus_handles_each_complete_size_and_rejected_prefix);
  RUN_TEST(captured_elapsed_display_reports_recording_with_owned_seconds);
  RUN_TEST(captured_settings_report_stopped_with_distinct_modes);
  RUN_TEST(remaining_runtime_and_count_do_not_prove_stopped_or_mode);
  RUN_TEST(display_admission_checks_profile_direction_bounds_and_pointer);
  RUN_TEST(incomplete_extra_and_wrong_magic_frames_never_expose_state);
  RUN_TEST(unknown_types_and_full_capacity_do_not_inherit_previous_recording);
  RUN_TEST(altered_display_controls_and_unrecognized_settings_remain_unknown);
  RUN_TEST(elapsed_text_is_exact_and_checks_ranges_without_colon_heuristic);
  RUN_TEST(finite_ce80_display_corpus_rejects_every_proper_prefix_and_opaque_text);
  return UNITY_END();
}

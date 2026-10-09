#include "../fixtures/insta360/be80_envelope.h"
#include "../fixtures/insta360/be80_recording.h"
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
  return UNITY_END();
}

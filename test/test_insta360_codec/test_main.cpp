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
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(shutter_button_event_matches_independent_pinned_literal);
  RUN_TEST(explicit_recording_commands_match_full_independent_literals);
  RUN_TEST(caller_sequence_changes_only_byte_ten_without_internal_state);
  RUN_TEST(disabled_unknown_profiles_and_invalid_commands_fail_atomically);
  RUN_TEST(invalid_sequences_and_error_precedence_expose_no_packet);
  return UNITY_END();
}

#include "../fixtures/insta360/wake.h"
#include "insta360_wake_encoder.h"
#include <cstring>
#include <unity.h>
using namespace ridesync::insta360;
void zero(const WakeEncoding &value, WakeCodecError expected) {
  TEST_ASSERT_EQUAL(int(expected), int(value.error));
  for (auto byte : value.advertisement)
    TEST_ASSERT_EQUAL(0, byte);
  for (auto byte : value.scan_response)
    TEST_ASSERT_EQUAL(0, byte);
}
void literals_and_owned_output() {
  const uint8_t id[] = {'A', '1', 'B', '2', 'C', '3'};
  auto result = encodeWake(WakeProfile::M5WakeV1, id, 6);
  TEST_ASSERT_EQUAL(int(WakeCodecError::None), int(result.error));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(wake_fixture::ad, result.advertisement.data(), 31);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(wake_fixture::rsp, result.scan_response.data(), 25);
  result.advertisement.fill(0);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(wake_fixture::ad,
                                encodeWake(WakeProfile::M5WakeV1, id, 6).advertisement.data(), 31);
}
void invalid_inputs_are_atomic() {
  uint8_t id[] = {'A', '1', 'B', '2', 'C', '3'};
  zero(encodeWake(WakeProfile::Disabled, nullptr, 99), WakeCodecError::Disabled);
  for (unsigned profile = 2; profile <= 255; ++profile)
    zero(encodeWake(static_cast<WakeProfile>(profile), nullptr, 0),
         WakeCodecError::UnsupportedProfile);
  zero(encodeWake(WakeProfile::M5WakeV1, nullptr, 6), WakeCodecError::InvalidIdentifier);
  for (unsigned length = 0; length != 9; ++length)
    if (length != 6)
      zero(encodeWake(WakeProfile::M5WakeV1, id, length), WakeCodecError::InvalidIdentifier);
  for (unsigned position = 0; position != 6; ++position) {
    const auto old = id[position];
    for (unsigned byte = 0; byte <= 255; ++byte) {
      id[position] = byte;
      const auto result = encodeWake(WakeProfile::M5WakeV1, id, 6);
      if (byte < 32 || byte > 126)
        zero(result, WakeCodecError::InvalidIdentifier);
      else
        TEST_ASSERT_EQUAL(int(WakeCodecError::None), int(result.error));
    }
    id[position] = old;
  }
}
void name_derivation_is_exact_and_never_mac() {
  const uint8_t expected[] = {'A', '1', 'B', '2', 'C', '3'};
  auto value = deriveWakeIdentifier(WakeProfile::M5WakeV1, "X5 A1B2C3", 9);
  TEST_ASSERT_EQUAL(int(WakeCodecError::None), int(value.error));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, value.bytes.data(), 6);
  for (const char *name : {"X4 A1B2C3", "X5 A1B2C", "X5 A1B2C3x", "AA:BB:CC:DD:EE:FF"}) {
    auto bad = deriveWakeIdentifier(WakeProfile::M5WakeV1, name, strlen(name));
    TEST_ASSERT_NOT_EQUAL(int(WakeCodecError::None), int(bad.error));
    for (auto b : bad.bytes)
      TEST_ASSERT_EQUAL(0, b);
  }
  TEST_ASSERT_EQUAL(int(WakeCodecError::UnsupportedName),
                    int(deriveWakeIdentifier(WakeProfile::M5WakeV1, nullptr, 9).error));
  const char nul[] = {'X', '5', ' ', 'A', 'B', 0, 'D', 'E', 'F'};
  TEST_ASSERT_NOT_EQUAL(int(WakeCodecError::None),
                        int(deriveWakeIdentifier(WakeProfile::M5WakeV1, nul, 9).error));
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(literals_and_owned_output);
  RUN_TEST(invalid_inputs_are_atomic);
  RUN_TEST(name_derivation_is_exact_and_never_mac);
  return UNITY_END();
}

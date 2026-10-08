#include "../fixtures/insta360/ce80_shutter.h"
#include "protocol/insta360_codec.h"
#include <unity.h>
void shutter_button_event_matches_independent_pinned_literal() {
  const auto event = ridesync::insta360::encodeShutterEvent();
  TEST_ASSERT_EQUAL_UINT(9, event.size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(insta360_fixture::kShutterEvent, event.data(), 9);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(shutter_button_event_matches_independent_pinned_literal);
  return UNITY_END();
}

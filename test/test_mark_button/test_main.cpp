#include "button_manager.h"
#include "handlebar_control.h"
#include <unity.h>
using namespace ridesync;
struct Input : ButtonInput {
  bool down = false;
  bool pressed() override { return down; }
};
void debounced_press_marks_once_without_waiting_for_release() {
  MarkButton button;
  Input input;
  TEST_ASSERT_TRUE(button.begin(20));
  TEST_ASSERT_FALSE(button.poll(input, 0));
  TEST_ASSERT_FALSE(button.poll(input, 20));
  input.down = true;
  TEST_ASSERT_FALSE(button.poll(input, 30));
  input.down = false;
  TEST_ASSERT_FALSE(button.poll(input, 35));
  input.down = true;
  TEST_ASSERT_FALSE(button.poll(input, 40));
  TEST_ASSERT_FALSE(button.poll(input, 59));
  TEST_ASSERT_TRUE(button.poll(input, 60));
  TEST_ASSERT_FALSE(button.poll(input, 5000));
  input.down = false;
  TEST_ASSERT_FALSE(button.poll(input, 5010));
  input.down = true;
  TEST_ASSERT_FALSE(button.poll(input, 5015));
  TEST_ASSERT_FALSE(button.poll(input, 5035));
  input.down = false;
  TEST_ASSERT_FALSE(button.poll(input, 5040));
  TEST_ASSERT_FALSE(button.poll(input, 5060));
  input.down = true;
  TEST_ASSERT_FALSE(button.poll(input, 5070));
  TEST_ASSERT_TRUE(button.poll(input, 5090));
}
void held_boot_reset_and_wrap_do_not_replay_marks() {
  MarkButton button;
  Input input;
  input.down = true;
  TEST_ASSERT_TRUE(button.begin(20));
  TEST_ASSERT_FALSE(button.poll(input, 0));
  TEST_ASSERT_FALSE(button.poll(input, 5000));
  input.down = false;
  TEST_ASSERT_FALSE(button.poll(input, 5010));
  TEST_ASSERT_FALSE(button.poll(input, 5030));
  input.down = true;
  TEST_ASSERT_FALSE(button.poll(input, 5040));
  TEST_ASSERT_TRUE(button.begin(20)); // Pending edge retired by reset.
  TEST_ASSERT_FALSE(button.poll(input, 5060));
  TEST_ASSERT_FALSE(button.poll(input, 6000));
  TEST_ASSERT_TRUE(button.begin(20));
  input.down = false;
  TEST_ASSERT_FALSE(button.poll(input, 0xffffffe0u));
  TEST_ASSERT_FALSE(button.poll(input, 0xfffffff4u));
  input.down = true;
  TEST_ASSERT_FALSE(button.poll(input, 0xfffffffau));
  TEST_ASSERT_TRUE(button.poll(input, 14));
  TEST_ASSERT_FALSE(button.begin(0));
  TEST_ASSERT_FALSE(button.poll(input, 100));
  TEST_ASSERT_FALSE(button.begin(0x80000000u));
}
void mark_pin_requires_qualification_and_no_control_conflicts() {
  ButtonGpioConfig mark, record;
  LedWiring led;
  TEST_ASSERT_TRUE(validateMarkButtonWiring(mark, 20, record, led));
  mark.enabled = true;
  mark.pin = 19;
  mark.pull = ButtonPull::Up;
  TEST_ASSERT_FALSE(validateMarkButtonWiring(mark, 20, record, led));
  mark.board_qualified = true;
  TEST_ASSERT_TRUE(validateMarkButtonWiring(mark, 20, record, led));
  TEST_ASSERT_FALSE(validateMarkButtonWiring(mark, 0, record, led));
  record.enabled = true;
  record.pin = 19;
  TEST_ASSERT_FALSE(validateMarkButtonWiring(mark, 20, record, led));
  record.pin = 32;
  led.mode = LedMode::Mono;
  led.pins[0] = 19;
  TEST_ASSERT_FALSE(validateMarkButtonWiring(mark, 20, record, led));
  led.pins[0] = 18;
  TEST_ASSERT_TRUE(validateMarkButtonWiring(mark, 20, record, led));
  mark.pin = 34;
  TEST_ASSERT_FALSE(validateMarkButtonWiring(mark, 20, record, led));
  mark.pull = ButtonPull::External;
  TEST_ASSERT_TRUE(validateMarkButtonWiring(mark, 20, record, led));
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(debounced_press_marks_once_without_waiting_for_release);
  RUN_TEST(held_boot_reset_and_wrap_do_not_replay_marks);
  RUN_TEST(mark_pin_requires_qualification_and_no_control_conflicts);
  return UNITY_END();
}

#include "button_manager.h"
#include <initializer_list>

#include <unity.h>
using namespace ridesync;
struct Harness : ButtonInput {
  bool down = false;
  unsigned count = 0;
  ButtonAction actions[16];
  ButtonManager manager;
  bool pressed() override { return down; }
  static void event(void *p, ButtonAction a) {
    auto &h = *static_cast<Harness *>(p);
    if (h.count < 16)
      h.actions[h.count++] = a;
  }
  void start(bool dbl = false) {
    ButtonConfig c;
    c.debounce_ms = 10;
    c.long_ms = 100;
    c.double_ms = 50;
    c.double_enabled = dbl;
    TEST_ASSERT_TRUE(manager.begin(c, event, this));
  }
  void tick(uint32_t t, bool pressed) {
    down = pressed;
    manager.poll(*this, t);
  }
  void arm(uint32_t t = 0) {
    tick(t, false);
    tick(t + 10, false);
  }
  void press(uint32_t t) {
    tick(t, true);
    tick(t + 10, true);
  }
  void release(uint32_t t) {
    tick(t, false);
    tick(t + 10, false);
  }
};
void bounce_and_repeated_short() {
  Harness h;
  h.start();
  h.arm();
  h.tick(20, true);
  h.tick(25, false);
  h.tick(30, true);
  h.tick(39, true);
  h.tick(40, true);
  h.release(60);
  TEST_ASSERT_EQUAL_UINT(1, h.count);
  TEST_ASSERT_EQUAL((int)ButtonAction::RecordingIntent, (int)h.actions[0]);
  h.tick(90, false);
  h.press(100);
  h.release(130);
  TEST_ASSERT_EQUAL_UINT(2, h.count);
}
void held_startup_and_long_boundary() {
  Harness h;
  h.start();
  h.tick(0, true);
  h.tick(1000, true);
  h.tick(1010, false);
  h.tick(1015, true);
  h.tick(1100, true);
  TEST_ASSERT_EQUAL_UINT(0, h.count);
  h.release(1200);
  h.press(1220);
  h.tick(1329, true);
  TEST_ASSERT_EQUAL_UINT(0, h.count);
  h.tick(1330, true);
  TEST_ASSERT_EQUAL_UINT(1, h.count);
  TEST_ASSERT_EQUAL((int)ButtonAction::WakeReconnect, (int)h.actions[0]);
  h.tick(2000, true);
  h.release(2010);
  TEST_ASSERT_EQUAL_UINT(1, h.count);
}
void short_before_long_and_release_at_long() {
  Harness h;
  h.start();
  h.arm();
  h.press(20);
  h.release(119);
  TEST_ASSERT_EQUAL_UINT(1, h.count);
  TEST_ASSERT_EQUAL((int)ButtonAction::RecordingIntent, (int)h.actions[0]);
  h.press(150);
  h.release(250);
  TEST_ASSERT_EQUAL_UINT(2, h.count);
  TEST_ASSERT_EQUAL((int)ButtonAction::WakeReconnect, (int)h.actions[1]);
}
void double_boundaries_and_rollover() {
  for (uint32_t base : {0u, 0xfffffff0u}) {
    Harness h;
    h.start(true);
    h.arm(base);
    h.press(base + 20);
    h.release(base + 40);
    h.tick(base + 89, false);
    TEST_ASSERT_EQUAL_UINT(0, h.count);
    h.press(base + 90); // debounced at exact expiry: expiry wins
    TEST_ASSERT_EQUAL_UINT(1, h.count);
    h.release(base + 110);
    h.tick(base + 170, false);
    TEST_ASSERT_EQUAL_UINT(2, h.count);
    Harness d;
    d.start(true);
    d.arm(base);
    d.press(base + 20);
    d.release(base + 40);
    d.press(base + 79);
    d.release(base + 100);
    d.tick(base + 300, false);
    TEST_ASSERT_EQUAL_UINT(1, d.count);
    TEST_ASSERT_EQUAL((int)ButtonAction::Resync, (int)d.actions[0]);
  }
}
void release_bounce_and_long_rollover() {
  Harness h;
  h.start();
  h.arm(0xffffffc0u);
  h.press(0xffffffe0u);
  h.tick(0x4du, true);
  TEST_ASSERT_EQUAL_UINT(0, h.count);
  h.tick(0x4eu, true);
  TEST_ASSERT_EQUAL_UINT(1, h.count);
  h.tick(0x50u, false);
  h.tick(0x55u, true);
  h.tick(0x60u, false);
  h.tick(0x69u, false);
  h.tick(0x6au, false);
  TEST_ASSERT_EQUAL_UINT(1, h.count);
  h.press(0x80u);
  h.release(0xa0u);
  TEST_ASSERT_EQUAL_UINT(2, h.count);
}
void invalid_configuration_and_gpio() {
  ButtonConfig c;
  TEST_ASSERT_TRUE(validateButtonConfig(c));
  c.debounce_ms = 0;
  TEST_ASSERT_FALSE(validateButtonConfig(c));
  c = ButtonConfig();
  c.long_ms = c.debounce_ms;
  TEST_ASSERT_FALSE(validateButtonConfig(c));
  c = ButtonConfig();
  c.double_ms = 0x80000000u;
  TEST_ASSERT_FALSE(validateButtonConfig(c));
  c = ButtonConfig();
  c.short_action = static_cast<ButtonAction>(999);
  TEST_ASSERT_FALSE(validateButtonConfig(c));
  c = ButtonConfig();
  c.long_action = static_cast<ButtonAction>(-1);
  TEST_ASSERT_FALSE(validateButtonConfig(c));
  c = ButtonConfig();
  c.double_action = static_cast<ButtonAction>(3);
  TEST_ASSERT_FALSE(validateButtonConfig(c));
  ButtonGpioConfig g;
  TEST_ASSERT_FALSE(validateButtonGpioConfig(g));
  g.enabled = true;
  g.board_qualified = true;
  g.pin = 34;
  TEST_ASSERT_TRUE(validateButtonGpioConfig(g));
  g.pull = ButtonPull::Up;
  TEST_ASSERT_FALSE(validateButtonGpioConfig(g));
  g.pull = ButtonPull::External;
  for (int pin : {-1, 0,  2,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
                  15, 16, 17, 20, 24, 25, 26, 27, 28, 29, 30, 31, 33, 40}) {
    g.pin = pin;
    TEST_ASSERT_FALSE(validateButtonGpioConfig(g));
  }
  g.pin = 32;
  g.pull = static_cast<ButtonPull>(99);
  TEST_ASSERT_FALSE(validateButtonGpioConfig(g));
  g.pull = ButtonPull::Up;
  TEST_ASSERT_TRUE(validateButtonGpioConfig(g));
  g.board_qualified = false;
  TEST_ASSERT_FALSE(validateButtonGpioConfig(g));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(bounce_and_repeated_short);
  RUN_TEST(held_startup_and_long_boundary);
  RUN_TEST(short_before_long_and_release_at_long);
  RUN_TEST(double_boundaries_and_rollover);
  RUN_TEST(release_bounce_and_long_rollover);
  RUN_TEST(invalid_configuration_and_gpio);
  return UNITY_END();
}
void setUp() {}
void tearDown() {}

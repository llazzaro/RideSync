#include "bench_console.h"
#include <unity.h>
using namespace ridesync;
void setUp() {}
void tearDown() {}
static void input(BenchConsole &c, const char *s) {
  while (*s)
    c.push(static_cast<uint8_t>(*s++));
}
void exact_commands_and_partial_lines() {
  BenchConsole c;
  BenchCommand cmd;
  BenchConsoleError error;
  input(c, "RE");
  TEST_ASSERT_FALSE(c.take(cmd, error));
  input(c, "C\r\n");
  TEST_ASSERT_TRUE(c.take(cmd, error));
  TEST_ASSERT_EQUAL(static_cast<int>(BenchCommand::Record), static_cast<int>(cmd));
  TEST_ASSERT_EQUAL(static_cast<int>(BenchConsoleError::None), static_cast<int>(error));
  const char *names[] = {"STATUS\n", "CONNECT\n",  "STOP\n",  "QUERY\n",
                         "WAKE\n",   "SHUTDOWN\n", "CLEAR\n", "STATIONARY\n"};
  const BenchCommand expected[] = {
      BenchCommand::Status, BenchCommand::Connect,  BenchCommand::Stop,  BenchCommand::Query,
      BenchCommand::Wake,   BenchCommand::Shutdown, BenchCommand::Clear, BenchCommand::Stationary};
  for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
    input(c, names[i]);
    TEST_ASSERT_TRUE(c.take(cmd, error));
    TEST_ASSERT_EQUAL(static_cast<int>(expected[i]), static_cast<int>(cmd));
    TEST_ASSERT_EQUAL(static_cast<int>(BenchConsoleError::None), static_cast<int>(error));
  }
}
void burst_discards_intent_and_recovers() {
  BenchConsole c;
  BenchCommand cmd;
  BenchConsoleError error;
  input(c, "REC\nSTOP\n");
  TEST_ASSERT_TRUE(c.take(cmd, error));
  TEST_ASSERT_EQUAL(static_cast<int>(BenchConsoleError::LostInput), static_cast<int>(error));
  TEST_ASSERT_FALSE(c.take(cmd, error));
  input(c, "STATUS\n");
  TEST_ASSERT_TRUE(c.take(cmd, error));
  TEST_ASSERT_EQUAL(static_cast<int>(BenchConsoleError::None), static_cast<int>(error));
  TEST_ASSERT_EQUAL(static_cast<int>(BenchCommand::Status), static_cast<int>(cmd));
}
void malformed_never_becomes_command() {
  const char *bad[] = {"rec\n",
                       " REC\n",
                       "REC extra\n",
                       "RE\n",
                       "R\rEC\n",
                       "REC\r\r\n",
                       "01234567890123456789REC\n"};
  for (const auto *s : bad) {
    BenchConsole c;
    BenchCommand cmd;
    BenchConsoleError error;
    input(c, s);
    TEST_ASSERT_TRUE(c.take(cmd, error));
    TEST_ASSERT_EQUAL(static_cast<int>(BenchConsoleError::Invalid), static_cast<int>(error));
    input(c, "STOP\n");
    TEST_ASSERT_TRUE(c.take(cmd, error));
    TEST_ASSERT_EQUAL(static_cast<int>(BenchConsoleError::None), static_cast<int>(error));
    TEST_ASSERT_EQUAL(static_cast<int>(BenchCommand::Stop), static_cast<int>(cmd));
  }
  BenchConsole c;
  BenchCommand cmd;
  BenchConsoleError error;
  input(c, "RE");
  c.push(0);
  input(c, "C\n");
  TEST_ASSERT_TRUE(c.take(cmd, error));
  TEST_ASSERT_EQUAL(static_cast<int>(BenchConsoleError::Invalid), static_cast<int>(error));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(exact_commands_and_partial_lines);
  RUN_TEST(burst_discards_intent_and_recovers);
  RUN_TEST(malformed_never_becomes_command);
  return UNITY_END();
}

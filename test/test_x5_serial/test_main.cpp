#include "x5_serial_control.h"
#include <unity.h>
using namespace ridesync;
struct FakePort : X5CommandPort {
  unsigned requests = 0, disconnects = 0, statuses = 0;
  Operation op = Operation::Wake;
  CameraError outcome = CameraError::None;
  CameraError request(Operation value) override {
    ++requests;
    op = value;
    return outcome;
  }
  void disconnect() override { ++disconnects; }
  void status() override { ++statuses; }
};
void feed(X5SerialControl &serial, const char *text) {
  while (*text)
    serial.consume(*text++);
}
void exact_lines_dispatch_once() {
  FakePort port;
  X5SerialControl serial(port);
  feed(serial, "CONNECT\n");
  TEST_ASSERT_EQUAL_INT(int(X5SerialCommand::Connect), int(serial.service()));
  TEST_ASSERT_EQUAL_INT(int(Operation::Connect), int(port.op));
  feed(serial, "REC\r\n");
  serial.service();
  TEST_ASSERT_EQUAL_INT(int(Operation::Start), int(port.op));
  feed(serial, "STOP\n");
  serial.service();
  TEST_ASSERT_EQUAL_INT(int(Operation::Stop), int(port.op));
  feed(serial, "QUERY\n");
  serial.service();
  TEST_ASSERT_EQUAL_INT(int(Operation::Query), int(port.op));
  TEST_ASSERT_EQUAL_UINT(4, port.requests);
  serial.service();
  TEST_ASSERT_EQUAL_UINT(4, port.requests);
}
void status_and_disconnect_have_no_shutter_side_effect() {
  FakePort port;
  X5SerialControl serial(port);
  feed(serial, "STATUS\n");
  serial.service();
  feed(serial, "DISCONNECT\n");
  serial.service();
  TEST_ASSERT_EQUAL_UINT(1, port.statuses);
  TEST_ASSERT_EQUAL_UINT(1, port.disconnects);
  TEST_ASSERT_EQUAL_UINT(0, port.requests);
}
void oversized_line_cannot_embed_recording() {
  FakePort port;
  X5SerialControl serial(port);
  for (unsigned i = 0; i < 40; ++i)
    serial.consume('x');
  feed(serial, "REC\n");
  TEST_ASSERT_EQUAL_INT(int(X5SerialCommand::Invalid), int(serial.service()));
  TEST_ASSERT_EQUAL_UINT(0, port.requests);
  feed(serial, "STATUS\r\n");
  serial.service();
  TEST_ASSERT_EQUAL_UINT(1, port.statuses);
}
void partial_and_cr_without_lf_never_dispatch() {
  FakePort port;
  X5SerialControl serial(port);
  feed(serial, "REC\r");
  serial.service();
  TEST_ASSERT_EQUAL_UINT(0, port.requests);
  feed(serial, "CONNECT\n");
  serial.service();
  TEST_ASSERT_EQUAL_UINT(0, port.requests);
}
void invalid_bytes_and_unknown_lines_are_atomic() {
  FakePort port;
  X5SerialControl serial(port);
  const char *lines[] = {"rec\n",        " REC\n", "REC \n", "REC\rSTOP\n",
                         "CONNECTREC\n", "\n",     "REC\t\n"};
  for (auto line : lines) {
    feed(serial, line);
    TEST_ASSERT_EQUAL_INT(int(X5SerialCommand::Invalid), int(serial.service()));
  }
  feed(serial, "RE");
  serial.consume('\0');
  feed(serial, "C\n");
  serial.service();
  TEST_ASSERT_EQUAL_UINT(0, port.requests);
  TEST_ASSERT_EQUAL_UINT(8, serial.rejected());
}
void full_mailbox_discards_whole_next_line() {
  FakePort port;
  X5SerialControl serial(port);
  feed(serial, "STATUS\nCONNECT");
  serial.service();
  feed(serial, "\n");
  serial.service();
  TEST_ASSERT_EQUAL_UINT(0, port.requests);
  feed(serial, "QUERY\n");
  serial.service();
  TEST_ASSERT_EQUAL_UINT(1, port.requests);
  TEST_ASSERT_EQUAL_UINT(1, serial.rejected());
}
void busy_result_is_reported_without_retry() {
  FakePort port;
  port.outcome = CameraError::Busy;
  X5SerialControl serial(port);
  feed(serial, "REC\n");
  serial.service();
  TEST_ASSERT_EQUAL_INT(int(CameraError::Busy), int(serial.error()));
  for (unsigned i = 0; i < 20; ++i)
    serial.service();
  TEST_ASSERT_EQUAL_UINT(1, port.requests);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(exact_lines_dispatch_once);
  RUN_TEST(status_and_disconnect_have_no_shutter_side_effect);
  RUN_TEST(oversized_line_cannot_embed_recording);
  RUN_TEST(partial_and_cr_without_lf_never_dispatch);
  RUN_TEST(invalid_bytes_and_unknown_lines_are_atomic);
  RUN_TEST(full_mailbox_discards_whole_next_line);
  RUN_TEST(busy_result_is_reported_without_retry);
  return UNITY_END();
}

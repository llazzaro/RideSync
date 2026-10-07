#include "../fixtures/gopro/documentary_vectors.h"
#include "protocol/gopro_codec.h"
#include <initializer_list>

#include <unity.h>
using namespace ridesync::gopro;
void setUp() {}
void tearDown() {}
void documented_requests_and_routes() {
  struct Case {
    Request request;
    Channel channel;
    uint8_t bytes[6];
    size_t size;
  };
  const Case cases[] = {{Request::Video, Channel::Command, {4, 0x3e, 2, 3, 0xe8}, 5},
                        {Request::ShutterOn, Channel::Command, {3, 1, 1, 1}, 4},
                        {Request::ShutterOff, Channel::Command, {3, 1, 1, 0}, 4},
                        {Request::KeepAlive, Channel::Settings, {3, 0x5b, 1, 0x42}, 4},
                        {Request::HardwareInfo, Channel::Command, {1, 0x3c}, 2},
                        {Request::ApiVersion, Channel::Command, {1, 0x51}, 2},
                        {Request::GetBusy, Channel::Query, {2, 0x13, 8}, 3},
                        {Request::GetEncoding, Channel::Query, {2, 0x13, 10}, 3},
                        {Request::GetReady, Channel::Query, {2, 0x13, 82}, 3},
                        {Request::RegisterBusy, Channel::Query, {2, 0x53, 8}, 3},
                        {Request::RegisterEncoding, Channel::Query, {2, 0x53, 10}, 3},
                        {Request::RegisterReady, Channel::Query, {2, 0x53, 82}, 3}};
  for (const auto &c : cases) {
    Packet p;
    TEST_ASSERT_TRUE(encode(c.request, p, false));
    TEST_ASSERT_EQUAL_INT((int)c.channel, (int)p.channel);
    TEST_ASSERT_EQUAL_UINT8(c.bytes[1], p.id);
    TEST_ASSERT_EQUAL_UINT(c.size, p.size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(c.bytes, p.bytes, p.size);
    TEST_ASSERT_TRUE(encode(c.request, p));
    TEST_ASSERT_EQUAL_UINT8(0x20, p.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(c.bytes, p.bytes + 1, c.size);
  }
  Packet p;
  TEST_ASSERT_FALSE(encode(static_cast<Request>(99), p));
}
Message body(const uint8_t *b, size_t n) {
  Message m;
  m.size = n;
  for (size_t i = 0; i < n && i < 256; ++i)
    m.bytes[i] = b[i];
  return m;
}
void responses_preserve_ack_errors_unknown_and_explicit_state() {
  const uint8_t ack[] = {1, 0};
  auto r = decode(Channel::Command, body(ack, 2));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)r.outcome);
  TEST_ASSERT_EQUAL_UINT8(1, r.id);
  TEST_ASSERT_EQUAL_UINT8(0, r.result);
  TEST_ASSERT_FALSE(r.encoding_known);
  const uint8_t error[] = {1, 2};
  r = decode(Channel::Command, body(error, 2));
  TEST_ASSERT_EQUAL_UINT8(2, r.result);
  TEST_ASSERT_FALSE(r.encoding_known);
  const uint8_t states[] = {0x93, 0, 8, 1, 1, 10, 1, 0, 82, 1, 1};
  r = decode(Channel::Query, body(states, sizeof(states)));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)r.outcome);
  TEST_ASSERT_TRUE(r.busy_known && r.busy && r.encoding_known && !r.encoding && r.ready_known &&
                   r.ready);
  for (uint8_t id : {uint8_t(0x13), uint8_t(0x53), uint8_t(0x93)}) {
    uint8_t b[] = {id, 0, 10, 1, 1};
    r = decode(Channel::Query, body(b, 5));
    TEST_ASSERT_TRUE(r.encoding_known && r.encoding);
  }
  const uint8_t unknown[] = {0xaa, 0, 0xbb};
  r = decode(Channel::Command, body(unknown, 3));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Unknown, (int)r.outcome);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(unknown, r.raw.bytes, 3);
  const uint8_t unknown_element[] = {0x93, 0, 99, 1, 1};
  r = decode(Channel::Query, body(unknown_element, 5));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Unknown, (int)r.outcome);
  const uint8_t malformed[] = {0x93, 0, 10, 2, 1};
  r = decode(Channel::Query, body(malformed, 5));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Invalid, (int)r.outcome);
  TEST_ASSERT_FALSE(r.encoding_known);
  const uint8_t invalid_bool[] = {0x93, 0, 10, 1, 2};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Invalid,
                        (int)decode(Channel::Query, body(invalid_bool, 5)).outcome);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Unknown,
                        (int)decode(Channel::Settings, body(ack, 2)).outcome);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Invalid,
                        (int)decode(Channel::Command, body(ack, 257)).outcome);
}
void fragments_are_isolated_and_counters_tolerant() {
  Reassembler a;
  Message m;
  const uint8_t start[] = {0x20, 5, 0x93, 0};
  const uint8_t end[] = {0x8f, 10, 1, 1};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Pending, (int)a.feed(1, Channel::Query, start, 4, 0, m));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Pending, (int)a.feed(2, Channel::Query, start, 4, 0, m));
  TEST_ASSERT_EQUAL_INT((int)Outcome::MissingStart, (int)a.feed(1, Channel::Command, end, 4, 1, m));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)a.feed(2, Channel::Query, end, 4, 1, m));
  TEST_ASSERT_TRUE(decode(Channel::Query, m).encoding);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)a.feed(1, Channel::Query, end, 4, 2, m));
  const uint8_t ext16[] = {0x40, 0, 2, 1, 0};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)a.feed(1, Channel::Command, ext16, 5, 3, m));
  TEST_ASSERT_EQUAL_UINT(2, m.size);
}
void malformed_limits_interruption_timeout_reset() {
  Reassembler a;
  Message m;
  const uint8_t start[] = {5, 0x93};
  const uint8_t end[] = {0x80, 0, 10, 1, 1};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Pending, (int)a.feed(1, Channel::Query, start, 2, 0, m));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Interrupted, (int)a.feed(1, Channel::Query, start, 2, 1, m));
  TEST_ASSERT_EQUAL_INT((int)Outcome::MissingStart, (int)a.feed(1, Channel::Query, end, 5, 2, m));
  a.feed(1, Channel::Query, start, 2, 0, m);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Timeout, (int)a.feed(1, Channel::Query, end, 5, 1000, m));
  a.feed(1, Channel::Query, start, 2, 0xfffffff0, m);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)a.feed(1, Channel::Query, end, 5, 20, m));
  a.feed(1, Channel::Query, start, 2, 0, m);
  a.reset(1, Channel::Query);
  TEST_ASSERT_EQUAL_INT((int)Outcome::MissingStart, (int)a.feed(1, Channel::Query, end, 5, 2, m));
  for (int i = 1; i <= 4; ++i)
    a.feed(i, Channel::Query, start, 2, 0, m);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Capacity, (int)a.feed(5, Channel::Query, start, 2, 1, m));
  a.resetPeer(1);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Pending, (int)a.feed(5, Channel::Query, start, 2, 1, m));
  a.resetPeer(5);
  const uint8_t bad[][4] = {{0x20, 0, 0, 0}, {0x40, 0, 0, 0}, {0x60, 0, 0, 0},
                            {0x41, 0, 2, 0}, {2, 1, 0, 1},    {0x21, 1, 0, 0}};
  const size_t sizes[] = {1, 2, 1, 4, 4, 3};
  for (size_t i = 0; i < 6; ++i)
    TEST_ASSERT_TRUE(a.feed(5, Channel::Query, bad[i], sizes[i], 2, m) != Outcome::Complete);
  uint8_t large[65] = {2};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Oversize, (int)a.feed(5, Channel::Query, large, 65, 2, m));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Invalid, (int)a.feed(5, Channel::Query, nullptr, 1, 2, m));
  a.feed(5, Channel::Query, start, 2, 2, m);
  const uint8_t reserved[] = {0x90, 0};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Invalid, (int)a.feed(5, Channel::Query, reserved, 2, 3, m));
}
void documentary_info_bounds_and_packet_budget() {
  // Synthetic schema fixtures: these bytes are not a camera identity/capture.
  const uint8_t hw[] = {0x3c, 0,   1, 7, 1, 'x', 1, 0, 1, 'f', 1, 's', 1, 'a',
                        1,    'm', 0, 0, 0, 0,   0, 0, 0, 0,   0, 0,   0};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete,
                        (int)decode(Channel::Command, body(hw, sizeof(hw))).outcome);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Invalid,
                        (int)decode(Channel::Command, body(hw, sizeof(hw) - 1)).outcome);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete,
                        (int)decode(Channel::Command, body(hw, sizeof(hw) - 11)).outcome);
  const uint8_t api[] = {0x51, 0, 2, 0, 2, 1, 0};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete,
                        (int)decode(Channel::Command, body(api, sizeof(api))).outcome);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Invalid,
                        (int)decode(Channel::Command, body(api, sizeof(api) - 1)).outcome);
  const uint8_t keep[] = {0x5b, 0};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete,
                        (int)decode(Channel::Settings, body(keep, 2)).outcome);
  Reassembler a;
  Message m;
  const uint8_t start[] = {0x21, 0, 1};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Pending, (int)a.feed(1, Channel::Query, start, 3, 0, m));
  for (unsigned i = 0; i < 31; ++i) {
    const uint8_t cont[] = {static_cast<uint8_t>(0x80 | (i & 15)), 0};
    TEST_ASSERT_EQUAL_INT((int)Outcome::Pending, (int)a.feed(1, Channel::Query, cont, 2, i, m));
  }
  const uint8_t cont[] = {0x80, 0};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Oversize, (int)a.feed(1, Channel::Query, cont, 2, 32, m));
  TEST_ASSERT_EQUAL_INT((int)Outcome::MissingStart, (int)a.feed(1, Channel::Query, cont, 2, 33, m));
  a.feed(1, Channel::Query, start, 3, 0, m);
  uint8_t chunk[64] = {0x80};
  for (int i = 0; i < 4; ++i)
    TEST_ASSERT_EQUAL_INT((int)Outcome::Pending, (int)a.feed(1, Channel::Query, chunk, 64, i, m));
  const uint8_t final[] = {0x81, 0, 0, 0};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)a.feed(1, Channel::Query, final, 4, 5, m));
  TEST_ASSERT_EQUAL_UINT(256, m.size);
  // Expired slots cannot starve a new peer.
  for (int i = 0; i < 4; ++i)
    a.feed(i, Channel::Query, start, 3, 0, m);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Pending, (int)a.feed(9, Channel::Query, start, 3, 1000, m));
}
void active_characteristics_and_boolean_states_remain_independent() {
  Reassembler a;
  Message m;
  const uint8_t query_start[] = {5, 0x93};
  const uint8_t command_start[] = {2, 1};
  const uint8_t setting_start[] = {2, 0x5b};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Pending,
                        (int)a.feed(1, Channel::Query, query_start, 2, 0, m));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Pending,
                        (int)a.feed(1, Channel::Command, command_start, 2, 0, m));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Pending,
                        (int)a.feed(1, Channel::Settings, setting_start, 2, 0, m));
  const uint8_t ack[] = {0x80, 0};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)a.feed(1, Channel::Settings, ack, 2, 999, m));
  TEST_ASSERT_EQUAL_UINT8(0x5b, m.bytes[0]);
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)a.feed(1, Channel::Command, ack, 2, 999, m));
  TEST_ASSERT_EQUAL_UINT8(1, m.bytes[0]);
  const uint8_t query_end[] = {0x80, 0, 82, 1, 0};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete,
                        (int)a.feed(1, Channel::Query, query_end, 5, 999, m));
  TEST_ASSERT_TRUE(decode(Channel::Query, m).ready_known);
  TEST_ASSERT_FALSE(decode(Channel::Query, m).ready);
  for (uint8_t id : {uint8_t(8), uint8_t(10), uint8_t(82)}) {
    for (uint8_t value = 0; value < 2; ++value) {
      const uint8_t b[] = {0x13, 0, id, 1, value};
      auto r = decode(Channel::Query, body(b, 5));
      TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)r.outcome);
      if (id == 8) {
        TEST_ASSERT_TRUE(r.busy_known);
        TEST_ASSERT_EQUAL_INT(value, r.busy);
      }
      if (id == 10) {
        TEST_ASSERT_TRUE(r.encoding_known);
        TEST_ASSERT_EQUAL_INT(value, r.encoding);
      }
      if (id == 82) {
        TEST_ASSERT_TRUE(r.ready_known);
        TEST_ASSERT_EQUAL_INT(value, r.ready);
      }
    }
  }
  const uint8_t error[] = {0x13, 0xff};
  auto r = decode(Channel::Query, body(error, 2));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)r.outcome);
  TEST_ASSERT_EQUAL_UINT8(0xff, r.result);
  TEST_ASSERT_FALSE(r.ready_known);
  const uint8_t alternate[] = {0x96, 0, 0, 10, 1, 1};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Unknown,
                        (int)decode(Channel::Query, body(alternate, 6)).outcome);
  const uint8_t partial[] = {0x93, 0, 10, 1, 1, 8};
  r = decode(Channel::Query, body(partial, 6));
  TEST_ASSERT_EQUAL_INT((int)Outcome::Invalid, (int)r.outcome);
  TEST_ASSERT_FALSE(r.encoding_known);
  a.feed(1, Channel::Query, query_start, 2, 0, m);
  const uint8_t empty[] = {0x80};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Invalid, (int)a.feed(1, Channel::Query, empty, 1, 1, m));
  TEST_ASSERT_EQUAL_INT((int)Outcome::MissingStart,
                        (int)a.feed(1, Channel::Query, query_end, 5, 2, m));
  const uint8_t zero[] = {0};
  TEST_ASSERT_EQUAL_INT((int)Outcome::Invalid, (int)a.feed(1, Channel::Query, zero, 1, 3, m));
}
void official_documentary_vectors_decode_end_to_end() {
  Reassembler a;
  Message m;
  Packet p;
  TEST_ASSERT_TRUE(encode(Request::ShutterOn, p));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(gopro_fixture::shutter_on, p.bytes, p.size);
  TEST_ASSERT_TRUE(encode(Request::Video, p, false));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(gopro_fixture::video_group, p.bytes, p.size);
  const uint8_t *vectors[] = {gopro_fixture::busy_register, gopro_fixture::encoding_register,
                              gopro_fixture::busy_notify, gopro_fixture::encoding_notify};
  for (size_t i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL_INT((int)Outcome::Complete,
                          (int)a.feed(1, Channel::Query, vectors[i], 6, 0, m));
    auto r = decode(Channel::Query, m);
    TEST_ASSERT_EQUAL_INT((int)Outcome::Complete, (int)r.outcome);
    if (i == 0 || i == 2) {
      TEST_ASSERT_TRUE(r.busy_known);
      TEST_ASSERT_EQUAL_INT(i == 0, r.busy);
    } else {
      TEST_ASSERT_TRUE(r.encoding_known && r.encoding);
    }
  }
  TEST_ASSERT_EQUAL_INT((int)Outcome::Complete,
                        (int)a.feed(1, Channel::Command, gopro_fixture::shutter_success, 3, 0, m));
  TEST_ASSERT_FALSE(decode(Channel::Command, m).encoding_known);
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(active_characteristics_and_boolean_states_remain_independent);
  RUN_TEST(official_documentary_vectors_decode_end_to_end);
  RUN_TEST(documentary_info_bounds_and_packet_budget);
  RUN_TEST(documented_requests_and_routes);
  RUN_TEST(responses_preserve_ack_errors_unknown_and_explicit_state);
  RUN_TEST(fragments_are_isolated_and_counters_tolerant);
  RUN_TEST(malformed_limits_interruption_timeout_reset);
  return UNITY_END();
}

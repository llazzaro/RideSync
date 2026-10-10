#include "protocol/insta360_go3s_codec.h"
#include <cstring>
#include <unity.h>
using namespace ridesync::go3s;
// Literal vectors generated independently from published layout and the
// standard CRC16/MODBUS algorithm, not hardware captures or codec round trips.
static const uint8_t start[] = {0xff, 0x7, 0x40, 0x12, 0x0, 0x12, 0x0, 0x0, 0x0,
                                0x4,  0x0, 0x0,  0x4,  0x0, 0x2,  0x1, 0x0, 0x0,
                                0x80, 0x0, 0x0,  0x8,  0x1, 0x29, 0xa3};
static const uint8_t stop[] = {0xff, 0x7, 0x40, 0x10, 0x0, 0x10, 0x0,  0x0, 0x0, 0x4,  0x0, 0x0,
                               0x5,  0x0, 0x2,  0x2,  0x0, 0x0,  0x80, 0x0, 0x0, 0x44, 0x3f};
static const uint8_t sync[] = {0xff, 0x7, 0x41, 0x7, 0x0, 0x0,  0x0,
                               0x0,  0x0, 0x0,  0x0, 0x0, 0x5c, 0x42};
static const uint8_t ack[] = {0xff, 0x6, 0x40, 0x10, 0x0, 0x10, 0x0,  0x0, 0x0, 0x4,  0x0, 0x0,
                              0xc8, 0x0, 0x2,  0x1,  0x0, 0x0,  0xc0, 0x0, 0x0, 0x70, 0x9c};
static const uint8_t auth[] = {0xff, 0x07, 0x40, 0x18, 0x00, 0x18, 0x00, 0x00, 0x00, 0x04, 0x00,
                               0x00, 0x27, 0x00, 0x02, 0x03, 0x00, 0x00, 0x80, 0x00, 0x00, 0x0a,
                               0x04, 0x74, 0x65, 0x73, 0x74, 0x10, 0x02, 0x0a, 0xa2};
static const uint8_t video[] = {0xff, 0x07, 0x40, 0x16, 0x00, 0x16, 0x00, 0x00, 0x00, 0x04,
                                0x00, 0x00, 0x02, 0x00, 0x02, 0x04, 0x00, 0x00, 0x80, 0x00,
                                0x00, 0x0a, 0x04, 0x08, 0x29, 0x10, 0x00, 0x56, 0xdc};
void literal_requests_and_empty_errors() {
  auto a = encode(Command::Authorize, 3, "test", 4);
  TEST_ASSERT_EQUAL_UINT(sizeof(auth), a.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(auth, a.bytes.data(), sizeof(auth));
  auto v = encode(Command::VideoMode, 4);
  TEST_ASSERT_EQUAL_UINT(sizeof(video), v.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(video, v.bytes.data(), sizeof(video));
  auto s = encode(Command::StartVideo, 1);
  TEST_ASSERT_EQUAL_UINT(sizeof(start), s.size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(start, s.bytes.data(), sizeof(start));
  auto t = encode(Command::Stop, 2);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(stop, t.bytes.data(), sizeof(stop));
  auto y = encodeSync();
  TEST_ASSERT_EQUAL_UINT8_ARRAY(sync, y.bytes.data(), sizeof(sync));
  for (auto seq : {0, 255}) {
    auto e = encode(Command::Stop, seq);
    TEST_ASSERT_EQUAL_UINT(0, e.size);
    for (auto b : e.bytes)
      TEST_ASSERT_EQUAL_UINT8(0, b);
  }
  TEST_ASSERT_EQUAL_UINT(0, encode(static_cast<Command>(99), 1).size);
  TEST_ASSERT_EQUAL_UINT(0, encode(Command::Authorize, 1).size);
  TEST_ASSERT_EQUAL_UINT(0, encode(Command::Authorize, 1, "", 0).size);
  TEST_ASSERT_EQUAL_UINT(0, encode(Command::Authorize, 1, "a\nb", 3).size);
  char full[32];
  std::memset(full, 'A', sizeof(full));
  TEST_ASSERT_EQUAL_UINT(59, encode(Command::Authorize, 1, full, sizeof(full)).size);
  TEST_ASSERT_EQUAL_UINT(0, encode(Command::Authorize, 1, full, 33).size);
}
void validates_receive_without_inventing_state() {
  auto d = decode(ack, sizeof(ack));
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::Response), static_cast<int>(d.kind));
  TEST_ASSERT_EQUAL_UINT8(1, d.sequence);
  TEST_ASSERT_EQUAL_UINT16(200, d.status);
  for (size_t n = 0; n < sizeof(ack); ++n) {
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::Invalid), static_cast<int>(decode(ack, n).kind));
  }
  uint8_t changed[sizeof(ack)];
  for (size_t n = 0; n < sizeof(ack); ++n) {
    std::memcpy(changed, ack, sizeof(ack));
    changed[n] ^= 1;
    auto e = decode(changed, sizeof(changed));
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::Invalid), static_cast<int>(e.kind));
    TEST_ASSERT_EQUAL_UINT8(0, e.sequence);
    TEST_ASSERT_EQUAL_UINT16(0, e.status);
  }
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::Invalid), static_cast<int>(decode(nullptr, 23).kind));
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::Invalid),
                    static_cast<int>(decode(start, sizeof(start)).kind));
}
void fragmented_and_coalesced_stream_is_bounded() {
  Receiver r;
  for (size_t n = 0; n < sizeof(ack); ++n) {
    auto d = r.push(ack[n]);
    TEST_ASSERT_EQUAL(static_cast<int>(n + 1 == sizeof(ack) ? Kind::Response : Kind::Pending),
                      static_cast<int>(d.kind));
  }
  for (unsigned n = 0; n < 2; ++n) {
    Decoded d;
    for (auto b : ack)
      d = r.push(b);
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::Response), static_cast<int>(d.kind));
  }
  TEST_ASSERT_FALSE(r.pending());
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::Invalid), static_cast<int>(r.push(0).kind));
  const uint8_t oversized[] = {0xff, 6, 0x40, 0xff, 0xff};
  Decoded d;
  for (auto b : oversized)
    d = r.push(b);
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::Invalid), static_cast<int>(d.kind));
  r.push(0xff);
  TEST_ASSERT_TRUE(r.pending());
  r.reset();
  TEST_ASSERT_FALSE(r.pending());
}
// Recompute only synthetic input checksums, independently of production codec.
static void repairCrc(uint8_t *bytes, size_t size) {
  unsigned c = 65535;
  for (size_t n = 0; n < size - 2; ++n) {
    c ^= bytes[n];
    for (unsigned bit = 0; bit < 8; ++bit) {
      unsigned low = c % 2;
      c /= 2;
      if (low)
        c ^= 40961;
    }
  }
  bytes[size - 2] = c & 255;
  bytes[size - 1] = c >> 8;
}
void correct_crc_cannot_hide_invalid_lengths_or_inner_fragments() {
  uint8_t changed[sizeof(ack)];
  const unsigned offsets[] = {3, 5, 7, 8, 9, 10, 11, 14, 16, 17, 18, 19, 20};
  for (auto offset : offsets) {
    std::memcpy(changed, ack, sizeof(ack));
    changed[offset] ^= 1;
    repairCrc(changed, sizeof(changed));
    TEST_ASSERT_EQUAL(static_cast<int>(Kind::Invalid),
                      static_cast<int>(decode(changed, sizeof(changed)).kind));
  }
  std::memcpy(changed, ack, sizeof(ack));
  changed[12] = 0;
  changed[13] = 0x20;
  repairCrc(changed, sizeof(changed));
  auto opaque = decode(changed, sizeof(changed));
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::Response), static_cast<int>(opaque.kind));
  TEST_ASSERT_EQUAL_UINT16(0x2000, opaque.status);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(correct_crc_cannot_hide_invalid_lengths_or_inner_fragments);
  RUN_TEST(literal_requests_and_empty_errors);
  RUN_TEST(validates_receive_without_inventing_state);
  RUN_TEST(fragmented_and_coalesced_stream_is_bounded);
  return UNITY_END();
}

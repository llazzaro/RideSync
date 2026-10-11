#include "protocol/dji_codec.h"
#include <algorithm>
#include <cstdio>
#include <unity.h>
#include <vector>
using namespace ridesync;
using namespace ridesync::dji;
// Independent test oracle uses polynomial long division, no production CRC calls.
uint32_t oracle(const uint8_t *p, size_t n, unsigned width, uint32_t polynomial) {
  uint32_t r = 0x3aa3;
  for (size_t i = 0; i < n; ++i) {
    for (unsigned bit = 0; bit < 8; ++bit) {
      const bool feedback = ((r ^ (p[i] >> bit)) & 1) != 0;
      r >>= 1;
      if (feedback)
        r ^= polynomial;
    }
  }
  return width == 16 ? r & 0xffff : r;
}
void put(std::vector<uint8_t> &v, size_t at, uint32_t x, unsigned n) {
  for (unsigned i = 0; i < n; ++i)
    v[at + i] = uint8_t(x >> (8 * i));
}
void seal(std::vector<uint8_t> &v) {
  put(v, 10, oracle(v.data(), 10, 16, 0xa001), 2);
  put(v, v.size() - 4, oracle(v.data(), v.size() - 4, 32, 0xedb88320), 4);
}
std::vector<uint8_t> frame(uint8_t set, uint8_t id, uint8_t type, std::vector<uint8_t> payload,
                           uint16_t seq = 0xbeef) {
  std::vector<uint8_t> v(18 + payload.size());
  v[0] = 0xaa;
  put(v, 1, v.size(), 2);
  v[3] = type;
  put(v, 8, seq, 2);
  v[12] = set;
  v[13] = id;
  for (size_t i = 0; i < payload.size(); ++i)
    v[14 + i] = payload[i];
  seal(v);
  return v;
}
std::vector<uint8_t> hex(const char *s) {
  std::vector<uint8_t> v;
  for (; *s; s += 2) {
    unsigned n;
    TEST_ASSERT_TRUE(sscanf(s, "%2x", &n) == 1);
    v.push_back(n);
  }
  return v;
}
Decoded read(const std::vector<uint8_t> &v) { return decode(v.data(), v.size()); }
void published_vectors() {
  auto v = hex("AA330002000000000100982F00197856341206383456789ABC00000000000000000000000000000000F"
               "E1F00000000796C667C");
  auto d = read(v);
  TEST_ASSERT_TRUE(d.kind == Kind::ConnectionRequest);
  TEST_ASSERT_TRUE(d.device_id == 0x12345678);
  TEST_ASSERT_TRUE(d.verify_data == 8190 && d.verify_mode == 0 && d.sequence == 1);
  auto p = encodeConnection(1, 0x12345678, {0x38, 0x34, 0x56, 0x78, 0x9a, 0xbc}, 0, 8190);
  TEST_ASSERT_TRUE(p.size == v.size());
  TEST_ASSERT_TRUE(std::equal(v.begin(), v.end(), p.bytes.begin()));
  auto mode = hex("AA1B000100000000050057EE1D04000033FF0A01473936F4FAE1D0");
  TEST_ASSERT_TRUE(read(mode).kind ==
                   Kind::Unknown); // Published command remains structurally valid.
  TEST_ASSERT_TRUE(oracle(mode.data(), 10, 16, 0xa001) == 0xee57);
  TEST_ASSERT_TRUE(oracle(mode.data(), 23, 32, 0xedb88320) == 0xd0e1faf4);
}
void encoders() {
  TEST_ASSERT_TRUE(encodeConnection(2, 1, {}, 2, 1).size == 0);
  auto auth = encodeAuthorizationResponse(0xffff, 0x12345678, 0, 7);
  TEST_ASSERT_TRUE(auth.bytes[19] == 7);
  auto d = decode(auth.bytes.data(), auth.size);
  TEST_ASSERT_TRUE(d.kind == Kind::ConnectionResponse && d.response && d.sequence == 0xffff &&
                   d.device_id == 0x12345678);
  for (bool start : {false, true}) {
    auto p = encodeRecord(12, 0xff44, start);
    auto want = frame(0x1d, 3, 2, {0x44, 0xff, 0, 0, uint8_t(start ? 0 : 1), 0, 0, 0, 0}, 12);
    TEST_ASSERT_TRUE(p.size == want.size() &&
                     std::equal(want.begin(), want.end(), p.bytes.begin()));
  }
  for (bool periodic : {false, true}) {
    auto p = encodeStatusSubscription(13, periodic);
    auto want = frame(0x1d, 5, 0, {uint8_t(periodic ? 3 : 1), 20, 0, 0, 0, 0}, 13);
    TEST_ASSERT_TRUE(p.size == want.size() &&
                     std::equal(want.begin(), want.end(), p.bytes.begin()));
  }
  auto video = encodeVideoMode(14, 0xff55);
  auto want = frame(0x1d, 4, 2, {0x55, 0xff, 0, 0, 1, 0, 0, 0, 0}, 14);
  TEST_ASSERT_TRUE(video.size == want.size() &&
                   std::equal(want.begin(), want.end(), video.bytes.begin()));
  TEST_ASSERT_TRUE(modelWireId(4) == 0xff33 && modelWireId(5) == 0xff44 &&
                   modelWireId(6) == 0xff55 && modelWireId(360) == 0xff66 && modelWireId(7) == 0);
}
void responses_and_validation() {
  for (size_t n : {size_t(1), size_t(5)}) {
    auto d = read(frame(0x1d, 3, 0x20, std::vector<uint8_t>(n, 2)));
    TEST_ASSERT_TRUE(d.kind == Kind::RecordResponse && d.return_code == 2 &&
                     d.recording == RecordingState::Unknown);
  }
  TEST_ASSERT_TRUE(read(frame(0x1d, 3, 0x20, {0, 0})).kind == Kind::Invalid);
  TEST_ASSERT_TRUE(read(frame(0x1d, 4, 0x20, {0, 0, 0, 0, 0})).kind == Kind::ModeResponse);
  TEST_ASSERT_TRUE(read(frame(0x1d, 4, 0x20, {0})).kind == Kind::Invalid);
  auto conn = frame(0, 0x19, 2, std::vector<uint8_t>(33));
  conn[40] = 2;
  conn[41] = 1;
  seal(conn);
  auto d = read(conn);
  TEST_ASSERT_TRUE(d.kind == Kind::ConnectionRequest && d.verify_mode == 2 && d.verify_data == 1 &&
                   d.reply_type == 2);
  auto valid = frame(0x99, 1, 0, {});
  TEST_ASSERT_TRUE(read(valid).kind == Kind::Unknown);
  TEST_ASSERT_TRUE(decode(nullptr, 0).kind == Kind::Invalid);
  for (size_t i = 0; i < valid.size(); ++i)
    TEST_ASSERT_TRUE(decode(valid.data(), i).kind == Kind::Invalid);
  auto bad = valid;
  bad.push_back(0);
  TEST_ASSERT_TRUE(read(bad).kind == Kind::Invalid);
  for (size_t at : {size_t(0), size_t(10), size_t(14), valid.size() - 1}) {
    bad = valid;
    bad[at] ^= 1;
    TEST_ASSERT_TRUE(read(bad).kind == Kind::Invalid);
  }
  for (size_t at : {size_t(2), size_t(3), size_t(4)}) {
    bad = valid;
    bad[at] = at == 2 ? 4 : at == 3 ? 0x40 : 1;
    seal(bad);
    TEST_ASSERT_TRUE(read(bad).kind == Kind::Invalid);
  }
  TEST_ASSERT_TRUE(read(frame(1, 1, 0, std::vector<uint8_t>(239))).kind == Kind::Invalid);
  for (size_t n : {size_t(32), size_t(34)})
    TEST_ASSERT_TRUE(read(frame(0, 0x19, 2, std::vector<uint8_t>(n))).kind == Kind::Invalid);
}
void status_semantics() {
  std::vector<uint8_t> p(38);
  p[0] = 1;
  p[1] = 3;
  p[29] = 1;
  auto status = [&]() { return read(frame(0x1d, 2, 0, p)); };
  auto d = status();
  TEST_ASSERT_TRUE(d.kind == Kind::Status && d.recording == RecordingState::Recording &&
                   d.camera_status == 3);
  for (uint8_t s : {uint8_t(0), uint8_t(1), uint8_t(2)}) {
    p[1] = s;
    TEST_ASSERT_TRUE(status().recording == RecordingState::Stopped);
  }
  p[1] = 5;
  TEST_ASSERT_TRUE(status().recording == RecordingState::Unknown);
  p[1] = 3;
  for (uint8_t mode : {uint8_t(0), uint8_t(2), uint8_t(5), uint8_t(0x0a), uint8_t(0x28),
                       uint8_t(0x34), uint8_t(0x3a), uint8_t(0x3c), uint8_t(0x3f), uint8_t(0x43),
                       uint8_t(0x44), uint8_t(0x4a), uint8_t(0xff), uint8_t(0x1a), uint8_t(0x23)}) {
    p[0] = p[29] = mode;
    TEST_ASSERT_TRUE(status().recording == RecordingState::Unknown);
  }
  for (uint8_t mode : {uint8_t(1), uint8_t(0x38), uint8_t(0x41)}) {
    p[0] = p[29] = mode;
    TEST_ASSERT_TRUE(status().recording == RecordingState::Recording);
  }
  p[0] = p[29] = 1;
  p[28] = 3;
  TEST_ASSERT_TRUE(status().recording == RecordingState::Unknown);
  p[28] = 0;
  p[30] = 2;
  TEST_ASSERT_TRUE(status().recording == RecordingState::Unknown);
  p[30] = 1;
  TEST_ASSERT_TRUE(status().recording == RecordingState::Recording);
  p[30] = 0;
  p[29] = 5;
  TEST_ASSERT_TRUE(status().recording == RecordingState::Unknown);
  TEST_ASSERT_TRUE(read(frame(0x1d, 2, 0, std::vector<uint8_t>(37))).kind == Kind::Invalid);
  TEST_ASSERT_TRUE(read(frame(0x1d, 2, 0x20, std::vector<uint8_t>(38))).kind == Kind::Invalid);
}
void streaming() {
  Receiver r;
  auto v = frame(0x1d, 3, 0x20, {0});
  TEST_ASSERT_TRUE(r.push(0).kind == Kind::Invalid && !r.pending());
  for (size_t split = 1; split < v.size(); ++split) {
    r.reset();
    for (size_t i = 0; i < split; ++i)
      TEST_ASSERT_TRUE(r.push(v[i]).kind == Kind::Pending);
    TEST_ASSERT_TRUE(r.pending());
    for (size_t i = split; i < v.size(); ++i) {
      auto d = r.push(v[i]);
      TEST_ASSERT_TRUE(d.kind == (i + 1 == v.size() ? Kind::RecordResponse : Kind::Pending));
    }
    TEST_ASSERT_TRUE(!r.pending());
  }
  for (unsigned repeat = 0; repeat < 2; ++repeat)
    for (uint8_t b : v)
      r.push(b);
  TEST_ASSERT_TRUE(!r.pending());
  r.push(0xaa);
  r.push(0xff);
  TEST_ASSERT_TRUE(r.push(3).kind == Kind::Invalid && !r.pending());
  for (size_t i = 0; i < 8; ++i)
    r.push(v[i]);
  r.reset();
  TEST_ASSERT_TRUE(!r.pending());
  auto corrupt = v;
  corrupt[10] ^= 1;
  for (size_t i = 0; i < 12; ++i)
    r.push(corrupt[i]);
  TEST_ASSERT_TRUE(!r.pending());
  Decoded d;
  for (uint8_t b : v)
    d = r.push(b);
  TEST_ASSERT_TRUE(d.kind == Kind::RecordResponse);
}
void bounded_noise_and_sequence() {
  for (uint16_t sequence : {uint16_t(0), uint16_t(1), uint16_t(65535)}) {
    auto d = read(frame(0x1d, 3, 0x20, {0}, sequence));
    TEST_ASSERT_EQUAL_UINT16(sequence, d.sequence);
    auto reply = encodeAuthorizationResponse(sequence, 0x12345678, 2, 0xabcdef01);
    auto result = decode(reply.bytes.data(), reply.size);
    TEST_ASSERT_EQUAL_UINT16(sequence, result.sequence);
    TEST_ASSERT_EQUAL_UINT8(2, result.return_code);
    TEST_ASSERT_EQUAL_UINT8(1, reply.bytes[19]);
    TEST_ASSERT_EQUAL_UINT8(0xab, reply.bytes[22]);
  }
  auto max = frame(0x99, 1, 0, std::vector<uint8_t>(238));
  TEST_ASSERT_EQUAL_UINT(256, max.size());
  TEST_ASSERT_TRUE(read(max).kind == Kind::Unknown);
  Receiver receiver;
  Decoded d;
  for (uint8_t b : max)
    d = receiver.push(b);
  TEST_ASSERT_TRUE(d.kind == Kind::Unknown && !receiver.pending());
  auto corrupt = frame(0x1d, 3, 0x20, {0});
  corrupt.back() ^= 1;
  for (uint8_t b : corrupt)
    d = receiver.push(b);
  TEST_ASSERT_TRUE(d.kind == Kind::Invalid && !receiver.pending());
  // Deterministic bounded hostile stream; sanitizer checks all parser accesses.
  uint32_t random = 17;
  for (unsigned i = 0; i < 100000; ++i) {
    random = random * 1664525u + 1013904223u;
    receiver.push(uint8_t(random >> 24));
    if ((i % 257) == 0)
      receiver.reset();
  }
  receiver.reset();
  auto ack = frame(0x1d, 3, 0x20, {0});
  for (uint8_t b : ack)
    d = receiver.push(b);
  TEST_ASSERT_TRUE(d.kind == Kind::RecordResponse && !receiver.pending());
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(published_vectors);
  RUN_TEST(encoders);
  RUN_TEST(responses_and_validation);
  RUN_TEST(status_semantics);
  RUN_TEST(streaming);
  RUN_TEST(bounded_noise_and_sequence);
  return UNITY_END();
}

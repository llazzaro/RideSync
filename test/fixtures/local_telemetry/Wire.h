
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
struct TwoWire {
  unsigned calls = 0;
  bool short_read = false, short_write = false, nack = false, short_register = false;
  unsigned ends = 0;
  uint8_t power = 6;
  uint8_t reg = 0;
  uint32_t available_bytes = 0, offset = 0;
  uint8_t payload[112]{};
  void beginTransmission(uint8_t) {
    ++calls;
    offset = 0;
  }
  size_t write(uint8_t r) {
    reg = r;
    return short_register ? 0 : 1;
  }
  size_t write(const uint8_t *, size_t n) { return short_write ? n - 1 : n; }
  uint8_t endTransmission(bool) {
    ++ends;
    return nack ? 2 : 0;
  }
  uint32_t requestFrom(uint8_t, size_t n, bool) {
    ++calls;
    offset = 0;
    available_bytes = short_read ? n - 1 : n;
    return available_bytes;
  }
  int available() { return available_bytes - offset; }
  int read() {
    if (reg == 0x7d) {
      ++offset;
      return power;
    }
    return payload[offset++];
  }
};

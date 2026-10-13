#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
struct FakeSerial {
  std::string input, output;
  int room = 0;
  unsigned blocking = 0, writes = 0;
  size_t short_limit = SIZE_MAX;
  int available() { return int(input.size()); }
  int read() {
    int ch = input[0];
    input.erase(0, 1);
    return ch;
  }
  int availableForWrite() { return room; }
  size_t write(const uint8_t *p, size_t n) {
    assert(n <= size_t(room) && n <= 64);
    const auto actual = n < short_limit ? n : short_limit;
    output.append(reinterpret_cast<const char *>(p), actual);
    room -= int(actual);
    ++writes;
    return actual;
  }
  template <class... T> void printf(const char *, T...) { ++blocking; }
  void println(const char *) { ++blocking; }
};
extern FakeSerial Serial;
inline unsigned uxTaskGetStackHighWaterMark(void *) { return 0; }

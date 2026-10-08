#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#define SERIAL_8N1 0
#define OUTPUT 1
extern std::atomic<uint32_t> raw_time;
inline uint32_t millis() { return raw_time.load(); }
inline void delayMicroseconds(uint32_t) {}
inline void digitalWrite(int, int) {}
inline void pinMode(int, int) {}
struct HardwareSerial {
  unsigned begins = 0, writes = 0;
  std::string tx;
  void begin(uint32_t, int, int, int) { ++begins; }
  int available() { return 0; }
  int read() { return -1; }
  int availableForWrite() { return 256; }
  size_t write(const uint8_t *b, size_t n) {
    ++writes;
    tx.append(reinterpret_cast<const char *>(b), n);
    return n;
  }
};

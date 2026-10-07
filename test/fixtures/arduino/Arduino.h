#pragma once
// Host-only adapter fixture; no claim about physical GPIO/UART timing.
#include <cstddef>
#include <cstdint>
#define SERIAL_8N1 0
#define OUTPUT 1
extern int pin_calls;
extern int levels[40];
inline void digitalWrite(int pin, int level) {
  ++pin_calls;
  levels[pin] = level;
}
inline void pinMode(int, int) { ++pin_calls; }
class HardwareSerial {
public:
  int begins = 0, writes = 0, space = 4;
  void begin(uint32_t, int, int, int) { ++begins; }
  int available() { return 0; }
  int read() { return -1; }
  int availableForWrite() { return space; }
  size_t write(const uint8_t *, size_t n) {
    ++writes;
    space -= n;
    return n;
  }
};

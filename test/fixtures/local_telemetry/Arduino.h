#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#define SERIAL_8N1 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define LOW 0
extern int button_level;
inline int digitalRead(int) { return button_level; }
extern std::atomic<uint32_t> raw_time;
inline uint32_t millis() { return raw_time.load(); }
inline void delayMicroseconds(uint32_t) {}
inline void digitalWrite(int, int) {}
extern unsigned button_configs;
inline void pinMode(int, int) { ++button_configs; }
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

struct SerialPort {
  void begin(unsigned) {}
  void println(const char *) {}
  template <typename... T> void printf(const char *, T...) {}
  int available() { return 0; }
  int read() { return -1; }
};
static SerialPort Serial;
inline void delay(unsigned) {}

// Temporary bench diagnostic: no file writes, formatting, modem reset or PWRKEY.
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
// Documentary T-A7670X V1.4 pin candidates; see README for qualification limits.
constexpr int BOARD_POWERON_PIN = 12;
constexpr int BOARD_SCK_PIN = 14, BOARD_MISO_PIN = 2, BOARD_MOSI_PIN = 15;
constexpr int BOARD_SD_CS_PIN = 13;
uint32_t word(const uint8_t *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t crc = 0xffffffff;
  for (size_t i = 0; i < n; ++i) {
    crc ^= p[i];
    for (unsigned b = 0; b < 8; ++b)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return ~crc;
}
void probe() {
  Serial.println("SD_READONLY: mounting at 1 MHz; format disabled");
  if (!SD.begin(BOARD_SD_CS_PIN, SPI, 1000000, "/sdprobe", 1, false)) {
    Serial.println("SD_READONLY: mount FAILED");
    return;
  }
  Serial.printf("SD_READONLY: type=%u capacity_bytes=%llu\n", SD.cardType(), SD.cardSize());
  uint8_t records[2][40]{};
  bool valid = true;
  const char *paths[] = {"/.session-id-a", "/.session-id-b"};
  for (unsigned i = 0; i < 2; ++i) {
    File f = SD.open(paths[i], FILE_READ);
    bool good = f && !f.isDirectory() && f.size() == 40 && f.read(records[i], 40) == 40;
    if (f)
      f.close();
    auto p = records[i];
    good = good && word(p) == 0x44495352 && word(p + 4) == 1 && word(p + 8) != 0 &&
           word(p + 12) == 0 && word(p + 16) == 0 && word(p + 20) == ~word(p + 8) &&
           word(p + 24) == 0xffffffff && word(p + 28) == 0xffffffff && word(p + 32) == 0 &&
           word(p + 36) == crc32(p, 36);
    Serial.printf("SD_READONLY: baseline_slot_%u=%s\n", i, good ? "VALID" : "INVALID");
    valid = valid && good;
  }
  valid = valid && memcmp(records[0], records[1], 40) == 0;
  Serial.printf("SD_READONLY: baseline_pair=%s; no files written\n", valid ? "VALID" : "INVALID");
  SD.end();
}
void setup() {
  Serial.begin(115200);
  digitalWrite(BOARD_POWERON_PIN, HIGH);
  pinMode(BOARD_POWERON_PIN, OUTPUT);
  SPI.begin(BOARD_SCK_PIN, BOARD_MISO_PIN, BOARD_MOSI_PIN, BOARD_SD_CS_PIN);
  delay(1000);
  Serial.println("SD_READONLY: boot; modem control disabled; send S to probe");
}
void loop() {
  if (Serial.available() && Serial.read() == 'S')
    probe();
  delay(10);
}

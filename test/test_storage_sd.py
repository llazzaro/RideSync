"""Exercise deferred SD task ownership with host framework stand-ins."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StorageAdapterTest(unittest.TestCase):
    def test_opt_in_defers_all_io_to_worker_and_preserves_existing_path(self):
        files = {
            'SPI.h': '#pragma once\nstruct SPIClass {};\n',
            'SD.h': r'''
#pragma once
#include <cstddef>
#include "SPI.h"
#define FILE_WRITE "w"
extern unsigned io_calls;
struct File {
  bool valid = false;
  operator bool() const { return valid; }
  size_t write(const unsigned char *, size_t n) { ++io_calls; return n; }
  void flush() { ++io_calls; }
  void close() { ++io_calls; valid = false; }
};
struct SDClass {
  bool collision = false;
  unsigned opens = 0;
  bool begin(unsigned char, SPIClass &, unsigned, const char *, unsigned char, bool format) { ++io_calls; return !format; }
  bool exists(const char *) { ++io_calls; return collision; }
  File open(const char *, const char *) { ++io_calls; ++opens; File f; f.valid = true; return f; }
};
extern SDClass SD;
''',
            'freertos/FreeRTOS.h': '#pragma once\n#define pdPASS 1\n',
            'freertos/task.h': r'''
#pragma once
#include "FreeRTOS.h"
using TaskHandle_t = void *;
extern void (*task_fn)(void *);
extern void *task_arg;
inline int xTaskCreate(void (*fn)(void *), const char *, unsigned, void *arg, unsigned, TaskHandle_t *h) { task_fn = fn; task_arg = arg; *h = arg; return pdPASS; }
inline void vTaskDelay(unsigned) {}
inline void vTaskDelete(void *) {}
''',
            'main.cpp': r'''
#include "storage_sd.h"
#include <cassert>
unsigned io_calls = 0;
SDClass SD;
void (*task_fn)(void *) = nullptr;
void *task_arg = nullptr;
int main() {
  using namespace ridesync;
  SPIClass spi;
  QualifiedSdConfig q;
  ArduinoSdStorage logger(spi, q, {42, "test", "synthetic"});
  assert(!logger.start() && io_calls == 0);
  q.opt_in = q.wiring_card_qualified = q.exclusive_volume = true;
  q.chip_select = 5; q.frequency_hz = 1000000;
  ArduinoSdStorage ready(spi, q, {43, "test", "synthetic"});
  assert(ready.start() && io_calls == 0);
  assert(!ready.start());
  ready.storage().requestStop(); task_fn(task_arg);
  assert(io_calls > 0 && SD.opens == 1 && ready.storage().health().stopped);
  SD.collision = true;
  ArduinoSdStorage collision(spi, q, {44, "test", "synthetic"});
  assert(collision.start()); task_fn(task_arg);
  assert(SD.opens == 1 && collision.storage().health().terminal);
}
''',
        }
        with tempfile.TemporaryDirectory() as folder:
            for name, content in files.items():
                path = Path(folder) / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            executable = Path(folder) / 'test'
            subprocess.run([
                'c++', '-std=c++11', '-DARDUINO', '-Wall', '-Wextra', '-Werror',
                '-I', folder, '-I', str(ROOT / 'include'),
                str(Path(folder) / 'main.cpp'), str(ROOT / 'src/storage.cpp'),
                str(ROOT / 'src/storage_sd.cpp'), '-o', str(executable),
            ], check=True, capture_output=True)
            subprocess.run([str(executable)], check=True, capture_output=True)

"""Exercise worker IO, exclusive creation and owned mount cleanup with SDK stand-ins."""
from pathlib import Path
import subprocess
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StorageAdapterTest(unittest.TestCase):
    def test_opt_in_exclusive_creation_and_owned_mount_lifecycle(self):
        files = {
            'SPI.h': '#pragma once\nstruct SPIClass {};\n',
            'vfs_api.h': '#pragma once\n#include <memory>\nstruct VFSImpl {}; namespace fs { using FSImplPtr = std::shared_ptr<VFSImpl>; }\n',
            'SD.h': r'''
#pragma once
#include <cstddef>
#include <string>
#include "SPI.h"
#include "vfs_api.h"
#define FILE_WRITE "w"
extern unsigned io_calls, owned_ends;
extern bool collision, probe_fail, mount_fail;
extern std::string existing_bytes;
extern SPIClass *owned_bus, *last_begin_bus;
struct File {
  bool valid = false;
  operator bool() const { return valid; }
  size_t write(const unsigned char *, size_t n) { ++io_calls; return n; }
  void flush() { ++io_calls; }
  void close() { ++io_calls; valid = false; }
};
struct SDClass {
  SPIClass *bus = nullptr;
  unsigned ends = 0;
  bool owned = false;
  bool begin(unsigned char, SPIClass &spi, unsigned, const char *, unsigned char, bool format) {
    ++io_calls; if (bus) return true; if (mount_fail || format) return false;
    last_begin_bus = &spi; bus = &spi; if (owned) owned_bus = bus; return true;
  }
  void end() { ++io_calls; ++ends; bus = nullptr; if (owned) { owned_bus = nullptr; ++owned_ends; } }
  bool exists(const char *) { ++io_calls; return collision && !probe_fail; }
  File open(const char *, const char *) { ++io_calls; existing_bytes.clear(); File f; f.valid = true; return f; }
};
extern SDClass SD;
namespace fs { struct SDFS : SDClass { explicit SDFS(FSImplPtr) { owned = true; } }; }
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
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
unsigned io_calls = 0, owned_ends = 0;
bool collision = false, probe_fail = false, mount_fail = false, open_fail = false, sync_fail = false;
std::string existing_bytes = "preserve-old-log";
SPIClass *owned_bus = nullptr, *last_begin_bus = nullptr;
SDClass SD;
void (*task_fn)(void *) = nullptr;
void *task_arg = nullptr;
int test_open(const char *, int flags, unsigned) {
  ++io_calls; assert((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)); assert(!(flags & O_TRUNC));
  if (open_fail) { errno = ENOMEM; return -1; }
  if (collision) { errno = EEXIST; return -1; }
  return 7;
}
ssize_t test_write(int, const void *, size_t n) { ++io_calls; return n; }
int test_fsync(int) { ++io_calls; if (sync_fail) { errno = EIO; return -1; } return 0; }
int test_close(int) { ++io_calls; return 0; }
int main(int argc, char **argv) {
  assert(argc == 2); using namespace ridesync;
  SPIClass spi; QualifiedSdConfig q;
  ArduinoSdStorage disabled(spi, q, {42, "test", "synthetic"});
  assert(!disabled.start() && io_calls == 0);
  q.opt_in = q.wiring_card_qualified = q.exclusive_volume = true;
  q.chip_select = 5; q.frequency_hz = 1000000;
  if (!strcmp(argv[1], "probe")) {
    collision = probe_fail = true;
    ArduinoSdStorage logger(spi, q, {43, "test", "synthetic"});
    assert(logger.start() && io_calls == 0); logger.storage().requestStop(); task_fn(task_arg);
    assert(existing_bytes == "preserve-old-log" && logger.storage().health().terminal);
    assert(logger.ioError() == EEXIST);
    assert(logger.workerFinished() && owned_bus == nullptr && owned_ends == 1);
  } else if (!strcmp(argv[1], "lifecycle")) {
    SPIClass unrelated; SD.bus = &unrelated;
    {
      ArduinoSdStorage logger(spi, q, {44, "test", "synthetic"});
      assert(logger.start() && io_calls == 0); assert(!logger.start());
      logger.storage().requestStop(); task_fn(task_arg);
      assert(logger.workerFinished() && logger.storage().health().stopped);
      assert(owned_bus == nullptr && owned_ends == 1 && SD.bus == &unrelated && SD.ends == 0);
    }
    SPIClass replacement;
    ArduinoSdStorage next(replacement, q, {45, "test", "synthetic"});
    assert(next.start()); next.storage().requestStop(); task_fn(task_arg);
    assert(last_begin_bus == &replacement);
    assert(next.workerFinished() && owned_ends == 2 && owned_bus == nullptr && SD.bus == &unrelated);
  } else if (!strcmp(argv[1], "failed-mount")) {
    SPIClass unrelated; SD.bus = &unrelated; mount_fail = true;
    ArduinoSdStorage failed(spi, q, {46, "test", "synthetic"});
    assert(failed.start()); failed.storage().requestStop(); task_fn(task_arg);
    assert(failed.storage().health().terminal && failed.workerFinished());
    assert(owned_ends == 0 && SD.bus == &unrelated && SD.ends == 0);
    mount_fail = false;
    ArduinoSdStorage next(spi, q, {47, "test", "synthetic"});
    assert(next.start()); next.storage().requestStop(); task_fn(task_arg);
    assert(!next.storage().health().terminal && next.workerFinished() && owned_ends == 1);
  } else {
    open_fail = !strcmp(argv[1], "open-error"); sync_fail = !open_fail;
    ArduinoSdStorage logger(spi, q, {48, "test", "synthetic"});
    RecordTimestamp t; t.session_id = 48; t.monotonic_quality = MonotonicQuality::Valid;
    ModemSnapshot m; m.session_id = 48; assert(logger.storage().enqueue(t, m));
    assert(logger.start()); logger.storage().requestStop(); task_fn(task_arg);
    assert(logger.workerFinished() && logger.storage().health().terminal);
    assert(logger.ioError() == (open_fail ? ENOMEM : EIO));
    assert(existing_bytes == "preserve-old-log" && owned_ends == 1 && owned_bus == nullptr);
  }
}
''',
        }
        source = (ROOT / 'src/storage_sd.cpp').read_text()
        # Temporary test copy substitutes only POSIX callbacks; production contains
        # no test seam. The stand-in models flags/error outcomes and mount lifetime.
        declarations = '''#include <sys/types.h>
int test_open(const char *, int, unsigned);
ssize_t test_write(int, const void *, size_t);
int test_fsync(int);
int test_close(int);
'''
        for operation in ('open', 'write', 'fsync', 'close'):
            source = re.sub(rf'(?<![\w:])::{operation}\(', f'::test_{operation}(', source)
        files['storage_sd.cpp'] = declarations + source
        with tempfile.TemporaryDirectory() as folder:
            for name, content in files.items():
                path = Path(folder) / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            executable = Path(folder) / 'test'
            build = subprocess.run([
                'c++', '-std=c++11', '-DARDUINO', '-Wall', '-Wextra', '-Werror',
                '-I', folder, '-I', str(ROOT / 'include'),
                str(Path(folder) / 'main.cpp'), str(ROOT / 'src/storage.cpp'),
                str(Path(folder) / 'storage_sd.cpp'), '-o', str(executable),
            ], capture_output=True)
            self.assertEqual(0, build.returncode, build.stderr.decode())
            for scenario in ('probe', 'lifecycle', 'failed-mount', 'open-error', 'sync-error'):
                with self.subTest(scenario=scenario):
                    result = subprocess.run([str(executable), scenario], capture_output=True,
                                            timeout=10)
                    self.assertEqual(0, result.returncode, result.stderr.decode())

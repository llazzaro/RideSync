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
#include <thread>
using TaskHandle_t = void *;
extern void (*task_fn)(void *);
extern void *task_arg;
extern bool task_fail;
inline int xTaskCreate(void (*fn)(void *), const char *, unsigned, void *arg, unsigned, TaskHandle_t *h) { if (task_fail) return 0; task_fn = fn; task_arg = arg; *h = arg; return pdPASS; }
inline void vTaskDelay(unsigned) { std::this_thread::yield(); }
inline void vTaskDelete(void *) {}
''',
            'main.cpp': r'''
#include "storage_sd.h"
#include <cassert>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <array>
#include <atomic>
unsigned io_calls = 0, owned_ends = 0;
bool collision = false, probe_fail = false, mount_fail = false, open_fail = false, sync_fail = false;
std::string existing_bytes = "preserve-old-log";
SPIClass *owned_bus = nullptr, *last_begin_bus = nullptr;
SDClass SD;
void (*task_fn)(void *) = nullptr;
void *task_arg = nullptr;
bool task_fail = false;
std::array<std::vector<uint8_t>, 2> ledger;
int slot = -1; size_t offset = 0;
std::atomic<bool> block_close{false}, close_entered{false}, release_close{false};
unsigned ledger_syncs = 0, ledger_closes = 0, mounts_at_csv = 0;
int test_open(const char *path, int flags, unsigned) {
  ++io_calls;
  if (strstr(path, ".session-id-")) {
    slot = strstr(path, "-a") ? 0 : 1; offset = 0;
    assert(!(flags & O_TRUNC));
    if (ledger[slot].empty()) { errno = ENOENT; return -1; }
    return 8;
  }
  assert((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)); assert(!(flags & O_TRUNC));
  mounts_at_csv = io_calls; assert(ledger_syncs == 1 && ledger_closes == 4);
  if (open_fail) { errno = ENOMEM; return -1; }
  if (collision) { errno = EEXIST; return -1; }
  return 7;
}
ssize_t test_read(int fd, void *b, size_t n) {
  assert(fd == 8); ++io_calls;
  n = std::min(n, ledger[slot].size() - offset);
  memcpy(b, ledger[slot].data() + offset, n); offset += n; return n;
}
ssize_t test_write(int fd, const void *b, size_t n) {
  ++io_calls;
  if (fd == 8) { ledger[slot].resize(n); memcpy(ledger[slot].data(), b, n); }
  return n;
}
int test_fsync(int fd) {
  ++io_calls; if (fd == 8) ++ledger_syncs;
  if (sync_fail) { errno = EIO; return -1; } return 0;
}
int test_close(int fd) {
  ++io_calls;
  if (fd == 8) ++ledger_closes;
  if (fd == 7 && block_close.load()) {
    close_entered.store(true);
    while (!release_close.load()) std::this_thread::yield();
  }
  return 0;
}
void put(uint8_t *b, uint32_t v) { for (unsigned j = 0; j < 4; ++j) b[j] = uint8_t(v >> (8*j)); }
void fixture() {
  uint8_t b[40]{};
  put(b, 0x44495352); put(b+4, 1); put(b+8, 7); put(b+20, ~uint32_t(7));
  put(b+24, ~uint32_t(0)); put(b+28, ~uint32_t(0));
  uint32_t crc = 0xffffffff;
  for (unsigned i=0;i<36;++i) { crc ^= b[i]; for (unsigned j=0;j<8;++j) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1))); }
  put(b+36, ~crc);
  for (auto &l : ledger) l.assign(b,b+40);
}
int main(int argc, char **argv) {
  assert(argc == 2); using namespace ridesync;
  SPIClass spi; QualifiedSdConfig q;
  ArduinoSdStorage disabled(spi, q);
  assert(!disabled.start() && io_calls == 0);
  q.opt_in = q.wiring_card_qualified = q.exclusive_volume = true;
  q.chip_select = 5; q.frequency_hz = 1000000;
  ArduinoSdStorage uncommissioned(spi, q); assert(!uncommissioned.start());
  q.namespace_commissioned = true; q.commissioned_namespace = 7;
  fixture();
  SPIClass unrelated; SD.bus = &unrelated;
  if (!strcmp(argv[1], "absent")) ledger[1].clear();
  if (!strcmp(argv[1], "corrupt")) ledger[1][12] ^= 1;
  mount_fail = !strcmp(argv[1], "failed-mount");
  sync_fail = !strcmp(argv[1], "commit-error");
  collision = !strcmp(argv[1], "collision");
  open_fail = !strcmp(argv[1], "open-error");
  ArduinoSdStorage logger(spi, q);
  task_fail = !strcmp(argv[1], "task-failure");
  if (task_fail) {
    assert(!logger.start() && io_calls == 0 && task_fn == nullptr);
    assert(!logger.workerFinished() && logger.allocation().status == IdentityStatus::Pending);
    // No task was created: caller handles false admission, without waiting for
    // workerFinished or constructing a session. Destruction is safe immediately.
    return 0;
  }
  assert(logger.start() && io_calls == 0); assert(!logger.start());
  auto fn = task_fn; auto arg = task_arg;
  std::thread worker([=] { fn(arg); });
  IdentityAllocation allocation;
  while ((allocation = logger.allocation()).status == IdentityStatus::Pending) std::this_thread::yield();
  if (allocation.status != IdentityStatus::Committed) {
    worker.join(); assert(logger.workerFinished() && owned_bus == nullptr);
    if (mount_fail) assert(allocation.status == IdentityStatus::MediaError && owned_ends == 0);
    else if (sync_fail) assert(allocation.status == IdentityStatus::CommitUncertain && allocation.error == EIO);
    else if (!strcmp(argv[1], "absent")) assert(allocation.status == IdentityStatus::IdentityUnavailable);
    else assert(allocation.status == IdentityStatus::LedgerCorrupt);
    assert(mounts_at_csv == 0);
  } else {
    assert(allocation.id == 0x700000001ULL && !logger.workerFinished());
    assert(owned_bus == &spi && SD.bus == &unrelated);
    if (!strcmp(argv[1], "unbound")) {
      logger.cancel(); worker.join();
      assert(logger.workerFinished() && owned_ends == 1 && mounts_at_csv == 0);
    } else {
      // Reject invalid/mismatched storage without relinquishing mounted owner.
      Storage bad(logger.sink(), {allocation.id, "bad\nfw", "test"}); assert(!logger.bind(bad));
      ArduinoSdStorage competing(spi, q);
      Storage wrong(competing.sink(), {allocation.id, "fw", "test"}); assert(!logger.bind(wrong));
      Storage storage(logger.sink(), {allocation.id, "test", "synthetic"});
      block_close.store(!strcmp(argv[1], "close-barrier"));
      assert(logger.bind(storage)); storage.requestStop();
      if (block_close.load()) {
        while (!close_entered.load()) std::this_thread::yield();
        assert(!logger.workerFinished() && owned_bus == &spi); release_close.store(true);
      }
      worker.join();
      assert(logger.workerFinished() && storage.health().stopped);
      assert(storage.health().terminal == (collision || open_fail));
      if (collision) assert(logger.ioStatus() == ArduinoSdStorage::IoStatus::PathCollision && logger.ioError() == EEXIST);
      else if (open_fail) assert(logger.ioError() == ENOMEM);
      else assert(logger.ioError() == 0);
    }
    assert(owned_bus == nullptr && owned_ends == 1);
  }
  assert(SD.bus == &unrelated && SD.ends == 0 && existing_bytes == "preserve-old-log");
}
''',
        }
        source = (ROOT / 'src/storage_sd.cpp').read_text()
        # Temporary test copy substitutes only POSIX callbacks; production contains
        # no test seam. The stand-in models flags/error outcomes and mount lifetime.
        declarations = '''#include <sys/types.h>
int test_open(const char *, int, unsigned);
ssize_t test_write(int, const void *, size_t);
ssize_t test_read(int, void *, size_t);
int test_fsync(int);
int test_close(int);
'''
        for operation in ('open', 'read', 'write', 'fsync', 'close'):
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
                '-pthread', '-I', folder, '-I', str(ROOT / 'include'),
                str(Path(folder) / 'main.cpp'), str(ROOT / 'src/storage.cpp'), str(ROOT/'src/motion_estimator.cpp'),
                str(Path(folder) / 'storage_sd.cpp'), str(ROOT / 'src/session_identity.cpp'),
                str(ROOT / 'src/session_storage_owner.cpp'), str(ROOT / 'src/health_supervisor.cpp'),
                str(ROOT / 'src/session_clock.cpp'), '-o', str(executable),
            ], capture_output=True)
            self.assertEqual(0, build.returncode, build.stderr.decode())
            for scenario in ('lifecycle', 'collision', 'failed-mount', 'absent', 'corrupt',
                             'commit-error', 'open-error', 'unbound', 'close-barrier', 'task-failure'):
                with self.subTest(scenario=scenario):
                    result = subprocess.run([str(executable), scenario], capture_output=True,
                                            timeout=10)
                    self.assertEqual(0, result.returncode, result.stderr.decode())

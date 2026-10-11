"""Execute the production mixed-runtime allocation factory with synthetic SDK boundaries.

No BLE, camera or heap-capacity hardware qualification is claimed. The factory
source and its public runtime header execute unchanged; small class doubles count
construction/attachment without peripheral or radio work.
"""
from pathlib import Path
import signal
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

STUBS = {
    'Arduino.h': '#pragma once\n#include <cstdint>\ninline uint32_t millis() { return 0; }\n',
    'esp_heap_caps.h': r'''
#pragma once
#include <cstddef>
constexpr unsigned MALLOC_CAP_INTERNAL = 0x800, MALLOC_CAP_8BIT = 0x4;
void *heap_caps_malloc(size_t, unsigned);
''',
    'profiles/mixed_camera.h': r'''
#pragma once
#include <cstdint>
namespace ridesync {
extern unsigned adapter_constructs, manager_constructs, group_constructs, attaches;
struct FakeHost;
struct Esp32X5Peripheral;
struct WakeRadio {};
struct Clock { virtual ~Clock() {} virtual uint32_t now() const = 0; };
struct CameraManager;
struct RecordingManager;
struct MixedCameraAdapter {
  MixedCameraAdapter(FakeHost &, Esp32X5Peripheral &, Clock &, WakeRadio *) {
    ++adapter_constructs;
  }
  static int managerPolicy() { return 0; }
  void attach(CameraManager &, RecordingManager &) { ++attaches; }
};
struct CameraManager {
  CameraManager(Clock &, MixedCameraAdapter &, int) { ++manager_constructs; }
};
struct RecordingManager {
  RecordingManager(CameraManager &, Clock &) { ++group_constructs; }
};
}
''',
    'ble_esp32.h': r'''
#pragma once
#include "profiles/mixed_camera.h"
namespace ridesync {
extern unsigned host_gets;
struct FakeHost {};
struct Esp32BleHost {
  static FakeHost &instance() { ++host_gets; static FakeHost host; return host; }
};
}
''',
    'x5_peripheral_esp32.h': r'''
#pragma once
namespace ridesync {
extern unsigned peripheral_constructs;
struct Esp32X5Peripheral { Esp32X5Peripheral() { ++peripheral_constructs; } };
}
''',
    'x5_wake_esp32.h': r'''
#pragma once
#include "profiles/mixed_camera.h"
namespace ridesync {
extern unsigned wake_gets;
inline WakeRadio &x5WakeRadio() { ++wake_gets; static WakeRadio radio; return radio; }
}
''',
}

FIXTURE = r'''
#include "profiles/mixed_camera_esp32.h"
#include <esp_heap_caps.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
namespace ridesync {
unsigned adapter_constructs = 0, manager_constructs = 0, group_constructs = 0, attaches = 0;
unsigned host_gets = 0, wake_gets = 0, peripheral_constructs = 0;
}
static bool allocation_fails = true;
static unsigned allocations = 0, allocation_caps = 0;
static size_t allocation_size = 0;
void *heap_caps_malloc(size_t size, unsigned caps) {
  ++allocations; allocation_caps = caps; allocation_size = size;
  return allocation_fails ? nullptr : std::malloc(size);
}
int main(int argc, char **argv) {
  using namespace ridesync;
  assert(argc == 2);
  if (!std::strcmp(argv[1], "legacy_failure")) {
    (void)mixedCameraRuntime();
    return 99; // Returning a reference after failed allocation is forbidden.
  }
  if (!std::strcmp(argv[1], "failure")) {
    assert(!ensureMixedCameraRuntime());
    assert(allocations == 1 && allocation_size > 0);
    assert(allocation_caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    assert(adapter_constructs == 0 && manager_constructs == 0 && group_constructs == 0);
    assert(attaches == 0 && host_gets == 0 && wake_gets == 0 && peripheral_constructs == 0);
    allocation_fails = false;
    assert(!ensureMixedCameraRuntime()); // Failure is terminal for this boot.
    assert(allocations == 1 && attaches == 0 && host_gets == 0 && wake_gets == 0);
    assert(peripheral_constructs == 0);
    assert(adapter_constructs == 0 && manager_constructs == 0 && group_constructs == 0);
    return 0;
  }
  allocation_fails = false;
  assert(ensureMixedCameraRuntime());
  assert(allocations == 1);
  assert(allocation_caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  assert(adapter_constructs == 1 && manager_constructs == 1 && group_constructs == 1);
  assert(attaches == 1 && host_gets == 1 && wake_gets == 1 && peripheral_constructs == 1);
  const auto first = mixedCameraRuntime();
  const auto second = mixedCameraRuntime();
  const auto exported = ridesync_mixed_camera_runtime();
  assert(&first.adapter == &second.adapter && &first.adapter == &exported.adapter);
  assert(&first.manager == &second.manager && &first.manager == &exported.manager);
  assert(&first.group == &second.group && &first.group == &exported.group);
  assert(ensureMixedCameraRuntime());
  assert(allocations == 1 && attaches == 1);
  assert(host_gets == 1 && wake_gets == 1 && peripheral_constructs == 1);
  assert(adapter_constructs == 1 && manager_constructs == 1 && group_constructs == 1);
  return 0;
}
'''


class MixedCameraFactoryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.path = Path(cls.directory.name)
        for name, source in STUBS.items():
            target = cls.path / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(source)
        (cls.path / 'fixture.cpp').write_text(FIXTURE)
        cls.binary = cls.path / 'factory'
        subprocess.run([
            'c++', '-std=c++11', '-DARDUINO_ARCH_ESP32', '-I'+str(cls.path),
            '-I'+str(ROOT/'include'), str(cls.path/'fixture.cpp'),
            str(ROOT/'src/profiles/mixed_camera_esp32.cpp'), '-o', str(cls.binary),
        ], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_failed_allocation_is_inert_and_terminal_for_the_boot(self):
        subprocess.run([str(self.binary), 'failure'], check=True)

    def test_success_allocates_one_stable_owner_and_attaches_once(self):
        subprocess.run([str(self.binary), 'success'], check=True)

    def test_legacy_reference_getter_aborts_on_failed_allocation(self):
        result = subprocess.run([str(self.binary), 'legacy_failure'], capture_output=True)
        self.assertEqual(-signal.SIGABRT, result.returncode)


if __name__ == '__main__':
    unittest.main()

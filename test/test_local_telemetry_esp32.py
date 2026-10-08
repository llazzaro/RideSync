"""Run the real ESP32 composition/worker/route code with hardware boundaries stubbed.

UART, SPI filesystem, I2C/Bosch and RTOS callbacks are synthetic, not physical
acceptance. Actual production ownership, worker creation, binding, admission,
clock, modem, IMU manager and stop code execute unchanged.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'test/fixtures/local_telemetry'


class LocalTelemetryEsp32Test(unittest.TestCase):
    def test_actual_composition_admission_route_and_final_close(self):
        files = {
            'SPI.h': '#pragma once\nstruct SPIClass {};\n',
            'vfs_api.h': '#pragma once\n#include <memory>\nstruct VFSImpl {}; namespace fs { using FSImplPtr=std::shared_ptr<VFSImpl>; }\n',
            'SD.h': r'''
#pragma once
#include "SPI.h"
#include "vfs_api.h"
extern unsigned mounts,unmounts;
namespace fs {
struct SDFS {
  explicit SDFS(FSImplPtr) {}
  bool begin(uint8_t,SPIClass &,uint32_t,const char *,uint8_t,bool format) { ++mounts;return !format; }
  void end() { ++unmounts; }
};
}
''',
            'freertos/FreeRTOS.h': '#pragma once\n#define pdPASS 1\n#define pdMS_TO_TICKS(n) (n)\n',
            'freertos/task.h': r'''
#pragma once
#include "FreeRTOS.h"
#include <thread>
#include <chrono>
using TaskHandle_t=void *;
int spawn(void(*)(void *),const char *,void *);
inline int xTaskCreate(void(*f)(void *),const char *n,unsigned,void *a,unsigned,TaskHandle_t *h) { *h=a;return spawn(f,n,a); }
inline int xTaskCreatePinnedToCore(void(*f)(void *),const char *n,unsigned,void *a,unsigned,void *,int) { return spawn(f,n,a); }
inline void vTaskDelay(unsigned) { std::this_thread::sleep_for(std::chrono::microseconds(100)); }
inline void vTaskDelete(void *) {}
''',
        }
        source = (ROOT / 'src/storage_sd.cpp').read_text()
        declarations = '''#include <sys/types.h>
int test_open(const char *,int,unsigned);
ssize_t test_read(int,void *,size_t);
ssize_t test_write(int,const void *,size_t);
int test_fsync(int);
int test_close(int);
'''
        for op in ('open', 'read', 'write', 'fsync', 'close'):
            source = re.sub(rf'(?<![\w:])::{op}\(', f'::test_{op}(', source)
        files['storage_sd.cpp'] = declarations + source
        vendor = ROOT / '.pio/libdeps/lilygo_t_a7670e_r2/SparkFun BMI270 Arduino Library/src'
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            for name, data in files.items():
                p = folder / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(data)
            portable = [p for p in (ROOT / 'src').rglob('*.cpp')
                        if p.name not in ('main.cpp', 'storage_sd.cpp', 'ble_esp32.cpp',
                                          'nvs_boot_guard.cpp', 'config_storage_nvs.cpp',
                                          'status_led.cpp', 'button_manager.cpp',
                                          'config_storage.cpp', 'config_bootstrap.cpp')]
            exe = folder / 'runtime'
            cmd = ['c++', '-std=c++11', '-DARDUINO', '-DARDUINO_ARCH_ESP32',
                   '-Wall', '-Wextra', '-Werror', '-Wno-return-type-c-linkage', '-pthread', '-I', str(folder),
                   '-I', str(FIXTURE), '-I', str(ROOT / 'include'), '-I', str(vendor),
                   str(FIXTURE / 'runtime.cpp'), str(FIXTURE / 'bosch.cpp'),
                   str(folder / 'storage_sd.cpp'), *map(str, portable), '-o', str(exe)]
            build = subprocess.run(cmd, capture_output=True)
            self.assertEqual(0, build.returncode, build.stderr.decode())
            for mode in ('default', 'uart-unqualified', 'imu-unqualified', 'power-unqualified',
                         'sd-task', 'imu-task', 'safe-mode', 'local', 'camera', 'close-barrier'):
                with self.subTest(mode=mode):
                    run = subprocess.run([str(exe), mode], capture_output=True, timeout=10)
                    self.assertEqual(0, run.returncode, run.stderr.decode())

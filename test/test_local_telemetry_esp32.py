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
            'driver/gpio.h': r'''
#pragma once
#include <cstdint>
using gpio_num_t=int;
constexpr int GPIO_MODE_OUTPUT=1, GPIO_PULLUP_DISABLE=0;
constexpr int GPIO_PULLDOWN_DISABLE=0, GPIO_INTR_DISABLE=0;
struct gpio_config_t { uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type; };
extern unsigned led_configs,led_writes;
extern int gpio_failure;
inline int gpio_config(const gpio_config_t *) { ++led_configs;return gpio_failure; }
inline int gpio_set_level(gpio_num_t,uint32_t) { ++led_writes;return gpio_failure; }
''',
            'esp_attr.h': '#pragma once\n#define RTC_NOINIT_ATTR\n',
            'esp_system.h': '''#pragma once
enum esp_reset_reason_t { ESP_RST_UNKNOWN,ESP_RST_POWERON,ESP_RST_BROWNOUT,ESP_RST_PANIC,ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT,ESP_RST_SW,ESP_RST_EXT,ESP_RST_DEEPSLEEP };
static esp_reset_reason_t test_reset_reason=ESP_RST_POWERON;
inline esp_reset_reason_t esp_reset_reason() { return test_reset_reason; }
''',
            'esp_task_wdt.h': '''#pragma once
constexpr int ESP_OK=0,ESP_ERR_NOT_FOUND=1,ESP_ERR_INVALID_STATE=2;
extern int sdk_add_error;
inline int esp_task_wdt_status(void *) { return ESP_ERR_NOT_FOUND; }
inline int esp_task_wdt_add(void *) { return sdk_add_error; }
inline int esp_task_wdt_reset() { return 0; }
inline int esp_task_wdt_delete(void *) { return 0; }
''',
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
#include <Arduino.h>
#include <thread>
#include <chrono>
using TaskHandle_t=void *;
using TickType_t=uint32_t;
inline TickType_t xTaskGetTickCount() { return millis(); }
inline void vTaskDelayUntil(TickType_t *,TickType_t) { throw std::runtime_error("yield"); }
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
        # Deterministic scheduler pause immediately before each real wrapper's final
        # flag. Production access/order logic stays real; pauses are test-only.
        source = source.replace('  self.finished_.store(',
                                '  test_final_sd_access();\n  self.finished_.store(')
        files['storage_sd.cpp'] = 'void test_final_sd_access();\n' + declarations + source
        imu = (ROOT / 'src/bmi270_imu.cpp').read_text().replace(
            '  self.finished_.store(', '  test_final_imu_access();\n  self.finished_.store(')
        files['bmi270_imu.cpp'] = 'void test_final_imu_access();\n' + imu
        vendor = ROOT / '.pio/libdeps/lilygo_t_a7670e_r2/SparkFun BMI270 Arduino Library/src'
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            for name, data in files.items():
                p = folder / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(data)
            portable = [p for p in (ROOT / 'src').rglob('*.cpp')
                        if p.name not in ('main.cpp', 'storage_sd.cpp', 'bmi270_imu.cpp', 'ble_esp32.cpp',
                                          'nvs_boot_guard.cpp', 'config_storage_nvs.cpp')]
            exe = folder / 'runtime'
            cmd = ['c++', '-std=c++11', '-DARDUINO', '-DARDUINO_ARCH_ESP32',
                   '-Wall', '-Wextra', '-Werror', '-Wno-return-type-c-linkage', '-pthread', '-I', str(folder),
                   '-I', str(FIXTURE), '-I', str(ROOT), '-I', str(ROOT / 'include'), '-I', str(vendor),
                   str(FIXTURE / 'runtime.cpp'), str(FIXTURE / 'bosch.cpp'),
                   str(folder / 'storage_sd.cpp'), str(folder / 'bmi270_imu.cpp'), *map(str, portable), '-o', str(exe)]
            build = subprocess.run(cmd, capture_output=True)
            self.assertEqual(0, build.returncode, build.stderr.decode())
            for mode in ('main-terminal-at', 'main-ble-refusal', 'main-runtime-safe-mode', 'main-button-unqualified', 'main-delayed-config', 'main-late-config', 'main-config-timeout', 'main-config-task-refusal',
                         'main-supervisor-refusal', 'main-health-task-refusal', 'main-qualification',
                         'main-sd-refusal', 'main-imu-refusal', 'main-blocked-allocation',
                         'main-blocked-close', 'main-current-gate', 'main-supervisor-timeout',
                         'main-supervisor-exact', 'main-supervisor-beyond',
                         'main-supervisor-exact-rollover', 'main-supervisor-beyond-rollover',
                         'main-supervisor-timely', 'main-supervisor-timely-rollover',
                         'main-early-stop', 'main-imu-final-barrier', 'main-sd-final-barrier', 'main-safe-mode', 'default', 'uart-unqualified', 'imu-unqualified', 'power-unqualified',
                         'sd-task', 'imu-task', 'safe-mode', 'local', 'camera', 'close-barrier',
                         'control-default', 'control', 'control-gpio-fault',
                         'control-gps-refusal', 'control-uart-refusal', 'control-task-refusal',
                         'control-imu-refusal', 'control-power-refusal', 'control-route-refusal'):
                with self.subTest(mode=mode):
                    run = subprocess.run([str(exe), mode], capture_output=True, timeout=10)
                    self.assertEqual(0, run.returncode, run.stderr.decode())

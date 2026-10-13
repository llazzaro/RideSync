"""Synthetic GPIO boundary tests; no electrical/cut-off or timing qualification."""
import subprocess
import tempfile
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PowerBackendTests(unittest.TestCase):
    def test_real_sdk_port_external_pulls_startup_order_and_raw_errors(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / 'driver').mkdir()
            (temp / 'driver/gpio.h').write_text(r'''
#pragma once
#include <cstdint>
typedef int gpio_num_t;
enum gpio_mode_t {GPIO_MODE_INPUT,GPIO_MODE_OUTPUT};
enum {GPIO_PULLUP_DISABLE,GPIO_PULLDOWN_DISABLE,GPIO_INTR_DISABLE,ESP_ERR_INVALID_RESPONSE=0x108};
struct gpio_config_t {uint64_t pin_bit_mask;gpio_mode_t mode;int pull_up_en,pull_down_en,intr_type;};
int gpio_config(const gpio_config_t *);
int gpio_get_level(gpio_num_t);
int gpio_set_level(gpio_num_t,uint32_t);
''')
            binary = temp / 'sdk'
            subprocess.run(['clang++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                            '-DARDUINO_ARCH_ESP32', '-fsanitize=address,undefined',
                            '-fno-sanitize-recover=all', '-I', str(temp),
                            '-I', str(ROOT / 'include'),
                            str(ROOT / 'test/fixtures/power_shutdown/sdk.cpp'),
                            str(ROOT / 'src/power_shutdown_esp32.cpp'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_backend_configuration_observation_and_one_shot_off(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'backend'
            subprocess.run(['clang++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-I', str(ROOT / 'include'),
                            str(ROOT / 'test/fixtures/power_shutdown/backend.cpp'),
                            str(ROOT / 'src/power_shutdown_esp32.cpp'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()

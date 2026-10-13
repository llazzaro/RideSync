"""Actual main must keep configuration receipt independent of power readiness."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_health_startup import STUB, HARNESS

ROOT = Path(__file__).resolve().parents[1]
PREFIX = HARNESS.split('#include "src/main.cpp"')[0]
GATE = r'''
#include "application_startup.h"
struct Gate : ridesync::ApplicationWorkers {
  bool ready = false;
  unsigned prepares = 0, launches = 0, services = 0, polls = 0;
  bool startupReady() override { ++polls; return ready; }
  std::array<ridesync::WorkerPolicy, 4> prepare(const ridesync::SettingsSnapshot &s,
      bool, bool, ridesync::HealthSupervisor &) override {
    assert(s.completed && s.effective && s.generation == 1);
    ++prepares;
    return {};
  }
  void launch() override { ++launches; }
  void current(const ridesync::SettingsSnapshot &, bool, bool) override {}
  void service(uint8_t, uint8_t, bool) override { ++services; }
};
static Gate gate;
namespace ridesync { ApplicationWorkers *commissionedApplication() { return &gate; } }
#include "src/main.cpp"
static void passTask(void (*fn)(void *)) {
  try { fn(nullptr); } catch (const std::runtime_error &) {}
}
int main(int argc, char **) {
  assert(argc == 1);
  nvs_status.init_observed = true;
  ridesync::SourceConfig source;
  assert(ridesync::encodeConfig(source, 1, startup_settings).status == ridesync::PersistStatus::Encoded);
  setup();
  assert(config_fn && !task_fn);
  passTask(config_fn);
  loop();
  assert(application_settings.snapshot().completed && !task_fn && gate.prepares == 0);
  // A legitimate 2s ignition check must not convert an already received config
  // into a timeout, nor start the required-worker grace clock while waiting.
  time_now = 2500;
  loop();
  assert(application_settings.snapshot().effective && !task_fn && gate.launches == 0);
  assert(config_startup == ridesync::StartupState::ConfigReady);
  gate.ready = true;
  loop();
  assert(task_fn && gate.prepares == 1 && gate.launches == 0);
  passTask(task_fn);
  loop();
  assert(gate.launches == 1 && gate.services == 1);
  loop();
  assert(gate.prepares == 1 && gate.launches == 1);
}
'''


class PowerStartup(unittest.TestCase):
    def test_power_guard_precedes_supervisor_and_preserves_received_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p / "standin.h").write_text(STUB)
            for name in ("Arduino.h", "esp_task_wdt.h", "esp_system.h", "esp_attr.h",
                         "freertos/FreeRTOS.h", "freertos/task.h", "esp_timer.h", "nvs.h"):
                f = p / name
                f.parent.mkdir(parents=True, exist_ok=True)
                f.write_text('#include "standin.h"\n')
            (p / "ble_esp32.h").write_text('''#pragma once
#include "ble_pairing_reset.h"
namespace ridesync { struct Esp32BleHost {
static Esp32BleHost &instance(){static Esp32BleHost h;return h;}
bool admittedProof(BleStoreProof &)const{return false;}
}; }
''')
            (p / "driver").mkdir()
            (p / "driver/gpio.h").write_text('''#pragma once
#include "standin.h"
using gpio_num_t=int;
constexpr int GPIO_MODE_OUTPUT=1, GPIO_PULLUP_DISABLE=0, GPIO_PULLDOWN_DISABLE=0, GPIO_INTR_DISABLE=0;
struct gpio_config_t { uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type; };
inline int gpio_config(const gpio_config_t *) { throw std::logic_error("GPIO"); }
inline int gpio_set_level(gpio_num_t,uint32_t) { throw std::logic_error("GPIO"); }
''')
            (p / "run.cpp").write_text(PREFIX + '#include <cassert>\n' + GATE)
            sources = ('application_startup', 'health_supervisor', 'config_storage',
                       'config_bootstrap', 'config', 'button_manager', 'storage',
                       'motion_estimator', 'session_clock', 'status_led')
            run = subprocess.run(['c++', '-std=c++11', '-DARDUINO_ARCH_ESP32',
                                  '-I', str(p), '-I', str(ROOT), '-I', str(ROOT / 'include'),
                                  str(p / 'run.cpp'),
                                  *(str(ROOT / 'src' / (s + '.cpp')) for s in sources),
                                  '-o', str(p / 'run')], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            subprocess.run([str(p / 'run')], check=True)

"""Run actual Arduino startup/task code against bounded SDK stand-ins."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUB = r'''
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <stdexcept>
using TaskHandle_t = void *;
using TickType_t = uint32_t;
using BaseType_t = int;
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_ERR_NOT_FOUND=1, ESP_ERR_INVALID_STATE=2;
constexpr int pdPASS=1;
#define pdMS_TO_TICKS(ms) (ms)
#define RTC_NOINIT_ATTR
#define ARDUINO 1
enum esp_reset_reason_t { ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW,
 ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO };
static void (*task_fn)(void *) = nullptr;
static int create_result=pdPASS, status_result=ESP_ERR_NOT_FOUND, add_result=0, feed_result=0;
static int adds=0, feeds=0, removes=0, deletes=0, loops=0;
static uint32_t time_now=0;
struct SerialPort {
 std::string output; char input=0;
 void begin(int) {}
 void println(const char *s) { output += s; output += '\n'; }
 void printf(const char *fmt, ...) {
  char b[256]; va_list args; va_start(args,fmt); vsnprintf(b,sizeof b,fmt,args); va_end(args); output += b;
 }
 int available() { return input != 0; }
 int read() { char c=input; input=0; return c; }
};
static SerialPort Serial;
static uint32_t millis() { return time_now; }
static void delay(int) { ++loops; }
static TickType_t xTaskGetTickCount() { return time_now; }
static void vTaskDelayUntil(TickType_t *last, TickType_t interval) {
 if (interval != 100) throw std::logic_error("cadence");
 *last += interval; time_now += interval; throw std::runtime_error("yield");
}
static void vTaskDelete(TaskHandle_t h) { ++deletes; if(h) throw std::logic_error("foreign delete"); throw std::runtime_error("delete"); }
static BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *, uint32_t stack,
 void *, unsigned priority, TaskHandle_t *, int core) {
 if (stack != 4096 || priority != 2 || core != 1) throw std::logic_error("task budget");
 task_fn=fn; return create_result;
}
static esp_err_t esp_task_wdt_status(TaskHandle_t h) { if(h) throw std::logic_error("foreign status"); return status_result; }
static esp_err_t esp_task_wdt_add(TaskHandle_t h) { if(h) throw std::logic_error("foreign add"); ++adds; return add_result; }
static esp_err_t esp_task_wdt_reset() { ++feeds; return feed_result; }
static esp_err_t esp_task_wdt_delete(TaskHandle_t h) { if(h) throw std::logic_error("foreign unregister"); ++removes; return 0; }
static esp_reset_reason_t reset_reason=ESP_RST_POWERON;
static esp_reset_reason_t esp_reset_reason() { return reset_reason; }
'''
HARNESS = r'''
#include "standin.h"
#include "nvs_boot_guard.h"
static ridesync::NvsBootStatus nvs_status;
namespace ridesync { NvsBootStatus nvsBootStatus() { return nvs_status; } }
#include "src/main.cpp"
#include <cassert>
int main(int argc, char **argv) {
 int scenario = argc > 1 ? std::stoi(argv[1]) : 0;
 if(scenario==1) create_result=0;
 if(scenario==2) status_result=ESP_ERR_INVALID_STATE;
 if(scenario==3) add_result=77;
 if(scenario==4) status_result=ESP_OK;
 if(scenario==5) feed_result=78;
 if(scenario>=6 && scenario<=8) {
  ridesync::BootRecovery boot;
  boot.begin({}, ridesync::ResetClass::Cold,0);
  for(int i=0;i<3;++i) boot.begin(boot.record(),ridesync::ResetClass::Watchdog,0);
  std::memcpy(retained_boot,&boot.record(),sizeof(ridesync::BootRecord));
  reset_reason=scenario==7 ? ESP_RST_POWERON : ESP_RST_TASK_WDT;
 }
 if(scenario==10) { nvs_status.init_observed=true; nvs_status.last_init_result=0x110d; nvs_status.first_init_failure=0x110d; }
 if(scenario==11) { nvs_status.init_observed=true; nvs_status.last_init_result=0x1110; nvs_status.first_init_failure=0x1110; }
 if(scenario==12) { nvs_status.init_observed=true; nvs_status.format_refused=true; nvs_status.refusal_error=0x106; }
 if(scenario==13) nvs_status.init_observed=true;
 setup();
 if(scenario>=9) {
  const char *admission=scenario==13 ? "config_ble_admission=1" : "config_ble_admission=0";
  assert(Serial.output.find(admission)!=std::string::npos);
  assert(!safe_mode.load()); // Config fault does not manufacture a scheduler/reboot failure.
 }
 assert(task_fn != nullptr); // actual startup must create independent supervision
 if(scenario==6) assert(safe_mode.load());
 if(scenario==7) assert(!safe_mode.load());
 if(scenario==8) { assert(safe_mode.load()); Serial.input='C'; }
 loop(); assert(loops==1);
 if(scenario==1) {
  assert(adds==0 && feeds==0);
  assert(Serial.output.find("task creation failed") != std::string::npos);
  return 0;
 }
 try { task_fn(nullptr); } catch(const std::runtime_error &) {}
 loop();
 if(scenario==0) { assert(adds==1 && feeds==1 && removes==0); }
 if(scenario==2 || scenario==4) { assert(adds==0 && feeds==0 && removes==0); }
 if(scenario==4) assert(deletes==0); // never delete a still-subscribed task
 if(scenario==3) { assert(adds==1 && feeds==0 && removes==0); }
 if(scenario==5) { assert(adds==1 && feeds==1 && removes==0); }
 if(scenario==8) {
  assert(!safe_mode.load()); assert(ridesync::BootRecovery::valid(recovery.record()));
  assert(recovery.record().failed_boots==0);
 }
 if(scenario>=2) assert(Serial.output.find("watchdog status=") != std::string::npos);
 assert(Serial.output.find("sdk_reset=") != std::string::npos);
 assert(Serial.output.find("app_cause=") != std::string::npos);
}
'''

class HealthStartup(unittest.TestCase):
    def test_actual_safe_startup_and_sdk_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / "standin.h").write_text(STUB)
            for name in ("Arduino.h", "esp_task_wdt.h", "esp_system.h", "esp_attr.h",
                         "freertos/FreeRTOS.h", "freertos/task.h"):
                p = temp / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text('#include "standin.h"\n')
            (temp / "run.cpp").write_text(HARNESS)
            subprocess.run(["c++", "-std=c++11", "-I", str(temp), "-I", str(ROOT),
                            "-I", str(ROOT / "include"), str(temp / "run.cpp"),
                            str(ROOT / "src/health_supervisor.cpp"), str(ROOT / "src/storage.cpp"),
                            str(ROOT / "src/session_clock.cpp"), "-o", str(temp / "run")], check=True)
            for scenario in range(14):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(temp / "run"), str(scenario)], check=True)

if __name__ == "__main__":
    unittest.main()

"""Exercise real setup-mode main and storage handoff with no peripheral startup."""

from pathlib import Path
import subprocess
import tempfile
import unittest
from test_health_startup import STUB

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#include "standin.h"
#include "nvs_boot_guard.h"
static ridesync::NvsBootStatus nvs_status;
namespace ridesync { NvsBootStatus nvsBootStatus() { return nvs_status; } }
#include "config_storage.h"
static ridesync::ConfigRecord records[2];
static unsigned writes=0;
namespace ridesync {
bool NvsConfigStore::allowed() const { return nvsBootStatus().persistenceAllowed(); }
StoreResult NvsConfigStore::read(unsigned slot, ConfigRecord &out) {
 out=records[slot]; return {out.size?StoreStatus::Ok:StoreStatus::Missing};
}
StoreResult NvsConfigStore::write(unsigned slot, const ConfigRecord &in) {
 records[slot]=in; ++writes; return {};
}
}
#include "src/pairing_proof_esp32.cpp"
#include "src/main.cpp"
#include <cassert>
static void configPass(){try{config_fn(nullptr);}catch(const std::runtime_error &) {}}
static void send(const std::string &op,const std::string &payload="") {
 const auto &snapshot=application_settings.snapshot();
 Serial.input="@RS1 "+std::to_string(setup_service.active()?setup_service.session():0)+" "+
 std::to_string(++wire_request)+" "+std::to_string(snapshot.epoch)+" "+
 std::to_string(snapshot.generation)+" "+op+(payload.empty()?"":" "+payload)+"\n";
 for(unsigned i=0;i<80 && !Serial.input.empty();++i) loop();
 loop();
}
int main(int argc,char **argv) {
 nvs_status.init_observed=true;
 setup();
 // A previous acknowledged lease cannot admit another NVS lifecycle operation.
 assert(requestConfigPause());
 const auto first_pause=config_pause_requested.load();
 config_pause_ack.store(first_pause);
 assert(configPauseAcknowledged());
 config_pause_requested.store(0);
 assert(requestConfigPause());
 assert(config_pause_requested.load()!=first_pause);
 assert(!configPauseAcknowledged());
 config_pause_requested.store(0);
 assert(workers==nullptr);
 assert(config_fn);
 configPass(); loop();
 try {task_fn(nullptr);}catch(const std::runtime_error &){}
 send("HELLO");
 assert(Serial.output.find("challenge=")!=std::string::npos);
 send("ENTER",std::to_string(setup_challenge)+" PARKED");
 assert(setup_service.active()); assert(setup_service.isolated());
 send("DRAFT_BEGIN");
 send("DRAFT_SET","1,4;2,4,1,0,0,46726f6e74,30313a32333a34353a36373a38393a4142,-");
 send("VALIDATE");
 assert(Serial.output.find("VALIDATE Ok None")!=std::string::npos);
 send("DISPATCH");
 assert(writes==0);
 configPass(); loop();
 time_now=1200; configPass();
 // STATUS occupies the response slot as the admitted save result arrives.
 Serial.input="@RS1 "+std::to_string(setup_service.session())+" "+
 std::to_string(++wire_request)+" 1 1 STATUS\n";
 loop();
 // Next pass must send the deferred DISPATCH reply before consuming INSPECT.
 send("INSPECT");
 assert(Serial.output.find("INSPECT Ok None")!=std::string::npos);
 assert(writes==1);
 assert(application_settings.snapshot().generation==2);
 assert(Serial.output.find("DISPATCH Ok None")!=std::string::npos);
 send("STATUS"); assert(Serial.output.find("Durable")!=std::string::npos);
 if(argc>1 && std::stoi(argv[1])==1) {
   send("RESTART"); send("EXIT");
   configPass();
   try{loop();}catch(const std::logic_error &){assert(restarts==1);return 0;}
   assert(false);
 }
 send("EXIT"); assert(!setup_service.active());
 assert(workers==nullptr); assert(restarts==0);
 send("ENTER",std::to_string(setup_challenge)+" PARKED");
 assert(!setup_service.active()); assert(writes==1);
}
"""


class SetupStartup(unittest.TestCase):
    def test_real_boot_only_isolation_save_and_quiescent_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            stub = STUB.replace(
                "std::string output; char input=0;", "std::string output, input;"
            )
            stub = stub.replace("return input != 0;", "return !input.empty();")
            stub = stub.replace(
                "char c=input; input=0; return c;",
                "char c=input.front(); input.erase(0,1); return c;",
            )
            stub += '\nstatic unsigned restarts=0, wire_request=0;\nstatic uint32_t esp_random(){static uint32_t r=40;return ++r;}\nstatic void esp_restart(){++restarts;throw std::logic_error("restart");}\n'
            (temp / "standin.h").write_text(stub)
            for name in (
                "Arduino.h",
                "esp_task_wdt.h",
                "esp_system.h",
                "esp_attr.h",
                "freertos/FreeRTOS.h",
                "freertos/task.h",
                "esp_timer.h",
                "nvs.h",
            ):
                p = temp / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text('#include "standin.h"\n')
            (temp / "ble_esp32.h").write_text("""#pragma once
#include "ble_pairing_reset.h"
#include <stdexcept>
namespace ridesync { struct Esp32BleHost {
 static Esp32BleHost &instance(){static Esp32BleHost h;return h;}
 bool admittedProof(BleStoreProof &)const{throw std::logic_error("unexpected BLE reset");}
}; }
""")
            (temp / "driver").mkdir()
            (temp / "driver/gpio.h").write_text("""#pragma once
#include "standin.h"
using gpio_num_t=int;
constexpr int GPIO_MODE_OUTPUT=1, GPIO_PULLUP_DISABLE=0, GPIO_PULLDOWN_DISABLE=0, GPIO_INTR_DISABLE=0;
struct gpio_config_t { uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type; };
inline int gpio_config(const gpio_config_t *) { throw std::logic_error("unexpected LED configure"); }
inline int gpio_set_level(gpio_num_t, uint32_t) { throw std::logic_error("unexpected LED write"); }
""")
            (temp / "run.cpp").write_text(HARNESS)
            sources = [
                "application_startup",
                "health_supervisor",
                "config_storage",
                "config_bootstrap",
                "config",
                "button_manager",
                "storage",
                "motion_estimator",
                "session_clock",
                "status_led",
                "setup_service",
                "setup_prerequisites",
                "setup_discovery",
                "ble_remote",
                "pairing_reset",
            ]
            subprocess.run(
                [
                    "c++",
                    "-std=c++11",
                    "-DARDUINO_ARCH_ESP32",
                    "-DRIDESYNC_SETUP_APPLICATION",
                    "-I",
                    str(temp),
                    "-I",
                    str(ROOT),
                    "-I",
                    str(ROOT / "include"),
                    str(temp / "run.cpp"),
                    *[str(ROOT / "src" / (name + ".cpp")) for name in sources],
                    "-o",
                    str(temp / "run"),
                ],
                check=True,
            )
            for scenario in (0, 1):
                subprocess.run([str(temp / "run"), str(scenario)], check=True)

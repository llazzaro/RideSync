"""Exercise the pinned ESP32 adapter contract through SDK boundary stand-ins."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LedGpioTest(unittest.TestCase):
    def test_sdk_errors_and_exact_discrete_output_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "driver").mkdir()
            (work / "driver/gpio.h").write_text(r'''
#pragma once
#include <cstdint>
using gpio_num_t=int;
constexpr int GPIO_MODE_OUTPUT=1, GPIO_PULLUP_DISABLE=0;
constexpr int GPIO_PULLDOWN_DISABLE=0, GPIO_INTR_DISABLE=0;
struct gpio_config_t {
 uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type;
};
extern int configs,writes,config_result,write_result,pin_seen,level_seen;
int gpio_config(const gpio_config_t *);
int gpio_set_level(gpio_num_t, uint32_t);
''')
            (work / "run.cpp").write_text(r'''
#include "status_led.h"
#include "driver/gpio.h"
#include <cassert>
using namespace ridesync;
int configs=0,writes=0,config_result=0,write_result=0,pin_seen=-1,level_seen=-1;
int gpio_config(const gpio_config_t *c) {
 ++configs;
 assert(c->pin_bit_mask==((uint64_t(1)<<18)|(uint64_t(1)<<19)|(uint64_t(1)<<23)));
 assert(c->mode==GPIO_MODE_OUTPUT);
 assert(c->pull_up_en==GPIO_PULLUP_DISABLE && c->pull_down_en==GPIO_PULLDOWN_DISABLE);
 assert(c->intr_type==GPIO_INTR_DISABLE);
 return config_result;
}
int gpio_set_level(gpio_num_t pin,uint32_t level) {
 ++writes; pin_seen=pin; level_seen=level; return write_result;
}
int main() {
 Esp32LedGpio sdk;
 GpioLedSink disabled(sdk); assert(disabled.begin({},false)==LedBackendState::Disabled);
 assert(disabled.write({true,true,true})==0); assert(configs==0 && writes==0);
 LedWiring c; c.mode=LedMode::Rgb; c.board_qualified=c.reservations_complete=true;
 c.pins={{18,19,23}};
 c.polarity={{LedPolarity::ActiveHigh,LedPolarity::ActiveLow,LedPolarity::ActiveHigh}};
 GpioLedSink sink(sdk); assert(sink.begin(c,true)==LedBackendState::Ready);
 assert(configs==1 && writes==3 && pin_seen==23 && level_seen==0);
 assert(sink.write({false,false,true})==0); assert(pin_seen==23 && level_seen==1);
 write_result=0x102;
 assert(sink.write({true,true,true})==0x102);
 assert(sink.error()==0x102 && sink.state()==LedBackendState::WriteFailed);
 int calls=writes; assert(sink.write({})==0x102 && calls==writes);
 config_result=0x103; GpioLedSink bad_config(sdk);
 assert(bad_config.begin(c,true)==LedBackendState::ConfigFailed);
 assert(bad_config.error()==0x103); assert(pin_seen==23 && level_seen==0);
}
''')
            subprocess.run([
                "c++", "-std=c++11", "-DARDUINO_ARCH_ESP32", "-I", str(work),
                "-I", str(ROOT / "include"), str(work / "run.cpp"),
                str(ROOT / "src/status_led.cpp"), "-o", str(work / "run")
            ], check=True)
            subprocess.run([str(work / "run")], check=True)


if __name__ == "__main__":
    unittest.main()

"""Compile the actual lazy X5 wake radio with controlled SDK outcomes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUB = r'''
#pragma once
#include "x5_wake_esp32.h"
#include <atomic>
#include <cassert>
#include <cstring>
using TaskHandle_t=void*;
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
static uint32_t now_ms=100;
static uint32_t millis(){return now_ms;}
static void(*pending_worker)(void*)=nullptr;
static void* pending_context=nullptr;
static bool allocation_fails=false;
static unsigned tasks=0;
static int xTaskCreate(void(*fn)(void*),const char*name,unsigned stack,void*ctx,unsigned priority,void**out){
 assert(std::strcmp(name,"x5_wake_start")==0&&stack==4096&&priority==2);++tasks;
 if(allocation_fails)return 0;pending_worker=fn;pending_context=ctx;*out=ctx;return 1;
}
static void vTaskDelay(unsigned ms){now_ms+=ms;if(ms==100)throw 1;}
namespace ridesync {
struct Esp32BleHost {
 unsigned starts=0,seals=0;BleHostState current=BleHostState::Disabled, outcome=BleHostState::Ready;
 static Esp32BleHost &instance(){static Esp32BleHost host;return host;}
 BleHostState start(bool a,bool b){assert(a&&b);++starts;return current=outcome;}
 BleHostState state(){return current;}
 void sealStartup(){++seals;current=BleHostState::Failed;}
};
struct SdkRadio {
 unsigned activations=0,advertisements=0,cancels=0;
 bool activate(bool enabled){assert(enabled);++activations;return true;}
 WakeSubmit begin(const WakeOperation&,const insta360::WakeEncoding&,uint32_t,uint32_t){++advertisements;return WakeSubmit::Accepted;}
 void cancel(const WakeOperation&){++cancels;}
 WakeRadioResult poll(const WakeOperation&,uint32_t){return {};}
};
static SdkRadio &insta360WakeRadio(){static SdkRadio radio;return radio;}
}
'''
HARNESS = r'''
#include "standin.h"
#include "src/x5_wake_esp32.cpp"
int main(int argc,char**argv){
 using namespace ridesync;assert(argc==2);int scenario=std::stoi(argv[1]);
 auto &radio=x5WakeRadio();auto &host=Esp32BleHost::instance();auto &sdk=insta360WakeRadio();
 assert(tasks==0&&host.starts==0&&sdk.advertisements==0);
 if(scenario==1)allocation_fails=true;
 if(scenario==2)host.outcome=BleHostState::Starting;
 if(scenario==3)host.outcome=BleHostState::Failed;
 WakeOperation op;op.generation=op.id=1;insta360::WakeEncoding bytes;
 auto first=radio.begin(op,bytes,1100,now_ms);
 assert(tasks==1&&sdk.advertisements==0);
 if(scenario==1){assert(first==WakeSubmit::Failed&&host.starts==0);}
 else {
  assert(first==WakeSubmit::Accepted);
  radio.cancel(op);
  auto pending=radio.poll(op,now_ms);
  assert(pending.terminal&&!pending.released);
  assert(radio.begin(op,bytes,1100,now_ms)==WakeSubmit::Busy);
  if(scenario==4)now_ms=1100;
  try {pending_worker(pending_context);}catch(int){}
  assert(host.starts==1&&sdk.advertisements==0);
 }
 if(scenario!=1){
  auto done=radio.poll(op,now_ms);assert(done.terminal&&done.released&&!done.submitted);
  if(scenario==4)assert(host.state()==BleHostState::Ready&&host.seals==0);
 }
 auto next=radio.begin(op,bytes,now_ms+1000,now_ms);
 if(scenario==0){assert(next==WakeSubmit::Accepted&&sdk.advertisements==1&&sdk.activations==1);}
 else {assert(next==WakeSubmit::Failed&&sdk.advertisements==0&&sdk.activations==0);}
 assert(tasks==1); // No retry/replacement startup worker.
}
'''


class X5WakeSdkTests(unittest.TestCase):
    def test_lazy_startup_deadline_failure_and_no_late_advertising(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'freertos').mkdir()
            (path / 'standin.h').write_text(STUB)
            (path / 'Arduino.h').write_text('#include "standin.h"\n')
            (path / 'freertos/task.h').write_text('#include "standin.h"\n')
            (path / 'insta360_wake_esp32.h').write_text('#include "standin.h"\n')
            (path / 'main.cpp').write_text(HARNESS)
            exe = path / 'worker'
            subprocess.run(['c++', '-std=c++11', '-DARDUINO_ARCH_ESP32',
                            '-I', str(path), '-I', str(ROOT), '-I', str(ROOT / 'include'),
                            str(path / 'main.cpp'), str(ROOT / 'src/wake_manager.cpp'),
                            str(ROOT / 'src/insta360_wake_encoder.cpp'), '-o', str(exe)], check=True)
            for scenario in range(5):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(exe), str(scenario)], check=True)


if __name__ == '__main__':
    unittest.main()

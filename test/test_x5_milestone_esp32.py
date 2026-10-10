"""Actual serial main/runtime selection with synthetic commissioning; no radio claim."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUB = r'''
#pragma once
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
static unsigned fake_time=100;
static unsigned millis(){return fake_time;}
static void delay(unsigned n){fake_time+=n;}
static void vTaskDelay(unsigned){throw 1;}
using TaskHandle_t=void*;
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define RTC_NOINIT_ATTR
static void(*config_task)(void*)=nullptr;
static bool task_failure=false;
static int xTaskCreate(void(*fn)(void*),const char *name,unsigned stack,void*,unsigned priority,void**out){
 assert(std::strcmp(name,"x5_config")==0&&stack==12288&&priority==1);
 if(task_failure)return 0;config_task=fn;*out=reinterpret_cast<void*>(1);return 1;
}
static void run_config(){try{config_task(nullptr);}catch(int) {}}
enum {ESP_RST_POWERON,ESP_RST_BROWNOUT,ESP_RST_SW,ESP_RST_EXT,ESP_RST_DEEPSLEEP,ESP_RST_PANIC,ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT};
static int esp_reset_reason(){return ESP_RST_POWERON;}
struct SerialStub {
 std::deque<char> bytes;std::string output;unsigned reads=0;
 void begin(unsigned baud){assert(baud==115200);}
 bool available(){return !bytes.empty();}
 int read(){++reads;char c=bytes.front();bytes.pop_front();return c;}
 void println(const char*s){output+=s;output+='\n';}
 void printf(const char *f,...){char b[512];va_list ap;va_start(ap,f);std::vsnprintf(b,sizeof b,f,ap);va_end(ap);output+=b;}
 void input(const char*s){while(*s)bytes.push_back(*s++);}
};
static SerialStub Serial;
'''
PROVIDER = r'''
#pragma once
#include "x5_wake.h"
static bool ridesyncPrivateX5Qualification(ridesync::X5Qualification &q,ridesync::SourceConfig &s){
 using namespace ridesync;
 q.enabled=true;q.identity.verified=true;q.identity.type=IdentityType::Public;q.identity.address={{1,2,3,4,5,6}};
 q.store.qualification_record=42;q.display.profile=insta360::Ce80DisplayProfile::X5CapturedDisplayV1;
 std::memcpy(q.firmware.data(),"1.11.10",7);q.firmware_size=7;
 s.count=1;auto&c=s.cameras[0];c.name="Synthetic X5";c.family=CameraFamily::Insta360;c.model=CameraModel::X5;
 c.identifier="06:05:04:03:02:01";c.address_type=AddressType::Public;return true;
}
static bool ridesyncPrivateX5WakeConfig(ridesync::WakePeerConfig &c, ridesync::WakePolicy &){
 c.enabled=c.source_qualified=true;c.profile=ridesync::insta360::WakeProfile::M5WakeV1;
 c.identifier={{'A','B','C','1','2','3'}};return true;
}
'''
HARNESS = r'''
#include "standin.h"
#include "nvs_boot_guard.h"
#include "pairing_proof_esp32.h"
#include "x5_runtime.h"
static ridesync::NvsBootStatus boot;
static unsigned proof_reads=0;
namespace ridesync {
NvsBootStatus nvsBootStatus(){return boot;}
PairingProofMaintenance &pairingProofMaintenance(){static PairingProofMaintenance p;return p;}
void PairingProofMaintenance::beginOwner(){++proof_reads;}
}
struct Peripheral : ridesync::X5PeripheralPort {
 unsigned configs=0,connects=0,notifies=0;
 bool configure(const ridesync::X5Qualification&)override{++configs;return true;}
 bool connect(ridesync::Token,uint32_t)override{++connects;return true;}
 bool notify(const ridesync::X5ShutterRequest&)override{++notifies;return true;}
 void cancel(ridesync::Token)override{}void close(uint32_t)override{}
 bool poll(ridesync::X5Input&)override{return false;}bool takeLoss(uint32_t)override{return false;}
 bool released(uint32_t)const override{return true;}void service(uint32_t)override{}
} peripheral;
extern "C" ridesync::X5PeripheralPort *ridesync_x5_peripheral_backend(){return &peripheral;}
#ifdef RIDESYNC_X5_WAKE_MILESTONE
#include "x5_wake_esp32.h"
namespace ridesync {
struct TestRadio : WakeRadio {
 unsigned calls=0;WakeRadioResult result;
 WakeSubmit begin(const WakeOperation &o,const insta360::WakeEncoding&,uint32_t,uint32_t)override{
  ++calls;result={};result.operation=o;return WakeSubmit::Accepted;
 }
 void cancel(const WakeOperation&)override{result.terminal=result.released=true;}
 WakeRadioResult poll(const WakeOperation&,uint32_t)override{return result;}
};
WakeRadio &x5WakeRadio(){static TestRadio radio;return radio;}
}
#endif
#include "src/profiles/insta360_x5_esp32.cpp"
#include "src/main.cpp"
int main(int argc,char**argv){
 assert(argc==2);int scenario=std::stoi(argv[1]);boot.init_observed=true;
 if(scenario==2)boot.format_refused=true;
 if(scenario==3)task_failure=true;
 if(scenario==1)ridesync::x5MilestoneBegin(true);else setup();
 if(config_task&&scenario!=4)run_config();
 if(scenario==4)fake_time+=1000;
 loop();assert(peripheral.connects==0&&peripheral.notifies==0);
 if(scenario==4){run_config();loop();}
#if defined(TEST_PROVIDER) && !defined(RIDESYNC_X5_STORE_INSPECT)
 bool valid=scenario==0||scenario==5||scenario==6;
#else
 bool valid=false;
#endif
 assert(peripheral.configs==unsigned(valid));
#ifdef RIDESYNC_X5_STORE_INSPECT
 if(config_task)assert(Serial.output.find("X5_STORE complete=1 error=0 counts=0,0,0,0,0,0,0 digest=ab")!=std::string::npos);
#endif
 Serial.input("STATUS\n");loop();assert(peripheral.connects==0);
 if(scenario==6){boot.format_refused=true;loop();boot.format_refused=false;loop();valid=false;}
#ifdef RIDESYNC_X5_WAKE_MILESTONE
 Serial.input("WAKE\n");loop();loop();
 auto &radio=static_cast<ridesync::TestRadio&>(ridesync::x5WakeRadio());
 assert(radio.calls==unsigned(valid)&&peripheral.connects==0&&peripheral.notifies==0);
 Serial.input("REC\n");loop();assert(peripheral.notifies==0);
 Serial.input("DISCONNECT\n");loop();loop();
#endif
 Serial.input("CONNECT\n");loop();assert(peripheral.connects==unsigned(valid));
 if(scenario==5&&valid){
   unsigned before=Serial.reads;for(int i=0;i<80;++i)Serial.bytes.push_back('x');
   Serial.input("REC\n");loop();assert(Serial.reads-before==32);
   while(Serial.available())loop();assert(peripheral.notifies==0);
   fake_time+=15000;loop();assert(ridesync::x5Runtime().status().lifecycle==ridesync::Lifecycle::Failed);
 }
 assert(Serial.output.find("06:05:04:03:02:01")==std::string::npos);
 return 0;
}
'''
class X5MilestoneEsp32(unittest.TestCase):
    def test_actual_main_and_commissioning_boundaries(self):
        with tempfile.TemporaryDirectory() as directory:
            temp=Path(directory)
            (temp/'standin.h').write_text(STUB)
            for name in ['Arduino.h','esp_attr.h','esp_system.h','freertos/task.h']:
                path=temp/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('#include "standin.h"\n')
            (temp/'ble_esp32.h').write_text(r'''#pragma once
#include "ble_pairing_reset.h"
namespace ridesync { class Esp32BleHost { public:
 static Esp32BleHost &instance(){static Esp32BleHost h;return h;}
 BleStoreObservation inspectStore(bool compare){assert(!compare);BleStoreObservation o;o.complete=true;o.snapshot.digest[0]=0xab;return o;}
}; }
''')
            (temp/'provider.h').write_text(PROVIDER)
            (temp/'harness.cpp').write_text(HARNESS)
            units=['recording_manager.cpp','x5_wake.cpp','wake_manager.cpp','insta360_wake_encoder.cpp','x5_runtime.cpp','x5_serial_control.cpp','profiles/insta360_x5.cpp','protocol/insta360_codec.cpp','camera_manager.cpp','config.cpp','health_supervisor.cpp']
            for provider,inspector,wake in [(False,False,False),(True,False,False),(True,True,False),(False,False,True),(True,False,True)]:
                binary=temp/f'harness-{provider}-{inspector}-{wake}'
                command=['clang++','-std=c++11','-DARDUINO_ARCH_ESP32','-DRIDESYNC_X5_SERIAL_MILESTONE','-DRIDESYNC_X5_NO_PRIVATE_HEADER','-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-O0','-ffunction-sections','-fdata-sections','-I',str(temp),'-I',str(ROOT/'include'),'-I',str(ROOT),str(temp/'harness.cpp')]
                if wake:command+=['-DRIDESYNC_X5_WAKE_MILESTONE']
                if inspector:command+=['-DRIDESYNC_X5_STORE_INSPECT']
                if provider:command+=['-DTEST_PROVIDER','-DRIDESYNC_X5_PRIVATE_HEADER="provider.h"']
                command += ['-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections']
                command += [str(ROOT/'src'/unit) for unit in units]+['-o',str(binary)]
                build=subprocess.run(command,capture_output=True,text=True)
                self.assertEqual(0,build.returncode,build.stderr)
                for scenario in range(7):
                    if not provider and scenario==4:continue
                    run=subprocess.run([str(binary),str(scenario)],capture_output=True,text=True)
                    self.assertEqual(0,run.returncode,f'provider={provider} scenario={scenario}: {run.stderr}')

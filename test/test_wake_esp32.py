"""Actual shared host and wake worker; synthetic SDK, no camera qualification."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_ble_esp32 import STUB, ROOT

EXTRA = r'''
#include <thread>
#include <mutex>
#include <condition_variable>
#define BLE_GAP_EVENT_ADV_COMPLETE 9
#define BLE_GAP_CONN_MODE_UND 2
#define BLE_GAP_DISC_MODE_GEN 2
#define pdMS_TO_TICKS(x) (x)
struct DelayBoundary {};
static void vTaskDelay(unsigned){throw DelayBoundary{};}
static unsigned millis(){return fake_time;}
static void (*worker_entry)(void*)=nullptr;
static void *worker_arg=nullptr;
static int worker_tasks=0;
static int xTaskCreate(void(*fn)(void*),const char*,unsigned stack,void*arg,unsigned priority,void**handle){
 assert(stack==4096&&priority==2);worker_entry=fn;worker_arg=arg;++worker_tasks;*handle=reinterpret_cast<void*>(2);return pdPASS;
}
struct ble_gap_adv_params { uint8_t conn_mode=0,disc_mode=0; };
static std::vector<uint8_t> captured_adv,captured_rsp;
static int (*adv_callback)(ble_gap_event*,void*)=nullptr;
static void *adv_arg=nullptr;
static int captured_duration=0,starts=0,stops=0,start_error=0,stop_error=0;
static std::function<void()> data_hook,rsp_hook,start_hook;
static int ble_gap_adv_set_data(const uint8_t*p,int n){captured_adv.assign(p,p+n);if(data_hook)data_hook();return 0;}
static int ble_gap_adv_rsp_set_data(const uint8_t*p,int n){captured_rsp.assign(p,p+n);if(rsp_hook)rsp_hook();return 0;}
static int ble_gap_adv_start(uint8_t,const ble_addr_t*,int duration,const ble_gap_adv_params*params,int(*cb)(ble_gap_event*,void*),void*arg){
 assert(params->conn_mode==2&&params->disc_mode==2);++starts;captured_duration=duration;adv_callback=cb;adv_arg=arg;if(start_hook)start_hook();gap_adv=!start_error;return start_error;
}
static int ble_gap_adv_stop(){++stops;if(!stop_error)gap_adv=false;return stop_error;}
static std::vector<uint16_t> terminated;
static int terminate_error=0,find_error=0;
static int wake_terminate(uint16_t handle,int){terminated.push_back(handle);return terminate_error;}
static int wake_find(uint16_t handle,ble_gap_conn_desc*out){out->conn_handle=handle;out->peer_id_addr.val[0]=9;out->peer_ota_addr.type=1;out->peer_ota_addr.val[0]=8;return find_error;}
'''
HARNESS = r'''
#include "standin.h"
#include "nvs_boot_guard.h"
static ridesync::NvsBootStatus boot_status;
namespace ridesync { NvsBootStatus nvsBootStatus(){return boot_status;} }
#include "src/ble_esp32.cpp"
#include "src/ble_wake_esp32.cpp"
#include "src/pairing_proof_esp32.cpp"
#include "src/insta360_wake_esp32.cpp"
using namespace ridesync;
static void runWorker(){try{worker_entry(worker_arg);}catch(const DelayBoundary&){};}
static void complete(){ble_gap_event event{};event.type=BLE_GAP_EVENT_ADV_COMPLETE;adv_callback(&event,adv_arg);}
static void incoming(uint16_t handle){ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;e.connect.conn_handle=handle;gap_adv=false;adv_callback(&e,adv_arg);}
static void disconnect(uint16_t handle){ble_gap_event e{};e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.conn_handle=handle;adv_callback(&e,adv_arg);}
int main(int argc,char**argv){
 int scenario=std::atoi(argv[1]);
 auto &host=Esp32BleHost::instance();auto &radio=insta360WakeRadio();
 assert(!radio.activate(false));assert(!radio.activate(true));assert(!worker_entry);
 boot_status.init_observed=true;pairingProofMaintenance().beginOwner();
 BleStoreProof proof;proof.qualification_record=42;proof.digest[0]=1;proof.digest[1]=2;proof.digest[2]=3;proof.digest[3]=6;
 assert(host.configureRestore(proof));assert(host.start(true,true)==BleHostState::Starting);ble_hs_cfg.sync_cb();
 assert(radio.activate(true));assert(radio.activate(true));assert(worker_tasks==1&&controller_init==1);
 WakeOperation op;op.peer=0;op.generation=1;op.id=1;
 const uint8_t id[]={'1','2','3','4','5','6'};
 auto bytes=insta360::encodeWake(insta360::WakeProfile::M5WakeV1,id,6);
 #include "test/fixtures/wake/sdk_cases.inc"
}
'''

class WakeEsp32(unittest.TestCase):
    def test_actual_worker_and_shared_host(self):
        with tempfile.TemporaryDirectory() as directory:
            temp=Path(directory)
            stub=STUB.replace('struct { int reason=0; } disc_complete;', 'struct { int reason=0; } disc_complete; struct { int reason=0; } adv_complete;')
            stub += EXTRA
            stub += '\n#define ble_gap_terminate wake_terminate\n#define ble_gap_conn_find wake_find\n'
            (temp/'standin.h').write_text(stub)
            includes=["Arduino.h","NimBLEDevice.h","esp_bt.h","freertos/task.h","freertos/queue.h","mbedtls/sha256.h","nvs.h","esp_timer.h",
                "nimble/nimble/host/include/host/ble_gatt.h","nimble/nimble/include/nimble/nimble_npl.h",
                "nimble/esp_port/esp-hci/include/esp_nimble_hci.h","nimble/nimble/host/include/host/ble_hs.h",
                "nimble/nimble/host/include/host/ble_gap.h","nimble/nimble/host/include/host/ble_store.h",
                "nimble/nimble/host/src/ble_hs_resolv_priv.h","nimble/nimble/host/store/config/include/store/config/ble_store_config.h",
                "nimble/nimble/host/store/config/src/ble_store_config_priv.h","nimble/porting/nimble/include/nimble/nimble_port.h",
                "nimble/porting/nimble/include/os/os_mbuf.h"]
            for name in includes:
                target=temp/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text('#include "standin.h"\n')
            (temp/'harness.cpp').write_text(HARNESS)
            binary=temp/'harness'
            subprocess.run(['clang++','-std=c++11','-DARDUINO_ARCH_ESP32','-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-O0','-I',str(temp),'-I',str(ROOT/'include'),'-I',str(ROOT),str(temp/'harness.cpp'),str(ROOT/'src/pairing_reset.cpp'),str(ROOT/'src/wake_radio_policy.cpp'),str(ROOT/'src/wake_manager.cpp'),str(ROOT/'src/insta360_wake_encoder.cpp'),'-o',str(binary)],check=True)
            for scenario in range(19):
                result=subprocess.run([str(binary),str(scenario)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,f'scenario {scenario}: {result.stderr}')

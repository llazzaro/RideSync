"""Actual X5 backend/shared host with synthetic SDK fault boundaries; no radio proof."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_ble_esp32 import STUB, ROOT

EXTRA = r'''
#include <mutex>
using portMUX_TYPE=std::recursive_mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
static unsigned critical_depth=0;
#define portENTER_CRITICAL(p) do{(p)->lock();++critical_depth;}while(0)
#define portEXIT_CRITICAL(p) do{--critical_depth;(p)->unlock();}while(0)
#define pdMS_TO_TICKS(x) (x)
#define BLE_GAP_EVENT_SUBSCRIBE 10
#define BLE_GAP_EVENT_ADV_COMPLETE 11
#define BLE_GAP_CONN_MODE_UND 2
#define BLE_GAP_DISC_MODE_GEN 2
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_GATT_CHR_F_WRITE 8
#define BLE_GATT_CHR_F_READ 2
#define BLE_GATT_CHR_F_NOTIFY 16
#define BLE_GATT_ACCESS_OP_READ_CHR 1
#define BLE_GATT_ACCESS_OP_WRITE_CHR 2
#define BLE_ATT_ERR_READ_NOT_PERMITTED 2
#define BLE_ATT_ERR_WRITE_NOT_PERMITTED 3
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define BLE_ATT_ERR_UNLIKELY 14
#define BLE_HS_EALREADY 21
#define BLE_UUID16_INIT(n) {{16},n}
#define BLE_UUID128_INIT(...) {{128},{__VA_ARGS__}}
#define OS_MBUF_PKTLEN(m) os_mbuf_len(m)
struct ble_uuid16_t { ble_uuid_t u; uint16_t value; };
struct ble_uuid128_t { ble_uuid_t u; uint8_t value[16]; };
struct ble_gatt_access_ctxt { uint8_t op=0; os_mbuf *om=nullptr; };
struct ble_gatt_chr_def {
 const ble_uuid_t *uuid=nullptr;
 int(*access_cb)(uint16_t,uint16_t,ble_gatt_access_ctxt*,void*)=nullptr;
 void *arg=nullptr; uint16_t flags=0; uint16_t *val_handle=nullptr;
};
struct ble_gatt_svc_def {
 uint8_t type=0; const ble_uuid_t *uuid=nullptr; const ble_gatt_chr_def *characteristics=nullptr;
};
static const ble_gatt_svc_def *registered_services=nullptr;
static int registration_error=0,registration_calls=0;
static int ble_gatts_count_cfg(const ble_gatt_svc_def*){++registration_calls;return registration_error;}
static int ble_gatts_add_svcs(const ble_gatt_svc_def *s){
 registered_services=s;unsigned h=20;
 for(unsigned i=0;s[i].type;++i)for(unsigned j=0;s[i].characteristics[j].uuid;++j)
  *s[i].characteristics[j].val_handle=h++;
 return registration_error;
}
static void ble_svc_gap_init(){}
static void ble_svc_gatt_init(){}
static int ble_svc_gap_device_name_set(const char *n){assert(!std::strcmp(n,"Insta360 GPS Remote"));return 0;}
static bool btStarted(){return false;}
static int os_mbuf_append(os_mbuf *m,const void *p,unsigned n){
 const auto *b=static_cast<const uint8_t*>(p);m->data.insert(m->data.end(),b,b+n);return 0;
}
static bool allocation_fail=false;
static os_mbuf *ble_hs_mbuf_from_flat(const void *p,unsigned n){
 if(allocation_fail)return nullptr;auto*m=new os_mbuf;os_mbuf_append(m,p,n);return m;
}
static void os_mbuf_free_chain(os_mbuf*m){delete m;}
static unsigned notify_calls=0;
static std::vector<uint8_t> notify_bytes;
static int notify_error=0;
static std::function<void()> notify_hook;
static int ble_gatts_notify_custom(uint16_t c,uint16_t h,os_mbuf*m){
 assert(c==7&&h==21);++notify_calls;notify_bytes=m->data;delete m;
 if(notify_hook)notify_hook();return notify_error;
}
struct ble_gap_adv_params { uint8_t conn_mode=0,disc_mode=0; };
static int(*peripheral_gap)(ble_gap_event*,void*)=nullptr;
static void *peripheral_arg=nullptr;
static unsigned adv_starts=0,adv_stops=0,terminates=0;
static int adv_duration=0;
static int ble_gap_adv_set_data(const uint8_t*,unsigned){return 0;}
static int ble_gap_adv_rsp_set_data(const uint8_t*,unsigned){return 0;}
static int ble_gap_adv_start(uint8_t,const ble_addr_t*,int duration,const ble_gap_adv_params*p,int(*cb)(ble_gap_event*,void*),void*a){
 assert(p->conn_mode==2&&p->disc_mode==2);++adv_starts;adv_duration=duration;
 peripheral_gap=cb;peripheral_arg=a;gap_adv=true;return 0;
}
static int ble_gap_adv_stop(){++adv_stops;gap_adv=false;return 0;}
static bool wrong_identity=false;
static int x5_find(uint16_t h,ble_gap_conn_desc*d){
 assert(critical_depth==0); // NimBLE lookup takes a blocking host mutex.
 d->conn_handle=h;d->peer_id_addr.val[0]=wrong_identity?9:1;return 0;
}
static int x5_terminate(uint16_t,int){++terminates;return 0;}
struct SchedulerBoundary{};
static void(*worker_fn)(void*)=nullptr;
static void *worker_arg=nullptr;
static int xTaskCreate(void(*f)(void*),const char*,unsigned stack,void*a,unsigned priority,void**h){
 assert(stack==4096&&priority==2);worker_fn=f;worker_arg=a;*h=reinterpret_cast<void*>(2);return pdPASS;
}
static void vTaskDelay(unsigned){throw SchedulerBoundary{};}
static void step(){try{worker_fn(worker_arg);}catch(SchedulerBoundary&) {}}
'''
HARNESS = r'''
#include "standin.h"
#include "nvs_boot_guard.h"
#include "test/fixtures/insta360/ce80_display.h"
static ridesync::NvsBootStatus boot;
namespace ridesync { NvsBootStatus nvsBootStatus(){return boot;} }
#include "src/pairing_proof_esp32.cpp"
#include "src/ble_esp32.cpp"
#include "src/ble_wake_esp32.cpp"
#include "src/ble_peripheral_esp32.cpp"
#include "src/x5_peripheral_esp32.cpp"
using namespace ridesync;
static void connected(){gap_adv=false;ble_gap_event e;e.type=BLE_GAP_EVENT_CONNECT;e.connect.conn_handle=7;peripheral_gap(&e,peripheral_arg);}
static void subscribed(bool enabled=true){ble_gap_event e;e.type=BLE_GAP_EVENT_SUBSCRIBE;e.subscribe.conn_handle=7;e.subscribe.attr_handle=21;e.subscribe.cur_notify=enabled;peripheral_gap(&e,peripheral_arg);}
static void disconnected(){ble_gap_event e;e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.conn_handle=7;peripheral_gap(&e,peripheral_arg);}
static void frame(const uint8_t* bytes=nullptr,size_t size=0){os_mbuf mb;mb.data={0xfe,0xef,0xfe,0x10,0x81,0x0b,0x01,0x24,0x5e,0x00,0x35,0x2e,0x37,0x4b,0x7c,0x33,0x30};
 if(bytes)mb.data.assign(bytes,bytes+size);
 ble_gatt_access_ctxt ctx;ctx.op=2;ctx.om=&mb;
 auto&c=registered_services[0].characteristics[0];c.access_cb(7,20,&ctx,c.arg);}
int main(int argc,char**argv){
 assert(argc==2);int scenario=std::stoi(argv[1]);boot.init_observed=true;
 pairingProofMaintenance().beginOwner();
 auto&host=Esp32BleHost::instance();Esp32X5Peripheral port;
 X5Qualification q;q.enabled=true;q.identity.verified=true;q.identity.type=IdentityType::Public;q.identity.address[0]=1;
 q.firmware_size=7;std::memcpy(q.firmware.data(),"1.11.10",7);
 q.display.profile=insta360::Ce80DisplayProfile::X5CapturedDisplayV1;
 q.store.qualification_record=42;q.store.digest={{1,2,3,6}};
 if(scenario==1)q.store.qualification_record=0;
 if(scenario==1){assert(!port.configure(q));return 0;}
 assert(port.configure(q));
 Token t;t.connection=1;t.operation=1;
 assert(port.connect(t,15000));
 if(scenario==2)registration_error=88;
 step();
 if(scenario==2){assert(registration_calls==1&&adv_starts==0&&host.state()==BleHostState::Failed);return 0;}
 assert(registration_calls==1&&registered_services&&host.state()==BleHostState::Starting);
 ble_hs_cfg.sync_cb();step();assert(adv_starts==1&&adv_duration>0&&adv_duration<=15000);
 if(scenario==3)wrong_identity=true;
 connected();
 if(scenario==3){step();assert(terminates==1&&notify_calls==0);return 0;}
 subscribed();frame();X5Input input;uint32_t observed=0;while(port.poll(input)){if(input.kind==X5InputKind::Display)observed=input.sequence;}
 if(scenario==13)fake_time=4000;
 X5ShutterRequest r;r.token=t;r.token.operation=2;r.handle=7;r.deadline_ms=scenario==13?9000:5000;r.observation_sequence=observed;
 assert(port.notify(r));
 if(scenario==4)subscribed(false);
 if(scenario==5)allocation_fail=true;
 if(scenario==6)port.cancel(r.token);
 if(scenario==7)fake_time=5000;
 if(scenario==8){for(int i=0;i<33;++i)frame();}
 if(scenario==12)frame(insta360_fixture::kCe80Photo,sizeof insta360_fixture::kCe80Photo);
 if(scenario==14)frame(insta360_fixture::kCe80Timer,sizeof insta360_fixture::kCe80Timer);
 if(scenario==13)fake_time=6001;
 if(scenario==9)notify_error=99;
 if(scenario==10)notify_hook=[&](){fake_time=6000;disconnected();pump();assert(!port.released(1));};
 if(scenario==11){disconnected();pump();}
 step();step();
 if(scenario>=4&&scenario<=8||scenario==11||scenario==12||scenario==13||scenario==14)assert(notify_calls==0);
 else {assert(notify_calls==1);assert((notify_bytes==std::vector<uint8_t>{0xfc,0xef,0xfe,0x86,0x00,0x03,0x01,0x02,0x00}));}
 if(scenario==8)assert(port.takeLoss(1));
 if(scenario==10)assert(port.released(1));
 if(scenario==0){
  assert(!port.notify(r));disconnected();assert(!port.released(1));pump();step();assert(port.released(1));
  t.connection=2;t.operation=4;assert(port.connect(t,15000));step();assert(notify_calls==1&&adv_starts==2);
 }
 assert(deleted==0&&written==0);
 return 0;
}
'''
class X5PeripheralEsp32(unittest.TestCase):
    def test_actual_backend_registration_admission_and_one_shot_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            temp=Path(directory)
            source=STUB.replace('struct ble_gap_event {','struct ble_gap_event {\n struct {uint16_t conn_handle=0,attr_handle=0;bool cur_notify=false;} subscribe;\n struct {int reason=0;} adv_complete;')
            source=source.replace('static int ble_gap_conn_find(uint16_t,ble_gap_conn_desc*){return 0;}','static int x5_find(uint16_t,ble_gap_conn_desc*);\nstatic int ble_gap_conn_find(uint16_t h,ble_gap_conn_desc*d){return x5_find(h,d);}')
            source=source.replace('static int ble_gap_terminate(uint16_t,int){return 0;}','static int x5_terminate(uint16_t,int);\nstatic int ble_gap_terminate(uint16_t h,int r){return x5_terminate(h,r);}')
            (temp/'standin.h').write_text(source+EXTRA)
            includes=['NimBLEDevice.h','esp_bt.h','esp32-hal-bt.h','Arduino.h','esp_timer.h','freertos/FreeRTOS.h','freertos/task.h','freertos/queue.h','mbedtls/sha256.h','nvs.h',
                'nimble/nimble/host/include/host/ble_gatt.h','nimble/nimble/host/include/host/ble_hs.h','nimble/nimble/host/include/host/ble_gap.h','nimble/nimble/host/include/host/ble_store.h',
                'nimble/nimble/include/nimble/nimble_npl.h','nimble/esp_port/esp-hci/include/esp_nimble_hci.h',
                'nimble/nimble/host/src/ble_hs_resolv_priv.h','nimble/nimble/host/store/config/include/store/config/ble_store_config.h',
                'nimble/nimble/host/store/config/src/ble_store_config_priv.h','nimble/porting/nimble/include/nimble/nimble_port.h','nimble/porting/nimble/include/os/os_mbuf.h',
                'nimble/nimble/host/services/gap/include/services/gap/ble_svc_gap.h','nimble/nimble/host/services/gatt/include/services/gatt/ble_svc_gatt.h']
            for name in includes:
                path=temp/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('#include "standin.h"\n')
            (temp/'harness.cpp').write_text(HARNESS)
            binary=temp/'harness'
            subprocess.run(['clang++','-std=c++11','-DARDUINO_ARCH_ESP32','-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-O0','-I',str(temp),'-I',str(ROOT/'include'),'-I',str(ROOT),str(temp/'harness.cpp'),str(ROOT/'src/x5_peripheral_policy.cpp'),str(ROOT/'src/protocol/insta360_codec.cpp'),str(ROOT/'src/wake_radio_policy.cpp'),str(ROOT/'src/wake_manager.cpp'),str(ROOT/'src/insta360_wake_encoder.cpp'),'-o',str(binary)],check=True)
            for scenario in range(15):
                result=subprocess.run([str(binary),str(scenario)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,f'scenario {scenario}: {result.stderr}')

"""Execute the real ESP32 adapter against explicit synthetic SDK boundaries.

Pinned target compilation separately checks these declarations against the actual
SDK. These tests do not claim radio, NVS timing, restore or pairing qualification.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUB = r'''
#pragma once
#include <array>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <string>
#include <functional>
#include <utility>
#define CONFIG_BT_NIMBLE_MAX_BONDS 5
#define CONFIG_BT_NIMBLE_MAX_CCCDS 32
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 5
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_BT_CONTROLLER_STATUS_IDLE 0
#define ESP_BT_MODE_BLE 2
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() esp_bt_controller_config_t{}
#define pdPASS 1
#define BLE_HS_ESTORE_FAIL 10
#define BLE_HS_ESTORE_CAP 11
#define BLE_HS_ENOENT 12
#define BLE_HS_ENOTSUP 13
#define BLE_HS_EDISABLED 14
#define BLE_HS_EBUSY 15
#define BLE_HS_ENOMEM 16
#define BLE_HS_ETIMEOUT 17
#define BLE_HS_EINVAL 18
#define BLE_HS_EDONE 19
#define BLE_HS_IO_NO_INPUT_OUTPUT 0
#define BLE_SM_PAIR_KEY_DIST_ENC 1
#define BLE_SM_PAIR_KEY_DIST_ID 2
#define BLE_STORE_OBJ_TYPE_OUR_SEC 1
#define BLE_STORE_OBJ_TYPE_PEER_SEC 2
#define BLE_STORE_OBJ_TYPE_CCCD 3
#define BLE_STORE_OBJ_TYPE_PEER_DEV_REC 4
#define BLE_STORE_OBJ_TYPE_CSFC 8
#define BLE_STORE_OBJ_TYPE_LOCAL_IRK 7
#define BLE_STORE_OBJ_TYPE_PEER_ADDR 6
#define BLE_STORE_EVENT_FULL 2
#define BLE_STORE_EVENT_OVERFLOW 1
#define BLE_ADDR_PUBLIC 0
#define BLE_ADDR_RANDOM 1
#define BLE_UUID_TYPE_16 16
#define BLE_UUID_TYPE_128 128
#define BLE_ERR_REM_USER_CONN_TERM 0x13
#define BLE_GAP_REPEAT_PAIRING_IGNORE 0
#define BLE_GAP_EVENT_DISC 1
#define BLE_GAP_EVENT_DISC_COMPLETE 2
#define BLE_GAP_EVENT_CONNECT 3
#define BLE_GAP_EVENT_DISCONNECT 4
#define BLE_GAP_EVENT_ENC_CHANGE 5
#define BLE_GAP_EVENT_NOTIFY_RX 6
#define BLE_GAP_EVENT_PASSKEY_ACTION 7
#define BLE_GAP_EVENT_REPEAT_PAIRING 8
#define BLE_HCI_ADV_RPT_EVTYPE_ADV_IND 0
#define BLE_HCI_ADV_RPT_EVTYPE_DIR_IND 1
#define BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND 2
#define BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND 3
#define BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP 4
struct ble_addr_t { uint8_t type=0,val[6]{}; };
struct ble_uuid_t { uint8_t type; };
union ble_uuid_any_t {
 ble_uuid_t u;
 struct { ble_uuid_t u; uint16_t value; } u16;
 struct { ble_uuid_t u; uint8_t value[16]; } u128;
};
struct ble_store_value_sec {
 ble_addr_t peer_addr; uint16_t bond_count=0; uint8_t key_size=16,ltk[16]{};
 uint8_t ltk_present=1,authenticated=0;
};
struct ble_store_value_cccd { ble_addr_t peer_addr; uint16_t chr_val_handle=0,flags=0; };
struct ble_store_value_csfc { ble_addr_t peer_addr; uint8_t csfc[1]{}; };
struct ble_store_value_local_irk { ble_addr_t addr; uint8_t irk[16]{}; };
struct ble_store_value_rpa_rec { ble_addr_t peer_rpa_addr,peer_addr; };
struct ble_hs_dev_records { bool rec_used; uint8_t identity_addr[6]; };
struct ble_store_key_sec { ble_addr_t peer_addr; uint8_t idx=0; };
union ble_store_key {
 ble_store_key_sec sec;
 struct { ble_addr_t peer_addr; uint16_t chr_val_handle; } cccd;
 struct { ble_addr_t peer_addr; } csfc;
 struct { ble_addr_t addr; } local_irk;
 struct { ble_addr_t peer_rpa_addr; } rpa_rec;
 ble_store_key() { std::memset(this,0,sizeof *this); }
};
union ble_store_value {
 ble_store_value_sec sec;
 ble_store_value_cccd cccd;
 ble_store_value_csfc csfc;
 ble_store_value_local_irk local_irk;
 ble_store_value_rpa_rec rpa_rec;
 ble_store_value() { std::memset(this,0,sizeof *this); }
};
struct ble_store_status_event {
 int event_code=0;
 struct { int obj_type=0; uint16_t conn_handle=0; } full;
 struct { int obj_type=0; const ble_store_value *value=nullptr; } overflow;
};
struct os_mbuf { std::vector<uint8_t> data; };
static uint16_t os_mbuf_len(os_mbuf *m) { return m->data.size(); }
static std::function<void()> copy_hook;
static int os_mbuf_copydata(os_mbuf*m,int offset,int length,void*p) {
 if(copy_hook)copy_hook();
 std::memcpy(p,m->data.data()+offset,length); return 0;
}
struct ble_gap_sec_state { bool encrypted=false,authenticated=false,bonded=false; };
struct ble_gap_conn_desc { ble_gap_sec_state sec_state; ble_addr_t peer_id_addr; uint16_t conn_handle=0; };
struct ble_gap_event {
 int type;
 struct { ble_addr_t addr; uint8_t event_type=0xff,length_data=0; const uint8_t *data=nullptr; } disc;
 struct { int reason=0; } disc_complete;
 struct { int status=0; uint16_t conn_handle=0; } connect;
 struct { ble_gap_conn_desc conn; int reason=0; } disconnect;
 struct { uint16_t conn_handle=0; int status=0; } enc_change;
 struct { uint16_t conn_handle=0,attr_handle=0; os_mbuf *om=nullptr; } notify_rx;
};
struct ble_gatt_error { uint16_t status=0,att_handle=0; };
struct ble_gatt_svc { uint16_t start_handle=0,end_handle=0; ble_uuid_any_t uuid; };
struct ble_gatt_chr { uint16_t def_handle=0,val_handle=0; uint8_t properties=0; ble_uuid_any_t uuid; };
struct ble_gatt_dsc { uint16_t handle=0; ble_uuid_any_t uuid; };
struct ble_gatt_attr { uint16_t handle=0,offset=0; os_mbuf *om=nullptr; };
struct ble_gap_disc_params { uint8_t passive=0,filter_duplicates=0; };
struct ble_npl_event { bool queued=false; void (*fn)(ble_npl_event*)=nullptr; void *arg=nullptr; };
struct ble_npl_eventq { void *q=nullptr; };
static ble_npl_eventq eventq;
static std::vector<ble_npl_event*> pending;
static void ble_npl_event_init(ble_npl_event*e,void(*fn)(ble_npl_event*),void*a){e->fn=fn;e->arg=a;}
static void *ble_npl_event_get_arg(ble_npl_event*e){return e->arg;}
static ble_npl_eventq *nimble_port_get_dflt_eventq(){return &eventq;}
static bool queue_full=false;
static unsigned queue_wait=0;
static int xQueueSendToBack(void*,ble_npl_event**e,unsigned ticks){queue_wait=ticks;if(queue_full)return 0;pending.push_back(*e);return pdPASS;}
static void ble_npl_eventq_put(ble_npl_eventq*q,ble_npl_event*e){if(e->queued)return;e->queued=true;xQueueSendToBack(q->q,&e,~0u);}
static void pump(){auto events=pending;pending.clear();for(auto*e:events){e->queued=false;e->fn(e);}}
struct HsConfig {
 void(*reset_cb)(int)=nullptr; void(*sync_cb)()=nullptr;
 int(*store_status_cb)(ble_store_status_event*,void*)=nullptr;
 int(*store_read_cb)(int,const ble_store_key*,ble_store_value*)=nullptr;
 int(*store_write_cb)(int,const ble_store_value*)=nullptr;
 int(*store_delete_cb)(int,const ble_store_key*)=nullptr;
 int sm_io_cap=0,sm_bonding=0,sm_mitm=0,sm_sc=0,sm_our_key_dist=0,sm_their_key_dist=0;
} ble_hs_cfg;
class NimBLEDeviceCallbacks { public: virtual ~NimBLEDeviceCallbacks()=default;
 virtual int onStoreStatus(ble_store_status_event*,void*){return 0;} };
struct NimBLEDevice {
 static bool isInitialized(){return false;}
 static void setDeviceCallbacks(NimBLEDeviceCallbacks*){}
};
struct esp_bt_controller_config_t { int mode=0,ble_max_conn=0; };
static int controller_init=0,controller_enable=0,hci_init=0,port_init=0,create_tasks=0;
static int controller_error=0,task_error=0,readback_error=0;
static bool stack_bad=false;
static std::vector<std::pair<std::string,std::vector<uint8_t>>> nvs_entries;
static ble_store_value_sec stored_sec;
static ble_store_value_cccd stored_cccd;
static int esp_bt_controller_get_status(){return ESP_BT_CONTROLLER_STATUS_IDLE;}
static int esp_bt_controller_init(const esp_bt_controller_config_t *c){
 ++controller_init;assert(c->mode==2&&c->ble_max_conn==5);return controller_error;}
static int esp_bt_controller_enable(int){++controller_enable;return 0;}
static int esp_nimble_hci_init(){++hci_init;return 0;}
static int nimble_port_init(){++port_init;return 0;}
static void nimble_port_run(){assert(false);}
static void vTaskSuspend(void*){assert(false);}
using TaskHandle_t=void*;
static int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned stack,void*,unsigned priority,void **handle,int core){
 ++create_tasks;assert(stack==4096&&priority==5&&core==0);*handle=reinterpret_cast<void*>(1);return task_error?0:pdPASS;}
static int ble_hs_id_infer_auto(int,uint8_t*p){*p=0;return 0;}
static int ble_store_config_read(int,const ble_store_key*,ble_store_value*){return BLE_HS_ENOENT;}
static int deleted=0,written=0;
static int ble_store_config_write(int,const ble_store_value*){++written;return 0;}
static int ble_store_config_delete(int,const ble_store_key*){++deleted;return 0;}
extern "C" void ble_store_config_init(){assert(ble_hs_cfg.store_status_cb!=nullptr);}
static int ble_store_util_count(int type,int*n){
 if(readback_error)return 99;*n=0;
 for(auto &entry:nvs_entries)
  if((type==1&&entry.first=="our_sec_0")||(type==3&&entry.first=="cccd_sec_0"))++*n;
 return 0;
}
static int ble_store_read_our_sec(const ble_store_key_sec*key,ble_store_value_sec*value){
 for(auto &entry:nvs_entries)if(entry.first=="our_sec_0"){
  assert(key->peer_addr.val[0]==1);*value=stored_sec;if(stack_bad)value->ltk[0]^=1;return 0;
 }return BLE_HS_ENOENT;
}
static int ble_store_read_peer_sec(const ble_store_key_sec*,ble_store_value_sec*){return BLE_HS_ENOENT;}
static int ble_store_read_cccd(const void*pointer,ble_store_value_cccd*value){
 const auto*key=static_cast<const decltype(ble_store_key{}.cccd)*>(pointer);
 for(auto &entry:nvs_entries)if(entry.first=="cccd_sec_0"){
  assert(key->peer_addr.val[0]==1&&key->chr_val_handle==3);*value=stored_cccd;if(stack_bad)value->flags^=1;return 0;
 }return BLE_HS_ENOENT;
}
static int ble_store_read_csfc(const void*,ble_store_value_csfc*){return BLE_HS_ENOENT;}
static int ble_store_read_local_irk(const void*,ble_store_value_local_irk*){return BLE_HS_ENOENT;}
static int ble_store_read_rpa_rec(const void*,ble_store_value_rpa_rec*){return BLE_HS_ENOENT;}
static ble_hs_dev_records *ble_rpa_get_peer_dev_records(){return nullptr;}
static int ble_rpa_get_num_peer_dev_records(){return 0;}
static int (*gap_callbacks[5])(ble_gap_event*,void*){};
static void *gap_args[5]{};
static unsigned connections=0;
static int ble_gap_connect(uint8_t,const ble_addr_t*,uint32_t,const void*,int(*cb)(ble_gap_event*,void*),void*a){
 gap_callbacks[connections]=cb;gap_args[connections++]=a;return 0;}
static int ble_gap_disc(uint8_t,uint32_t,const ble_gap_disc_params*,int(*cb)(ble_gap_event*,void*),void*a){gap_callbacks[4]=cb;gap_args[4]=a;return 0;}
static int ble_gap_disc_cancel(){return 0;}
static int ble_gap_conn_cancel(){return 0;}
static int ble_gap_terminate(uint16_t,int){return 0;}
static int ble_gap_security_initiate(uint16_t){return 0;}
static int ble_gap_conn_find(uint16_t,ble_gap_conn_desc*){return 0;}
static int ble_att_mtu(uint16_t){return 23;}
static int ble_gattc_disc_svc_by_uuid(uint16_t,const ble_uuid_t*,int(*)(uint16_t,const ble_gatt_error*,const ble_gatt_svc*,void*),void*){return 0;}
static int ble_gattc_disc_all_chrs(uint16_t,uint16_t,uint16_t,int(*)(uint16_t,const ble_gatt_error*,const ble_gatt_chr*,void*),void*){return 0;}
static int ble_gattc_disc_all_dscs(uint16_t,uint16_t,uint16_t,int(*)(uint16_t,const ble_gatt_error*,uint16_t,const ble_gatt_dsc*,void*),void*){return 0;}
static int(*att_callback)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*)=nullptr;
static void *att_arg=nullptr;
static int ble_gattc_read(uint16_t,uint16_t,int(*cb)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void*a){att_callback=cb;att_arg=a;return 0;}
static int ble_gattc_write_flat(uint16_t,uint16_t,const void*,uint16_t,int(*cb)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void*a){att_callback=cb;att_arg=a;return 0;}
using nvs_handle_t=unsigned;
using nvs_iterator_t=void*;
#define NVS_READONLY 0
#define NVS_TYPE_ANY 0
#define NVS_TYPE_BLOB 1
struct nvs_entry_info_t { int type; char key[16]; };
static int nvs_error=ESP_ERR_NVS_NOT_FOUND;
static int nvs_open(const char*,int,nvs_handle_t*p){*p=1;return nvs_entries.empty()?nvs_error:0;}
static nvs_iterator_t nvs_entry_find(const char*,const char*,int){return nvs_entries.empty()?nullptr:reinterpret_cast<void*>(1);}
static void nvs_entry_info(void*it,nvs_entry_info_t*info){info->type=NVS_TYPE_BLOB;std::strncpy(info->key,nvs_entries[reinterpret_cast<uintptr_t>(it)-1].first.c_str(),16);}
static nvs_iterator_t nvs_entry_next(void*it){auto next=reinterpret_cast<uintptr_t>(it)+1;return next>nvs_entries.size()?nullptr:reinterpret_cast<void*>(next);}
static void nvs_release_iterator(void*){}
static int nvs_get_blob(unsigned,const char*name,void*value,size_t*size){
 for(auto &entry:nvs_entries)if(entry.first==name){
  if(*size<entry.second.size())return 99;
  std::memcpy(value,entry.second.data(),entry.second.size());*size=entry.second.size();return 0;
 }return ESP_ERR_NVS_NOT_FOUND;
}
static void nvs_close(unsigned){}
// SHA library is checked by real SDK compile; this deterministic boundary double
// lets wrong snapshot/readback admission be tested without platform crypto.
struct mbedtls_sha256_context { uint8_t hash[32]{}; };
static void mbedtls_sha256_init(mbedtls_sha256_context*){}
static int mbedtls_sha256_starts_ret(mbedtls_sha256_context*c,int){std::memset(c->hash,0,32);return 0;}
static int mbedtls_sha256_update_ret(mbedtls_sha256_context*c,const uint8_t*p,size_t n){for(size_t i=0;i<n;++i)c->hash[i%32]^=p[i];return 0;}
static int mbedtls_sha256_finish_ret(mbedtls_sha256_context*c,uint8_t*p){std::memcpy(p,c->hash,32);return 0;}
static void mbedtls_sha256_free(mbedtls_sha256_context*){}
'''
HARNESS = r'''
#include "standin.h"
#include "nvs_boot_guard.h"
static ridesync::NvsBootStatus boot_status;
namespace ridesync { NvsBootStatus nvsBootStatus(){return boot_status;} }
#include "src/ble_esp32.cpp"
using namespace ridesync;
struct Receiver:BleCallbacks { unsigned copies=0; BleEvent latest;
 void copied(BleContext &ctx,BleEvent e) override {++copies;latest=e;
 if(e.kind==BleEventKind::Connected)ctx.connection.store(e.connection);}
};
int main(int argc,char**argv){
 int scenario=argc>1?std::atoi(argv[1]):0;
 auto &host=Esp32BleHost::instance();
 boot_status.init_observed=true;
 if(scenario==0){
  assert(host.start(false,true)==BleHostState::Disabled);
  assert(host.start(true,false)==BleHostState::Disabled);
  assert(controller_init==0&&create_tasks==0);return 0;
 }
 // A synthetic independently retained empty commissioning snapshot; no device data.
 BleStoreProof proof; proof.qualification_record=42;proof.digest[0]=1;proof.digest[1]=2;proof.digest[2]=3;proof.digest[3]=6;
 if((scenario>=11&&scenario<=16)||scenario==20){
  std::memset(&stored_sec,0,sizeof stored_sec);stored_sec.peer_addr.val[0]=1;stored_sec.key_size=scenario==20?0:16;stored_sec.ltk_present=1;
  std::memset(&stored_cccd,0,sizeof stored_cccd);stored_cccd.peer_addr.val[0]=1;stored_cccd.chr_val_handle=3;stored_cccd.flags=1;
  const void*blob=scenario==13||scenario==14?static_cast<void*>(&stored_cccd):static_cast<void*>(&stored_sec);
  size_t size=scenario==13||scenario==14?sizeof stored_cccd:sizeof stored_sec;
  std::vector<uint8_t> bytes(static_cast<const uint8_t*>(blob),static_cast<const uint8_t*>(blob)+size);
  if(scenario==16)bytes.push_back(0);
  nvs_entries.push_back({scenario==15?"unknown_0":scenario==13||scenario==14?"cccd_sec_0":"our_sec_0",bytes});
  // Read a synthetic previous-boot fixture. Production never gives a snapshot
  // its nonzero independent qualification record or installs it automatically.
  auto observed=host.inspectStore(false);
  if(scenario<=14||scenario==20){assert(observed.complete);proof=observed.snapshot;proof.qualification_record=42;}
  stack_bad=scenario==12||scenario==14;
 }
 if(scenario==17){BleStoreProof unverified;assert(!host.configureRestore(unverified));
  assert(host.start(true,true)==BleHostState::Failed&&controller_init==0);return 0;}
 if(scenario==18)proof.digest[0]^=1;
 assert(host.configureRestore(proof));
 if(scenario==1) boot_status.format_refused=true;
 if(scenario==2) nvs_error=77;
 if(scenario==3) controller_error=78;
 if(scenario==4) readback_error=79;
 if(scenario==5) task_error=1;
 auto state=host.start(true,true);
 if(scenario==12||scenario==14||scenario==15||scenario==16||scenario==18){
  assert(state==BleHostState::Failed&&host.fault()==BleFault::Store&&deleted==0);
  assert(create_tasks==0);return 0;
 }
 if(scenario==11||scenario==13||scenario==20){
  assert(state==BleHostState::Starting);ble_hs_cfg.sync_cb();assert(host.state()==BleHostState::Ready);
  if(scenario==11||scenario==20){BondIdentity id;id.type=IdentityType::Public;id.verified=true;id.address[0]=1;
   auto admission=host.bondAdmission(id);if(scenario==20){assert(!admission.identity_matches);return 0;}assert(admission.stack_ready&&admission.restore_verified&&admission.refusal_installed&&admission.persistence_allowed&&admission.identity_matches&&admission.existing_verified_identity&&admission.used==1&&admission.capacity==5);}
  return 0;
 }
 if(scenario>=1&&scenario<=5){
  assert(state==BleHostState::Failed);
  assert(create_tasks==(scenario==5?1:0));
  auto initial=controller_init;host.start(true,true);assert(controller_init==initial);return 0;
 }
 assert(state==BleHostState::Starting);
 assert(create_tasks==1&&controller_init==1&&port_init==1&&hci_init==1);
 if(scenario==6){
  host.sealStartup();ble_hs_cfg.sync_cb();assert(host.state()==BleHostState::Failed);
  host.start(true,true);assert(create_tasks==1);return 0;
 }
 ble_hs_cfg.sync_cb();assert(host.state()==BleHostState::Ready);
 assert(!host.inspectStore(true).complete); // Never read live NVS/store concurrently.
 if(scenario==7){
  ble_store_status_event event;event.event_code=BLE_STORE_EVENT_OVERFLOW;
  assert(ble_hs_cfg.store_status_cb(&event,nullptr)!=0);
  assert(deleted==0&&host.fault()==BleFault::Store);return 0;
 }
 if(scenario==8){
  boot_status.format_refused=true;ble_store_value value;
  assert(ble_hs_cfg.store_write_cb(1,&value)!=0);assert(written==0);
  assert(host.fault()==BleFault::Store);return 0;
 }
 if(scenario==23){
  Receiver receiver;BleContext scan;scan.receiver=&receiver;scan.peer=kBlePeers;
  scan.generation=1;scan.phase=BlePhase::Scan;scan.terminal.store(false);
  BleCommand command;command.phase=BlePhase::Scan;command.duration_ms=3000;
  assert(host.submit(command,scan)==0);
  const uint8_t service[]={3,3,0xa6,0xfe};
  for(uint8_t type: {uint8_t(0),uint8_t(1),uint8_t(2),uint8_t(3),uint8_t(4),uint8_t(99)}){
   ble_gap_event ad;ad.type=BLE_GAP_EVENT_DISC;ad.disc.addr.val[0]=1;
   ad.disc.event_type=type;ad.disc.length_data=sizeof service;ad.disc.data=service;
   gap_callbacks[4](&ad,gap_args[4]);
   assert(receiver.latest.kind==BleEventKind::Advertisement);
   auto expected=type<=4?static_cast<BleAdvertisementType>(type):BleAdvertisementType::Unknown;
   assert(receiver.latest.advertisement_type==expected);
   assert(receiver.latest.bytes[2]==0xa6);
  }
  assert(host.cancelScan(scan)==0);pump();assert(host.quiescent(scan));
  assert(host.releaseContext(scan));return 0;
 }
 Receiver receiver;
 auto *link=new BleContext;link->receiver=&receiver;link->peer=0;link->generation=42;link->phase=BlePhase::Connect;link->terminal.store(false);
 BleCommand c;c.phase=BlePhase::Connect;c.identity.type=IdentityType::Public;c.identity.verified=true;c.identity.address[0]=1;c.duration_ms=5000;
 assert(host.submit(c,*link)==0);
 ble_gap_event e;e.type=BLE_GAP_EVENT_CONNECT;e.connect.conn_handle=10;gap_callbacks[0](&e,gap_args[0]);
 if(scenario==9){
  ble_store_status_event full;full.event_code=BLE_STORE_EVENT_FULL;full.full.conn_handle=10;
  assert(ble_hs_cfg.store_status_cb(&full,nullptr)!=0);
  assert(link->sealed.load()&&link->fault.load()==BleFault::Store);
  assert(host.state()==BleHostState::Ready&&host.fault()==BleFault::None&&deleted==0);
 }
 if(scenario==10){
  BleContext procedure;procedure.receiver=&receiver;procedure.peer=0;procedure.generation=42;procedure.phase=BlePhase::Read;procedure.connection.store(10);procedure.terminal.store(false);
  c.phase=BlePhase::Read;c.connection=10;c.handle=3;
  assert(host.submit(c,procedure)==0);
  os_mbuf bytes;bytes.data.assign(65,0x42);
  ble_gatt_attr a;a.handle=3;a.om=&bytes;ble_gatt_error error;
  att_callback(10,&error,&a,att_arg);
  assert(procedure.sealed.load()&&procedure.fault.load()==BleFault::Malformed);
  assert(!host.quiescent(procedure));pump();assert(host.quiescent(procedure));
  assert(host.releaseContext(procedure));assert(bytes.data.size()==65);
 }
 if(scenario==22){
  copy_hook=[&](){ble_gap_event end;end.type=BLE_GAP_EVENT_DISCONNECT;end.disconnect.conn.conn_handle=10;gap_callbacks[0](&end,gap_args[0]);pump();assert(!host.quiescent(*link)&&!host.releaseContext(*link));};
  os_mbuf bytes;bytes.data.assign(1,0x42);ble_gap_event notify;notify.type=BLE_GAP_EVENT_NOTIFY_RX;notify.notify_rx.conn_handle=10;notify.notify_rx.attr_handle=3;notify.notify_rx.om=&bytes;
  gap_callbacks[0](&notify,gap_args[0]);copy_hook=nullptr;assert(host.quiescent(*link));assert(host.releaseContext(*link));delete link;return 0;
 }
 if(scenario==21)queue_full=true;
 e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.conn_handle=10;gap_callbacks[0](&e,gap_args[0]);
 if(scenario==21){assert(queue_wait==0);assert(host.state()==BleHostState::Failed);assert(link->sealed.load()&&link->fault.load()==BleFault::Host);assert(!host.quiescent(*link));pump();assert(!host.quiescent(*link)&&!host.releaseContext(*link));return 0;}
 assert(queue_wait==0);
 assert(!host.quiescent(*link));assert(!host.releaseContext(*link));
 pump();assert(host.quiescent(*link));assert(host.releaseContext(*link));delete link;
 // ASan verifies the boot-lifetime router no longer touches a freed context.
 ble_store_status_event full;full.event_code=BLE_STORE_EVENT_FULL;full.full.conn_handle=99;
 assert(ble_hs_cfg.store_status_cb(&full,nullptr)!=0);assert(deleted==0);
}
'''


class BleEsp32(unittest.TestCase):
    def test_real_adapter_sdk_failures_store_refusal_copy_bounds_and_final_access(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / "standin.h").write_text(STUB)
            includes = ["NimBLEDevice.h", "esp_bt.h", "freertos/task.h", "freertos/queue.h", "mbedtls/sha256.h", "nvs.h",
                        "nimble/nimble/host/include/host/ble_gatt.h",
                        "nimble/nimble/include/nimble/nimble_npl.h",
                        "nimble/esp_port/esp-hci/include/esp_nimble_hci.h",
                        "nimble/nimble/host/include/host/ble_hs.h",
                        "nimble/nimble/host/include/host/ble_store.h",
                        "nimble/nimble/host/src/ble_hs_resolv_priv.h",
                        "nimble/nimble/host/store/config/include/store/config/ble_store_config.h",
                        "nimble/porting/nimble/include/nimble/nimble_port.h",
                        "nimble/porting/nimble/include/os/os_mbuf.h"]
            for name in includes:
                target = temp / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text('#include "standin.h"\n')
            (temp / "harness.cpp").write_text(HARNESS)
            binary = temp / "harness"
            subprocess.run(["clang++", "-std=c++11", "-DARDUINO_ARCH_ESP32", "-fsanitize=address,undefined",
                            "-fno-sanitize-recover=all", "-g", "-O0", "-I", str(temp), "-I", str(ROOT / "include"),
                            "-I", str(ROOT), str(temp / "harness.cpp"), str(ROOT / "src/pairing_reset.cpp"),
                            "-o", str(binary)], check=True)
            for scenario in range(24):
                result = subprocess.run([str(binary), str(scenario)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, f"scenario {scenario}: {result.stderr}")

"""Real host reset behavior; hardware/NVS/radio are synthetic boundaries."""
from pathlib import Path
import subprocess
import re
import sys
from test_nimble_privacy_patch import patch
import tempfile
import unittest
from test_ble_esp32 import STUB, ROOT

INCLUDES = ["NimBLEDevice.h", "esp_bt.h", "freertos/task.h", "freertos/queue.h", "mbedtls/sha256.h", "nvs.h", "esp_timer.h",
            "nimble/nimble/host/include/host/ble_gatt.h", "nimble/nimble/include/nimble/nimble_npl.h",
            "nimble/esp_port/esp-hci/include/esp_nimble_hci.h", "nimble/nimble/host/include/host/ble_hs.h",
            "nimble/nimble/host/include/host/ble_store.h", "nimble/nimble/host/src/ble_hs_resolv_priv.h",
            "nimble/nimble/host/store/config/include/store/config/ble_store_config.h",
            "nimble/nimble/host/store/config/src/ble_store_config_priv.h",
            "nimble/porting/nimble/include/nimble/nimble_port.h", "nimble/porting/nimble/include/os/os_mbuf.h"]
def replace_function(source, name, replacement):
    match=re.search(r'(?m)^.*\b'+name+r'\([^;]*?\)\s*\{',source)
    if not match:raise ValueError(name)
    level=1;i=match.end()
    while level:
        if source[i]=='{':level+=1
        if source[i]=='}':level-=1
        i+=1
    return source[:match.start()]+replacement+source[i:]

BOUNDARY = r'''
extern "C" {
void *reset_sdk_values(int);int *reset_sdk_count(int);int reset_sdk_find(int,const void*);
int reset_sdk_delete(int,const void*);int reset_sdk_dispatch(int,const void*);ble_hs_resolv_entry *reset_sdk_resolving();
void reset_sdk_resolving_count(unsigned);
ble_hs_resolv_entry *ble_hs_resolv_list_find(uint8_t*);
int ble_hs_resolv_list_rmv(uint8_t,uint8_t*);
int ble_rpa_remove_peer_dev_rec(ble_hs_dev_records*);
ble_hs_dev_records *ble_rpa_get_peer_dev_records();int ble_rpa_get_num_peer_dev_records();
}
static const int sdk_types[]={1,2,3,8,7,6,4};
static const char *sdk_prefixes[]={"our_sec_","peer_sec_","cccd_sec_","csfc_sec_","local_irk_","rpa_rec_","p_dev_rec_"};
static const size_t sdk_sizes[]={sizeof(ble_store_value_sec),sizeof(ble_store_value_sec),sizeof(ble_store_value_cccd),sizeof(ble_store_value_csfc),sizeof(ble_store_value_local_irk),sizeof(ble_store_value_rpa_rec),sizeof(ble_hs_dev_records)};
static int sdk_schema(int type){for(int i=0;i<7;++i)if(sdk_types[i]==type)return i;assert(false);return -1;}
static int erase_error=0,commit_error=0,delete_error_type=0,blob_error=0;
static std::string erase_pending;
static std::function<void()> read_hook,delete_hook,commit_hook;
static bool sdk_locked=false;
extern "C" void ble_hs_lock(){assert(!sdk_locked);sdk_locked=true;}
extern "C" void ble_hs_unlock(){assert(sdk_locked);sdk_locked=false;}
extern "C" int get_nvs_max_obj_value(int type){return type==3?32:type==4?6:5;}
extern "C" void get_nvs_key_string(int type,int index,char *name){std::snprintf(name,16,"%s%d",sdk_prefixes[sdk_schema(type)],index);}
extern "C" int get_nvs_db_attribute(int type,int,const void*ram,int count){
 int schema=sdk_schema(type),total=0,missing=-1;size_t length=sdk_sizes[schema];
 for(auto &entry:nvs_entries)if(entry.first.find(sdk_prefixes[schema])==0){
  ++total;bool found=false;
  for(int i=0;i<count;++i)if(entry.second.size()==length&&!std::memcmp(entry.second.data(),static_cast<const uint8_t*>(ram)+i*length,length))found=true;
  if(!found)missing=std::atoi(entry.first.c_str()+std::strlen(sdk_prefixes[schema]));
 }
 return ram?missing:total;
}
extern "C" int reset_nvs_open(const char*name,int mode,unsigned*handle){return nvs_open(name,mode,handle);}
extern "C" int reset_nvs_erase_key(unsigned,const char*name){if(erase_error)return erase_error;erase_pending=name;return 0;}
extern "C" int reset_nvs_commit(unsigned){
 auto hook=commit_hook;if(hook)hook();
 if(commit_error)return commit_error;
 for(auto it=nvs_entries.begin();it!=nvs_entries.end();++it)if(it->first==erase_pending){nvs_entries.erase(it);erase_pending.clear();return 0;}
 return 99;
}
extern "C" void reset_nvs_close(unsigned h){nvs_close(h);}
extern "C" int ble_store_nvs_write(int,const void*){return 99;}
extern "C" int ble_store_nvs_peer_records(int,const void*){return 99;}
static int actual_read(int type,const ble_store_key*key,ble_store_value*value){
 if(readback_error)return readback_error;
 int schema=sdk_schema(type),index=reset_sdk_find(type,key);
 if(type==7){index=-1;const auto *records=static_cast<const ble_store_value_local_irk*>(reset_sdk_values(type));
  for(int i=0;i<*reset_sdk_count(type);++i)if(key->local_irk.addr.type==255?i==key->local_irk.idx:records[i].addr.type==key->local_irk.addr.type&&!std::memcmp(records[i].addr.val,key->local_irk.addr.val,6)){index=i;break;}}
 if(index<0)return BLE_HS_ENOENT;
 std::memcpy(value,static_cast<uint8_t*>(reset_sdk_values(type))+index*sdk_sizes[schema],sdk_sizes[schema]);return 0;
}
extern "C" int reset_sdk_callback_delete(int type,const void*raw){
 const auto *key=static_cast<const ble_store_key*>(raw);
 assert(sdk_locked);
 if(delete_hook)delete_hook();
 if(type==delete_error_type)return 99;
 ++deleted;return reset_sdk_delete(type,key);
}
static int actual_delete(int type,const ble_store_key*key){return reset_sdk_dispatch(type,key);}
static void populate(int type,const void *record){
 int schema=sdk_schema(type),index=(*reset_sdk_count(type))++;size_t size=sdk_sizes[schema];
 std::memcpy(static_cast<uint8_t*>(reset_sdk_values(type))+index*size,record,size);
 nvs_entries.push_back({std::string(sdk_prefixes[schema])+std::to_string(index),std::vector<uint8_t>(static_cast<const uint8_t*>(record),static_cast<const uint8_t*>(record)+size)});
}
'''

def sdk_standin():
    source=STUB.replace('#include <cassert>','#include <cassert>\n#include <cstdio>\n#include <thread>')
    source=re.sub(r'(?m)^static (?:ble_store_value_\w+|int) ble_store_config_[^;]+;\n','',source)
    source=source.replace('static int ble_store_read_our_sec(', 'static int actual_read(int,const ble_store_key*,ble_store_value*);\nstatic int ble_store_read_our_sec(')
    for name in ('ble_hs_resolv_list_find','ble_hs_resolv_list_rmv','ble_rpa_remove_peer_dev_rec','ble_rpa_get_peer_dev_records','ble_rpa_get_num_peer_dev_records'):
        source=replace_function(source,name,'')
    replacements={
      'ble_store_config_delete':'static int actual_delete(int,const ble_store_key*);\nstatic int ble_store_config_delete(int t,const ble_store_key*k){return actual_delete(t,k);}',
      'ble_store_read':'static int actual_read(int,const ble_store_key*,ble_store_value*);\nstatic int ble_store_read(int t,const ble_store_key*k,ble_store_value*v){return actual_read(t,k,v);}',
      'ble_store_util_count':'extern "C" int *reset_sdk_count(int);\nstatic int ble_store_util_count(int t,int*n){*n=t==6?0:*reset_sdk_count(t);return readback_error;}',
    }
    source=replace_function(source,'ble_gap_conn_find','static int ble_gap_conn_find(uint16_t,ble_gap_conn_desc*d){d->peer_id_addr.val[0]=2;d->sec_state.encrypted=d->sec_state.bonded=true;return 0;}')
    for name,kind,typ,extra in [('ble_gattc_disc_svc_by_uuid','svc','ble_gatt_svc','const ble_uuid_t*'),('ble_gattc_disc_all_chrs','chr','ble_gatt_chr','uint16_t,uint16_t')]:
        source=replace_function(source,name,f'static int(*{kind}_cb)(uint16_t,const ble_gatt_error*,const {typ}*,void*);static void *{kind}_arg;\nstatic int {name}(uint16_t,{extra},int(*cb)(uint16_t,const ble_gatt_error*,const {typ}*,void*),void*a){{{kind}_cb=cb;{kind}_arg=a;return 0;}}')
    for name,replacement in replacements.items():source=replace_function(source,name,replacement)
    for name,type in [('ble_store_read_our_sec',1),('ble_store_read_peer_sec',2),('ble_store_read_cccd',3),('ble_store_read_csfc',8),('ble_store_read_local_irk',7),('ble_store_read_rpa_rec',6)]:
        source=replace_function(source,name,f'static int {name}(const void*k,void*v){{return actual_read({type},static_cast<const ble_store_key*>(k),static_cast<ble_store_value*>(v));}}')
    source=source.replace('static int nvs_get_blob(unsigned,const char*name,void*value,size_t*size){','static int blob_error=0;\nstatic std::function<void()> read_hook;\nstatic int nvs_get_blob(unsigned,const char*name,void*value,size_t*size){\n auto hook=read_hook;if(hook)hook();if(blob_error)return blob_error;')
    source += r'''
extern "C" {
extern ble_store_value_sec ble_store_config_our_secs[5],ble_store_config_peer_secs[5];
extern ble_store_value_cccd ble_store_config_cccds[32];
extern ble_store_value_csfc ble_store_config_csfcs[5];
extern ble_store_value_local_irk ble_store_config_local_irks[5];
extern ble_store_value_rpa_rec ble_store_config_rpa_recs[5];
extern int ble_store_config_num_our_secs,ble_store_config_num_peer_secs,ble_store_config_num_cccds,ble_store_config_num_csfcs,ble_store_config_num_local_irks,ble_store_config_num_rpa_recs;
}
'''
    return source+BOUNDARY.replace(',blob_error=0','').replace('static std::function<void()> read_hook,delete_hook,commit_hook;','static std::function<void()> delete_hook,commit_hook;')

HARNESS = r'''
#include "standin.h"
#include "nvs_boot_guard.h"
static ridesync::NvsBootStatus boot_status;
namespace ridesync { NvsBootStatus nvsBootStatus(){return boot_status;} }
#include "src/ble_esp32.cpp"
using namespace ridesync;
#include "src/ble_remote.cpp"
struct Receiver:BleCallbacks{void copied(BleContext&,BleEvent)override{}};
struct Sink:BleResultSink{std::vector<BleResult> values;void result(const BleResult&r)override{values.push_back(r);}};
static BleProfileSpec profile(){BleProfileSpec p;p.service_count=p.endpoint_count=1;p.services[0]=BleUuid::shortUuid(0xfea6);p.endpoints[0].uuid=BleUuid::shortUuid(0x72);p.endpoints[0].properties=10;return p;}
static BondIdentity other(){BondIdentity id;id.verified=true;id.type=IdentityType::Public;id.address[0]=2;return id;}
static void disconnectEvent(unsigned index){ble_gap_event e;e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.conn_handle=10;gap_callbacks[index](&e,gap_args[index]);}
static void failedConnect(unsigned index){ble_gap_event e;e.type=BLE_GAP_EVENT_CONNECT;e.connect.status=BLE_HS_ENOTCONN;gap_callbacks[index](&e,gap_args[index]);}
static void ready(BleCentral&central){
 assert(central.connect(0,1,other(),profile()));central.service(0);
 ble_gap_event e;e.type=BLE_GAP_EVENT_CONNECT;e.connect.conn_handle=10;gap_callbacks[0](&e,gap_args[0]);central.service(1);
 e.type=BLE_GAP_EVENT_ENC_CHANGE;e.enc_change.conn_handle=10;gap_callbacks[0](&e,gap_args[0]);central.service(2);
 ble_gatt_error error;ble_gatt_svc svc;svc.start_handle=1;svc.end_handle=5;svc.uuid.u16.u.type=BLE_UUID_TYPE_16;svc.uuid.u16.value=0xfea6;
 svc_cb(10,&error,&svc,svc_arg);error.status=BLE_HS_EDONE;svc_cb(10,&error,nullptr,svc_arg);pump();central.service(3);
 ble_gatt_chr chr;chr.def_handle=2;chr.val_handle=3;chr.properties=10;chr.uuid.u16.u.type=BLE_UUID_TYPE_16;chr.uuid.u16.value=0x72;
 error.status=0;chr_cb(10,&error,&chr,chr_arg);error.status=BLE_HS_EDONE;chr_cb(10,&error,nullptr,chr_arg);pump();central.service(4);
 assert(central.phase(0)==BlePhase::ReadyForProfile);
}
int main(int argc,char**argv){
 const int scenario=argc>1?std::atoi(argv[1]):0;
 auto &host=Esp32BleHost::instance();boot_status.init_observed=true;
 BleStoreProof proof;proof.qualification_record=42;proof.digest[0]=1;proof.digest[1]=2;proof.digest[2]=3;proof.digest[3]=6;
 if(scenario!=38){assert(host.configureRestore(proof));assert(host.start(true,true)==BleHostState::Starting);
 ble_hs_cfg.sync_cb();assert(host.state()==BleHostState::Ready);}
 BondIdentity id;id.type=IdentityType::Public;id.verified=true;id.address[0]=1;
 ble_addr_t target;target.val[0]=1;
 if(scenario==26){id.type=IdentityType::RandomStatic;id.address[5]=0xc0;target.type=1;target.val[5]=0xc0;}
 if(scenario==25){id.address[5]=0xc0;target.val[5]=0xc0;}
 ble_addr_t foreign;foreign.val[0]=2;
 ble_store_value_sec sec;sec.peer_addr=foreign;populate(1,&sec);populate(2,&sec);
 ble_store_value_cccd cccd;cccd.peer_addr=foreign;cccd.chr_val_handle=3;cccd.flags=1;populate(3,&cccd);
 ble_store_value_csfc csfc;csfc.peer_addr=foreign;populate(8,&csfc);
 ble_store_value_local_irk local;local.addr=foreign;local.irk[0]=42;populate(7,&local);
 ble_store_value_rpa_rec rpa;rpa.peer_addr=foreign;rpa.peer_rpa_addr.type=1;rpa.peer_rpa_addr.val[0]=7;populate(6,&rpa);
 ble_hs_dev_records dev;dev.rec_used=true;dev.peer_sec.peer_addr=foreign;dev.identity_addr[0]=2;dev.rand_addr[0]=7;populate(4,&dev);
 auto *rl=reset_sdk_resolving();rl[1].rl_addr_type=0;rl[1].rl_identity_addr[0]=2;reset_sdk_resolving_count(2);
 auto unrelated=nvs_entries;

 const bool all=scenario==1||(scenario>=8&&scenario!=56);
 if(scenario!=0){
  if(all||scenario==2){sec.peer_addr=target;populate(1,&sec);}
  if(all||scenario==3){sec.peer_addr=target;populate(2,&sec);}
  if(all||scenario==4){cccd.peer_addr=target;populate(3,&cccd);cccd.chr_val_handle=4;populate(3,&cccd);}
  if(all||scenario==5){csfc.peer_addr=target;populate(8,&csfc);}
  if(all||scenario==6){rpa.peer_addr=target;rpa.peer_rpa_addr.val[0]=8;populate(6,&rpa);}
  if(all||scenario==7||scenario==56){dev.peer_sec.peer_addr=target;std::memcpy(dev.identity_addr,target.val,6);dev.rand_addr[0]=8;populate(4,&dev);}
  if(all){rl[2].rl_addr_type=target.type;std::memcpy(rl[2].rl_identity_addr,target.val,6);reset_sdk_resolving_count(3);}
 }
 if(scenario==38){auto observed=host.inspectStore(false);assert(observed.complete);proof=observed.snapshot;proof.qualification_record=42;assert(host.configureRestore(proof));assert(host.start(true,true)==BleHostState::Starting);ble_hs_cfg.sync_cb();assert(host.state()==BleHostState::Ready);}
 if(scenario==40||scenario==41){
  for(int i=3;i<=5;++i){ble_addr_t extra;extra.val[0]=i;
   sec.peer_addr=extra;populate(1,&sec);unrelated.push_back(nvs_entries.back());populate(2,&sec);unrelated.push_back(nvs_entries.back());
   rpa.peer_addr=extra;rpa.peer_rpa_addr.val[0]=i+10;populate(6,&rpa);unrelated.push_back(nvs_entries.back());
   dev.peer_sec.peer_addr=extra;std::memcpy(dev.identity_addr,extra.val,6);dev.rand_addr[0]=i+10;populate(4,&dev);unrelated.push_back(nvs_entries.back());
   rl[i].rl_addr_type=0;rl[i].rl_identity_addr[0]=i;
  }
  reset_sdk_resolving_count(6);
  if(scenario==41){std::swap(rl[2],rl[5]);}
 }
 if(scenario==42){cccd.peer_addr=target;for(int i=0;i<29;++i){cccd.chr_val_handle=100+i;populate(3,&cccd);}}
 if(scenario==54){cccd.peer_addr=foreign;cccd.chr_val_handle=3;populate(3,&cccd);unrelated.push_back(nvs_entries.back());}
 if(scenario==55){
  for(int schema=0;schema<7;++schema){if(schema==4)continue;int type=sdk_types[schema];size_t size=sdk_sizes[schema];auto *records=static_cast<uint8_t*>(reset_sdk_values(type));std::vector<uint8_t> temp(records,records+size);std::memcpy(records,records+size,size);std::memcpy(records+size,temp.data(),size);
   auto first=std::find_if(nvs_entries.begin(),nvs_entries.end(),[&](const std::pair<std::string,std::vector<uint8_t>>&e){return e.first==std::string(sdk_prefixes[schema])+"0";});
   auto second=std::find_if(nvs_entries.begin(),nvs_entries.end(),[&](const std::pair<std::string,std::vector<uint8_t>>&e){return e.first==std::string(sdk_prefixes[schema])+"1";});std::swap(first->second,second->second);unrelated[schema].first=second->first;
  }
  std::swap(rl[1],rl[2]);
 }
 if(scenario==25){sec.peer_addr=target;sec.peer_addr.type=1;populate(1,&sec);}
 if(scenario==27)id.address={};
 if(scenario==28)id.type=IdentityType::UnresolvedPrivate;
 if(scenario==29){id.type=IdentityType::RandomStatic;id.address[5]=0x40;}
 if(scenario==30)id.verified=false;
 if(scenario>=27&&scenario<=30){assert(host.requestBondReset(id,1,1000,0)==BondResetSubmission::Refused);assert(deleted==0);return 0;}
 if(scenario==8)gap_scan=true;
 if(scenario==9)gap_adv=true;
 if(scenario==10)gap_connect=true;
 Receiver receiver;BleContext link;link.receiver=&receiver;link.peer=0;link.generation=42;link.phase=BlePhase::Connect;link.terminal.store(false);
 if(scenario==11||scenario==12){BleCommand c;c.phase=BlePhase::Connect;c.identity=id;assert(host.submit(c,link)==0);
  if(scenario==12){ble_gap_event e;e.type=BLE_GAP_EVENT_CONNECT;e.connect.conn_handle=10;gap_callbacks[0](&e,gap_args[0]);e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.conn_handle=10;gap_callbacks[0](&e,gap_args[0]);}}
 Sink sink;BleCentral central(host,sink);
 if(scenario>=44&&scenario<=50){assert(central.begin(true,true,0));if(scenario==44||scenario==45||scenario==48||scenario==49)ready(central);}
 if(scenario==49){assert(central.read(0,0,5));assert(att_callback);}
 BleContext retained;retained.receiver=&receiver;retained.peer=0;retained.generation=42;retained.phase=BlePhase::Read;retained.connection.store(10);retained.terminal.store(false);
 if(scenario==52){BleCommand c;c.phase=BlePhase::Read;c.connection=10;c.handle=3;assert(host.submit(c,retained)==0);}
 if(scenario==53){BleCommand c;c.phase=BlePhase::Connect;c.identity=id;assert(host.submit(c,link)==0);ble_gap_event e;e.type=BLE_GAP_EVENT_CONNECT;e.connect.conn_handle=10;gap_callbacks[0](&e,gap_args[0]);c.phase=BlePhase::Read;c.connection=10;c.handle=3;assert(host.submit(c,retained)==0);disconnectEvent(0);pump();assert(host.releaseContext(link));}
 if(scenario==13)queue_full=true;
 auto submitted=host.requestBondReset(id,1,1000,0);
 if(scenario==13){assert(submitted==BondResetSubmission::Busy);assert(host.bondResetResult(1,0).releasable);assert(deleted==0&&queue_wait==0);return 0;}
 assert(submitted==BondResetSubmission::Queued);
 assert(!host.bondResetResult(1,0).finished);
 assert(host.requestBondReset(id,2,1000,0)==BondResetSubmission::Busy);
 if(scenario==14)assert(host.cancelBondReset(1));
 if(scenario==15)fake_time=1000;
 if(scenario==16){auto early=host.bondResetResult(1,1000);assert(early.finished&&!early.releasable&&early.timed_out);}
 if(scenario==17)read_hook=[&](){host.cancelBondReset(1);};
 if(scenario==19)erase_error=77;
 if(scenario==20||scenario==56)commit_error=77;
 if(scenario==21)delete_error_type=2;
 if(scenario==22)blob_error=77;
 if(scenario==23)delete_hook=[&](){blob_error=77;};
 if(scenario==24){auto *records=static_cast<ble_hs_dev_records*>(reset_sdk_values(4));records[0].pseudo_addr[0]=1;std::memcpy(nvs_entries[6].second.data(),&records[0],sizeof dev);}
 if(scenario==33)boot_status.format_refused=true;
 if(scenario==34)read_hook=[&](){boot_status.format_refused=true;};
 if(scenario==35)delete_hook=[&](){auto *records=static_cast<ble_store_value_cccd*>(reset_sdk_values(3));records[0].flags=2;std::memcpy(nvs_entries[2].second.data(),&records[0],sizeof cccd);};
 if(scenario==36)nvs_entries.push_back({"unknown_0",{1}});
 if(scenario==39)rl[2].rl_addr_type=1;
 if(scenario==18||scenario==37){
  read_hook=[&](){
   read_hook=nullptr;
   auto pending=host.bondResetResult(1,scenario==37?1000:0);
   assert(!pending.releasable);
   assert(host.requestBondReset(id,2,1000,0)==BondResetSubmission::Busy);
   host.cancelBondReset(1);
  };
 }
 if(scenario>=43&&scenario<=50){
  commit_hook=[&](){
   commit_hook=nullptr;
   assert(sdk_locked);
   BleCommand c;c.phase=BlePhase::Read;c.connection=10;c.handle=3;
   BleContext temporary;temporary.peer=1;temporary.phase=BlePhase::Read;temporary.terminal.store(false);
   assert(host.submit(c,temporary)==kBleHostReserved);
   assert(host.retire(temporary)==kBleHostReserved);
   assert(host.cancelScan(temporary)==kBleHostReserved);
   assert(host.mtu(10)==kBleMtuReserved);
   assert(host.bondAdmission(id).reserved);
   assert(!host.bondResetResult(1,0).releasable);
   if(scenario==44){uint8_t byte=1;assert(central.write(0,0,&byte,1,10));central.service(11);assert(!att_callback);}
   if(scenario==45){assert(central.read(0,0,10));central.service(11);assert(!att_callback);}
   if(scenario==46||scenario==50){assert(central.scan(100,10));central.service(11);assert(!gap_callbacks[4]);if(scenario==46)assert(central.cancelScan());central.service(scenario==50?110:12);assert(!central.cancelScan());}
   if(scenario==47){assert(central.connect(0,1,other(),profile()));central.service(10);assert(connections==0);central.disconnect(0);central.service(11);assert(central.phase(0)==BlePhase::Closed);}
   if(scenario==48||scenario==49){central.disconnect(0);central.service(11);assert(central.phase(0)!=BlePhase::Closed);}
  };
 }
 if(scenario==51){
  std::atomic<unsigned> stage{0};
  commit_hook=[&](){commit_hook=nullptr;assert(sdk_locked);stage.store(1);while(stage.load()!=2)std::this_thread::yield();};
  std::thread worker([](){pump();});while(stage.load()!=1)std::this_thread::yield();
  assert(!host.bondResetResult(1,0).releasable);assert(host.cancelBondReset(1));assert(host.requestBondReset(id,2,1000,0)==BondResetSubmission::Busy);
  BleCommand c;c.phase=BlePhase::Read;BleContext context;assert(host.submit(c,context)==kBleHostReserved);
  stage.store(2);worker.join();assert(host.bondResetResult(1,0).outcome==BondOutcome::Indeterminate);return 0;
 }
 pump();
 if(scenario>=44&&scenario<=50){
  if(scenario==44||scenario==45){central.service(12);assert(att_callback);ble_gatt_error error;ble_gatt_attr attr;attr.handle=3;os_mbuf value;attr.om=&value;att_callback(10,&error,&attr,att_arg);pump();central.service(13);assert(central.phase(0)==BlePhase::ReadyForProfile);central.disconnect(0);}
  if(scenario==44||scenario==45||scenario==48||scenario==49){disconnectEvent(0);assert(central.phase(0)!=BlePhase::Closed);pump();central.service(14);assert(central.phase(0)==BlePhase::Closed);assert(central.connect(0,2,other(),profile()));central.service(15);central.disconnect(0);failedConnect(1);pump();central.service(16);}
  if(scenario==46||scenario==50){assert(central.scan(100,120));assert(gap_callbacks[4]);ble_gap_event e;e.type=BLE_GAP_EVENT_DISC_COMPLETE;gap_callbacks[4](&e,gap_args[4]);pump();central.service(121);}
  if(scenario==47){assert(central.connect(0,2,other(),profile()));central.service(12);assert(connections==1);central.disconnect(0);failedConnect(0);pump();central.service(13);}
  central.stop();central.service(200);assert(central.canDestroy());
 }

 auto result=host.bondResetResult(1,0);assert(result.finished&&result.releasable);std::fprintf(stderr,"outcome=%d error=%d deleted=%d\n",int(result.outcome),result.error,deleted);
 if((scenario>=8&&scenario<=12)||scenario==52||scenario==53){assert(result.outcome==BondOutcome::Busy&&deleted==0);return 0;}
 if(scenario==14||scenario==15||scenario==16||scenario==17||scenario==18||scenario==33||scenario==34||scenario==37){assert(result.outcome==BondOutcome::Refused&&deleted==0);return 0;}
 if(scenario==19||scenario==20||scenario==21||scenario==23||scenario==35||scenario==56){assert(result.outcome==BondOutcome::Indeterminate&&result.mutation&&result.requalification_required);return 0;}
 if(scenario==22||scenario==36){assert(result.outcome==BondOutcome::Error&&deleted==0);return 0;}
 if(scenario==24||scenario==39){assert((result.outcome==BondOutcome::Refused||result.outcome==BondOutcome::Indeterminate)&&deleted==0);return 0;}
 assert(result.outcome==(scenario==0?BondOutcome::Absent:BondOutcome::Removed));
 assert(result.requalification_required==(scenario!=0));
 for(const auto &entry:unrelated){bool found=false;for(const auto &after:nvs_entries)if(after==entry)found=true;assert(found);}
 assert(nvs_entries.size()==unrelated.size()+(scenario==25?1:0));
 assert(rl[1].rl_identity_addr[0]==2);
 assert(host.requestBondReset(id,1,1000,0)==BondResetSubmission::Stale);
 assert(!host.cancelBondReset(1));
}
''' 

class PairingResetEsp32(unittest.TestCase):
    def test_concrete_host_owns_explicit_reset(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / 'standin.h').write_text(sdk_standin())
            fixtures=ROOT/'test/fixtures/nimble'
            sdk_source=(fixtures/'reset_sdk_prefix.c.txt').read_text()
            for name in ('config_reset','nvs_reset','privacy_reset','delete_dispatch'):
                body=(fixtures/(name+'.c.txt')).read_text()
                sdk_source+=patch.guard_privacy(body) if name=='privacy_reset' else body
            sdk_source+=(fixtures/'reset_sdk_suffix.c.txt').read_text()
            (temp/'sdk.c').write_text(sdk_source)
            subprocess.run(['clang','-fno-common','-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-O0','-c',str(temp/'sdk.c'),'-o',str(temp/'sdk.o')],check=True)
            for name in INCLUDES:
                path = temp / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "standin.h"\n')
            source = temp / 'harness.cpp'
            source.write_text(HARNESS)
            binary = temp / 'harness'
            subprocess.run(['clang++', '-std=c++11', '-DARDUINO_ARCH_ESP32', '-fsanitize=address,undefined',
                            '-fno-sanitize-recover=all', '-ffunction-sections', '-g', '-O0', '-I', str(temp), '-I', str(ROOT / 'include'),
                            '-I', str(ROOT), str(source), str(ROOT / 'src/pairing_reset.cpp'), str(ROOT/'src/health_supervisor.cpp'), str(temp/'sdk.o'), '-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections', '-o', str(binary)], check=True)
            for scenario in list(range(57)):
                result = subprocess.run([str(binary),str(scenario)], capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 0, f'scenario {scenario}: {result.stderr}')

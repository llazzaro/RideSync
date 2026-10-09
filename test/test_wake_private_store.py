"""Pinned private NVS functions must consult wake refusal before mutations."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("patch",ROOT/'scripts/patch_nimble_store.py')
patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
PREFIX=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#define MYNEWT_VAL(x) 1
#define BLE_STORE_OBJ_TYPE_PEER_DEV_REC 4
#define NIMBLE_NVS_STR_NAME_MAX_LEN 16
#define NIMBLE_NVS_NAMESPACE "nimble_bond"
#define BLE_HS_ESTORE_FAIL 10
#define BLE_HS_ESTORE_CAP 11
#define BLE_HS_EUNKNOWN 12
#define NVS_READWRITE 1
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
typedef int nvs_handle_t;
typedef int esp_err_t;
struct ble_hs_dev_records {uint8_t peer;};
static int incoming=1,stored_peer=9,reads=0,writes=0,erases=0,commits=0;
int ridesync_ble_wake_store_allowed(int type,const void*key,const void*value){(void)type;(void)key;return !incoming || (value && ((const struct ble_hs_dev_records*)value)->peer!=9);}
static int get_nvs_max_obj_value(int type){(void)type;return 5;}
static int nvs_open(const char*n,int m,int*h){(void)n;(void)m;*h=1;return 0;}
static void nvs_close(int h){(void)h;}
static int nvs_get_blob(int h,const char*k,void*v,size_t*s){(void)h;(void)k;++reads;*s=sizeof(struct ble_hs_dev_records);if(v)((struct ble_hs_dev_records*)v)->peer=stored_peer;return 0;}
static int nvs_erase_key(int h,const char*k){(void)h;(void)k;++erases;return 0;}
static int nvs_commit(int h){(void)h;++commits;return 0;}
static void get_nvs_key_string(int t,int i,char*k){(void)t;(void)i;strcpy(k,"p_dev_rec_1");}
static int get_nvs_db_attribute(int t,int i,const void*v,int n){(void)t;(void)i;(void)v;(void)n;return 1;}
static int ble_nvs_write_key_value(char*k,const void*v,size_t s){(void)k;(void)v;(void)s;++writes;return 0;}
'''
SUFFIX=r'''
int main(int argc,char**argv){(void)argc;int mode=atoi(argv[1]);struct ble_hs_dev_records record={9};
 if(mode==0){assert(ble_store_nvs_peer_records(4,&record)==10);assert(writes==0&&reads==0);}
 if(mode==1){assert(ble_nvs_delete_value(4,1)==10);assert(erases==0&&commits==0);}
 if(mode==2){record.peer=1;assert(ble_store_nvs_peer_records(4,&record)==0&&writes==1);stored_peer=1;assert(ble_nvs_delete_value(4,1)==0&&erases==1);}
 if(mode==3){incoming=0;assert(ble_store_nvs_peer_records(4,&record)==0&&writes==1);assert(ble_nvs_delete_value(4,1)==0&&erases==1);}
 if(mode==4){assert(get_nvs_peer_record("p_dev_rec_1",&record)==10);}
 return 0;}
'''
class WakePrivateStore(unittest.TestCase):
 def test_guard_reapplication_and_drift_refusal(self):
  for name,guard,correct in [('private_store.c.txt',patch.guard_wake_private,patch.corrected),('private_ram.c.txt',patch.guard_wake_privacy,patch.corrected_privacy),('security_rx.c.txt',patch.guard_wake_smp,patch.corrected_smp)]:
   source=(ROOT/'test/fixtures/wake'/name).read_text();fixed=guard(source)
   self.assertEqual(guard(fixed),fixed)
   with self.assertRaises(RuntimeError):correct(source)
   with self.assertRaises(RuntimeError):guard('unrecognized source')

 def test_actual_private_persistence_refusal(self):
  source=(ROOT/'test/fixtures/wake/private_store.c.txt').read_text()
  if hasattr(patch,'guard_wake_private'):source=patch.guard_wake_private(source)
  with tempfile.TemporaryDirectory() as directory:
   temp=Path(directory);probe=temp/'probe.c';binary=temp/'probe';probe.write_text(PREFIX+source+SUFFIX)
   subprocess.run(['clang','-fsanitize=address,undefined','-fno-sanitize-recover=all','-O0','-g',str(probe),'-o',str(binary)],check=True)
   for mode in range(5):
    run=subprocess.run([str(binary),str(mode)],capture_output=True,text=True)
    self.assertEqual(run.returncode,0,f'mode {mode}: {run.stderr}')

    # Guard application is repeatable; full install additionally checks exact SHA.

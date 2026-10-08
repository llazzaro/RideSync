"""Exercise pinned privacy removal at the actual full table bound."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('patch',ROOT/'scripts/patch_nimble_store.py')
patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
PREFIX = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define MYNEWT_VAL(x) V_##x
#define V_BLE_STORE_MAX_BONDS 5
#define BLE_DEV_ADDR_LEN 6
#define BLE_RESOLV_LIST_SIZE 6
#define BLE_ADDR_RANDOM 1
#define BLE_HS_ENOENT 12
#define BLE_HS_EUNKNOWN 13
#define BLE_HS_ENOMEM 14
#define BLE_HS_EINVAL 15
#define BLE_HS_LOG(...) ((void)0)
struct ble_hs_resolv_entry { uint8_t rl_addr_type,rl_identity_addr[6],rl_peer_rpa[6],rl_pseudo_id[6],rl_peer_irk[16],rl_local_irk[16]; };
struct { unsigned rl_cnt; } g_ble_hs_resolv_data;
struct ble_hs_resolv_entry g_ble_hs_resolv_list[6];
struct ble_hs_dev_records { uint8_t identity_addr[6],pseudo_addr[6]; } peer_dev_rec[6];
int ble_store_num_peer_dev_rec;
void ble_rpa_replace_id_with_rand_addr(uint8_t*a,uint8_t*b){}
int ble_store_persist_peer_records(){return 0;}
void swap_buf(uint8_t*a,const uint8_t*b,int n){memcpy(a,b,n);}
int is_irk_nonzero(uint8_t*a){return 0;}
struct ble_hs_dev_records *ble_rpa_find_peer_dev_by_irk(uint8_t*a){return NULL;}
void ble_hs_resolv_gen_priv_addr(struct ble_hs_resolv_entry*r,int p){}
'''
SUFFIX = r'''
int main(int argc,char **argv){
 int scenario=argc>1?atoi(argv[1]):0;
 g_ble_hs_resolv_data.rl_cnt=scenario>=3?5:6;
 for(int i=1;i<6;++i){g_ble_hs_resolv_list[i].rl_addr_type=0;g_ble_hs_resolv_list[i].rl_identity_addr[0]=i;}
 uint8_t target[6]={scenario==0?5:scenario==1?1:3};
 if(scenario==3){
  g_ble_hs_resolv_list[2].rl_addr_type=1;g_ble_hs_resolv_list[2].rl_identity_addr[0]=4;target[0]=4;
  assert(ble_hs_is_on_resolv_list(target,0)==4 && "typed identity lookup");
  assert(ble_hs_is_on_resolv_list(target,1)==2);
  assert(ble_hs_resolv_list_rmv(0,target)==0);
  assert(g_ble_hs_resolv_list[2].rl_addr_type==1 && "foreign typed alias must survive");return 0;
 }
 if(scenario==4){
  target[0]=4;g_ble_hs_resolv_list[1].rl_peer_rpa[0]=4;
  assert(ble_hs_is_on_resolv_list(target,0)==4 && "public identity cannot match random RPA");
  assert(ble_hs_is_on_resolv_list(target,1)==1 && "RPA belongs to random address domain");return 0;
 }
 if(scenario==5){
  uint8_t command[39]={1,4};
  assert(ble_hs_resolv_list_add(command)==0 && "different identity type is not duplicate");
  g_ble_hs_resolv_data.rl_cnt=5;
  command[0]=0;assert(ble_hs_resolv_list_add(command)==BLE_HS_EINVAL);
  g_ble_hs_resolv_list[1].rl_peer_rpa[0]=9;command[0]=1;command[1]=9;
  assert(ble_hs_resolv_list_add(command)==BLE_HS_EINVAL);
  command[0]=0;assert(ble_hs_resolv_list_add(command)==0);return 0;
 }
 assert(ble_hs_resolv_list_rmv(0,target)==0);
 assert(g_ble_hs_resolv_data.rl_cnt==5);
 for(int i=1;i<5;++i)assert(g_ble_hs_resolv_list[i].rl_identity_addr[0]==(i>=target[0]?i+1:i));
}
'''

class PrivacyPatch(unittest.TestCase):
    def test_full_table_targeted_remove_is_memory_safe(self):
        fixture=(ROOT/'test/fixtures/nimble/privacy_reset.c.txt').read_text()+(ROOT/'test/fixtures/nimble/privacy_add.c.txt').read_text()
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'probe.c';binary=Path(directory)/'probe'
            source.write_text(PREFIX+patch.guard_privacy(fixture)+SUFFIX)
            subprocess.run(['clang','-fno-common','-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-O0',str(source),'-o',str(binary)],check=True)
            for scenario in range(6):
                result=subprocess.run([str(binary),str(scenario)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
            source.write_text(PREFIX+fixture+SUFFIX)
            subprocess.run(['clang','-fno-common','-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-O0',str(source),'-o',str(binary)],check=True)
            for scenario,reason in [(0,'AddressSanitizer'),(3,'typed identity lookup'),(4,'public identity cannot match random RPA'),(5,'different identity type is not duplicate')]:
                result=subprocess.run([str(binary),str(scenario)],capture_output=True,text=True)
                self.assertNotEqual(result.returncode,0,f'upstream regression missing: {scenario}')
                self.assertIn(reason,result.stderr)
                print(f'upstream exact-body RED scenario={scenario} exit={result.returncode}: {reason}')
        self.assertEqual(patch.guard_privacy(patch.guard_privacy(fixture)),patch.guard_privacy(fixture))
        with self.assertRaises(RuntimeError):patch.corrected_privacy(fixture)
        with self.assertRaises(RuntimeError):patch.guard_privacy("drift")

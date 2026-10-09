"""Actual pinned RAM privacy mutations before CONNECT must be refused."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_ble_esp32 import ROOT, STUB
from test_wake_private_store import patch
PREFIX=r'''
#define BLE_HS_EUNKNOWN 30
#define BLE_DEV_ADDR_LEN 6
#define BLE_HS_LOG(...) ((void)0)
static ble_hs_dev_records peer_dev_rec[6];
static int ble_store_num_peer_dev_rec=0,persisted=0,incoming=1;
static bool resolvable=true;
static int ble_store_persist_peer_records(){++persisted;return 0;}
static bool is_rpa_resolvable_by_peer_rec(ble_hs_dev_records*,uint8_t*){return resolvable;}
extern "C" int ridesync_ble_wake_store_allowed(int,const void*,const void*value){return !incoming || (value && static_cast<const ble_hs_dev_records*>(value)->peer_sec.peer_addr.val[0]!=9);}
extern "C" int ridesync_ble_wake_peer_allowed(uint8_t,const uint8_t*address){return !incoming || address[0]!=9;}
'''
SUFFIX=r'''
int main(int argc,char**argv){int mode=std::atoi(argv[1]);ble_store_num_peer_dev_rec=1;peer_dev_rec[0].rec_used=true;peer_dev_rec[0].peer_sec.peer_addr.val[0]=9;peer_dev_rec[0].identity_addr[0]=9;peer_dev_rec[0].rand_addr[0]=2;uint8_t address[6]={9};uint8_t type=1;
 if(mode==0){assert(ble_rpa_resolv_add_peer_rec(address)==BLE_HS_ESTORE_FAIL);assert(ble_store_num_peer_dev_rec==1);}
 if(mode==1){assert(ble_rpa_remove_peer_dev_rec(&peer_dev_rec[0])==BLE_HS_ESTORE_FAIL);assert(ble_store_num_peer_dev_rec==1&&peer_dev_rec[0].rec_used);}
 if(mode==2||mode==3){resolvable=mode==2;assert(ble_rpa_find_rl_from_peer_records(address,&type)==nullptr);assert(ble_store_num_peer_dev_rec==1&&peer_dev_rec[0].rand_addr[0]==2&&persisted==0);}
 if(mode==4){peer_dev_rec[0].peer_sec.peer_addr.val[0]=1;assert(ble_rpa_remove_peer_dev_rec(&peer_dev_rec[0])==0&&ble_store_num_peer_dev_rec==0&&persisted==1);}
 if(mode==5){incoming=0;assert(ble_rpa_remove_peer_dev_rec(&peer_dev_rec[0])==0&&persisted==1);}
 if(mode==6){address[0]=1;assert(ble_rpa_resolv_add_peer_rec(address)==0&&ble_store_num_peer_dev_rec==2);}
 return 0;}
'''
class WakePrivateRam(unittest.TestCase):
 def test_actual_preconnect_privacy_mutations(self):
  source=(ROOT/'test/fixtures/wake/private_ram.c.txt').read_text()
  if hasattr(patch,'guard_wake_privacy'):source=patch.guard_wake_privacy(source)
  stub=STUB.replace('static int ble_rpa_remove_peer_dev_rec(ble_hs_dev_records*){return 0;}','')
  with tempfile.TemporaryDirectory() as directory:
   temp=Path(directory);probe=temp/'probe.cpp';binary=temp/'probe';probe.write_text(stub+PREFIX+source+SUFFIX)
   subprocess.run(['clang++','-std=c++11','-fsanitize=address,undefined','-fno-sanitize-recover=all','-O0','-g',str(probe),'-o',str(binary)],check=True)
   for mode in range(7):
    with self.subTest(mode=mode):
     run=subprocess.run([str(binary),str(mode)],capture_output=True,text=True)
     self.assertEqual(run.returncode,0,run.stderr)

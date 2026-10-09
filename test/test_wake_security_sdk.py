"""Pinned incoming SMP dispatch is refused before the delayed CONNECT callback."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_ble_esp32 import ROOT, STUB
from test_wake_private_store import patch
PREFIX=r'''
#define STATS_INC(...) ((void)0)
#define BLE_HS_CONN_HANDLE_NONE 0xffff
#define BLE_HS_EBADDATA 40
#define BLE_HS_DBG_ASSERT(x) assert(x)
#define BLE_SM_OP_PAIR_FAIL 5
struct ble_sm_result {int app_status=0;};
struct ble_l2cap_chan {uint16_t handle=19;os_mbuf*rx_buf=nullptr;};
using ble_sm_rx_fn=void(uint16_t,os_mbuf**,ble_sm_result*);
static int incoming=1,dispatched=0;
extern "C" int ridesync_ble_wake_security_allowed(uint16_t handle){return !incoming || handle==10;}
static uint16_t ble_l2cap_get_conn_handle(ble_l2cap_chan*chan){return chan->handle;}
static void os_mbuf_adj(os_mbuf*,int){}
static void dispatch(uint16_t,os_mbuf**,ble_sm_result*){++dispatched;}
static ble_sm_rx_fn *ble_sm_dispatch_get(uint8_t){return dispatch;}
static void ble_sm_process_result(uint16_t,ble_sm_result*,bool){}
'''
SUFFIX=r'''
int main(int argc,char**argv){int mode=std::atoi(argv[1]);os_mbuf bytes;bytes.data.push_back(1);ble_l2cap_chan channel;channel.rx_buf=&bytes;
 if(mode==0){assert(ble_sm_rx(&channel)==BLE_HS_ENOTSUP&&dispatched==0&&bytes.data.size()==1);}
 if(mode==1){channel.handle=10;assert(ble_sm_rx(&channel)==0&&dispatched==1);}
 if(mode==2){incoming=0;assert(ble_sm_rx(&channel)==0&&dispatched==1);}
 return 0;}
'''
class WakeSecuritySdk(unittest.TestCase):
 def test_actual_smp_preconnect_refusal(self):
  source=(ROOT/'test/fixtures/wake/security_rx.c.txt').read_text()
  if hasattr(patch,'guard_wake_smp'):source=patch.guard_wake_smp(source)
  with tempfile.TemporaryDirectory() as directory:
   temp=Path(directory);probe=temp/'probe.cpp';binary=temp/'probe';probe.write_text(STUB+PREFIX+source+SUFFIX)
   subprocess.run(['clang++','-std=c++11','-fsanitize=address,undefined','-fno-sanitize-recover=all','-O0','-g',str(probe),'-o',str(binary)],check=True)
   for mode in range(3):
    run=subprocess.run([str(binary),str(mode)],capture_output=True,text=True)
    self.assertEqual(run.returncode,0,run.stderr)

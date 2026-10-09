"""Actual boot/host/proof-owner code; NVS boundaries and previous-boot evidence synthetic."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_ble_esp32 import STUB, ROOT
from test_pairing_reset_esp32 import INCLUDES

HARNESS = r'''
#include "standin.h"
#include "nvs_boot_guard.h"
#include <fstream>
#include <iterator>
static ridesync::NvsBootStatus boot_status;
namespace ridesync { NvsBootStatus nvsBootStatus(){return boot_status;} }
#include "src/ble_esp32.cpp"
#include "src/ble_wake_esp32.cpp"
#include "src/pairing_proof_esp32.cpp"
using namespace ridesync;
// Independent test encoding, not production codec output used to validate itself.
std::vector<uint8_t> retained(uint32_t floor,unsigned version=1){
 std::vector<uint8_t>b{'R','B','P','R',uint8_t(version),0,0,0,0,0,0,0,0,0,0,0};
 for(unsigned i=0;i<4;++i)b[8+i]=uint8_t(floor>>(8*i));
 uint32_t c=~0U;for(unsigned i=0;i<12;++i){c^=b[i];for(unsigned bit=0;bit<8;++bit)c=(c>>1)^((0U-(c&1))&0xedb88320U);}c=~c;
 for(unsigned i=0;i<4;++i)b[12+i]=uint8_t(c>>(8*i));return b;
}
int main(int argc,char**argv){
 assert(argc==3);const int mode=std::atoi(argv[1]);boot_status.init_observed=true;
 auto &owner=pairingProofMaintenance();auto &host=Esp32BleHost::instance();
 BleStoreProof proof;proof.qualification_record=42;proof.digest[0]=1;proof.digest[1]=2;proof.digest[2]=3;proof.digest[3]=6;
 if(mode==17||mode==18){std::ifstream in(argv[2],std::ios::binary);proof_marker.assign(std::istreambuf_iterator<char>(in),{});assert(proof_marker==retained(42));if(mode==18)proof.qualification_record=43;}
 if(mode==19){proof_marker=retained(41);proof_marker[12]^=1;}
 if(mode==20)proof_marker=retained(41,2);
 if(mode==21)proof_marker={1,2,3};
 if(mode==22)proof_open_error=77;
 if(mode==23){proof_marker=retained(41);proof_read_error=77;}
 if(mode==24||mode==35)proof.qualification_record=UINT32_MAX;
 if(mode==28)proof_marker=retained(40);
 if(mode==29)proof_marker=retained(0);
 if(mode==35)proof_marker=retained(UINT32_MAX-1);
 if(mode==36){proof_marker=retained(UINT32_MAX);proof.qualification_record=UINT32_MAX;}
 assert(host.configureRestore(proof)); // Staging cannot itself grant boot admission.
 if(mode!=16)owner.beginOwner();
 const auto state=host.start(true,true);
 if(mode==16||mode==17||(mode>=19&&mode<=23)||mode==29||mode==36){assert(state==BleHostState::Failed&&controller_init==0&&create_tasks==0);return 0;}
 assert(state==BleHostState::Starting);ble_hs_cfg.sync_cb();assert(host.state()==BleHostState::Ready);
 BleStoreProof admitted;assert(host.admittedProof(admitted));assert(admitted.qualification_record==proof.qualification_record&&admitted.digest==proof.digest&&admitted.counts==proof.counts);
 if(mode==18){assert(owner.restorationAllowed(43)&&!owner.restorationAllowed(42));return 0;}
 uint32_t now=(mode==8)?UINT32_MAX-10:0;fake_time=now;const uint32_t deadline=now+100;
 auto requested=admitted;
 if(mode==13)++requested.qualification_record;
 if(mode==14)requested.digest[0]^=1;
 if(mode==15)++requested.counts[0];
 const auto reads_before=proof_reads;
 const auto submission=owner.request(1,requested,deadline,now);
 assert(proof_reads==reads_before&&proof_sets==0); // Request/poll do no NVS IO.
 if(mode==13||mode==14||mode==15||mode==24||mode==35){assert(submission==BondResetSubmission::Refused&&proof_sets==0);return 0;}
 assert(submission==BondResetSubmission::Queued);
 assert(owner.request(2,admitted,deadline,now)==BondResetSubmission::Busy);
 assert(!owner.result(1).finished&&!owner.release(1));
 if(mode==1||mode==34)proof_set_error=77; // Synthetic SDK exposes bytes despite failed set.
 if(mode==2)proof_commit_error=77;
 if(mode==3)proof_read_error=77;
 if(mode==4)proof_readback_bad=true;
 if(mode==5)proof_open_error=77;
 if(mode==6)assert(owner.cancel(1));
 if(mode==7||mode==8)fake_time=deadline;
 if(mode==9)proof_set_hook=[&](){assert(!owner.result(1).releasable);assert(owner.cancel(1));};
 if(mode==10)proof_commit_hook=[&](){assert(owner.cancel(1));};
 if(mode==11)proof_read_hook=[&](){if(proof_sets){assert(owner.cancel(1));}};
 if(mode==12)proof_close_hook=[&](){if(proof_sets){assert(!owner.result(1).finished&&!owner.release(1));assert(owner.request(2,admitted,deadline,now)==BondResetSubmission::Busy);assert(owner.cancel(1));}};
 if(mode==28)proof_marker=retained(39); // Valid-looking rollback is not silently repaired.
 if(mode==30)proof_read_hook=[&](){if(proof_sets)proof_marker.push_back(0);};
 if(mode==31)proof_set_hook=[&](){boot_status.format_refused=true;};
 if(mode==32)boot_status.format_refused=true;
 if(mode==33)proof_commit_hook=[&](){fake_time=deadline+1;};
 owner.service();const auto result=owner.result(1);assert(result.finished&&result.releasable&&result.operation==1);
 assert(controller_init==1&&deleted==0); // Marker path never deletes camera data.
 if(mode==0||mode==11||mode==12||mode==25||mode==26||mode==27){assert(result.durable&&result.write_attempted&&proof_marker==retained(42)&&proof_sets==1&&proof_commits==1&&proof_closes>=1);assert(!owner.restorationAllowed(42)&&owner.restorationAllowed(43));}
 else assert(!result.durable);
 if(mode==6||mode==9||mode==10||mode==11||mode==12)assert(result.cancelled);
 if(mode==7||mode==8||mode==33)assert(result.timed_out);
 if(mode==6||mode==7||mode==8||mode==28||mode==32)assert(!result.write_attempted&&proof_sets==0);
 if(mode==1||mode==2||mode==9||mode==10||mode==31||mode==33||mode==34)assert(result.write_attempted);
 assert(owner.release(1));
 if(mode==34){proof_set_error=0;assert(proof_marker==retained(42));assert(owner.request(2,admitted,deadline,now)==BondResetSubmission::Refused&&"uncertain bytes cannot launder same-boot success");}
 if(mode==25||mode==26||mode==27){if(mode==25)proof_marker=retained(41);if(mode==26)proof_marker.clear();assert(owner.request(2,admitted,deadline,now)==BondResetSubmission::Queued);owner.service();auto second=owner.result(2);assert(second.finished&&second.releasable);if(mode==27){assert(second.durable&&!second.write_attempted&&proof_sets==1);}else{assert(!second.durable&&!second.write_attempted&&proof_sets==1&&"no repair of rollback or missing marker");}assert(owner.release(2));}
 if(mode==0){std::ofstream out(argv[2],std::ios::binary);out.write(reinterpret_cast<const char*>(proof_marker.data()),proof_marker.size());assert(out.good());}
 std::fprintf(stderr,"proof mode=%d durable=%d attempted=%d cancelled=%d timeout=%d sets=%u commits=%u\n",mode,result.durable,result.write_attempted,result.cancelled,result.timed_out,proof_sets,proof_commits);
}
'''

class PairingProofEsp32(unittest.TestCase):
    def test_actual_boot_marker_owner_and_durable_denial(self):
        with tempfile.TemporaryDirectory() as directory:
            temp=Path(directory)
            (temp/'standin.h').write_text(STUB)
            for name in INCLUDES:
                p=temp/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "standin.h"\n')
            (temp/'harness.cpp').write_text(HARNESS)
            binary=temp/'proof'
            subprocess.run(['clang++','-std=c++11','-DARDUINO_ARCH_ESP32','-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-O0','-I',str(temp),'-I',str(ROOT/'include'),'-I',str(ROOT),str(temp/'harness.cpp'),str(ROOT/'src/pairing_reset.cpp'),str(ROOT/'src/wake_radio_policy.cpp'),str(ROOT/'src/wake_manager.cpp'),str(ROOT/'src/insta360_wake_encoder.cpp'),'-o',str(binary)],check=True)
            retained=temp/'retained.bin'
            for mode in range(37):
                run=subprocess.run([str(binary),str(mode),str(retained)],capture_output=True,text=True,timeout=15)
                self.assertEqual(run.returncode,0,f'mode {mode}: {run.stderr}')
                print(f'proof scenario={mode} exit={run.returncode}: {run.stderr.strip()}')

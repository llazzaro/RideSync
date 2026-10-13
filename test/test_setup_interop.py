"""Host client against the actual C++ setup service and persistence owner."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.setup.protocol import Client, decode_cameras, encode_cameras

ROOT = Path(__file__).resolve().parents[1]
PROGRAM = r"""
#include "setup_service.h"
#include <iostream>
#include <string>
using namespace ridesync;
struct Store : ConfigStore {
  ConfigRecord slots[2];
  bool allowed() const override {return true;}
  StoreResult read(unsigned i, ConfigRecord &out) override {
    out=slots[i];return {out.size?StoreStatus::Ok:StoreStatus::Missing};
  }
  StoreResult write(unsigned i,const ConfigRecord &in) override {slots[i]=in;return {};}
};
int main() {
 Store store; ConfigPersistence persistence(store);
 SettingsPublication publication; SettingsRequests requests; SettingsSavePublication results;
 ConfigBootstrap owner(persistence,publication,requests,7,{},nullptr,&results);
 SetupService service; service.reset(0,7,42,50); service.setIsolationAvailable(true);
 owner.start(false,true); SettingsSnapshot snapshot; publication.take(snapshot);service.setSnapshot(snapshot);
 char response[kSetupFrameBytes+1];
 auto drain=[&](){while(service.takeResponse(response,sizeof response))std::cout<<response<<std::endl;};
 uint32_t now=0;std::string line;
 while(std::getline(std::cin,line)) {
  now+=10;for(char byte:line)service.feed(byte,now);service.feed('\n',now);drain();
  if(service.isolationRequested()){service.acknowledgeIsolation(true);service.tick(now);drain();}
  SetupSaveDispatch save;
  if(service.takeSave(save)) {
   save.save.session=save.session;save.save.request=save.request;requests.put(save.save);
   owner.service(now,false,true);SettingsSaveResult result;
   while(results.take(result))service.completeSave(result.session,result.request,result.outcome,result.result,result.epoch,result.generation);
   owner.service(now+1000,false,true);
   if(publication.take(snapshot))service.setSnapshot(snapshot);
   owner.service(now+1100,false,true);
   while(results.take(result))service.completeSave(result.session,result.request,result.outcome,result.result,result.epoch,result.generation);
   drain();
  }
  service.ownerReleased(true);service.tick(now);drain();
 }
}
"""


class ProcessSerial:
    def __init__(self, process):
        self.process = process

    def write(self, data):
        self.process.stdin.write(data)
        self.process.stdin.flush()
        return len(data)

    def read(self, size):
        return self.process.stdout.read(size)


class SetupInterop(unittest.TestCase):
    def test_actual_wire_save_and_readonly_recovery(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            source = temp / "server.cpp"
            source.write_text(PROGRAM)
            names = [
                "setup_service",
                "setup_discovery",
                "config_bootstrap",
                "config_storage",
                "config",
                "button_manager",
                "ble_remote",
                "pairing_reset",
                "health_supervisor",
                "storage",
                "motion_estimator",
                "session_clock",
            ]
            binary = temp / "server"
            subprocess.run(
                [
                    "c++",
                    "-std=c++11",
                    "-pthread",
                    "-I",
                    str(ROOT / "include"),
                    str(source),
                    *[str(ROOT / "src" / (name + ".cpp")) for name in names],
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            with subprocess.Popen(
                [str(binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE
            ) as process:
                try:
                    client = Client(ProcessSerial(process))
                    self.assertEqual(client.command("HELLO").payload, "challenge=42")
                    self.assertEqual(client.command("ENTER", "42 PARKED").outcome, "Ok")
                    self.assertEqual(client.command("DRAFT_BEGIN").outcome, "Ok")
                    candidate = {
                        "capacity": 4,
                        "cameras": [
                            {
                                "family": 2,
                                "model": 4,
                                "address_type": 1,
                                "enabled": False,
                                "gps_telemetry": False,
                                "name": "Front",
                                "identifier": "01:23:45:67:89:AB",
                                "wake_identifier": "",
                            }
                        ],
                    }
                    self.assertEqual(
                        client.command("DRAFT_SET", encode_cameras(candidate)).outcome,
                        "Ok",
                    )
                    self.assertEqual(client.command("VALIDATE").outcome, "Ok")
                    self.assertEqual(client.command("DISPATCH").outcome, "Ok")
                    self.assertEqual(client.generation, 2)
                    self.assertIn("status=Durable", client.command("STATUS").payload)
                    self.assertEqual(
                        decode_cameras(client.command("INSPECT").payload), candidate
                    )
                    self.assertEqual(client.command("EXIT").outcome, "Ok")
                    recovered = Client(ProcessSerial(process))
                    hello = recovered.command("HELLO", "RECOVERY")
                    self.assertTrue(hello.payload.startswith("challenge="))
                    self.assertEqual(
                        recovered.command(
                            "RECOVER", hello.payload[10:] + " PARKED"
                        ).outcome,
                        "Ok",
                    )
                    self.assertIn("status=Durable", recovered.command("STATUS").payload)
                    self.assertEqual(
                        decode_cameras(recovered.command("INSPECT").payload), candidate
                    )
                    self.assertEqual(recovered.command("EXIT").outcome, "Ok")
                finally:
                    process.stdin.close()
                    process.wait(timeout=10)

"""Guard behavior with IDF 4.4.7 ABI stand-ins; no installed SDK needed.

The structure/signatures/constants mirror esp_partition.h, nvs_flash.h,
 nvs.h and esp_err.h from the pinned Arduino package. This host harness
 models the audited initArduino fallback, not real SDK recovery or flash.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SDK = r'''
#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_ERR_NOT_SUPPORTED=0x106;
constexpr int ESP_ERR_NVS_NOT_INITIALIZED=0x1101;
constexpr int ESP_ERR_NVS_NO_FREE_PAGES=0x110d, ESP_ERR_NVS_NEW_VERSION_FOUND=0x1110;
struct esp_flash_t;
enum esp_partition_type_t { ESP_PARTITION_TYPE_APP=0, ESP_PARTITION_TYPE_DATA=1 };
enum esp_partition_subtype_t { ESP_PARTITION_SUBTYPE_DATA_NVS=2, ESP_PARTITION_SUBTYPE_DATA_FAT=0x81 };
struct esp_partition_t {
 esp_flash_t *flash_chip; esp_partition_type_t type; esp_partition_subtype_t subtype;
 uint32_t address, size; char label[17]; bool encrypted;
};
extern "C" esp_err_t esp_partition_erase_range(const esp_partition_t *, size_t, size_t);
extern "C" esp_err_t nvs_flash_init();
'''
HARNESS = r'''
#include "sdk.h"
#include "nvs_boot_guard.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <initializer_list>
static int next_init=ESP_OK, init_calls=0, erase_calls=0;
static const esp_partition_t *seen_partition;
static size_t seen_offset, seen_size;
extern "C" esp_err_t __real_nvs_flash_init() {
 ++init_calls;
 assert(ridesync::nvsBootStatus().init_in_progress);
 assert(!ridesync::nvsBootStatus().persistenceAllowed());
 return next_init;
}
extern "C" esp_err_t __real_esp_partition_erase_range(const esp_partition_t *p, size_t o, size_t s) {
 ++erase_calls; seen_partition=p; seen_offset=o; seen_size=s; return 0x102;
}
// Host calls wrapper ABI directly (portable to macOS); final ESP32 ELF proves GNU wrapping.
extern int call_init();
extern int call_erase(const esp_partition_t *, size_t, size_t);
int main(int argc, char **argv) {
 using namespace ridesync;
 int scenario=argv[1][0]-'0';
 assert(!nvsBootStatus().persistenceAllowed());
 assert(!nvsBootStatus().init_observed);
 esp_partition_t p{nullptr, ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS,
 0x9000, 0x5000, "nvs", false};
 if(scenario==0 || scenario==1) {
  next_init=scenario==0 ? 0x110d : 0x1110;
  // Audited Arduino initArduino control flow before setup.
  int err=call_init();
  if(err==0x110d || err==0x1110) {
   err=call_erase(&p,0,p.size);
   if(err==0) err=call_init();
  }
  assert(err==0x106 && erase_calls==0 && init_calls==1);
  auto status=nvsBootStatus();
  assert(status.init_observed && status.first_init_failure==next_init);
  assert(status.last_init_result==next_init && status.format_refused);
  assert(status.refusal_error==0x106 && !status.persistenceAllowed());
  next_init=ESP_OK; assert(call_init()==ESP_OK);
  status=nvsBootStatus();
  assert(status.last_init_result==0 && status.first_init_failure==(scenario==0?0x110d:0x1110));
  assert(!status.persistenceAllowed());
 } else if(scenario==2) {
  assert(call_init()==0 && nvsBootStatus().persistenceAllowed());
  for(const char *label : {"nvs", "ridesync_cfg", "other"}) {
   std::strcpy(p.label,label);
   assert(call_erase(&p,0,0x5000)==0x106);
  }
  assert(erase_calls==0 && !nvsBootStatus().persistenceAllowed());
 } else if(scenario==3) {
  struct Range { size_t offset, size; } ranges[]={{0,4096},{4096,4096},{0,0},
   {1,0x5000},{0,0x5001},{0x5000,4096},{std::numeric_limits<size_t>::max(),4096}};
  for(auto range:ranges) {
   assert(call_erase(&p,range.offset,range.size)==0x102);
   assert(seen_partition==&p && seen_offset==range.offset && seen_size==range.size);
  }
  p.size=0; assert(call_erase(&p,0,0)==0x102);
  p.size=0x5000; p.type=ESP_PARTITION_TYPE_APP;
  assert(call_erase(&p,0,p.size)==0x102);
  p.type=ESP_PARTITION_TYPE_DATA; p.subtype=ESP_PARTITION_SUBTYPE_DATA_FAT;
  assert(call_erase(&p,0,p.size)==0x102);
  assert(call_erase(nullptr,0,0x5000)==0x102 && seen_partition==nullptr);
  assert(erase_calls==11 && !nvsBootStatus().format_refused);
 } else if(scenario==4) {
  next_init=0x1101; assert(call_init()==0x1101);
  next_init=0x1110; assert(call_init()==0x1110);
  assert(nvsBootStatus().first_init_failure==0x1101);
  assert(nvsBootStatus().last_init_result==0x1110);
  assert(!nvsBootStatus().persistenceAllowed());
 }
}
'''
CALLERS = r'''
#include "sdk.h"
extern "C" esp_err_t __wrap_nvs_flash_init();
extern "C" esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t *, size_t, size_t);
int call_init() { return __wrap_nvs_flash_init(); }
int call_erase(const esp_partition_t *p, size_t o, size_t s) {
 return __wrap_esp_partition_erase_range(p,o,s);
}
'''

class NvsBootStartup(unittest.TestCase):
    def test_partition_guard_and_boot_evidence(self):
        self.assertTrue((ROOT / "src/nvs_boot_guard.cpp").exists(),
                        "pre-setup NVS preservation guard is missing")
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / "sdk.h").write_text(SDK)
            for name in ("esp_partition.h", "nvs_flash.h"):
                (temp / name).write_text('#include "sdk.h"\n')
            (temp / "run.cpp").write_text(HARNESS)
            (temp / "callers.cpp").write_text(CALLERS)
            subprocess.run(["c++", "-std=c++11", "-DARDUINO", "-I", str(temp),
                            "-I", str(ROOT / "include"), str(temp / "run.cpp"),
                            str(temp / "callers.cpp"), str(ROOT / "src/nvs_boot_guard.cpp"),
                            "-o", str(temp / "run")], check=True)
            for scenario in range(5):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(temp / "run"), str(scenario)], check=True)

if __name__ == "__main__":
    unittest.main()

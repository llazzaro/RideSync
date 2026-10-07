"""Exercise the concrete pinned NVS API adapter with fault boundaries, not flash durability."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
SDK = r'''
#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t=int; using nvs_handle_t=uint32_t;
constexpr int ESP_OK=0, ESP_ERR_NVS_NOT_FOUND=0x1102, ESP_ERR_NVS_INVALID_LENGTH=0x110c;
enum nvs_open_mode { NVS_READONLY, NVS_READWRITE };
int nvs_open(const char *, nvs_open_mode, nvs_handle_t *);
int nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
int nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
'''
HARNESS = r'''
#include "config_storage.h"
#include "nvs_boot_guard.h"
#include "nvs.h"
#include <cassert>
#include <cstring>
using namespace ridesync;
static bool gate=true; static int opens=0, blob_reads=0, sets=0, commits=0, closes=0;
static int open_error=0, get_error=0, set_error=0, commit_error=0;
static bool close_gate_after_open=false, close_gate_after_set=false, changed_length=false;
static size_t blob_size=3;
namespace ridesync { NvsBootStatus nvsBootStatus() { NvsBootStatus s; s.init_observed=gate; return s; } }
int nvs_open(const char *space, nvs_open_mode, nvs_handle_t *h) {
 assert(gate); assert(!strcmp(space,"ridesync_cfg")); ++opens; *h=17;
 if(close_gate_after_open) gate=false; return open_error;
}
int nvs_get_blob(nvs_handle_t h, const char *key, void *p, size_t *n) {
 assert(gate && h==17); assert(!strcmp(key,"cfg_a") || !strcmp(key,"cfg_b")); ++blob_reads;
 if(get_error) return get_error;
 if(p) { assert(*n>=blob_size); memset(p,42,blob_size); }
 *n=blob_size+(p && changed_length ? 1 : 0); return 0;
}
int nvs_set_blob(nvs_handle_t h, const char *key, const void *, size_t n) {
 assert(gate && h==17 && !strcmp(key,"cfg_b") && n==3); ++sets;
 if(close_gate_after_set) gate=false; return set_error;
}
int nvs_commit(nvs_handle_t h) { assert(gate && h==17); ++commits; return commit_error; }
void nvs_close(nvs_handle_t h) { assert(gate && h==17); ++closes; }
int main() {
 NvsConfigStore s; ConfigRecord r;
 gate=false; assert(s.read(0,r).status==StoreStatus::Refused); assert(opens==0);
 gate=true; open_error=ESP_ERR_NVS_NOT_FOUND;
 assert(s.read(0,r).status==StoreStatus::Missing); assert(closes==0);
 open_error=55; assert(s.read(0,r).status==StoreStatus::Error);
 open_error=0; blob_size=2049;
 assert(s.read(0,r).status==StoreStatus::Oversized); assert(blob_reads==1 && closes==1);
 blob_size=3; changed_length=true;
 assert(s.read(0,r).status==StoreStatus::Error); assert(r.size==0);
 changed_length=false; assert(s.read(0,r).status==StoreStatus::Ok); assert(r.size==3);
 set_error=77; assert(s.write(1,r).status==StoreStatus::SetError); assert(commits==0);
 set_error=0; commit_error=88;
 auto failure=s.write(1,r); assert(failure.status==StoreStatus::CommitError && failure.code==88);
 commit_error=0; assert(s.write(1,r).status==StoreStatus::Ok); assert(commits==2);
 close_gate_after_open=true; int before_blob_reads=blob_reads, before_closes=closes;
 assert(s.read(0,r).status==StoreStatus::Refused);
 assert(blob_reads==before_blob_reads && closes==before_closes);
 gate=true; close_gate_after_open=false; close_gate_after_set=true; r.size=3;
 assert(s.write(1,r).status==StoreStatus::Refused); assert(commits==2);
 gate=true; close_gate_after_set=false; assert(s.read(0,r).status==StoreStatus::Ok);
 assert(s.read(2,r).status==StoreStatus::Refused);
}
'''
class ConfigNvs(unittest.TestCase):
    def test_concrete_adapter_boundaries(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / "nvs.h").write_text(SDK)
            (temp / "run.cpp").write_text(HARNESS)
            subprocess.run(["c++", "-std=c++11", "-DARDUINO_ARCH_ESP32", "-I", str(temp),
                            "-I", str(ROOT / "include"), str(temp / "run.cpp"),
                            str(ROOT / "src/config_storage_nvs.cpp"), "-o", str(temp / "run")], check=True)
            subprocess.run([str(temp / "run")], check=True)

"""Execute the unchanged pinned restore function with NVS boundary stand-ins."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("patch", ROOT / "scripts/patch_nimble_store.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
PREFIX = r'''
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#define ESP_OK 0
#define MYNEWT_VAL(x) V_##x
#define V_BLE_STORE_MAX_BONDS 5
#define V_BLE_STORE_MAX_CCCDS 0
#define V_BLE_STORE_MAX_CSFCS 0
#define V_ENC_ADV_DATA 0
#define BLE_STORE_OBJ_TYPE_OUR_SEC 1
#define BLE_STORE_OBJ_TYPE_PEER_SEC 2
#define BLE_STORE_OBJ_TYPE_LOCAL_IRK 7
#define BLE_STORE_OBJ_TYPE_PEER_ADDR 6
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
typedef int esp_err_t;
struct ble_store_value_sec { uint16_t bond_count; };
struct ble_store_value_sec ble_store_config_our_secs[5],ble_store_config_peer_secs[5];
int ble_store_config_num_our_secs,ble_store_config_num_peer_secs;
int ble_store_config_local_irks[5],ble_store_config_rpa_recs[5];
int ble_store_config_num_local_irks,ble_store_config_num_rpa_recs;
uint16_t ble_store_config_our_bond_count,ble_store_config_peer_bond_count;
static int our_count,peer_count,restore_error;
int ble_store_config_compare_bond_count(const void *a,const void *b) {
 return ((const struct ble_store_value_sec*)a)->bond_count - ((const struct ble_store_value_sec*)b)->bond_count;
}
int populate_db_from_nvs(int t,void *p,int *n) {
 if(restore_error) return restore_error;
 *n=t==1 ? our_count : t==2 ? peer_count : 0;
 struct ble_store_value_sec *values=p;
 for(int i=0;i<*n;++i) values[i].bond_count=(*n-i)*3;
 return 0;
}
'''
SUFFIX = r'''
int main(int argc,char **argv) {
 int scenario=argc>1?atoi(argv[1]):0;
 our_count=(scenario==1||scenario==3)?3:0;
 peer_count=(scenario==2||scenario==3)?2:0;
 restore_error=scenario==4?77:0;
 assert(ble_nvs_restore_sec_keys()==restore_error);
 if(!restore_error) {
  assert(ble_store_config_our_bond_count==(our_count?9:0));
  assert(ble_store_config_peer_bond_count==(peer_count?6:0));
 }
 return 0;
}
'''


class NimblePatch(unittest.TestCase):
    def test_pinned_function_empty_one_sided_order_and_restore_error(self):
        original = (ROOT / "test/fixtures/nimble/restore_function.c.txt").read_text()
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            source = temp / "probe.c"
            binary = temp / "probe"
            for fixed in (False, True):
                source.write_text(PREFIX + (patch.guard_zero_counts(original) if fixed else original) + SUFFIX)
                subprocess.run(["clang", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                                "-g", "-O0", str(source), "-o", str(binary)], check=True)
                for scenario in range(5):
                    result = subprocess.run([str(binary), str(scenario)], capture_output=True, text=True)
                    if fixed or scenario in (3, 4):
                        self.assertEqual(result.returncode, 0, result.stderr)
                    else:
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn("index -1 out of bounds", result.stderr)

    def test_source_drift_rejected_and_patch_idempotent(self):
        fixture = (ROOT / "test/fixtures/nimble/restore_function.c.txt").read_text()
        fixed = patch.guard_zero_counts(fixture)
        self.assertEqual(patch.guard_zero_counts(fixed), fixed)
        with self.assertRaises(RuntimeError):
            patch.corrected(fixture)  # Partial/modified source never qualifies for install.
        with self.assertRaises(RuntimeError):
            patch.guard_zero_counts("no matching pinned source")

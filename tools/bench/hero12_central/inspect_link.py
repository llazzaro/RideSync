"""Read-only check of the isolated probe ELF's NVS and RAM-store boundaries."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'x5_peripheral'))
from inspect_link import (assert_symbol_boundaries, assert_store_boundaries,
                          assert_hal_boundaries, assert_nvs_callers,
                          assert_refusal_wrappers)
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from link_audit import callers_from_disassembly

BUILD = Path(__file__).resolve().parent / '.pio/build/hero12_central'
TC = Path.home() / '.platformio/packages/toolchain-xtensa-esp32/bin'
nm = TC / 'xtensa-esp32-elf-nm'
objdump = TC / 'xtensa-esp32-elf-objdump'

def run(tool, *args):
    return subprocess.check_output([str(tool), *map(str, args)], text=True)

if __name__ == '__main__':
    symbols = run(nm, '-C', BUILD / 'firmware.elf')
    names = assert_symbol_boundaries(symbols)
    objects = list(BUILD.glob('lib*/NimBLE-Arduino/nimble/nimble/host/store/config/src/ble_store_config.c.o'))
    assert_store_boundaries(nm, objects)
    disassembly = run(objdump, '-d', BUILD / 'firmware.elf')
    callers = callers_from_disassembly(disassembly)
    assert_hal_boundaries(disassembly, callers)
    assert_nvs_callers(names, callers)
    assert_refusal_wrappers({'nvs_flash_init', 'nvs_flash_erase', 'nvs_open',
                            'nvs_open_from_partition', 'esp_partition_erase_range'}, callers)
    print('PASS: strong BLE HAL, RAM-only store, empty persistence object, NVS/PHY refusal wrappers')
    for target in ('ble_gap_disc', 'ble_gap_connect', 'ble_gap_security_initiate',
                   'ble_gattc_write_flat', 'ble_gattc_read', 'ble_gap_terminate'):
        observed = callers.get(target, set())
        assert observed, f'missing retained operation {target}'
        assert not any(owner in ('loop', 'loop()') for owner in observed), observed
    print('PASS: retained central operations; no SDK submissions from loop')

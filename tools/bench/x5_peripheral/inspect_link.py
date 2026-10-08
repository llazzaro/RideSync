"""Fail closed on unexpected NVS paths in this exact compile-only bench ELF.

Use the pinned cross-toolchain nm/objdump; this does not access hardware/flash.
Remaining NVS getters/setters are PHY functions behind a refused open. Their
source error branches must also be reviewed; symbol checks are not execution.
"""
import argparse
from pathlib import Path
import re
import subprocess

BENCH = Path(__file__).resolve().parent
BUILD = BENCH / '.pio/build/x5_peripheral'


def run(tool, *args):
    return subprocess.check_output([tool, *map(str, args)], text=True)


def inspect(nm, objdump):
    elf = BUILD / 'firmware.elf'
    symbols = run(nm, '-C', elf)
    assert re.search(r'^\S+ T btInUse$', symbols, re.MULTILINE), (
        'Bluetooth HAL missing: weak core btInUse releases BLE memory before setup')
    assert re.search(r'^\S+ T btStarted$', symbols, re.MULTILINE), (
        'missing pinned Arduino Bluetooth HAL linkage anchor')
    names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
    assert 'btStart' not in names, 'unexpected Arduino controller startup linked'
    guarded = {
        'nvs_flash_init', 'nvs_flash_erase', 'nvs_open',
        'nvs_open_from_partition', 'esp_partition_erase_range',
    }
    for name in guarded:
        assert name not in names, f'unguarded implementation linked: {name}'
        assert '__wrap_' + name in names, f'missing refusal wrapper: {name}'
    assert 'NimBLEDevice::init(' not in symbols, 'unsafe wrapper startup linked'
    for name in names:
        if name.startswith('nvs_flash_'):
            raise AssertionError(f'unexpected flash lifecycle entry: {name}')
    objects = list(BUILD.glob('lib*/NimBLE-Arduino/nimble/nimble/host/store/config/src/ble_store_config.c.o'))
    assert len(objects) == 1, 'missing/ambiguous compiled config store'
    undefined = run(nm, '-u', objects[0])
    assert not re.search(r'nvs|persist|restore|conf_init', undefined), undefined
    nvs_object = objects[0].with_name('ble_store_nvs.c.o')
    assert not run(nm, nvs_object).strip(), 'persistent store object is not empty'
    disassembly = run(objdump, '-d', elf)
    bt_in_use = re.search(r'<btInUse>:\n(.*?)(?=\n\S+ <|\Z)', disassembly, re.DOTALL)
    assert bt_in_use and re.search(r'\bmovi(?:\.n)?\s+a2,\s*1\b', bt_in_use.group(1)), (
        'pinned ESP32 Bluetooth HAL must return true from btInUse')
    callers = {}
    owner = None
    for line in disassembly.splitlines():
        label = re.match(r'[0-9a-f]+ <(.+)>:', line)
        if label:
            owner = label.group(1)
        call = re.search(r'\bcall\w*\s+[0-9a-f]+ <([^>]+)>', line)
        if call:
            callers.setdefault(call.group(1), set()).add(owner)
    phy = {'esp_phy_load_cal_data_from_nvs', 'esp_phy_store_cal_data_to_nvs'}
    hal_queries = {target for target, owners in callers.items() if 'btStarted' in owners}
    assert hal_queries == {'esp_bt_controller_get_status'}, (
        f'Bluetooth HAL anchor must only query controller status: {hal_queries}')
    for name in names:
        if name.startswith('nvs_') and name != 'nvs_find_ns_handle':
            observed = callers.get(name, set())
            assert observed <= phy, f'unexpected actual NVS caller: {name}: {observed}'
    assert callers.get('__wrap_nvs_open') == phy, 'PHY open boundary changed'
    assert callers.get('__wrap_nvs_flash_init') == {'initArduino'}, 'startup init boundary changed'
    for name in guarded:
        wrapper = '__wrap_' + name
        outgoing = {target for target, owners in callers.items() if wrapper in owners}
        assert all(target.startswith('_ZNSt13__atomic_base') for target in outgoing), (
            f'non-counter call from refusal wrapper: {wrapper}: {outgoing}')
    # Application start/cleanup must stay off loop(), including after compiler
    # inlining. These are the only pinned SDK-internal terminate callers here.
    sdk_internal = {'ble_gap_update_notify', 'ble_gattc_timer'}
    for target in ('ble_gap_adv_start', 'ble_gap_adv_stop', 'ble_gap_terminate'):
        observed = callers.get(target, set())
        assert any('ownerTask' in owner for owner in observed), f'missing SDK owner: {target}'
        assert all('ownerTask' in owner or owner in sdk_internal for owner in observed), (
            f'SDK submission outside retained owner: {target}: {observed}')
    print('PASS: RAM config store; empty persistent object; core/PHY refusal boundaries; no unexpected NVS callers')
    print('PASS: application advertising start/stop/terminate callers stay in retained SDK owner')
    print('PASS: strong Arduino Bluetooth HAL retains controller memory before setup')
    print('PHY getters/setters remain linked but are behind open refusal; source error-branch review is still required.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--nm', required=True)
    parser.add_argument('--objdump', required=True)
    args = parser.parse_args()
    inspect(args.nm, args.objdump)

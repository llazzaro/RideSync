"""Audit this pinned compile artifact; no hardware/flash access or runtime claim."""
import argparse
from pathlib import Path
import re
import subprocess

BENCH = Path(__file__).resolve().parent


def inspect(nm, objdump):
    elf = BENCH / '.pio/build/gnss_driver/firmware.elf'
    symbols = subprocess.check_output([nm, '-C', str(elf)], text=True)
    names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
    guarded = {'nvs_flash_init', 'nvs_flash_erase', 'nvs_open',
               'nvs_open_from_partition', 'esp_partition_erase_range'}
    for name in guarded:
        assert name not in names, f'real flash/NVS lifecycle linked: {name}'
        assert '__wrap_' + name in names, f'missing refusal: {name}'
    assert not any(name.startswith('nvs_') for name in names), 'real NVS entry linked'
    for forbidden in ('NimBLE', 'ble_gap_', 'ArduinoGnssPower', 'restartAfterVerifiedBarrier',
                      'SDClass', 'SDFS', 'SPIClass', 'digitalWrite', 'pinMode',
                      'esp_bt_controller_init', 'esp_bt_controller_enable', 'esp_phy_load_cal'):
        assert forbidden not in symbols, f'unexpected dependency/recovery/GPIO: {forbidden}'
    for required in ('ridesync::ModemGnss::tick(', 'ridesync::GpsManager::tick()',
                     'ridesync::GpsManager::cancel()', 'ridesync::SessionClock::snapshot()',
                     'ridesync::parseGnssLine(', 'HardwareSerial::begin('):
        assert required in symbols, f'missing real production route: {required}'
    disassembly = subprocess.check_output([objdump, '-d', str(elf)], text=True)
    callers = {}
    owner = None
    for line in disassembly.splitlines():
        label = re.match(r'[0-9a-f]+ <(.+)>:', line)
        if label:
            owner = label.group(1)
        call = re.search(r'\bcall\w*\s+[0-9a-f]+ <([^>]+)>', line)
        if call:
            callers.setdefault(call.group(1), set()).add(owner)
    assert callers.get('__wrap_nvs_flash_init') == {'initArduino'}, 'core init boundary changed'
    assert callers.get('__wrap_esp_partition_erase_range') == {
        'initArduino', 'rewrite_ota_seq$part$1'}, 'core/OTA erase boundary changed'
    # Pinned Arduino also links OTA boot verification. Its write follows a
    # successful erase only: the explicit erase refusal branches to the return.
    ota = re.search(r'<rewrite_ota_seq\$part\$1>:\n(.*?)(?=\n\S+ <|\Z)',
                    disassembly, re.DOTALL)
    assert ota and re.search(
        r'call\w*[^\n]+<__wrap_esp_partition_erase_range>\n'
        r'[^\n]+\bbnez(?:\.n)?\s+a10,[^\n]+<rewrite_ota_seq\$part\$1\+0x32>',
        ota.group(1)), 'OTA erase refusal must skip partition write'
    refusal_exit = re.search(r'\bbnez(?:\.n)?\s+a10,\s*([0-9a-f]+)', ota.group(1)).group(1)
    assert re.search(r'^' + refusal_exit +
                     r':\s+[0-9a-f]+\s+mov(?:\.n)?\s+a2,\s*a10\n[^\n]+\bretw',
                     ota.group(1), re.MULTILINE), 'OTA refusal target must return the error'
    for name in guarded:
        wrapper = '__wrap_' + name
        outgoing = {target for target, owners in callers.items() if wrapper in owners}
        assert all(target.startswith('_ZNSt13__atomic_base') for target in outgoing), (
            f'refusal wrapper calls beyond counters: {wrapper}: {outgoing}')
    for name in ('nvs_open', 'nvs_open_from_partition', 'nvs_flash_erase'):
        assert not callers.get('__wrap_' + name), f'unexpected NVS user: {name}'
    # HardwareSerial's virtual flush stays linked through its vtable. No bench
    # call is allowed. Pinned uartBegin has an SDK-only reconfiguration branch
    # that flushes; this one-shot diagnostic never reopens an installed UART2.
    assert not callers.get('_ZN14HardwareSerial5flushEv'), 'application serial flush call'
    assert callers.get('uartFlush', set()) <= {'_ZN14HardwareSerial5flushEv', 'uartBegin'}
    print('PASS: real production parser/clock/modem/manager; no BLE/SD/power GPIO/restart route')
    print('PASS: real NVS lifecycle absent; core init/erase branches reach explicit refusal only')
    print('PASS: no application flush call; SDK UART call latency remains physically unmeasured')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--nm', required=True)
    parser.add_argument('--objdump', required=True)
    args = parser.parse_args()
    inspect(args.nm, args.objdump)

#!/usr/bin/env python3
"""Measure health-only fixed RAM with native and pinned Xtensa C++ layouts.

A temporary header copy removes only explicitly listed health members. Other
included feature members remain. No firmware link or SDK headers are needed.
The probe emits object symbol sizes, so the target executable is never run.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TYPES = {
    'field': 'CameraHealthField', 'peer': 'CameraHealthPeer',
    'transition': 'CameraHealthTransition', 'evaluator': 'CameraHealthEvaluator',
    'logger': 'CameraHealthLogger', 'summary': 'CameraHealthSummary',
    'serial_sink': 'CameraHealthSerialSink', 'manager': 'CameraManager',
    'camera_state': 'CameraState', 'event': 'Event',
    'ble_event': 'BleEvent', 'ble_result': 'BleResult', 'central': 'BleCentral',
    'hero': 'Hero12Adapter', 'hero_pending': 'Hero12Adapter::PendingEvent',
    'hero_poll': 'Hero12Adapter::HealthPoll', 'hero_qualification': 'Hero12Qualification',
    'dji': 'DjiAdapter', 'dji_pending': 'DjiAdapter::Pending',
    'storage': 'Storage', 'record': 'TelemetryRecord', 'evidence': 'CameraHealthEvidence',
    'admission': 'TelemetryAdmission', 'session': 'CameraEventSession',
    'local_status': 'LocalTelemetryStatus', 'local_config': 'LocalTelemetryConfig',
    'local_runtime': 'LocalTelemetryRuntime', 'control_status': 'ControlStatus',
    'control': 'HandlebarControl', 'led_health': 'LedHealth',
}
SOURCE = '''#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#define private public
#include "camera_event_session.h"
#include "local_telemetry_runtime.h"
#include "handlebar_control.h"
#include "profiles/dji_camera.h"
#undef private
using namespace ridesync;
extern "C" {
%s
}
'''


def remove_health(headers):
    """Produce a size-only counterfactual, preserving all non-health features."""
    def replace(file, old, new=''):
        path = headers / file
        text = path.read_text()
        if text.count(old) != 1:
            raise RuntimeError(f'{file}: expected exactly one health declaration {old!r}')
        path.write_text(text.replace(old, new))
    replace('camera_manager.h', '  uint32_t observed_sequence = 0, qualified_received_ms = 0;\n')
    replace('camera_manager.h', '  bool has_qualified_observation = false;\n')
    replace('camera_manager.h', '  CameraHealthObserver *health_ = nullptr;\n')
    path = headers / 'camera_manager.h'
    text = path.read_text()
    text, count = re.subn(r'  bool attachHealthObserver\(.*?\n  }\n  void detachHealthObserver\(.*?\n  }', '', text, flags=re.S)
    if count != 1:
        raise RuntimeError('camera manager health attachment shape changed')
    path.write_text(text)
    replace('camera_manager.h', '  QualifiedHealthSample health;\n')
    replace('camera_manager.h', '  bool has_health = false;\n')
    replace('ble_remote.h', '  uint32_t received_ms = 0; // Host callback receipt, not camera acquisition.\n')
    replace('ble_remote.h', '  Clock *receipt_clock_ = nullptr;\n')
    replace('ble_remote.h', '  bool receipt_known = false;\n')
    replace('profiles/gopro_hero12.h', '  bool health_enabled = false;\n')
    replace('profiles/gopro_hero12.h', '  bool ordinary_video_confirmed = false;\n')
    replace('profiles/gopro_hero12.h', '    uint32_t received_ms = 0, sequence = 0;\n')
    replace('profiles/gopro_hero12.h', '    bool qualified_receipt = false;\n')
    replace('profiles/gopro_hero12.h', '  CameraHealthSampleSink *health_sink_ = nullptr;\n')
    replace('profiles/gopro_hero12.h', '  std::array<HealthPoll, kBlePeers> health_poll_{};\n')
    replace('profiles/gopro_hero12.h', '  uint32_t health_admitted_ms_ = 0, health_coalesced_ = 0, health_failures_ = 0;\n')
    replace('profiles/gopro_hero12.h', '  uint8_t health_cursor_ = 0;\n')
    replace('profiles/gopro_hero12.h', '  bool health_admitted_ = false;\n')
    replace('profiles/gopro_hero12.h', 'return health_coalesced_;', 'return 0;')
    replace('profiles/gopro_hero12.h', 'return health_failures_;', 'return 0;')
    replace('profiles/dji_camera.h', ', health_sequence = 0;', ';')
    replace('profiles/dji_camera.h', '  uint32_t notification_receipt_ = 0;\n')
    replace('telemetry_record.h', '  CameraHealth,\n')
    replace('telemetry_admission.h', '  HealthLogSource *health_ = nullptr;\n')
    replace('camera_event_session.h', 'class CameraEventSession : public CameraHealthSampleSink {', 'class CameraEventSession {')
    replace('camera_event_session.h', '  const CameraHealthEvaluator &health() const { return health_; }\n')
    replace('camera_event_session.h', '  void healthSample(uint8_t, const QualifiedHealthSample &) override;', '  void healthSample(uint8_t, const QualifiedHealthSample &);')
    replace('camera_event_session.h', '  void healthPollPeriod(uint8_t, uint32_t, uint32_t) override;', '  void healthPollPeriod(uint8_t, uint32_t, uint32_t);')
    for line in ('  CameraHealthEvaluator health_;\n', '  CameraHealthLogger health_logger_;\n',
                 '  Clock &raw_;\n', '  bool health_enabled_ = false, health_sink_owned_ = false;\n',
                 '  uint32_t health_coalesced_seen_ = 0;\n'):
        replace('camera_event_session.h', line)
    replace('local_telemetry_runtime.h', '  bool health_enabled = false;\n')
    replace('local_telemetry_runtime.h', '  CameraHealthTransitionSink *health_sink = nullptr;\n')
    replace('local_telemetry_runtime.h', '  CameraHealthSummary camera_health;\n')
    replace('status_led.h', '  bool camera_critical = false, camera_warning = false;\n')


def probe(compiler, nm, directory):
    result = {}
    version = subprocess.check_output([compiler, '--version'], text=True).splitlines()[0]
    for variant in ('current', 'without_health'):
        headers = directory / variant
        shutil.copytree(ROOT / 'include', headers)
        if variant == 'without_health':
            remove_health(headers)
        source = directory / (variant + '.cpp')
        source.write_text(SOURCE % '\n'.join(f'unsigned char size_{key}[sizeof({value})];' for key, value in TYPES.items()))
        sizes = {}
        if 'xtensa' in compiler:
            obj = source.with_suffix('.o')
            subprocess.run([compiler, '-std=c++11', '-I' + str(headers), '-c', str(source), '-o', str(obj)], check=True)
            for line in subprocess.check_output([nm, '-S', '--radix=d', str(obj)], text=True).splitlines():
                words = line.split()
                if len(words) == 4 and words[3].startswith('size_'):
                    sizes[words[3][5:]] = int(words[1])
        else:
            # Mach-O nm intentionally reports zero symbol sizes. Native sizeof
            # can be executed safely; the Xtensa object above is never run.
            native = '\n'.join(f'  std::printf("{key}=%zu\\n", sizeof({value}));' for key, value in TYPES.items())
            source.write_text(source.read_text() + '\n#include <cstdio>\nint main() {\n' + native + '\n}\n')
            binary = source.with_suffix('.bin')
            subprocess.run([compiler, '-std=c++11', '-I' + str(headers), str(source), '-o', str(binary)], check=True)
            sizes = {name: int(size) for name, size in
                     (line.split('=') for line in subprocess.check_output([str(binary)], text=True).splitlines())}
        if sizes.keys() != TYPES.keys():
            raise RuntimeError('compiler did not emit every size symbol')
        result[variant] = sizes
    now, before = result['current'], result['without_health']
    delta = {name: now[name] - before[name] for name in TYPES}
    # One real mixed adapter owns FOUR central services, even with fewer live peers.
    # Hero/DJI full deltas include their own central; add only GO3S/ONE RS centrals.
    # Session contains evaluator/logger/storage/admission. Runtime contains session
    # and ONE LocalTelemetryConfig/LocalTelemetryStatus. Control contains second
    # status copy. Three additional config copies live in bench/app/ESP32 wrappers.
    components = {
        'local_runtime_including_session': delta['local_runtime'],
        'handlebar_control_second_status_copy': delta['control'],
        'camera_manager_eight_slots': delta['manager'],
        'hero_adapter_including_central': delta['hero'],
        'dji_adapter_including_central': delta['dji'],
        'go3s_and_one_rs_centrals': 2 * delta['central'],
        'three_additional_config_copies': 3 * delta['local_config'],
        'serial_sink_and_port_vptr': now['serial_sink'] + (4 if 'xtensa' in compiler else 8),
    }
    return {'compiler': version, 'sizes': now, 'health_only_delta': delta,
            'persistent_components': components,
            'persistent_total': sum(components.values()), 'cap': 4096}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target-only', action='store_true')
    parser.add_argument('--stack-dir', type=Path, help='Scan existing pinned -fstack-usage build evidence')
    args = parser.parse_args()
    toolroot = Path.home() / '.platformio/packages/toolchain-xtensa-esp32/bin'
    pairs = [('xtensa', str(toolroot / 'xtensa-esp32-elf-g++'), str(toolroot / 'xtensa-esp32-elf-nm'))]
    if not args.target_only:
        pairs.append(('host', shutil.which('c++'), shutil.which('nm')))
    results = {}
    for name, compiler, nm in pairs:
        if not compiler or not nm or not Path(compiler).exists():
            raise SystemExit(f'{name} compiler/nm unavailable')
        with tempfile.TemporaryDirectory(prefix='ridesync-health-size-') as temp:
            results[name] = probe(compiler, nm, Path(temp))
    if args.stack_dir:
        own = []
        frames = {}
        for path in args.stack_dir.rglob('*.su'):
            for line in path.read_text().splitlines():
                fields = line.rsplit('\t', 2)
                if len(fields) == 3:
                    frames[fields[0]] = int(fields[1])
                    if path.name in ('camera_health.cpp.su', 'camera_health_logger.cpp.su', 'camera_health_output.cpp.su') or 'Storage::enqueueHealth(' in fields[0] or 'Storage::formatHealth(' in fields[0] or 'TelemetryAdmission::health(' in fields[0]:
                        own.append({'function': fields[0], 'bytes': int(fields[1]), 'bound': fields[2]})
        results['stack_frames'] = sorted(own, key=lambda item: item['bytes'], reverse=True)
        def frame(name):
            matches = [size for function, size in frames.items() if name in function]
            if len(matches) != 1:
                raise RuntimeError(f'Expected one measured stack frame for {name}')
            return matches[0]
        chains = {
            'health_serial': frame('CameraHealthLogger::drain(') + frame('CameraHealthSerialSink::transition('),
            'health_storage': frame('CameraHealthLogger::drain(') + frame('TelemetryAdmission::health(') + frame('Storage::enqueueHealth(') + frame('Storage::publish('),
            'existing_camera_storage': frame('TelemetryAdmission::admitCameraInbox(') + frame('Storage::enqueueCamera(') + frame('Storage::publish('),
        }
        extra = max(0, max(chains['health_serial'], chains['health_storage']) - chains['existing_camera_storage'])
        results['stack_chains'] = chains
        results['incremental_logging_stack_high_water'] = extra
        results['xtensa']['persistent_plus_incremental_logging_stack'] = results['xtensa']['persistent_total'] + extra
        reserved = max(extra, chains['health_serial'])
        results['xtensa']['conservative_scratch_reserved'] = reserved
        results['xtensa']['conservative_total'] = results['xtensa']['persistent_total'] + reserved
        results['xtensa']['budget_passed'] = results['xtensa']['conservative_total'] <= results['xtensa']['cap']
        results['stack_accounting_limits'] = 'Same serialized owner; common upstream frames cancel. Excludes library callees, other existing owner paths, IRQ/task stack margin and physical high-water qualification.'
    print(json.dumps(results, indent=2))
    if args.stack_dir and not results['xtensa']['budget_passed']:
        raise SystemExit('Camera health target RAM/scratch budget exceeds 4096 bytes')

if __name__ == '__main__':
    main()

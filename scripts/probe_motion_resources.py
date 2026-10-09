"""Measure before/after ABI sizes using the retained build compile database.

Run pio run -e hero12_adapter_compile -t compiledb first. Baseline a41a428
is approved pre-implementation main. Private access measures Active in a probe;
production headers are unchanged. Outputs live in the ignored SDD workspace.
"""
import json
from pathlib import Path
import shlex
import subprocess
import tarfile

root = Path(__file__).resolve().parents[1]
output = root / '.superpowers/sdd/2026-10-09-motion-logging'
output.mkdir(parents=True, exist_ok=True)
archive = output / 'baseline.tar'
archive.write_bytes(subprocess.check_output(['git', 'archive', 'a41a428', 'include'], cwd=root))
baseline = output / 'baseline'
with tarfile.open(archive) as files:
    for member in files.getmembers():
        if member.isfile():
            destination = baseline / member.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(files.extractfile(member).read())

source = '''#include <array>
#include <atomic>
#include <type_traits>
#define private public
#include "application_esp32.h"
#include "local_telemetry_runtime.h"
#include "motion_estimator.h"
#undef private
#include <cstdio>
using namespace ridesync;
'''
types = ['ImuEvidence', 'ImuBatch', 'ImuInbox', 'TelemetryRecord', 'Storage',
         'MotionEstimator', 'TelemetryAdmission', 'CameraEventSession',
         'LocalTelemetryRuntime::Active', 'LocalTelemetryStatus', 'LocalTelemetryRuntime',
         'Esp32LocalTelemetry', 'SupervisedEsp32Application']
entries = json.loads((root / 'compile_commands.json').read_text())
entry = next(item for item in entries if item['file'].endswith('local_telemetry_runtime.cpp'))
arguments = shlex.split(entry['command'])
compiler = arguments[0]
nm = str(Path(compiler).with_name('xtensa-esp32-elf-nm'))
flags = []
index = 1
while index < len(arguments):
    flag = arguments[index]
    if flag == '-o':
        index += 2
        continue
    if flag not in ('-c', '-MMD', '-Iinclude', '-Isrc') and not flag.endswith('.cpp'):
        flags.append(flag.replace('\\"', '"'))
    index += 1

for version, include in [('before', baseline / 'include'), ('after', root / 'include')]:
    for abi in ('target', 'native'):
        selected = types + (['MotionEvidence'] if version == 'after' else [])
        content = source
        if abi == 'target':
            for name in selected:
                content += f'char size_{name.replace("::", "_")}[sizeof({name})];\n'
        else:
            # Concrete native wrapper sizes are measured by the SDK fixture.
            selected = [name for name in selected if name not in
                        ('Esp32LocalTelemetry', 'SupervisedEsp32Application')]
            content += 'int main() {\n'
            for name in selected:
                content += f'std::printf("{name}=%zu\\n",sizeof({name}));\n'
            content += '}\n'
        probe = output / f'{version}-{abi}.cpp'
        probe.write_text(content)
        artifact = probe.with_suffix('.o')
        if abi == 'target':
            command = [compiler, '-I'+str(include), *flags, '-c', str(probe), '-o', str(artifact)]
        else:
            command = ['c++', '-std=c++11', '-I'+str(include), str(probe), '-o', str(artifact)]
        subprocess.run(command, check=True, cwd=root)
        command = [nm, '-S', str(artifact)] if abi == 'target' else [str(artifact)]
        sizes = subprocess.check_output(command, text=True)
        (output / f'{version}-{abi}-sizes.txt').write_text(sizes)
        print(version, abi, sizes, sep='\n')

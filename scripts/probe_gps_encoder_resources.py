"""Audit the pinned retained ESP32 encoder, without hardware or activation.

Reports individual emitted compiler frames, not worst-case physical stack or
latency. The allowlisted reachable call graph fails closed on unexpected calls.
"""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ANCHOR = 'ridesync_insta360_gps_encoder_backend'
ENCODER = '_ZN8ridesync8insta3609encodeGps'


def run(tool, *args):
    return subprocess.check_output([str(tool), *map(str, args)], text=True)


def symbols_from(text):
    entries = {}
    for line in text.splitlines():
        match = re.fullmatch(r'([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(\w)\s+(.+)', line)
        if match:
            address, size, kind, name = match.groups()
            entries[name] = (int(address, 16), int(size, 16), kind)
    return entries


def parse_direct_call_target(line):
    match = re.search(r'\b(?:call(?:0|4|8|12)|j)\s+[0-9a-fA-F]+ <([^>]+)>', line)
    return match[1].split('+0x')[0] if match else None


def _record_indirect_call(graph, owner, opcode, operands, addresses, literal_value, registers):
    destination = operands.split(',')[0].strip()
    if opcode == 'l32r':
        literal = re.match(r'a\d+,\s*([0-9a-fA-F]+)', operands)
        if literal:
            registers[destination] = addresses.get(literal_value(int(literal[1], 16)))
    elif opcode == 'jx':
        graph[owner].add('<unresolved-register-call>')
        registers.clear()
    elif opcode.startswith('callx'):
        graph[owner].add(registers.get(destination) or '<unresolved-register-call>')
        registers.clear()
    else:
        target = parse_direct_call_target(operands)
        if target and target != owner:
            graph[owner].add(target)
        registers.pop(destination, None)
        if opcode.startswith(('call', 'b')) or opcode == 'j':
            registers.clear()


def _record_instruction(graph, owner, line, addresses, literal_value, registers):
    instruction = re.match(r'\s*[0-9a-fA-F]+:\s+[0-9a-fA-F]+\s+([\w.]+)\s*(.*)', line)
    if instruction:
        _record_indirect_call(graph, owner, *instruction.groups(), addresses, literal_value, registers)


def call_graph(disassembly, addresses, literal_value):
    graph, instructions = {}, {}
    owner, registers = None, {}
    for line in disassembly.splitlines():
        label = re.fullmatch(r'([0-9a-fA-F]+) <(.+)>:', line)
        if label:
            owner, registers = label[2], {}
            graph.setdefault(owner, set())
            instructions.setdefault(owner, [])
        elif owner:
            instructions[owner].append(line)
            _record_instruction(graph, owner, line, addresses, literal_value, registers)
    return graph, instructions


def audit_calls(graph, instructions, encoder, rom=()):
    # Compiler runtime float/integer arithmetic and byte copying only. An
    # unexpected direct call or indirect dispatch must be inspected explicitly.
    runtime = {
        'memcpy', 'memset', '__stack_chk_fail', '__adddf3', '__subdf3', '__muldf3', '__divdf3',
        '__eqdf2', '__nedf2', '__gtdf2', '__gedf2', '__ltdf2', '__ledf2', '__cmpdf2',
        '__unorddf2', '__extendsfdf2', '__truncdfsf2', '__eqsf2', '__nesf2',
        '__gtsf2', '__gesf2', '__ltsf2', '__lesf2', '__cmpsf2', '__unordsf2',
        '__floatundidf', '__floatunsidf', '__floatsidf', '__udivdi3', '__umoddi3',
        '__udivmoddi4', '__clzsi2', '__clzdi2', '__ashldi3', '__lshrdi3',
    }
    visited, pending = set(), [encoder]
    while pending:
        name = pending.pop()
        if name in visited:
            continue
        visited.add(name)
        if name == '__stack_chk_fail':
            continue  # Compiler integrity trap; exceptional abort, not encoding.
        if name in runtime and name in rom and name not in instructions:
            continue  # Pinned absolute ROM arithmetic/byte symbols; body unavailable.
        assert name in instructions, f'missing disassembly for reachable function: {name}'
        for target in graph.get(name, ()):
            assert target != '<unresolved-register-call>', f'unresolved register call from {name}'
            assert target.startswith('_ZN8ridesync8insta36012_GLOBAL__N_1') or target in runtime, (
                f'unexpected encoder dependency: {name} -> {target}')
            pending.append(target)
    return visited


def abi_sizes(nm):
    compiler = Path(nm).with_name('xtensa-esp32-elf-g++')
    content = '''#include "insta360_gps_encoder.h"
#include <cstddef>
using namespace ridesync::insta360;
extern "C" {
char gps_result_size[sizeof(GpsEncodingResult)];
char gps_packet_size[sizeof(GpsEncodingResult::bytes)];
char gps_config_size[sizeof(GpsEncoderConfig)];
char gps_bytes_offset_plus_one[offsetof(GpsEncodingResult, bytes)+1];
}
'''
    with tempfile.TemporaryDirectory(prefix='ridesync-gps-abi-') as temporary:
        source = Path(temporary) / 'abi.cpp'
        source.write_text(content)
        obj = source.with_suffix('.o')
        subprocess.run([str(compiler), '-std=c++11', '-I'+str(ROOT/'include'),
                        '-c', str(source), '-o', str(obj)], check=True, capture_output=True)
        sizes = symbols_from(run(nm, '-S', obj))
    assert sizes['gps_packet_size'][1] == 71, 'target packet storage changed'
    result = {name: item[1] for name, item in sizes.items() if name.startswith('gps_')}
    result['gps_bytes_offset'] = result.pop('gps_bytes_offset_plus_one') - 1
    assert result['gps_result_size'] >= result['gps_bytes_offset'] + 71, 'target storage is short'
    return result


def _anchor_encoder(symbols, elf, objdump):
    assert ANCHOR in symbols, 'missing GPS data-only retention anchor'
    encoders = [name for name in symbols if name.startswith(ENCODER)]
    assert len(encoders) == 1, 'missing/ambiguous real encoder'
    encoder = encoders[0]
    anchor_address, anchor_size, anchor_kind = symbols[ANCHOR]
    assert anchor_size == 4 and anchor_kind.upper() in ('D', 'R'), 'anchor is not target pointer data'
    dump = run(objdump, '-s', f'--start-address={anchor_address}',
               f'--stop-address={anchor_address+4}', elf)
    match = re.search(r'^\s*[0-9a-fA-F]+\s+([0-9a-fA-F]{8})\s', dump, re.MULTILINE)
    assert match, 'missing anchor contents'
    pointer = struct.unpack('<I', bytes.fromhex(match[1]))[0]
    assert pointer == symbols[encoder][0], 'anchor does not reference real encodeGps'
    return encoder


def _raw_symbols_and_rom(nm, elf):
    raw_names, rom = {}, set()
    for line in run(nm, elf).splitlines():
        match = re.fullmatch(r'([0-9a-fA-F]+)\s+\w\s+(.+)', line)
        if match:
            raw_names[int(match[1], 16)] = match[2]
            if line.split()[1] == 'A':
                rom.add(match[2])
    return raw_names, rom


def _section_memory(elf, objdump):
    # Load allocated literal-bearing sections once; do not launch objdump for
    # every long call in the full firmware image.
    memory = {}
    dump = run(objdump, '-s', '-j', '.flash.text', '-j', '.iram0.text', elf)
    for line in dump.splitlines():
        match = re.match(r'^\s*([0-9a-fA-F]+)\s+((?:[0-9a-fA-F]{8} ?){1,4})', line)
        if match:
            address = int(match[1], 16)
            data = bytes.fromhex(match[2])
            memory.update((address+n, byte) for n, byte in enumerate(data))
    return memory


def _literal_reader(memory):
    def literal_value(address):
        try:
            return struct.unpack('<I', bytes(memory[address+n] for n in range(4)))[0]
        except KeyError:
            return None

    return literal_value


def _encoder_frames(build_dir):
    frames = []
    for file in build_dir.rglob('insta360_gps_encoder*.su'):
        for line in file.read_text().splitlines():
            function, count, kind = line.rsplit('\t', 2)
            assert kind == 'static', f'unbounded/dynamic encoder frame: {line}'
            frames.append((function, int(count)))
    assert frames and any('encodeGps(' in name for name, _ in frames), 'missing emitted encoder frame'
    return frames


def _assert_helper_frames(reachable, frames, nm):
    for name in reachable:
        if name.startswith('_ZN8ridesync8insta36012_GLOBAL__N_1'):
            demangled = run(Path(nm).with_name('xtensa-esp32-elf-c++filt'), name).strip()
            basename = demangled.split('::')[-1].split('(')[0]
            assert any(basename+'(' in entry for entry, _ in frames), f'missing helper frame: {demangled}'


def inspect(elf, build_dir, nm, objdump):
    symbols = symbols_from(run(nm, '-S', elf))
    encoder = _anchor_encoder(symbols, elf, objdump)
    raw_names, rom = _raw_symbols_and_rom(nm, elf)
    memory = _section_memory(elf, objdump)
    literal_value = _literal_reader(memory)
    graph, instructions = call_graph(run(objdump, '-d', elf), raw_names, literal_value)
    reachable = audit_calls(graph, instructions, encoder, rom)
    frames = _encoder_frames(build_dir)
    _assert_helper_frames(reachable, frames, nm)
    print('PASS: retained data anchor points to real encoder; ordinary calls resolve to pure helpers')
    print('Exceptional compiler integrity trap __stack_chk_fail is retained; abort path excluded.')
    print('Reachable emitted functions:', ', '.join(sorted(reachable)))
    print('Trusted pinned absolute ROM helpers:', ', '.join(sorted(reachable & rom)))
    print('Target ABI:', abi_sizes(nm))
    for name, count in frames:
        print(f'Individual static compiler frame: {count} bytes: {name}')
    print('Encoder entry:', next(line.strip() for line in instructions[encoder]
                                 if re.search(r'\bentry\b', line)))
    print('Compile/link proof only: no camera, physical stack, latency or activation claim.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--nm', required=True)
    parser.add_argument('--objdump', required=True)
    args = parser.parse_args()
    inspect(args.elf, args.build_dir, args.nm, args.objdump)


if __name__ == '__main__':
    main()

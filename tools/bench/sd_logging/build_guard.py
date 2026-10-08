"""Inject private bench acknowledgments at build time; never save a namespace here."""
import os

Import('env')

try:
    namespace = int(os.environ.get('RIDESYNC_BENCH_NAMESPACE', ''), 0)
except ValueError:
    raise RuntimeError('Set RIDESYNC_BENCH_NAMESPACE to the reserved nonzero uint32 value')
if not 0 < namespace <= 0xffffffff:
    raise RuntimeError('RIDESYNC_BENCH_NAMESPACE must be a nonzero uint32 value')
if os.environ.get('RIDESYNC_BENCH_WRITE_OPT_IN') != '1':
    raise RuntimeError('Set RIDESYNC_BENCH_WRITE_OPT_IN=1 to acknowledge this bench writer')
env.Append(CPPDEFINES=[
    ('RIDESYNC_BENCH_NAMESPACE', str(namespace) + 'UL'),
    ('RIDESYNC_BENCH_WRITE_OPT_IN', 1),
])

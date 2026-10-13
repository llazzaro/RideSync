# Camera health fixed-memory audit

October 13, 2026 **Synthetic / compiler-measured** evidence. The pinned Xtensa
ESP32 compiler (`8.4.0`, esp-2021r2-patch5) gives a conservative **3504-byte**
health-only persistent ledger. Reserving **448 bytes** for the overlapping logger/serial
scratch gives **3952 bytes**, within the 4096-byte implementation cap by 144 bytes.
These measurements do not establish physical task stack margin or camera support.

Run the reproducible probe after the pinned HERO12 build:

```sh
PLATFORMIO_BUILD_DIR=/tmp/ridesync-health-target .venv/bin/pio run -e hero12_adapter_compile -e bench_application
.venv/bin/python scripts/probe_camera_health_resources.py --stack-dir /tmp/ridesync-health-target/hero12_adapter_compile
```

The script compiles size-only objects with the pinned target compiler and reads
symbol sizes without executing target code. It also executes a native `sizeof`
probe because Mach-O `nm` reports zero symbol sizes. Temporary header copies
remove only the explicitly named health members, preserving the common bookmark
data contracts and existing setup/power composition. Exact before/after layouts include padding,
alignment, virtual pointers, fixed arrays and enclosing objects. Missing expected
members fail the probe so future layout changes require an accounting review.
With `--stack-dir`, a budget regression exits nonzero. `--target-only` skips
native measurements; without stack evidence it reports persistent sizes only.

## Persistent composition ledger

| Allocation | ESP32 health increment | Native health increment |
|---|---:|---:|
| LocalTelemetryRuntime, including its active CameraEventSession, Storage, admission, configuration and status | 2512 | 2544 |
| HandlebarControl, including the second persistent status copy | 56 | 56 |
| CameraManager, eight registry slots and observer pointer | 100 | 72 |
| HERO12 adapter, including its central service | 392 | 400 |
| DJI adapter, including its central service | 152 | 168 |
| GO 3S and ONE RS central services | 264 | 272 |
| Three additional configuration copies in bench configuration, supervised ports and ESP32 telemetry wrapper | 12 | 24 |
| Global health serial sink and its port object | 16 | 32 |
| **Total persistent increment** | **3504** | **3568** |

The target cap applies to the ESP32 composition. Native pointer/alignment sizes
are reported separately rather than substituted for target measurements.
Core runtime, control, manager and adapter deltas are measured as enclosing
objects. The three additional SDK-wrapper configuration copies are conservatively
charged their LocalTelemetryConfig delta; wrapper padding may absorb part of
that increment. This avoids understating the budget without attributing unrelated
SDK-wrapper feature growth to health.

The evaluator occupies 2336 bytes: eight 224-byte peers, eight 64-byte transitions
and 32 bytes of evaluator bookkeeping/padding. Fields remain 32 bytes and retain
both historical receipt and latest ordering receipt, sequence, connection,
capability, validity, profile, unit and uint64 value. No evidence field packing
or global enum ABI changes were made. The eight-entry transition ring is within
issue #67's at-most-16 requirement; overflow retains current state and reports
history loss. Logger size is 84 target / 96 native bytes. It drains this existing
ring and allocates no second inbox.

The owner/runtime ledger includes the often hidden health increments: Storage
kind counters/cached-kind alignment adds 16 target bytes; the admission source
pointer adds eight after enclosing alignment; each LocalTelemetryStatus adds
48 bytes (24-byte summary plus one 24-byte kind-counter snapshot). The LED health
flags add four bytes after alignment. TelemetryRecord stays 824 bytes: the health
evidence shares the existing IMU union, so the eight-slot Storage queue does not
grow. Each of the four central services adds 32 x 4-byte event receipts plus one
4-byte clock pointer. DJI pending event stamps overlay existing capabilities and
add no pending-queue bytes. HERO12 polling adds 260 bytes beyond its central.

## Stack and bounded scratch

Pinned `-fstack-usage` evidence reports these static caller frames:

| Function | Bytes |
|---|---:|
| CameraHealthLogger::drain | 224 |
| CameraHealthSerialSink::transition | 224 |
| TelemetryAdmission::health | 128 |
| Storage::enqueueHealth | 880 |
| Storage::publish | 48 |
| Existing TelemetryAdmission::admitCameraInbox | 336 |
| Existing Storage::enqueueCamera | 864 |

The serial branch overlaps 224 + 224 = **448 bytes**, conservatively charged in
full to the health budget. Its 128-byte text buffer fits the longest qualified
line: 126 bytes including newline plus NUL, including maximum-width
IDs/counters and a 21-character reason. The regression test verifies an actual
complete write, newline and saturated counter text. The pinned Arduino SDK defaults
to a zero-length UART TX ring and reports free FIFO space from `availableForWrite`.
A capacity-127 fixture first rejected the original verbose line, then accepted the
complete compact 126-byte line; capacity 125 rejects it and counts a drop. No UART
TX buffer allocation was added. The runtime guide documents the compact keys.

The health Storage branch is 224 + 128 + 880 + 48 = **1280 bytes**. The existing
camera Storage branch is 336 + 864 + 48 = **1248 bytes**. These branches run
serially on the existing owner stack, sharing its reservation rather than
allocating another task or stack. Their measured logging high-water difference
is 32 bytes. The probe reserves `max(448, 32)`, yielding the conservative 3952-byte
result; the incremental-only logging calculation would be 3536 bytes. Common
upstream frames cancel in this comparison. This is a scoped static call-chain
comparison, not whole-task stack certification. Library callees, interrupts,
other existing owner paths and physical high-water margin remain outside these
numbers and require the existing activation qualification.

The largest individual new health function frame is Storage::enqueueHealth at
880 bytes, dominated by the existing 824-byte TelemetryRecord scratch contract.
Among evaluator/logger/output functions the largest frame is the serial sink at
224 bytes. Polling and notification work remains fixed and bounded; no memory,
heap, task or stack allocation grows with notification count.

## Verification

The capacity change first failed the bounded-ring test with `Expected 8 Was 16`.
After implementation: evaluator 20/20 and output 6/6 passed under ASan/UBSan;
health disk/logger/parser tests passed 10/10, including pressure, incomplete
history, final retirement and malformed schema. Rebuilt pinned HERO12 and bench
application targets passed. The probe passed its 4096-byte guard; a synthetic
oversized stack fixture verified that the guard exits nonzero. These are software
measurements only; new health support remains Experimental / Not tested physically.

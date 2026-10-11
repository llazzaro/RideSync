"""Check compiled owner frames with a fixed application budget and SDK reserve.

This checks real target instructions, not C++ source layout. It does not measure
interrupts, indirect SDK/libc calls or physical task high-water usage.
"""
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
objdump = Path.home() / ".platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-objdump"
build = root / ".pio/build/lilygo_t_a7670e_r2"
frames = {}
for artifact in (build / "firmware.elf", build / "src/config_storage.cpp.o", build / "src/config_bootstrap.cpp.o"):
    output = subprocess.check_output([str(objdump), "-d", "-C", str(artifact)], text=True)
    for name, body in re.findall(r"^[0-9a-f]+ <([^\n]+)>:\n(.*?)(?=^[0-9a-f]+ <|\Z)", output, re.M | re.S):
        entry = re.search(r"\bentry\s+a1,\s*(0x[0-9a-f]+|[0-9]+)", body)
        if entry:
            if name.startswith("_Z") and "$" in name:
                base, suffix = name.split("$", 1)
                name = subprocess.check_output([str(objdump.with_name("xtensa-esp32-elf-c++filt")), base], text=True).strip() + "$" + suffix
            # Retained object code can differ from the linked copy. Keep the
            # larger frame so either artifact remains a conservative bound.
            frames[name] = max(frames.get(name, 0), int(entry.group(1), 0))

def frame(fragment):
    matching = [(name, size) for name, size in frames.items()
                if fragment in name and "lambda" not in name]
    if not matching:
        raise RuntimeError("Missing compiled function: " + fragment)
    # GCC's $part$ split helpers execute under their public wrapper frame.
    normal, parts = [], []
    for name, size in matching:
        if "$part$" in name:
            parts.append(size)
        else:
            normal.append(size)
    return max(normal or [0]) + sum(parts)

# 1 KiB conservative application-helper/standard-library allowance on top of
# these nested frames; 8 KiB remains for SDK/libc/allocator/RTOS/interrupt paths.
# Those remaining paths require installed-device high-water qualification.
helper_reserve = 1024
budget = 4096
worker = frame("configTask(void*)")
startup = worker + frame("ConfigBootstrap::start(")
service = worker + frame("ConfigBootstrap::service(")
# These helpers retain separate call frames in the pinned build. Include them
# explicitly; the fixed allowance remains for inlined/helper/library work.
request_service = service + frame("ConfigBootstrap::serviceRequest(")
persistence_service = service + frame("ConfigBootstrap::servicePersistence(")
load_scan = startup + frame("ConfigPersistence::load(") + frame("ConfigPersistence::scan(") + frame("ConfigPersistence::scanSlot(")
service_scan = persistence_service + frame("ConfigPersistence::service(") + frame("ConfigPersistence::scan(") + frame("ConfigPersistence::scanSlot(")
proof_service = worker + frame("PairingProofMaintenance::service(") + frame("PairingProofMaintenance::perform(")
paths = {
    "load/decode": load_scan + frame("decodeConfig("),
    "load/scan/encode": load_scan + frame("ConfigPersistence::mergeSlot(") + frame("encodeConfig("),
    "startup/canonical copy": startup + frame("copySettings("),
    "service/debounce": persistence_service + frame("ConfigPersistence::service(") + frame("ConfigPersistence::saveDue("),
    "service/scan refusal": persistence_service + frame("ConfigPersistence::service(") + frame("ConfigPersistence::blockFailedScan("),
    "service/unchanged payload": persistence_service + frame("ConfigPersistence::service(") + frame("ConfigPersistence::unchangedPayload("),
    "service/write failure": persistence_service + frame("ConfigPersistence::service(") + frame("ConfigPersistence::failedWrite("),
    "service/scan/encode": service_scan + frame("ConfigPersistence::mergeSlot(") + frame("encodeConfig("),
    "service/decode": service_scan + frame("decodeConfig("),
    "service/expand request": request_service + frame("expandSettings("),
    "service/canonical copy": persistence_service + frame("ConfigBootstrap::adoptSaved(") + frame("copySettings("),
    "service/refusal": max(request_service, persistence_service) + frame("ConfigBootstrap::refuse("),
    "request/encode": request_service + frame("ConfigPersistence::request(") + frame("encodeConfig("),
    # Reset/retry are persistence API budgets only: the bootstrap exposes neither.
    "reset/request/encode": worker + frame("ConfigPersistence::reset(") + frame("ConfigPersistence::request(") + frame("encodeConfig("),
    "proof boot read": worker + frame("PairingProofMaintenance::beginOwner(") + frame("readFloor("),
    "proof service read": proof_service + frame("readFloor("),
    "proof service write": proof_service + frame("PairingProofMaintenance::writeFloor("),
    "retry": worker + frame("ConfigPersistence::retry("),
    "service/scan/read SDK boundary": service_scan + frame("NvsConfigStore::read("),
    "service/verify/read SDK boundary": persistence_service + frame("ConfigPersistence::service(") + frame("ConfigPersistence::verifyWrite(") + frame("NvsConfigStore::read("),
    "service/write SDK boundary": persistence_service + frame("ConfigPersistence::service(") + frame("NvsConfigStore::write("),
    "load/scan/read SDK boundary": load_scan + frame("NvsConfigStore::read("),
}
for name, size in paths.items():
    print(f"{name}: nested={size}, application allowance={size + helper_reserve}, limit={budget}")
for fragment in ("configTask(void*)", "ConfigBootstrap::", "copySettings(", "expandSettings(", "ConfigPersistence::", "decodeConfig(", "encodeConfig(", "PairingProofMaintenance::", "readFloor(", "NvsConfigStore::", "nvs_open", "nvs_commit", "nvs_set_blob", "nvs_get_blob"):
    for name, size in sorted(frames.items()):
        if fragment in name and "lambda" not in name:
            print(f"frame {size}: {name}")
if max(paths.values()) + helper_reserve > budget:
    print("FAIL: owner application stack exceeds 4096-byte budget; task is 12288 bytes", file=sys.stderr)
    sys.exit(1)
print("PASS: compiled application paths within budget; 8192-byte SDK/RTOS reserve, physical high-water still unverified")

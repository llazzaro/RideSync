"""Offline first-use commissioning for an exclusively held, host-mounted SD card.

The authority supplies a never-used opaque namespace from its external registry.
This tool cannot prove uniqueness, card power-loss durability or anti-rollback.
Never run while firmware/another process owns the volume. Partial failure retires
this namespace/card until explicit external recovery; never rerun to reset it.
"""
import argparse
import os
from pathlib import Path
import struct
import zlib


def commission(directory: Path, namespace: int) -> None:
    if not 0 < namespace <= 0xffffffff:
        raise ValueError("namespace must be a nonzero 32-bit commissioned value")
    payload = struct.pack("<9I", 0x44495352, 1, namespace, 0, 0,
                          namespace ^ 0xffffffff, 0xffffffff, 0xffffffff, 0)
    record = payload + struct.pack("<I", zlib.crc32(payload))
    paths = [directory / ".session-id-a", directory / ".session-id-b"]
    # No truncation, deletion, mkdir, fallback or automatic recovery.
    for path in paths:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            if os.write(fd, record) != len(record):
                raise OSError("short commissioning write; continuity uncertain")
            os.fsync(fd)
        finally:
            os.close(fd)
        if path.read_bytes() != record:
            raise OSError("commissioning verification failed")
    if any(path.read_bytes() != record for path in paths):
        raise OSError("commissioning pair verification failed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="existing root of host-mounted SD volume")
    parser.add_argument("--namespace", required=True, type=lambda s: int(s, 0))
    parser.add_argument("--authority-confirms-never-used", required=True, action="store_true",
                        help="external authority assigned this namespace uniquely and exclusively")
    args = parser.parse_args()
    commission(args.directory, args.namespace)
    print("Both baseline slots verified. Safely unmount the card before firmware ownership.")
    print("Actual power-loss durability and whole-state anti-rollback remain unqualified.")


if __name__ == "__main__":
    main()

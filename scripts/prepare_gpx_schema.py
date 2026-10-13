#!/usr/bin/env python3
"""Explicit development setup: fetch pinned GPX schema into an ignored cache."""
import hashlib
from pathlib import Path
import urllib.request

URL = 'https://www.topografix.com/GPX/1/1/gpx.xsd'
SHA256 = '9e4d1988b862edbe556305b130f8f6f1b29864fefd0dc02d5dab04ccdd1f34d6'
DESTINATION = Path(__file__).resolve().parents[1] / 'test/fixtures/gpx_schema/.cache/gpx-1.1.xsd'


def main():
    if DESTINATION.exists() and hashlib.sha256(DESTINATION.read_bytes()).hexdigest() == SHA256:
        print(f'Pinned GPX schema ready: {DESTINATION}')
        return
    with urllib.request.urlopen(URL, timeout=30) as response:
        data = response.read(128 * 1024 + 1)
    if len(data) > 128 * 1024 or hashlib.sha256(data).hexdigest() != SHA256:
        raise SystemExit('GPX schema size/hash mismatch; cache not changed.')
    DESTINATION.parent.mkdir(parents=True, exist_ok=True)
    temporary = DESTINATION.with_suffix('.tmp')
    try:
        temporary.write_bytes(data)
        temporary.replace(DESTINATION)
    finally:
        temporary.unlink(missing_ok=True)
    print(f'Pinned GPX schema ready: {DESTINATION}')


if __name__ == '__main__':
    main()

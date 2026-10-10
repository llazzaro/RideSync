#!/usr/bin/env python3
"""Compile and run the experimental C++ motion core against normalized CSV."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--experimental', action='store_true', required=True)
    parser.add_argument('--max-step-us', type=int, default=20000)
    parser.add_argument('--horizon-us', type=int, default=2000000)
    args = parser.parse_args()
    if not 0 < args.max_step_us <= args.horizon_us <= 2000000:
        parser.error('require 0 < max-step-us <= horizon-us <= 2000000')
    compiler = shutil.which('c++') or shutil.which('g++')
    if not compiler:
        parser.error('a local C++ compiler is required (c++ or g++)')
    if args.output.exists():
        parser.error('output exists; choose a new path')
    with tempfile.TemporaryDirectory(prefix='ridesync-motion-') as directory:
        executable = Path(directory) / 'replay'
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'include'),
                        str(ROOT / 'src/dynamic_motion_estimator.cpp'),
                        str(ROOT / 'tools/motion_replay/main.cpp'), '-o', str(executable)], check=True)
        # Publish only a fully parsed run; exclusive creation preserves existing data.
        with tempfile.TemporaryFile(mode='w+') as result:
            with args.input.open() as source:
                subprocess.run([str(executable), str(args.max_step_us), str(args.horizon_us)],
                               stdin=source, stdout=result, check=True)
            result.seek(0)
            with args.output.open('x') as destination:
                shutil.copyfileobj(result, destination)


if __name__ == '__main__':
    main()

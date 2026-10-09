#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

if [ -x "$root/.venv/bin/python" ]; then
  python_bin="$root/.venv/bin/python"
else
  python_bin=$(command -v python3 || command -v python)
fi

if [ -x "$root/.venv/bin/pio" ]; then
  pio_bin="$root/.venv/bin/pio"
else
  pio_bin=$(command -v pio)
fi

if [ -x "$root/.venv/bin/clang-format" ]; then
  format_bin="$root/.venv/bin/clang-format"
else
  format_bin=$(command -v clang-format)
fi

export RIDESYNC_CLANG_FORMAT="$format_bin"

"$python_bin" -m unittest discover -s test -v
"$python_bin" tools/bench/sd_logging/test_run.py -v
"$python_bin" tools/bench/x5_peripheral/test_capture.py -v
"$python_bin" tools/bench/gnss_driver/test_bench.py -v
"$pio_bin" test -e native
"$python_bin" scripts/check_format.py

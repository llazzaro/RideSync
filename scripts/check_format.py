"""Check tracked firmware source formatting without editing files."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
sources = sorted(
    path for directory in ("src", "include", "test", "tools/bench", "tools/motion_replay")
    for path in (root / directory).rglob("*")
    if path.suffix in {".cpp", ".h", ".hpp"} and ".pio" not in path.parts
)
formatter = os.environ.get("RIDESYNC_CLANG_FORMAT") or shutil.which("clang-format")
if not formatter:
    raise SystemExit("clang-format is required; install requirements-dev.txt first")
subprocess.run(
    [formatter, "--dry-run", "--Werror", *map(str, sources)],
    check=True, cwd=root,
)

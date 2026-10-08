"""Check tracked firmware source formatting without editing files."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
sources = sorted(
    path for directory in ("src", "include", "test", "tools/bench")
    for path in (root / directory).rglob("*")
    if path.suffix in {".cpp", ".h", ".hpp"} and ".pio" not in path.parts
)
subprocess.run(
    ["clang-format", "--dry-run", "--Werror", *map(str, sources)],
    check=True, cwd=root,
)

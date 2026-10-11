"""Compile native sources with Linux GCC/libstdc++, including from macOS hooks."""

import argparse
import platform
from pathlib import Path
import subprocess
import sys

# Debian Bookworm provides GCC 12/libstdc++; pin the Linux image by digest.
LINUX_IMAGE = (
    "python@sha256:b99029c95d3d37fb1e4e76d287f7984373dca77c665885986e31b2c95260c13c"
)


def check(root):
    sources = sorted(
        path.relative_to(root).as_posix()
        for path in (root / "src").rglob("*.cpp")
        if path.name != "main.cpp"
    )
    if not sources:
        raise SystemExit("Linux C++ portability check: no native sources found")
    compiler = ["g++", "-std=c++11", "-fsyntax-only", "-I", "include", *sources]
    if sys.platform == "linux":
        command = compiler
    else:
        architecture = (
            "arm64" if platform.machine() in ("arm64", "aarch64") else "amd64"
        )
        command = [
            "docker",
            "run",
            "--rm",
            "--platform",
            f"linux/{architecture}",
            "--network",
            "none",
            "--mount",
            f"type=bind,source={root},target=/work,readonly",
            "--workdir",
            "/work",
            LINUX_IMAGE,
            *compiler,
        ]
    print("Checking native C++ portability with Linux GCC/libstdc++", flush=True)
    try:
        return subprocess.run(command, cwd=root, check=False, timeout=180).returncode
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f"Linux C++ portability check could not run: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    arguments = parser.parse_args()
    raise SystemExit(check(arguments.root.resolve()))

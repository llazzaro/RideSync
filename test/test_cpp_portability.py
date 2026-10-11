"""The local Linux check must catch headers accepted by macOS libc++."""

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class CppPortabilityTests(unittest.TestCase):
    def check_header(self, portable, commit=False):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            root = pathlib.Path(directory)
            (root / "include").mkdir()
            (root / "src").mkdir()
            header = (ROOT / "include/health_supervisor.h").read_text()
            if not portable:
                header = header.replace("std::size_t", "size_t").replace(
                    "#include <cstddef>\n", ""
                )
            (root / "include/health_supervisor.h").write_text(header)
            (root / "src/check.cpp").write_text('#include "health_supervisor.h"\n')
            environment = os.environ.copy()
            # A real parent commit exports repository-local Git variables. The
            # nested fixture must discover its own repository and index.
            git_variables = subprocess.check_output(
                ["git", "rev-parse", "--local-env-vars"], text=True
            )
            for name in git_variables.splitlines():
                environment.pop(name, None)
            command = [
                sys.executable,
                str(ROOT / "scripts/check_cpp_portability.py"),
                "--root",
                str(root),
            ]
            if commit:
                (root / "scripts").mkdir()
                (root / ".githooks").mkdir()
                for name in ("check_pipeline.sh", "check_cpp_portability.py"):
                    shutil.copy2(ROOT / "scripts" / name, root / "scripts" / name)
                shutil.copy2(
                    ROOT / ".githooks/pre-commit", root / ".githooks/pre-commit"
                )
                if (ROOT / ".venv").exists():
                    (root / ".venv").symlink_to(
                        ROOT / ".venv", target_is_directory=True
                    )
                subprocess.run(
                    ["git", "init", "--quiet", str(root)], check=True, env=environment
                )
                subprocess.run(
                    ["git", "-C", str(root), "config", "core.hooksPath", ".githooks"],
                    check=True,
                    env=environment,
                )
                command = [
                    "git",
                    "-C",
                    str(root),
                    "-c",
                    "user.name=Hook test",
                    "-c",
                    "user.email=hook-test@example.invalid",
                    "-c",
                    "commit.gpgsign=false",
                    "commit",
                    "--allow-empty",
                    "--quiet",
                    "-m",
                    "Portability regression",
                ]
            return subprocess.run(
                command,
                capture_output=True,
                text=True,
                check=False,
                env=environment,
            )

    def test_self_contained_supervisor_header_compiles(self):
        result = self.check_header(True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unqualified_size_type_blocks_validation(self):
        result = self.check_header(False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("size_t", result.stderr)
        self.assertIn("has not been declared", result.stderr)

    def test_pre_commit_hook_rejects_the_linux_header_regression(self):
        result = self.check_header(False, commit=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("size_t", result.stderr)
        self.assertIn("has not been declared", result.stderr)


if __name__ == "__main__":
    unittest.main()

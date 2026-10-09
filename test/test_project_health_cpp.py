import shutil
import subprocess
import unittest


class ProjectHealthCppTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("project-health"), "optional project-health CLI is unavailable")
    def test_top_cpp_priorities_do_not_include_known_dispatchers(self):
        result = subprocess.run(
            ["project-health", "check", ".", "--languages", "cpp", "--format", "console"],
            check=False,
            capture_output=True,
            text=True,
        )
        priorities = result.stdout.split("PRIORITIES", 1)[-1]
        for symbol in (
            "handleCommand(uint32_t)",
            "ridesync::BleCentral::service(uint32_t)",
            "ridesync::CameraManager::event(const Event&)",
        ):
            self.assertNotIn(symbol, priorities)


if __name__ == "__main__":
    unittest.main()

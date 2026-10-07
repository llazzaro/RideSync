"""Repository contract checks; no firmware/runtime config loader exists yet."""
import json
from pathlib import Path
import unittest


class ExampleConfigTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(__file__).resolve().parents[1] / "examples/cameras.example.json"
        cls.cameras = json.loads(path.read_text())["cameras"]

    def test_all_initial_models_are_represented(self):
        self.assertEqual({c["model"] for c in self.cameras}, {"X5", "GO3S", "ONE_RS"})
        self.assertGreaterEqual(len(self.cameras), 3)

    def test_names_are_unique_and_telemetry_is_opt_in(self):
        self.assertEqual(len({c["name"] for c in self.cameras}), len(self.cameras))
        for camera in self.cameras:
            self.assertIs(camera["enabled"], True)
            self.assertIs(camera["gps_telemetry"], False)

    def test_no_invented_identifiers(self):
        for camera in self.cameras:
            self.assertIsNone(camera["identifier"])
            self.assertIsNone(camera["wake_identifier"])


if __name__ == "__main__":
    unittest.main()

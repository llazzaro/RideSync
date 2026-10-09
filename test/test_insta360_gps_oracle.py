# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy was not distributed with this file, obtain one
# at https://mozilla.org/MPL/2.0/.
# Wire layout attributed to arsfabula/Insta360-Remote-CIQ, BLE Barrel/BLEBarrel.mc,
# sendPosition/sendCMD at 39c51b3aa7c453227831d811355899371bbb8b94.
"""Independent standard-library oracle for actual pure C++ encoder output.

All locations/times are synthetic. No packet is a camera capture, and the
oracle does not certify camera acceptance. Expected bytes never use the C++
encoder or its test fixture/helper.
"""
import calendar
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def decode_packet(payload: bytes) -> dict:
    if len(payload) != 71:
        raise ValueError("GPS packet must contain exactly 71 bytes")
    return {
        "epoch": struct.unpack_from("<I", payload, 18)[0],
        "lat": struct.unpack_from("<d", payload, 29)[0],
        "ns": chr(payload[37]),
        "lon": struct.unpack_from("<d", payload, 38)[0],
        "ew": chr(payload[46]),
        "speed": struct.unpack_from("<d", payload, 47)[0],
        "course": struct.unpack_from("<d", payload, 55)[0],
        "altitude": struct.unpack_from("<d", payload, 63)[0],
    }


def expected_scalar(value: float) -> bytes:
    narrowed = struct.unpack("<f", struct.pack("<f", value))[0]
    return struct.pack("<d", 0.0 if narrowed == 0 else narrowed)


class GpsOracleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="ridesync-gps-oracle-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.probe = Path(cls.temporary.name) / "gps-probe"
        subprocess.run([
            "c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-Iinclude",
            "test/fixtures/insta360/gps_encoder_probe.cpp",
            "src/insta360_gps_encoder.cpp", "-o", str(cls.probe),
        ], cwd=ROOT, check=True, capture_output=True, text=True)

    def packet(self, case):
        text = subprocess.check_output([str(self.probe), case], text=True).strip()
        self.assertRegex(text, r"^[0-9a-f]{142}$")
        return bytes.fromhex(text)

    def test_complete_packets_from_independent_arithmetic(self):
        cases = {
            "ordinary": (1, -2, 3, 90, 4, "N", "W"),
            "south-east": (-1, 2, 3, 90, 4, "S", "E"),
            "zero": (-0.0, -0.0, -0.0, -0.0, -0.0, "N", "E"),
            "subnormal": (1, -2, 2.0**-149, 90, 2.0**-149, "N", "W"),
            "precision": (1.1, -2.2, 3.3, 90.1, 4.4, "N", "W"),
        }
        for case, (lat, lon, speed, course, altitude, ns, ew) in cases.items():
            with self.subTest(case=case):
                expected = bytes.fromhex("470000000400003500020100008000000a35")
                expected += struct.pack("<I", calendar.timegm((2000, 1, 1, 0, 0, 0)))
                expected += bytes.fromhex("00000000000041")
                expected += expected_scalar(abs(lat)) + ns.encode("ascii")
                expected += expected_scalar(abs(lon)) + ew.encode("ascii")
                expected += expected_scalar(speed) + expected_scalar(course) + expected_scalar(altitude)
                actual = self.packet(case)
                self.assertEqual(actual, expected)
                self.assertEqual(decode_packet(actual)["epoch"], 946684800)

    def test_zero_conversion_does_not_reproduce_upstream_exponent_bug(self):
        packet = self.packet("zero")
        decoded = decode_packet(packet)
        self.assertEqual((decoded["ns"], decoded["ew"]), ("N", "E"))
        for offset in (29, 38, 47, 55, 63):
            self.assertEqual(packet[offset:offset+8], bytes(8))
            self.assertNotEqual(packet[offset:offset+8], struct.pack("<d", 2.0**-127))

    def test_subnormal_is_promoted_without_exponent_rebias(self):
        packet = self.packet("subnormal")
        decoded = decode_packet(packet)
        self.assertEqual(decoded["speed"], 2.0**-149)
        self.assertEqual(decoded["altitude"], 2.0**-149)

    def test_binary32_precision_is_visible_in_the_wire(self):
        decoded = decode_packet(self.packet("precision"))
        self.assertEqual(decoded["lat"], 1.10000002384185791015625)
        self.assertNotEqual(decoded["lat"], 1.1)

    def test_packet_length_rejects_truncation_and_extra_bytes(self):
        packet = self.packet("ordinary")
        for invalid in (packet[:-1], packet + b"\x00"):
            with self.assertRaises(ValueError):
                decode_packet(invalid)

    def test_copied_bytes_cannot_modify_original_output(self):
        packet = self.packet("ordinary")
        copy = bytearray(packet)
        copy[:] = bytes(71)
        self.assertEqual(packet, self.packet("ordinary"))
        self.assertEqual(decode_packet(packet)["speed"], 3.0)


if __name__ == "__main__":
    unittest.main()

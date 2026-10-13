"""Finite synthetic RS1 host transcripts; no device IO."""

import os
import tempfile
import unittest
from tools.setup.protocol import (
    Client,
    ProtocolError,
    UncertainSave,
    decode_cameras,
    encode_cameras,
    parse_reply,
)
from tools.setup.preferences import (
    add_camera,
    edit_camera,
    private_export,
    safe_text,
    prerequisite_lines,
)
from tools.setup.__main__ import wizard, recover_inspection

CAMERA = {
    "capacity": 4,
    "cameras": [
        {
            "family": 1,
            "model": 1,
            "address_type": 1,
            "enabled": False,
            "gps_telemetry": False,
            "name": "Camera",
            "identifier": "AA:BB:CC:DD:EE:FF",
            "wake_identifier": "",
        }
    ],
}


class Serial:
    def __init__(self, lines):
        self.data = bytearray(b"\n".join(lines) + b"\n")
        self.writes = []

    def write(self, data):
        self.writes.append(data)
        return len(data)

    def read(self, count):
        if not self.data:
            raise OSError("disconnected")
        result = bytes(self.data[:count])
        del self.data[:count]
        return result


class HostTests(unittest.TestCase):
    def test_transcript_complete_wizard_one_dispatch(self):
        payload = encode_cameras(CAMERA).encode()
        transport = Serial(
            [
                b"diagnostic Ok None",
                b"@RS1 0 1 9 4 HELLO Ok None challenge=12",
                b"@RS1 0 2 9 4 ENTER Pending IsolationRequired",
                b"@RS1 7 2 9 4 ENTER Ok None",
                b"@RS1 7 3 9 4 INSPECT Ok None " + payload,
                b"@RS1 7 4 9 4 REPORT Ok None P1;2,2,0,1,1,0;0,1,0,1,0,1,0,0,0,7,7,0",
                b"@RS1 7 5 9 4 DRAFT_BEGIN Ok None",
                b"@RS1 7 6 9 4 DRAFT_SET Ok None",
                b"@RS1 7 7 9 4 VALIDATE Ok None",
                b"@RS1 7 8 9 4 DISPATCH Pending None",
                b"@RS1 7 8 9 5 DISPATCH Ok None",
                b"@RS1 7 9 9 5 STATUS Ok None status=Durable,session=7,request=8,epoch=9,generation=5,outcome=2,persist=1,code=0,indeterminate=0",
                b"@RS1 7 10 9 5 EXIT Pending OwnerRelease",
                b"@RS1 7 10 9 5 EXIT Ok None",
            ]
        )
        inputs = iter(["PARKED", "save", "SAVE", "status", "exit"])
        wizard(Client(transport), ask=lambda _: next(inputs), output=lambda _: None)
        self.assertEqual(sum(b" DISPATCH" in frame for frame in transport.writes), 1)
        self.assertFalse(
            any(b"REC" in frame or b"STOP" in frame for frame in transport.writes)
        )

    def test_roundtrip(self):
        self.assertEqual(decode_cameras(encode_cameras(CAMERA)), CAMERA)

    def test_wrong_session_version_partial_and_oversize(self):
        for line in [
            b"@RS1 8 1 0 0 INSPECT Ok None",
            b"@RS2 0 1 0 0 INSPECT Ok None",
            b"@RS1 broken",
            b"x" * 2049,
        ]:
            with self.subTest(line=line[:30]), self.assertRaises(ProtocolError):
                Client(Serial([line])).command("INSPECT")

    def test_out_of_order_and_diagnostics(self):
        result = Client(
            Serial(
                [
                    b"noise",
                    b"@RS1 0 99 0 0 INSPECT Ok None",
                    b"@RS1 0 1 0 0 INSPECT Ok None",
                ]
            )
        ).command("INSPECT")
        self.assertEqual(result.request, 1)

    def test_save_disconnect_and_no_replay(self):
        client = Client(Serial([]))
        with self.assertRaises(UncertainSave):
            client.command("DISPATCH")
        with self.assertRaises(UncertainSave):
            client.command("DISPATCH")
        self.assertEqual(len(client.serial.writes), 1)

    def test_invalid_drafts_capacity_controls_utf8_duplicate(self):
        for name in ["", "a" * 65, "é" * 33, "bad\0", "bad\x1b", "bad\u202e"]:
            with self.subTest(name=name), self.assertRaises(ValueError):
                edit_camera(CAMERA, 0, {"name": name})
        with self.assertRaises(ValueError):
            edit_camera(CAMERA, 0, {"qualified": True})
        with self.assertRaises(ValueError):
            edit_camera({"cameras": CAMERA["cameras"] * 5}, 0, {"name": "OK"})
        with self.assertRaises(ValueError):
            edit_camera({"cameras": CAMERA["cameras"] * 2}, 0, {"name": "OK"})

    def test_device_validation_refusal(self):
        reply = Client(Serial([b"@RS1 0 1 0 0 VALIDATE Refused InvalidName"])).command(
            "VALIDATE"
        )
        self.assertEqual(reply.error, "InvalidName")

    def test_private_permissions_and_terminal_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "private.json")
            private_export(path, CAMERA)
            self.assertEqual(os.stat(path).st_mode & 0o777, 0o600)
            with self.assertRaises(FileExistsError):
                private_export(path, CAMERA)
        self.assertNotIn("\x1b", safe_text("hostile\x1b[2J"))

    def test_pending_dispatch_blocks_replay_and_reconciles_status(self):
        client = Client(
            Serial(
                [
                    b"@RS1 0 1 0 0 DISPATCH Pending None",
                    b"@RS1 0 1 0 1 DISPATCH Ok None",
                    b"@RS1 0 2 0 1 STATUS Ok None status=Durable",
                ]
            )
        )
        client.command("DISPATCH", completion=False)
        with self.assertRaises(UncertainSave):
            client.command("DISPATCH")
        result = client.command("STATUS")
        self.assertEqual(result.payload, "status=Durable")
        self.assertEqual(client.generation, 1)
        self.assertEqual(sum(b" DISPATCH" in f for f in client.serial.writes), 1)

    def test_stale_generation_and_epoch(self):
        for line in [
            b"@RS1 0 1 1 0 VALIDATE Ok None",
            b"@RS1 0 1 0 1 VALIDATE Ok None",
        ]:
            with self.assertRaises(ProtocolError):
                Client(Serial([line])).command("VALIDATE")

    def test_deadline_partial_frame(self):
        class QuietSerial(Serial):
            def read(self, count):
                return b"@"

        values = iter(range(10))
        with self.assertRaises(ProtocolError):
            Client(QuietSerial([]), timeout=2, clock=lambda: next(values)).command(
                "INSPECT"
            )

    def test_prerequisites_are_independent_and_unknown(self):
        text = "\n".join(prerequisite_lines("P1;2,2,2,1,1,0;0,1,1,1,0,1,0,0,0,7,7,0"))
        self.assertIn("Provider: Available", text)
        self.assertIn("identity: Unknown", text)
        self.assertIn("restore/store: Missing", text)
        self.assertIn("firmware: Unknown", text)
        self.assertIn("Recording observation: Unknown", text)
        with self.assertRaises(ValueError):
            prerequisite_lines("P1;0,0,-1,0,0,0")

    def test_readonly_recovery_retains_old_save_without_replay(self):
        payload = encode_cameras(CAMERA).encode()
        transport = Serial(
            [
                b"@RS1 0 1 9 4 HELLO Ok None challenge=13",
                b"@RS1 0 2 9 4 RECOVER Pending OwnerRelease",
                b"@RS1 7 99 9 5 DISPATCH Ok None",
                b"@RS1 8 2 9 5 RECOVER Ok None",
                b"@RS1 8 3 9 5 INSPECT Ok None " + payload,
                b"@RS1 8 4 9 5 STATUS Ok None status=Durable,session=7,request=99,epoch=9,generation=5,outcome=2,persist=1,code=0,indeterminate=0",
                b"@RS1 8 5 9 5 REPORT Ok None P1;2,2,0,1,0,0",
                b"@RS1 8 6 9 5 EXIT Pending OwnerRelease",
                b"@RS1 8 6 9 5 EXIT Ok None",
            ]
        )
        client = Client(transport)
        output = []
        recover_inspection(client, ask=lambda _: "PARKED", output=output.append)
        self.assertIn(b"HELLO RECOVERY", transport.writes[0])
        self.assertTrue(any("session=7,request=99" in line for line in output))
        self.assertFalse(
            any(b" DISPATCH" in line or b" ENTER" in line for line in transport.writes)
        )
        for operation in [
            "DRAFT_BEGIN",
            "DRAFT_SET",
            "VALIDATE",
            "DISPATCH",
            "RESTART",
        ]:
            with self.assertRaises(ProtocolError):
                client.command(operation)
        self.assertEqual(len(transport.writes), 6)

    def test_legacy_eight_slot_prerequisites_inspectable(self):
        rows = [f"{slot},1,0,3,0,0,0,0,0,7,7,6" for slot in range(8)]
        lines = prerequisite_lines("P1;2,6,0,3,8,0;" + ";".join(rows))
        self.assertTrue(any("Slot 7" in line for line in lines))

    def test_add_disabled_camera_to_empty_defaults(self):
        proposed = add_camera({"capacity": 8, "cameras": []}, CAMERA["cameras"][0])
        self.assertEqual(proposed["capacity"], 4)
        self.assertFalse(proposed["cameras"][0]["enabled"])
        self.assertFalse(proposed["cameras"][0]["gps_telemetry"])
        with self.assertRaises(ValueError):
            add_camera(
                {"capacity": 4, "cameras": CAMERA["cameras"] * 4}, CAMERA["cameras"][0]
            )

    def test_unresolved_selection_never_dispatches(self):
        payload = encode_cameras(CAMERA).encode()
        transport = Serial(
            [
                b"@RS1 0 1 9 4 HELLO Ok None challenge=12",
                b"@RS1 7 2 9 4 ENTER Ok None",
                b"@RS1 7 3 9 4 INSPECT Ok None " + payload,
                b"@RS1 7 4 9 4 REPORT Ok None P1;2,2,0,1,0,0",
                b"@RS1 7 5 9 4 DRAFT_BEGIN Ok None",
                b"@RS1 7 6 9 4 DRAFT_SET Ok None",
                b"@RS1 7 7 9 4 SELECT Ok UnresolvedIdentity enabled=0,identity_verified=0,profile_qualified=0,save_allowed=0",
                b"@RS1 7 8 9 4 DRAFT_GET Ok None " + payload,
                b"@RS1 7 9 9 4 EXIT Ok None",
            ]
        )
        inputs = iter(["PARKED", "select", "0", "2", "0", "REPLACE", "save", "exit"])
        wizard(Client(transport), ask=lambda _: next(inputs), output=lambda _: None)
        self.assertFalse(
            any(
                b" DISPATCH" in frame or b" VALIDATE" in frame
                for frame in transport.writes
            )
        )

    def test_local_edit_after_validate_survives_next_selection(self):
        import copy

        edited = copy.deepcopy(CAMERA)
        edited["cameras"][0]["name"] = "Edited name"
        selected = copy.deepcopy(edited)
        second = dict(
            CAMERA["cameras"][0],
            model=2,
            identifier="11:22:33:44:55:66",
            name="Selected",
        )
        selected["cameras"].append(second)
        payload = encode_cameras(CAMERA).encode()
        transport = Serial(
            [
                b"@RS1 0 1 9 4 HELLO Ok None challenge=12",
                b"@RS1 7 2 9 4 ENTER Ok None",
                b"@RS1 7 3 9 4 INSPECT Ok None " + payload,
                b"@RS1 7 4 9 4 REPORT Ok None P1;2,2,0,1,0,0",
                b"@RS1 7 5 9 4 DRAFT_BEGIN Ok None",
                b"@RS1 7 6 9 4 DRAFT_SET Ok None",
                b"@RS1 7 7 9 4 VALIDATE Ok None",
                b"@RS1 7 8 9 4 DRAFT_SET Ok None",
                b"@RS1 7 9 9 4 SELECT Ok NeedsCommissioning enabled=0,identity_verified=1,profile_qualified=0,save_allowed=1",
                b"@RS1 7 10 9 4 DRAFT_GET Ok None " + encode_cameras(selected).encode(),
                b"@RS1 7 11 9 4 EXIT Ok None",
            ]
        )
        inputs = iter(
            ["PARKED", "validate", "edit", "0", "name", "select", "0", "2", "1", "exit"]
        )
        output = []
        wizard(
            Client(transport),
            ask=lambda _: next(inputs),
            private_ask=lambda _: "Edited name",
            output=output.append,
        )
        sync = next(
            frame for frame in transport.writes if b" 8 9 4 DRAFT_SET " in frame
        )
        self.assertIn(encode_cameras(edited).encode(), sync)
        self.assertEqual(sum(b" DRAFT_BEGIN" in frame for frame in transport.writes), 1)
        self.assertTrue(
            any("Edited name" in line and "Selected" in line for line in output)
        )

    def test_verified_second_selection_does_not_clear_unresolved_latch(self):
        payload = encode_cameras(CAMERA).encode()
        transport = Serial(
            [
                b"@RS1 0 1 9 4 HELLO Ok None challenge=12",
                b"@RS1 7 2 9 4 ENTER Ok None",
                b"@RS1 7 3 9 4 INSPECT Ok None " + payload,
                b"@RS1 7 4 9 4 REPORT Ok None P1;2,2,0,1,0,0",
                b"@RS1 7 5 9 4 DRAFT_BEGIN Ok None",
                b"@RS1 7 6 9 4 DRAFT_SET Ok None",
                b"@RS1 7 7 9 4 SELECT Ok UnresolvedIdentity enabled=0,identity_verified=0,profile_qualified=0,save_allowed=0",
                b"@RS1 7 8 9 4 DRAFT_GET Ok None " + payload,
                b"@RS1 7 9 9 4 DRAFT_SET Ok None",
                b"@RS1 7 10 9 4 SELECT Ok NeedsCommissioning enabled=0,identity_verified=1,profile_qualified=0,save_allowed=1",
                b"@RS1 7 11 9 4 DRAFT_GET Ok None " + payload,
                b"@RS1 7 12 9 4 EXIT Ok None",
            ]
        )
        inputs = iter(
            [
                "PARKED",
                "select",
                "0",
                "2",
                "0",
                "REPLACE",
                "select",
                "1",
                "2",
                "0",
                "REPLACE",
                "save",
                "exit",
            ]
        )
        wizard(Client(transport), ask=lambda _: next(inputs), output=lambda _: None)
        self.assertEqual(sum(b" DRAFT_BEGIN" in frame for frame in transport.writes), 1)
        self.assertEqual(sum(b" DRAFT_SET" in frame for frame in transport.writes), 2)
        self.assertFalse(
            any(
                b" DISPATCH" in frame or b" VALIDATE" in frame
                for frame in transport.writes
            )
        )

    def test_numeric_bounds(self):
        with self.assertRaises(ProtocolError):
            parse_reply(b"@RS1 4294967296 1 0 0 HELLO Ok None")


if __name__ == "__main__":
    unittest.main()

import unittest
from dataclasses import replace
from controller import Controller, Request


class Owner:
    def __init__(self):
        self.requests = []

    def submit(self, request):
        self.requests.append(request)


class TrialTests(unittest.TestCase):
    def setUp(self):
        self.owner = Owner()
        self.c = Controller(self.owner)
        self.entry = dict(physical_entry=True, isolated=True, fresh_secrets=True,
                          trusted_certificate=True, session=1, allocation_ok=True)
        self.assertEqual(self.c.enter(0, **self.entry), "Ok")

    def test_entry_refuses_missing_premises_and_allocation(self):
        for field in self.entry:
            c = Controller(Owner())
            args = dict(self.entry, **{field: False})
            self.assertEqual(c.enter(0, **args), "Refused")
            self.assertEqual(c.state, "disabled")

    def test_security_and_bounds(self):
        cases = [({"authenticated": False}, "Unauthorized"),
                 ({"csrf_verified": False}, "Unauthorized"),
                 ({"origin": "https://evil.test"}, "Origin"),
                 ({"host": "evil.test"}, "Origin"),
                 ({"method": "GET", "operation": "DISPATCH"}, "Method"),
                 ({"body_size": 2049}, "Malformed"),
                 ({"complete": False}, "Malformed"),
                 ({"session": 2}, "Session")]
        for i, (fields, expected) in enumerate(cases):
            self.assertEqual(self.c.request(replace(Request(), **fields), i * 1000), expected)
        self.assertEqual(self.owner.requests, [])

    def test_single_client_replay_and_pressure(self):
        self.assertEqual(self.c.request(Request(operation="DISPATCH"), 1), "Pending")
        self.assertEqual(self.c.request(Request(request=2, client=2), 2), "Busy")
        self.assertEqual(self.c.request(Request(), 3), "Replay")
        for i in range(4, 9):
            self.c.request(Request(request=i, operation="STATUS"), i)
        self.assertEqual(self.c.request(Request(request=9), 9), "RateLimited")
        self.assertEqual(len(self.owner.requests), 1)

    def test_deadlines_and_rollover(self):
        self.assertEqual(self.c.request(Request(), 120000), "Expired")
        c = Controller(Owner())
        c.enter(0xFFFFFFF0, **self.entry)
        self.assertEqual(c.tick(119984), "stopping")
        c = Controller(Owner())
        c.enter(0, **self.entry)
        for i in range(1, 6):
            c.request(Request(request=i, operation="STATUS"), i * 100000)
        self.assertEqual(c.tick(600000), "stopping")

    def test_disconnect_commit_and_serial_status_recovery(self):
        self.c.request(Request(operation="DISPATCH"), 1)
        self.c.disconnect()
        self.assertEqual(self.c.network_stopped(False), "TeardownBlocked")
        self.assertFalse(self.c.complete(99, 1, "Durable"))
        self.assertTrue(self.c.complete(1, 1, "Durable"))
        self.assertFalse(self.c.complete(1, 1, "Durable"))
        self.assertEqual(self.c.network_stopped(True), "closed")
        self.assertEqual(self.c.result, "Durable")
        self.assertEqual(len(self.owner.requests), 1)

    def test_cancel_pending_camera_never_dispatches(self):
        self.c.request(Request(operation="DISCOVER"), 1)
        self.c.cancel()
        self.assertEqual(self.c.network_stopped(True), "closed")
        self.assertIsNone(self.c.pending)
        self.assertEqual(self.owner.requests, [])

    def test_pause_ack_precedes_camera_and_retains_result(self):
        self.assertEqual(self.c.request(Request(operation="DISCOVER"), 1), "PendingTeardown")
        self.assertEqual(self.owner.requests, [])
        self.assertEqual(self.c.network_stopped(False), "TeardownBlocked")
        self.assertEqual(self.owner.requests, [])
        self.assertEqual(self.c.network_stopped(True), "camera")
        self.assertEqual(len(self.owner.requests), 1)
        self.assertEqual(self.c.camera_released(), "Busy")
        self.c.complete(1, 1, "NeedsCommissioning")
        self.assertEqual(self.c.camera_released(), "ReconnectRequiresNewLease")
        self.assertEqual(self.c.result, "NeedsCommissioning")


if __name__ == "__main__":
    unittest.main()

"""Experimental pure policy trial; never opens sockets or calls SetupService.

Injected owner is the sole serial service owner. Auth/CSRF values below are
already verified by a future HTTPS adapter; this model does not verify TLS.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class Request:
    client: int = 1
    session: int = 1
    request: int = 1
    method: str = "POST"
    host: str = "192.168.4.1"
    origin: str = "https://192.168.4.1"
    authenticated: bool = True
    csrf_verified: bool = True
    complete: bool = True
    body_size: int = 1
    operation: str = "INSPECT"


class Controller:
    HOST = "192.168.4.1"
    ORIGIN = "https://192.168.4.1"
    IDLE = 120_000
    ABSOLUTE = 600_000
    BODY = 2048

    def __init__(self, owner):
        self.owner = owner
        self.state = "disabled"
        self.client = None
        self.started = self.last = self.last_request = 0
        self.session = 0
        self.pending = None
        self.result = None
        self.count = self.window = 0

    def enter(self, now, *, physical_entry, isolated, fresh_secrets, trusted_certificate,
              session, allocation_ok):
        if self.state != "disabled":
            return "Busy"
        if not all((physical_entry, isolated, fresh_secrets, trusted_certificate,
                    session, allocation_ok)):
            return "Refused"
        self.session = session
        self.started = self.last = self.window = now
        self.state = "active"
        return "Ok"

    def tick(self, now):
        if self.state == "active" and (
            ((now - self.last) & 0xFFFFFFFF) >= self.IDLE
            or ((now - self.started) & 0xFFFFFFFF) >= self.ABSOLUTE
        ):
            self.state = "stopping"
        return self.state

    def request(self, req, now):
        if self.tick(now) != "active":
            return "Expired"
        if ((now - self.window) & 0xFFFFFFFF) >= 1000:
            self.window, self.count = now, 0
        self.count += 1
        if self.count > 8:
            return "RateLimited"
        if req.host != self.HOST or req.origin != self.ORIGIN:
            return "Origin"
        if not req.authenticated or not req.csrf_verified:
            return "Unauthorized"
        if req.method != "POST":
            return "Method"
        if not req.complete or not 0 < req.body_size <= self.BODY:
            return "Malformed"
        if req.session != self.session or not 0 < req.request <= 0xFFFFFFFF:
            return "Session"
        if self.client is not None and self.client != req.client:
            return "Busy"
        if req.request <= self.last_request:
            return "Replay"
        self.client, self.last_request, self.last = req.client, req.request, now
        if req.operation == "STATUS":
            return self.result or ("Pending" if self.pending else "Idle")
        if self.pending is not None:
            return "Busy"
        if req.operation in ("DISCOVER", "PAIR"):
            # Stop requested now; no camera dispatch until actual SDK teardown acknowledgement.
            self.pending = req
            self.state = "pausing"
            return "PendingTeardown"
        if req.operation not in ("INSPECT", "DISPATCH"):
            return "Unavailable"
        # Only the injected serialized owner may invoke the real service/mailbox.
        self.pending = req
        self.owner.submit(req)
        return "Pending"

    def network_stopped(self, success):
        if self.state not in ("pausing", "stopping"):
            return "Ignored"
        if not success:
            return "TeardownBlocked"
        if self.state == "pausing":
            self.state = "camera"
            self.owner.submit(self.pending)
        else:
            self.state = "closed"
        return self.state

    def complete(self, session, request, result):
        if self.pending is None or (session, request) != (
            self.pending.session, self.pending.request
        ):
            return False
        self.result = result
        self.pending = None
        return True

    def cancel(self):
        if self.state == "pausing":
            self.pending = None  # Camera work was never submitted.
        self.state = "stopping"

    def disconnect(self):
        # Cannot roll back an in-flight commit or transfer ownership to another client.
        self.cancel()

    def camera_released(self):
        if self.state != "camera" or self.pending is not None:
            return "Busy"
        # Credential rotation, fresh authentication and owner lease are unimplemented gates.
        self.state = "closed"
        return "ReconnectRequiresNewLease"

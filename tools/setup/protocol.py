"""Bounded RS1 framing and one-owner request correlation."""

import time
from dataclasses import dataclass

MAX_FRAME = 2048


class ProtocolError(Exception):
    pass


class UncertainSave(ProtocolError):
    """Inspect after reconnect; never replay this dispatch."""


@dataclass(frozen=True)
class Reply:
    session: int
    request: int
    epoch: int
    generation: int
    operation: str
    outcome: str
    error: str
    payload: str = ""


def parse_reply(line):
    if len(line) > MAX_FRAME:
        raise ProtocolError("Oversized frame")
    try:
        parts = line.decode("ascii").split(" ", 8)
        if parts[0] != "@RS1":
            raise ProtocolError("Protocol version mismatch")
        if len(parts) < 8:
            raise ValueError()
        numbers = [int(value) for value in parts[1:5]]
        if any(
            v < 0 or v > maximum
            for v, maximum in zip(numbers, [2**32 - 1] * 3 + [2**64 - 1])
        ):
            raise ValueError()
        return Reply(*numbers, *parts[5:8], parts[8] if len(parts) == 9 else "")
    except (UnicodeError, ValueError):
        raise ProtocolError("Malformed setup reply") from None


class Client:
    def __init__(self, serial, timeout=5, clock=time.monotonic):
        self.serial, self.timeout, self.clock = serial, timeout, clock
        self.session = self.epoch = self.generation = self.request_id = 0
        self.buffer = bytearray()
        self.save_uncertain = False
        self.save_admitted = False
        self.read_only = False

    def command(self, operation, payload="", *, completion=True):
        if self.read_only and operation not in {
            "INSPECT",
            "STATUS",
            "REPORT",
            "EXIT",
            "CANCEL",
        }:
            raise ProtocolError("Recovery session permits inspection only")
        if (self.save_uncertain or self.save_admitted) and operation not in {
            "HELLO",
            "ENTER",
            "RECOVER",
            "CANCEL",
            "INSPECT",
            "STATUS",
            "REPORT",
            "EXIT",
        }:
            raise UncertainSave(
                "Reconnect and inspect; previous save must not be replayed"
            )
        self.request_id += 1
        frame = f"@RS1 {self.session} {self.request_id} {self.epoch} {self.generation} {operation}"
        if payload:
            frame += " " + payload
        encoded = frame.encode("ascii")
        if len(encoded) > MAX_FRAME or any(c in encoded for c in (10, 13, 0)):
            raise ProtocolError("Invalid outgoing frame")
        deadline = self.clock() + self.timeout
        try:
            if self.serial.write(encoded + b"\n") != len(encoded) + 1:
                raise ProtocolError("Incomplete serial write")
            while self.clock() < deadline:
                chunk = self.serial.read(1)
                if not chunk:
                    continue
                if chunk != b"\n":
                    self.buffer.extend(chunk)
                    if len(self.buffer) > MAX_FRAME:
                        raise ProtocolError("Oversized incoming frame")
                    continue
                line, self.buffer = bytes(self.buffer).rstrip(b"\r"), bytearray()
                if not line.startswith(b"@RS"):
                    continue  # diagnostics never count as replies, never log them
                reply = parse_reply(line)
                if reply.request != self.request_id or reply.operation != operation:
                    continue
                expected_session = self.session
                if reply.session != expected_session and not (
                    operation in {"ENTER", "RECOVER"}
                    and reply.outcome == "Ok"
                    and reply.session > 0
                ):
                    raise ProtocolError("Wrong session")
                if operation != "HELLO" and reply.epoch != self.epoch:
                    raise ProtocolError("Stale epoch")
                if (
                    operation
                    not in {
                        "HELLO",
                        "RECOVER",
                        "INSPECT",
                        "STATUS",
                        "REPORT",
                        "DISPATCH",
                    }
                    and reply.generation != self.generation
                ):
                    raise ProtocolError("Stale generation")
                if completion and reply.outcome == "Pending":
                    continue
                if operation == "RECOVER" and reply.outcome == "Ok":
                    self.read_only = True
                if operation == "DISPATCH" and reply.outcome in {"Pending", "Ok"}:
                    self.save_admitted = True
                self.session, self.epoch, self.generation = (
                    reply.session,
                    reply.epoch,
                    reply.generation,
                )
                return reply
            raise ProtocolError("Serial deadline expired")
        except (OSError, ProtocolError):
            if operation == "DISPATCH" or self.save_admitted:
                self.save_uncertain = True
                raise UncertainSave(
                    "Save outcome uncertain; reconnect for inspection only"
                ) from None
            raise


def decode_cameras(payload):
    groups = payload.split(";")
    try:
        count, capacity = map(int, groups[0].split(","))
    except ValueError:
        raise ProtocolError("Malformed camera count") from None
    if not 0 <= count <= 8 or not count <= capacity <= 8 or len(groups) != count + 1:
        raise ProtocolError("Malformed camera count")
    cameras = []
    try:
        for group in groups[1:]:
            fields = group.split(",")
            if len(fields) != 8:
                raise ValueError()
            family, model, address, enabled, gps = map(int, fields[:5])
            if (
                family not in range(4)
                or model not in range(9)
                or address not in range(3)
                or enabled not in (0, 1)
                or gps not in (0, 1)
            ):
                raise ValueError()
            strings = [
                "" if s == "-" else bytes.fromhex(s).decode("utf-8") for s in fields[5:]
            ]
            cameras.append(
                dict(
                    family=family,
                    model=model,
                    address_type=address,
                    enabled=bool(enabled),
                    gps_telemetry=bool(gps),
                    name=strings[0],
                    identifier=strings[1],
                    wake_identifier=strings[2],
                )
            )
        return {"capacity": capacity, "cameras": cameras}
    except (ValueError, UnicodeError):
        raise ProtocolError("Malformed camera fields") from None


def encode_cameras(candidate):
    cameras = candidate["cameras"]
    groups = [f"{len(cameras)},{candidate['capacity']}"]
    for camera in cameras:
        values = [camera[key] for key in ("family", "model", "address_type")]
        values += [int(camera[key]) for key in ("enabled", "gps_telemetry")]
        values += [
            camera[key].encode("utf-8").hex() or "-"
            for key in ("name", "identifier", "wake_identifier")
        ]
        groups.append(",".join(map(str, values)))
    return ";".join(groups)

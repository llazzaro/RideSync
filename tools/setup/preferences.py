"""Local safety checks; firmware remains the authoritative validator."""

import copy
import json
import os
import re
import unicodedata

CAMERA_FIELDS = frozenset(
    {
        "name",
        "model",
        "family",
        "identifier",
        "address_type",
        "wake_identifier",
        "enabled",
        "gps_telemetry",
    }
)


def safe_text(value):
    """Escape all terminal controls, including bidi/format characters."""
    return "".join(
        c if not unicodedata.category(c).startswith("C") else f"\\u{ord(c):04x}"
        for c in str(value)
    )


def validate_name(name):
    if not isinstance(name, str) or not 1 <= len(name.encode("utf-8")) <= 64:
        raise ValueError("Name must contain 1–64 UTF-8 bytes")
    if any(unicodedata.category(c).startswith("C") for c in name):
        raise ValueError("Name cannot contain controls or format characters")


def edit_camera(candidate, slot, changes):
    if set(changes) - CAMERA_FIELDS:
        raise ValueError("Unknown or non-camera field")
    result = copy.deepcopy(candidate)
    cameras = result["cameras"]
    if len(cameras) > 4 or not 0 <= slot < len(cameras):
        raise ValueError("Only existing slots in groups of at most four are editable")
    camera = cameras[slot]
    camera.update(changes)
    validate_name(camera["name"])
    for flag in ("enabled", "gps_telemetry"):
        if type(camera[flag]) is not bool:
            raise ValueError("Capability flags must be boolean")
    if not re.fullmatch(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", camera["identifier"]):
        raise ValueError("Identifier must be a typed MAC address")
    if len({c["identifier"].upper() for c in cameras}) != len(cameras):
        raise ValueError("Duplicate identifier")
    return result


def private_export(path, value):
    """Never overwrite an existing file; exported inspection is private."""
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=True, indent=2)
        stream.write("\n")


def public_summary(reply):
    """Allowlist fields for default diagnostics, never raw device payloads."""
    return {
        key: reply[key]
        for key in ("session", "request", "status", "epoch", "generation")
        if key in reply and isinstance(reply[key], (int, bool))
    }


def prerequisite_lines(payload):
    """Render bounded P1 facts independently from camera preferences."""
    evidence = [
        "Unknown",
        "Missing",
        "Matched",
        "Mismatched",
        "Revoked",
        "Unsupported",
        "Refused",
        "Not requested",
        "Denied",
        "Timeout",
    ]
    readiness = [
        "Unknown",
        "Needs commissioning",
        "Unsupported profile",
        "Refused",
        "Ready",
    ]
    provider = ["Missing", "Declined", "Available", "Refused", "Unknown"]
    rows = payload.split(";")
    if not rows or rows[0] != "P1":
        return [
            "Independent prerequisites: " + safe_text(payload),
            "Recording observation: Unknown",
        ]
    try:
        settings, group, supplied, overall, count, truncated = map(
            int, rows[1].split(",")
        )
        if (
            min(settings, group, supplied, overall, count, truncated) < 0
            or count > 8
            or len(rows) != count + 2
        ):
            raise ValueError()
        result = [
            f"Provider: {provider[supplied]}; overall: {readiness[overall]}",
            f"Settings: {evidence[settings]}; runtime group: {evidence[group]}; truncated: {bool(truncated)}",
        ]
        labels = [
            "identity",
            "restore/store",
            "profile",
            "firmware",
            "API",
            "wake",
            "GPS",
            "runtime",
        ]
        for row in rows[2:]:
            values = list(map(int, row.split(",")))
            if len(values) != 12 or any(v < 0 for v in values):
                raise ValueError()
            slot, model, enabled, ready = values[:4]
            result.append(
                f"Slot {slot}, model {model}, requested enabled {bool(enabled)}: {readiness[ready]}"
            )
            result.append(
                "; ".join(
                    f"{label}: {evidence[value]}"
                    for label, value in zip(labels, values[4:])
                )
            )
        result.append("Recording observation: Unknown")
        return result
    except (ValueError, IndexError):
        raise ValueError("Malformed prerequisite report") from None


def add_camera(candidate, camera):
    """Append one declared disabled preference; never qualify an identity."""
    if len(candidate["cameras"]) >= 4:
        raise ValueError("Four camera slots already occupied")
    result = copy.deepcopy(candidate)
    result["capacity"] = 4
    result["cameras"].append(dict(camera, enabled=False, gps_telemetry=False))
    return edit_camera(result, len(result["cameras"]) - 1, {})

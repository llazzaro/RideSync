"""Run with python -m tools.setup --port /dev/cu.usbserial-...."""

import argparse
import copy
import getpass
import json
import sys
from .preferences import (
    add_camera,
    edit_camera,
    private_export,
    prerequisite_lines,
    safe_text,
)
from .protocol import (
    Client,
    ProtocolError,
    UncertainSave,
    decode_cameras,
    encode_cameras,
)

MODELS = {
    1: (1, "X5"),
    2: (1, "GO 3S"),
    3: (1, "ONE RS"),
    4: (2, "HERO12"),
    5: (3, "Action 4"),
    6: (3, "Action 5"),
    7: (3, "Action 6"),
    8: (3, "Osmo 360"),
}


def require_ok(reply):
    if reply.outcome != "Ok":
        raise ProtocolError(
            f"Device outcome {safe_text(reply.outcome)} / {safe_text(reply.error)}"
        )
    return reply


def preview(saved, draft, output=print):
    output("Saved preferences:")
    output(safe_text(json.dumps(saved, ensure_ascii=False, indent=2)))
    output("Unsaved draft:")
    output(safe_text(json.dumps(draft, ensure_ascii=False, indent=2)))
    output(
        "Preferences cannot establish identity, restore, firmware/profile, wake or GPS qualification. Recording observation: Unknown."
    )


def wizard(client, ask=input, private_ask=getpass.getpass, output=print):
    hello = require_ok(client.command("HELLO"))
    if not hello.payload.startswith("challenge=") or not hello.payload[10:].isdigit():
        raise ProtocolError("Missing entry challenge")
    output(
        "Use only while parked. USB setup isolates ride control. Discovery requires a qualified scan owner; commissioning remains unavailable."
    )
    if ask("Type PARKED to enter setup: ") != "PARKED":
        return
    require_ok(client.command("ENTER", hello.payload[10:] + " PARKED"))
    saved = decode_cameras(require_ok(client.command("INSPECT")).payload)
    draft = copy.deepcopy(saved)
    device_draft = False
    save_allowed = True
    report = client.command("REPORT")
    for line in prerequisite_lines(report.payload):
        output(line)
    try:
        while True:
            action = ask(
                "inspect / add / edit / discover / inventory / select / cancel-discovery / preview / validate / save / status / export / exit: "
            ).strip()
            if action == "exit":
                require_ok(client.command("EXIT"))
                return
            if action == "inspect":
                saved = decode_cameras(require_ok(client.command("INSPECT")).payload)
                preview(saved, draft, output)
            elif action == "preview":
                preview(saved, draft, output)
            elif action == "add":
                if client.save_admitted:
                    output("Save already submitted; inspect or exit.")
                    continue
                output(str({key: value[1] for key, value in MODELS.items()}))
                model = int(ask("Declared model number: "))
                camera = {
                    "family": MODELS[model][0],
                    "model": model,
                    "name": private_ask("Camera name: "),
                    "identifier": private_ask("Known identifier: "),
                    "address_type": int(ask("Type 1 public or 2 random: ")),
                    "wake_identifier": "",
                }
                if camera["address_type"] not in (1, 2):
                    raise ValueError("Use 1 public or 2 random")
                proposed = add_camera(draft, camera)
                preview(saved, proposed, output)
                output(
                    "This declares disabled preferences, not verified identity; capacity becomes four."
                )
                if ask("Type ADD to keep this unsaved draft: ") == "ADD":
                    draft = proposed
            elif action == "discover":
                reply = client.command("DISCOVER", completion=False)
                output(
                    f"Discovery: {safe_text(reply.outcome)} / {safe_text(reply.error)}. Use inventory; scan completion requires owner release."
                )
            elif action == "inventory":
                index = int(ask("Candidate index 0–15, or 16 for summary: "))
                if not 0 <= index <= 16:
                    raise ValueError("Candidate index out of bounds")
                reply = client.command("INVENTORY", str(index))
                output(safe_text(reply.payload or reply.error))
            elif action == "cancel-discovery":
                reply = client.command("CANCEL_DISCOVERY", completion=False)
                output(
                    f"Cancellation: {safe_text(reply.outcome)}; use inventory to inspect actual released=1."
                )
            elif action == "select":
                if client.save_admitted:
                    output("Save already submitted; inspect or exit.")
                    continue
                index = int(ask("Candidate index: "))
                output(str({key: value[1] for key, value in MODELS.items()}))
                model = int(
                    ask("Explicit model number (not inferred from advertisement): ")
                )
                slot = int(ask("Slot 0–3, existing slot or next empty slot: "))
                if model not in MODELS or not 0 <= index < 16 or not 0 <= slot < 4:
                    raise ValueError("Invalid selection")
                preview(saved, draft, output)
                if (
                    slot < len(draft["cameras"])
                    and ask("Type REPLACE to replace this existing slot: ") != "REPLACE"
                ):
                    continue
                if not device_draft:
                    require_ok(client.command("DRAFT_BEGIN"))
                    device_draft = True
                if not draft["cameras"] and draft["capacity"] > 4:
                    draft = dict(draft, capacity=4)
                    preview(saved, draft, output)
                    output(
                        "Selection uses a four-slot draft; saved preferences remain unchanged."
                    )
                # Preserve local edits before asking the device to replace one slot.
                # DRAFT_BEGIN would reset the unresolved-selection admission latch.
                require_ok(client.command("DRAFT_SET", encode_cameras(draft)))
                reply = require_ok(client.command("SELECT", f"{index},{model},{slot}"))
                save_allowed = save_allowed and "save_allowed=1" in reply.payload
                draft = decode_cameras(require_ok(client.command("DRAFT_GET")).payload)
                preview(saved, draft, output)
                output(safe_text(reply.error + "; " + reply.payload))
            elif action == "edit":
                if client.save_admitted:
                    output(
                        "Save already submitted. Inspect status or exit; do not resubmit."
                    )
                    continue
                slot = int(ask("Existing slot number (zero based): "))
                field = ask(
                    "name / model / identifier / address_type / enabled / wake_identifier / gps_telemetry: "
                ).strip()
                if field == "model":
                    output(str({key: value[1] for key, value in MODELS.items()}))
                    model = int(ask("Model number: "))
                    changes = {"model": model, "family": MODELS[model][0]}
                else:
                    value = private_ask("New value (private input): ")
                    if field in {"enabled", "gps_telemetry"}:
                        if value not in {"true", "false"}:
                            raise ValueError("Use true or false")
                        value = value == "true"
                    elif field == "address_type":
                        value = int(value)
                        if value not in (1, 2):
                            raise ValueError("Use 1 public or 2 random")
                    changes = {field: value}
                draft = edit_camera(draft, slot, changes)
                output("Draft changed; unsaved.")
            elif action in {"validate", "save"}:
                if client.save_admitted:
                    output(
                        "Save already submitted. Inspect status or exit; do not resubmit."
                    )
                    continue
                if not save_allowed:
                    output(
                        "Unresolved identity selection cannot be saved; discard and inspect commissioning requirements."
                    )
                    continue
                preview(saved, draft, output)
                if not device_draft:
                    require_ok(client.command("DRAFT_BEGIN"))
                    device_draft = True
                require_ok(client.command("DRAFT_SET", encode_cameras(draft)))
                require_ok(client.command("VALIDATE"))
                if (
                    action == "save"
                    and ask("Type SAVE to submit this whole draft exactly once: ")
                    == "SAVE"
                ):
                    reply = client.command("DISPATCH", completion=False)
                    output(
                        f"Save: {safe_text(reply.outcome)} / {safe_text(reply.error)}. Restart required after durable verification."
                    )
            elif action == "status":
                reply = client.command("STATUS")
                output(
                    f"Save status: {safe_text(reply.outcome)} / {safe_text(reply.error)}; {safe_text(reply.payload)}"
                )
            elif action == "export":
                if (
                    ask("Type PRIVATE to export camera preferences to a private file: ")
                    == "PRIVATE"
                ):
                    private_export(
                        ask("New file path: "), {"saved": saved, "draft": draft}
                    )
                    output("Private export created (0600).")
            else:
                output("Unknown action.")
    except (ValueError, KeyError) as exc:
        output(safe_text(str(exc)))
        require_ok(client.command("EXIT"))


def recover_inspection(client, ask=input, output=print):
    hello = require_ok(client.command("HELLO", "RECOVERY"))
    if not hello.payload.startswith("challenge=") or not hello.payload[10:].isdigit():
        raise ProtocolError("Missing recovery challenge")
    output("Read-only recovery waits for old owners; no save is replayed.")
    if ask("Type PARKED for inspection-only recovery: ") != "PARKED":
        return
    require_ok(client.command("RECOVER", hello.payload[10:] + " PARKED"))
    reply = require_ok(client.command("INSPECT"))
    saved = decode_cameras(reply.payload)
    preview(saved, saved, output)
    status = require_ok(client.command("STATUS"))
    output("Retained save result: " + safe_text(status.payload))
    report = require_ok(client.command("REPORT"))
    for line in prerequisite_lines(report.payload):
        output(line)
    require_ok(client.command("EXIT"))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument(
        "--inspect-only",
        action="store_true",
        help="Reconcile after uncertain save; never dispatch mutations",
    )
    args = parser.parse_args(argv)
    if not 0 < args.timeout <= 30:
        parser.error("timeout must be greater than zero and at most 30 seconds")
    import serial

    try:
        with serial.Serial(
            args.port, 115200, timeout=0.1, write_timeout=args.timeout, exclusive=True
        ) as transport:
            client = Client(transport, args.timeout)
            if args.inspect_only:
                recover_inspection(client)
            else:
                wizard(client)
    except (ProtocolError, OSError, KeyboardInterrupt) as exc:
        print("Setup stopped: " + safe_text(str(exc)), file=sys.stderr)
        if isinstance(exc, UncertainSave):
            print(
                "Reconnect with --inspect-only. Never repeat the save automatically.",
                file=sys.stderr,
            )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

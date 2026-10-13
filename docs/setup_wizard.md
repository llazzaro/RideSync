# Parked USB camera preference wizard

Build the explicit setup-only image with the pinned development environment:

```sh
.venv/bin/pio run -e serial_setup_application
```

Uploading it is a separate backed-up, parked commissioning session. It loads the
existing guarded settings and can read private provider declarations, but never
initializes board buses or launches camera/telemetry workers. Normal bench and
production images retain their existing console. `serial_setup_private_compile`
only checks an always-declined private provider; it supplies no hardware facts.

For qualified central discovery, supply the actual reviewed host/store receipt
in private `BenchApplicationConfig::setup_store`, or reuse the existing reviewed
X5 store receipt. A missing receipt explicitly refuses radio startup. The setup
image pauses the exclusive NVS owner through host initialization/restoration;
no new proof, bond or identity is created. X5 CE80 pairing remains camera-initiated
and outside central discovery.

Install the pinned host dependencies in a virtual environment:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-dev.txt
.venv/bin/python -m tools.setup --port /dev/cu.usbserial-DEVICE
```

On Linux select `/dev/ttyUSB0` or the installed device's stable serial path.
Close other serial monitors first: the wizard takes exclusive ownership of the
port at 115200 baud. It never opens another reader or a BLE host. The default
response deadline is five seconds; `--timeout` accepts at most 30 seconds.

Attach and reboot RideSync while parked, then type `PARKED` during the device's
30-second entry window. Entry waits for the firmware isolation acknowledgement.
The device enforces 120 seconds idle and ten minutes absolute session duration.
USB possession and the operator declaration do not prove automatic motion safety.

Use `inspect` to view saved preferences privately in your terminal. `edit`
selects an existing slot and changes one permitted camera field. Values use
private input without echo; model and slot numbers are ordinary input. Names
must fit 64 UTF-8 bytes and contain no controls. Public/random address type is
explicit; syntax does not verify stable identity. `add` declares one known camera as a disabled preference, previews the complete
group and requires `ADD`; an empty eight-capacity default becomes an explicitly
previewed four-capacity draft. No automatic reorder is offered. Legacy groups larger than four remain inspectable and cannot be edited
through this workflow. The firmware preserves all non-camera settings.

`preview` displays the complete saved camera group and local unsaved draft.
`validate` submits the complete candidate to the authoritative firmware validator.
`save` previews and validates, then requires the exact word `SAVE`. The host sends
one DISPATCH, displays the outcome, and prevents another save in that client.
Pending is not Durable. Device validation and storage errors are terminal for
that request; there is no automatic retry. Every completed save requires restart
and fresh independent qualification before control.

Use `status` for the retained device result, including its original session/request,
boot epoch, generation, persistence result and indeterminate flag. A STATUS reply
header correlates the current query; its payload correlates the original save. On timeout, disconnect or uncertain
save, stop and reconnect with:

```sh
.venv/bin/python -m tools.setup --port /dev/cu.usbserial-DEVICE --inspect-only
```

This path uses `HELLO RECOVERY` and `RECOVER <challenge> PARKED` at any boot
time. It revokes old admission and waits for the actual save/discovery owner
barrier before issuing a fresh read-only session. That session permits only
inspection, status, prerequisite report and exit/cancel; mutations are refused. It never repeats save,
pairing, restart, REC or STOP. An admitted commit can finish after disconnect;
inspect its durable generation and retained result before any later manual edit.
A reset may erase transient request correlation, so a missing result must not be
presented as a cancelled commit. `exit` revokes the session and drops unsent work.
Restart the device deliberately after storage and setup owners are quiescent.

Provider, verified identity, bond/store restoration, exact installed firmware,
profile/API, wake and GPS qualification are independent of these preferences.
Recording observation remains Unknown unless independently observed. `discover`
requests the qualified shared scan owner; unavailable or missing qualification is
reported explicitly. `inventory` reads candidate indexes 0–15, or 16 for the
bounded summary. `cancel-discovery` requests cancellation; inventory must report
`released=1` before selection or another owner operation. `select` requires an
explicit candidate, model and slot; replacing a slot requires `REPLACE`. Selected
cameras stay disabled. Unresolved identity reports `save_allowed=0` and the host
refuses validate/save. Names/services/RSSI never select a model automatically.
Model-specific guidance is available in the prerequisite report and
[commissioning guide](setup_commissioning.md); automatic first-time commissioning
remains unavailable. A valid saved disabled
preference cannot manufacture evidence or enable control.

Raw diagnostic lines are discarded, including interleaved lines containing words
such as Ok. Only bounded RS1 frames with matching request, operation, session and
epoch are replies. Camera names are escaped before terminal output. The wizard
writes no default transcript. `export` requires `PRIVATE` and creates a new file
with mode 0600; it refuses to overwrite an existing path. The export and private
terminal inspection include names and addresses, so keep them out of public reports.
There are no token/key arguments or qualification checkboxes.

Synthetic host verification:

```sh
python3 -m unittest discover -s test -p test_setup_host.py -v
```

These fixtures verify software framing, drafts and request behavior; they do not
establish USB hardware behavior, camera commissioning or physical durability.

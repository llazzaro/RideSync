# Bounded setup discovery (#79)

The setup serial owner can inventory central-role advertisements only after the
private provider supplies independently reviewed shared-host qualification and
guarded restored-store proof. Missing proof returns `NotQualified`/`Unavailable`;
entering setup or editing preferences never supplies proof. The existing
`Esp32BleHost` remains the exclusive host, and `BleCentral` leases its scan slot.
There is no second host or host reset and no connect, new bond, camera approval,
recording command, eviction or targeted reset in this workflow.

`DISCOVER` first requests host readiness (state `Initializing`, enum 6). Only
after the configuration task pauses and the private restore proof is installed
does the owner start the shared host. ENTER alone never starts BLE. Qualified
readiness then starts one 10,000 ms scan. The inventory holds at most 16 copied
entries, merging only the same raw GAP public/random type and observed address.
It exposes truncation separately from completion. Names (32 bytes), up to four
16-bit/128-bit service UUID hints and RSSI are observations, never model/firmware or
identity proof. Names are hex encoded on serial; ASCII terminal control bytes are
replaced by `?`. Malformed or oversized hints are marked truncated. The host
copies the legacy GAP payload before callback return.

`INVENTORY <index>` returns summary and a single entry; index 16 returns summary
only. `SELECT <index>,<model>,<slot>` explicitly declares the model enum from
`camera_profile.h` and zero-based slot. Selection requires scan callback release,
an editable draft and fewer than five application slots, counting disabled
entries. X5 returns `X5ManualCommissioning`: its production CE80 route is
peripheral, requiring the existing private camera-initiated commissioning
procedure. This implementation cannot advertise for X5 or interpret a central
BE80 hint as the X5 production identity.

A verified identity comes only from `SetupIdentityMapping`, supplied by the
private evidence owner or authenticated existing stack resolution. Advertisements
never set `verified`. Mappings retain typed observed address separately from
stable public/random-static identity. Conflicting aliases fail unresolved;
verified aliases and configured textual duplicates are rejected across slots.
The existing host's bond admission is checked before verified selection for
bond-requiring models. DJI uses its documented application authentication route:
disabled selection requires the guarded ready store but does not demand an SMP
bond. No camera approval or encrypted channel is implied. No
bond is created, including when the store has available capacity.

Verified selection writes a **disabled** draft and returns `NeedsCommissioning`,
with identity and profile qualification reported independently. Unresolved
selection remains an unsaved disabled draft and returns `UnresolvedIdentity`;
`VALIDATE` and `DISPATCH` refuse it. `DRAFT_BEGIN` discards that draft. Model
approval, installed firmware, restored store and capability qualification still
come from the independent provider at next boot.

`CANCEL_DISCOVERY`, session expiry, detach and exit seal scan admission. Deadline
or cancellation alone does not release memory: terminal callback, host-queue
barrier and successful host-context release are all required. Another scan and
selection are refused during release. A cancellation failure retains the lease
until actual release; inventory does not claim success. Generations and sealed
callback contexts prevent old results from populating a later session.

## Finite software evidence

`test/test_setup_discovery/test_main.cpp` exercises qualified/unqualified host
entry, 10-second lease, concurrent requests, 16-entry truncation, repeated typed
observations, name/service/RSSI copies, completion and delayed release, deadline
cancellation, unresolved draft refusal, explicit X5 role handling, slot capacity,
verified duplicate identity and bond admission. Its serial transcript drives
DISCOVER, INVENTORY, SELECT, DISPATCH refusal and EXIT callback barrier.

Run in the serialized native suite, or use an isolated Clang binary linked with
`setup_service`, `setup_discovery`, `ble_remote`, `config`, `config_bootstrap`,
`config_storage`, `button_manager`, `health_supervisor`, `pairing_reset` and Unity.
Synthetic tests establish software contracts only. Camera approval, installed
firmware, physical compatibility and observed REC/STOP remain #46/#22.

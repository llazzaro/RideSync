# Single-X5 serial milestone (#3)

The production CE80 peripheral backend, captured-display adapter and one-attempt
camera manager are composed in `x5_serial_milestone`. This is an Experimental
single-X5 route for the captured firmware/settings profile. Other models,
GPS forwarding, mixed roles and local telemetry are separate issue scope.
The default image does not activate this route. Compilation and synthetic tests
do not qualify an installed camera; the final observed check belongs to #46.

## Private commissioning

Keep the verified factory backup, then back up the currently installed full flash
before upload. Do not erase/format NVS or change partition layout to obtain a pass.
Keep camera addresses, store receipts, serial transcripts and binary backups outside
Git. Never substitute an arbitrary digest, an assumed empty store or a synthetic
CI identity for observed commissioning evidence.

1. Declare board/revision, X5 firmware, video mode and card availability. The
   captured display profile is based on owner-reported X5 firmware 1.11.10.
   Confirm the stable public or random-static camera identity independently;
   unresolved private addresses are refused. No address is inferred from names.
2. Build/upload `x5_store_inspect` on the backed-up board. It initializes only the
   existing exclusive configuration owner and reads the guarded `nimble_bond`
   store through `Esp32BleHost::inspectStore(false)`. It never starts BLE,
   advertises, subscribes or commands the camera. Its one `X5_STORE` serial line
   publishes completion/error, seven schema counts and the exact SHA256 digest
   into the private receipt. A failed/incomplete observation cannot commission.
   This is a diagnostic build; ordinary milestone status never prints the digest.
3. Independently review that observation and stable identity. Retain a private
   qualification receipt with an increasing nonzero `qualification_record`
   (never UINT32_MAX), greater than any previously revoked record. Qualification
   is an operator decision; the observer does not create or persist it. Unknown
   revocation history or mismatching store must be resolved explicitly without
   bond erasure. `PairingProofMaintenance` reads the existing revocation floor
   and the host refuses a revoked record or changed digest/counts.
4. Create ignored `include/x5_commissioning.local.h` defining
   `bool ridesyncPrivateX5Qualification(ridesync::X5Qualification&,
   ridesync::SourceConfig&)`. Set `enabled`, verified stable identity, exact
   firmware ASCII bytes/size (zero trailing bytes), observed store digest/counts,
   independent qualification record and `X5CapturedDisplayV1`. Supply exactly one
   enabled Insta360/X5 camera, private uppercase colon-separated identity,
   matching address type, no wake identifier and `gps_telemetry=false`.
   SDK address bytes are least-significant octet first; the textual identifier
   is the reversed display order. The random-static SDK identity must have its
   top two bits set. Do not include credentials or private values in public
   templates. The provider returns false if any premise remains unqualified.
5. Build `x5_serial_milestone` with that provider. A missing provider is an explicit
   commissioning refusal. The configuration-owner ready handshake is bounded
   to 1000 ms; late readiness cannot reactivate the runtime. Safe mode/NVS
   failures revoke admission permanently for that boot. Boot is idle: only an
   explicit CONNECT advertises. Do not run two serial owners against the USB port.

```sh
.venv/bin/pio run -e x5_adapter_compile -e x5_serial_milestone -e x5_store_inspect
# After software verification/review and the declared backed-up hardware session:
.venv/bin/pio run -e x5_store_inspect -t upload --upload-port <private-port>
.venv/bin/pio run -e x5_serial_milestone -t upload --upload-port <private-port>
```

The inspector publishes no keys, addresses or raw bond bytes. It preserves the
existing store. It does not attest restoration compatibility: the production
host subsequently compares the exact guarded before/after store and live SDK
records on startup. A failed comparison is a concrete commissioning failure.
The milestone uses raw shared-host initialization, never NimBLEDevice::init or
another store owner. No automatic security handshake, bond creation or eviction
is admitted. Installed NVS remains a prerequisite, not a disposable test fixture.

## Commands and observation

Each complete LF/CRLF line is one command: CONNECT, REC, STOP, QUERY, STATUS,
DISCONNECT. Case and spelling are exact; no whitespace trimming. Lines exceeding
31 characters, invalid bytes, embedded CR or unknown commands are discarded
through LF. One complete-line mailbox is retained; extra input while it is full
is discarded as whole lines and counted in `rejected`. There is no delayed
recording-command queue. Each loop reads at most 32 serial bytes and the adapter
polls at most 32 receive events, servicing deadlines before serial dispatch.

CONNECT has a 15000 ms budget covering host readiness, advertising, matching
peer and CE82 notify subscription. REC/STOP/QUERY have 5000 ms budgets. The
manager has one attempt; no timeout, disconnect, error, reboot or reconnect can
replay a shutter. DISCONNECT retires intent and requests cleanup without an
implicit STOP. A new CONNECT waits for the actual SDK terminal/barrier release.

The retained service layout matches the observed probe: CE80 (CE81 Write,
CE82 Notify/CCCD, CE83 Read `01 02`) and its additional D0FF attributes. CE82
subscription is required. The advertisement and scan-response name remain
`Insta360 GPS Remote`; preserving the other attributes does not prove each one
is required. Only complete admitted CE81 writes are decoded, with 256-byte
storage and a 32-event mailbox. Loss/oversize revokes control; no prefix decode.

REC requires fresh Stopped + Video evidence. STOP requires fresh Recording and
that connection's qualified Video epoch. A typed timer can report Recording
without establishing mode; such a connection still refuses STOP. Photo,
unsupported settings, stale/missing evidence or Busy refuse explicitly. The
5000 ms observation age is application policy, not a measured camera guarantee.
Opaque/remaining-time packets do not refresh recording evidence. Silence is
Unknown, not Stopped. Both commands use the same mode-dependent shutter toggle;
there is no wire Start/Stop distinction. Separately verify current camera mode
before each operator command; unseen camera-side changes remain a limitation.

QUERY is passive observation, with no invented query or ACK bytes. Status prints
accepted operation ID (`id`) and independent latest request rejection (`last_id`,
`last_error`, `last_refusal`), configured/revoked state, link/subscription, active operation,
Recording/Stopped/Unknown, age availability, lifecycle/error and a named failure.
SDK send success is not camera success. An eligible desired display after the
submission cutoff completes the operation but proves no causal ACK or exact
camera-action time. An ambiguous outcome requires a fresh explicit request and
fresh state, never an automatic retry.

## Finite final camera check

After passing software CI and fresh review, declare the exact commissioned
revision and installed firmware/board/card/video profile. CONNECT, confirm fresh
video Stopped, then request one REC. The owner independently confirms recording
on the camera. After fresh Recording status, request one STOP and independently
confirm camera stop. Retain the serial outcomes and observed status separately.
Reuse the existing three-cycle/reconnect evidence when the wire profile matches;
do not repeat the generic capture campaign. Record failures honestly and rerun
only affected cases after a fix. Playback/support matrix/integrated ride claims
retain their existing owners. #3 closes only after this final composed-path
observation and its software criteria pass.

A lost receive generation stays unqualified until disconnect and a fresh CONNECT.
At final SDK admission, any newer CE81 receipt or expired approving observation
revokes the queued toggle; no automatic replacement toggle is sent.

## Optional bounded wake/recovery (#9)

`x5_wake_milestone` composes the source-qualified M5 wake advertisement with the
same production CE80 adapter. It remains uncommissioned unless the private
provider also implements:

```cpp
bool ridesyncPrivateX5WakeConfig(ridesync::WakePeerConfig &config,
                               ridesync::WakePolicy &policy);
```

Supply the separately evidenced six-byte wake identifier, `enabled=true`,
`source_qualified=true`, and `profile=insta360::WakeProfile::M5WakeV1`.
The identifier is not the BLE address. The default policy allows one three-second
advertisement within a fifteen-second total wake/recovery deadline. Keep actual
identifiers and store proofs private. Other camera profiles remain disabled.
Missing/invalid wake qualification refuses WAKE while preserving independently
qualified CONNECT/REC/STOP. The ordinary `x5_serial_milestone` refuses WAKE.

Boot is idle. An explicit `WAKE` initializes the already configured shared host
on a separate 4096-byte boot-lifetime startup worker, with a startup deadline
bounded by the first advertising slice. SDK initialization cannot block serial
service. Startup failure is sticky for that boot; the worker never retains or
sends a packet. A late return after cancellation cannot advertise by itself.
Once ready, the existing raw wake worker acquires one advertising lease. Its
actual callback/worker release must finish before the CE80 recovery Connect.

WAKE is refused while a control link, camera operation or cleanup lease exists;
it cannot disrupt an active recording connection. Recovery requires the verified
identity, CE82 subscription and fresh typed Video observation on the new link.
Subscription or an accepted SDK operation alone cannot make recovery Ready.
Unknown/photo/stale traffic waits only until the original total deadline. A
failed/canceled recovery retires intent and waits for the real peripheral cleanup
barrier; late display events cannot make it successful. There is no retry.

Use `WAKE`, wait for `X5_WAKE phase=4 error=0 released=1` and a fresh X5 Video
status, then separately request `REC`. Wake never issues a shutter command.
`STOP` uses the existing adapter; `DISCONNECT` cancels wake/recovery and disconnects
without an implicit STOP. CONNECT/QUERY/REC/STOP received during wake or cleanup
return Busy and are discarded. STATUS is read-only. `X5_WAKE command_error`
reports the last serial dispatch result; phase/error/released describe wake.
The Ready phase records completed recovery, while its observed state follows
current adapter evidence and can return Unknown after staleness/disconnection.

The reusable `X5WakeRecovery` implements the existing WakeRecovery contract for
peer zero of this single-X5 runtime. Other peer indices return Unsupported.
It neither ticks a group coordinator nor issues Start. For a group owner, use
`WakeManager(radio, &recovery)`, attach its `WakePreparation` to the existing
`RecordingManager`, and call `runtime.adapter().attachRecording(group)` before
control service. Attachment requires that group's exact CameraManager, rejects
replacement/active-operation attachment, and routes real adapter observations,
completion and failure through the group authority. The group outlives service;
once attached, all control requests come through it. Serialize
`group.beginServicePass()`, `runtime.service()`, `preparation.service()` and
`group.tick()` in that order. The native composition test verifies wake → connect
→ fresh Video → one Start → observed Recording through these real components,
then explicit STOP and another REC without a second advertisement or connection.
The recovery provider supplies a read-only current observation only while the
real adapter has fresh Video command admission and no pending recovery lease;
WakeManager reuses that qualified live link. Unknown/stale/busy links cannot take
this shortcut. This does not relax an explicit serial WAKE live-link refusal.
Existing finite group tests cover missing-peer progress; new-model/mixed
composition still depends on their own adapters and #22 qualification.

Software tests/builds establish composition, not physical wake or startup stack
adequacy. Support remains Experimental. The final declared power-state
wake → reconnect → fresh observation → explicit REC/STOP and playable-clip check
is collected once in [#46](https://github.com/llazzaro/RideSync/issues/46), reusing
the isolated wake and unchanged three-cycle evidence. Startup/worker stack and
radio latency observations belong to the existing integrated acceptance session.

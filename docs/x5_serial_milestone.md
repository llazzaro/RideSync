# Single-X5 serial milestone (#3)

The production CE80 peripheral backend, captured-display adapter and one-attempt
camera manager are composed in `x5_serial_milestone`. This is an Experimental
single-X5 route for the captured firmware/settings profile. Other models, wake,
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

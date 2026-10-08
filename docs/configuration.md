# NVS startup admission

The pinned firmware wraps the C SDK `nvs_flash_init()` and
`esp_partition_erase_range(const esp_partition_t *, size_t, size_t)` at link time.
Before `setup()`, Arduino initializes default NVS. Its no-free-pages/new-version
fallback attempts a full format; the guard returns `ESP_ERR_NOT_SUPPORTED`
for offset zero and the exact nonzero size of every DATA/NVS partition, regardless
of label. Partial page erases, non-NVS operations, null partitions and invalid
ranges retain the real SDK's validation/result. No SDK or partition layout edits
are required. The guard also refuses explicit whole-NVS formats.

`ridesync::nvsBootStatus()` exposes `init_observed`, `init_in_progress`,
`first_init_failure`, `last_init_result`, `format_refused` and `refusal_error`.
The first initialization failure and format refusal remain latched for this boot;
a later successful init does not clear them. `persistenceAllowed()` requires an
observed successful default init, no init in progress, no earlier init failure,
and no refused format. Evidence uses constant-initialized, lock-free integer
atomics, fixed publication steps and one first-failure compare/exchange; it does
not allocate, log, or access NVS handles before setup.

This snapshot is observational, not a transaction with an SDK operation. Issue
#18 must check it in the owning context at every config/BLE admission and
serialize initialization, erase and handle use; overlapping initializations are
not supported by the progress flag. Named partition initialization must supply
its own owner status. A denied IDF erase may already have deinitialized NVS and
invalidated handles: never continue with cached handles or automatically retry,
save defaults, format, reset or reboot. Successful default init does not prove
BLE bond restoration; stack restore outcomes still need separate observation.

Current `setup()` reports config/BLE admission and keeps diagnostic supervision
running on missing/failed init or refused formatting. All physical drivers remain
disabled. A separate configuration owner loads settings after admission; startup
does not activate the retained opt-in BLE host. RAM defaults must
not admit camera control while this gate is closed. NVS failure is a device/config
fault, not scheduler starvation. Future independently qualified standalone
GNSS/SD/IMU worker lifetimes must not depend on camera-NVS readiness.

This is whole-partition format interception, not complete forensic or physical
preservation. IDF initialization can still repair/write/discard corrupt pages;
partial GC erases remain enabled. Sequential partial erases, raw flash APIs,
bootloader/provisioning/upload paths and same-object/internal future SDK calls
can bypass the guard. Encryption/key partitions are not qualified. SDK/compiler/
LTO changes require new archive and final-ELF proof. Settings/bond persistence,
reboot and power-failure acceptance remain physical gates in #18/#31.

## Settings formats and storage ownership

`ConfigPersistence` owns application settings, not credentials. Its `ConfigStore`
port has bounded reads and set-plus-commit writes. The ESP32 `NvsConfigStore`
uses the pinned SDK's direct `nvs_open/get_blob/set_blob/commit/close` APIs in
namespace `ridesync_cfg`, with keys `cfg_a` and `cfg_b` in the default partition.
It retains detailed SDK error codes. It never initializes, deinitializes, erases,
formats, reboots, clears a namespace, or uses `nimble_bond`. Missing namespace/key
is distinct from read failure, type failure, an oversized blob, and gate refusal.
Each read queries length before reading one fixed 2048-byte record, then checks
that the actual returned length matches. No input-controlled allocation occurs.

The envelope is 24 bytes followed by a canonical payload, at most 898 bytes total
for eight maximum-sized cameras and current button settings. Integer values are
little endian. Envelope offsets are:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `RSCF` |
| 4 | 1 | envelope version 1 |
| 5 | 1 | reserved, zero |
| 6 | 2 | schema version 1 or 2 |
| 8 | 8 | unsigned generation, nonzero; overflow refused |
| 16 | 2 | exact payload byte length |
| 18 | 2 | reserved, zero |
| 20 | 4 | CRC32, reflected IEEE polynomial 0xedb88320, initial/final xor 0xffffffff |

CRC covers envelope bytes 0..19 and the entire payload, skipping the CRC field.
It detects accidental corruption, not malicious modification. Unknown envelope
versions with recognized magic are preserved and block writes. Any integrity-valid
future schema in either slot blocks writes conservatively, even if an older
supported record exists. No automatic downgrade or force-recovery mechanism is
provided by this milestone.

Both schemas start with one-byte count and capacity. Each used camera contains
three one-byte-length-prefixed byte strings (name 1..64, identifier 0..17, wake
identifier 0..17), then five bytes: family, model, address type, enabled, GPS
telemetry. Family wires are 1 Insta360, 2 GoPro; model wires are 1 X5, 2 GO3S,
3 ONE_RS, 4 HERO12_BLACK; address wires are 0 Unknown, 1 Public, 2 Random.
Booleans are exactly 0 or 1. The existing source camera validation remains
mandatory, including family/model agreement, typed address requirements, MAC
syntax and duplicate rejection. Only `[0,count)` is read or copied; hostile unused
strings are ignored. Enum encoding uses explicit mappings, not raw enum layout.

Schema 1 ends after cameras. Schema 2 appends three uint32 button times
(debounce, long, double), a double-enabled byte, three action bytes (short, long,
double: 0 RecordingIntent, 1 WakeReconnect, 2 Resync), then GPIO enabled,
board-qualified, uint32 pin (`0xffffffff` means -1), pull (0 External, 1 Up,
2 Down), and active-low. Button validation is required. Disabled/unassigned GPIO
uses pin -1 and false enabled/qualification; assigned pins must pass the existing
silicon/pull validation, and enabled GPIO requires the qualification setting.
No setting itself provides physical qualification: every actual driver `begin`
still requires separate explicit qualification acknowledgement. Startup does not
call any GPIO/driver begin.

These two schemas are intentionally designed formats. There is no historical
RideSync NVS deployment. Independent synthetic schema1 camera and schema2 default
golden fixtures specify the wire bytes and CRC; they are not captured installed
records. Schema1 decoding applies exactly SourceConfig button defaults: debounce
20 ms, long 800 ms, double 300 ms, double disabled, actions RecordingIntent /
WakeReconnect / Resync; GPIO disabled, unqualified, pin -1, External, active-low.
Load reports `Migrated` but never writes. A caller must explicitly request the
loaded settings to save schema2 through the normal throttled transaction path.

## Recovery, scheduling and reset semantics

Load scans both slots and atomically publishes one complete validated candidate.
Both missing gives RAM defaults with `Defaults`, with no write request. Greatest
valid supported generation wins. A malformed alternative gives `Recovered`;
both malformed gives RAM defaults with `Corrupt`. Equal generations with
divergent decoded settings/schema give `Ambiguous`. Read errors report
`ReadError`, never missing. Future, ambiguous, corrupt-with-no-valid-slot and
read/refusal errors block saves and supply RAM defaults on load. Explicit reset
also refuses future/ambiguous/both-corrupt data with the same status; bytes are
preserved and no success/durability claim is made. Controlled operator recovery
outside this ticket is required. No camera ingress exists in this milestone;
future owners must keep ingress closed on these refusal/error statuses as well
as on the SDK and physical-qualification gates.

A validated request stores only one fixed pending record. Later changes replace
it and restart 1000 ms coalescing. Used source strings are bounded before any
copy. Settings differing from the latest durable snapshot use the opposite slot,
with generation+1, set, explicit commit, and exact byte-for-byte read-back.
The previous valid slot is retained. There is no active-pointer key and no
multi-key atomicity assumption. Unchanged settings perform no write. Schema1
migration still writes schema2 even when settings themselves match.

Write attempts are at least 5000 ms apart, increasing to 10000 ms after the
second failure; at most three total attempts occur before a latched failure.
All intervals use unsigned millisecond subtraction (clock gaps must remain below
2^31 ms). A changed settings request or explicit `retry` releases the latch;
retry does not bypass the minimum interval. Read-scan and incompatibility errors
latch immediately, requiring a new request/explicit retry. A scheduler must call
`service` only in the storage owner. `Pending` means RAM-accepted/unsaved;
`Encoded` is a pure codec result and carries no storage claim.
`Durable` requires successful commit and matching read-back. `Unchanged` means
no changed write was needed. These are SDK-level observations, not physical
power-failure certification.

Write/commit/verification errors include SDK code where available and an
`indeterminate` flag; the underlying record may already exist. The latched
result retains its original failure `cause`. Before every subsequent attempt,
both slots are rescanned, so stale metadata cannot target the sole newest valid
slot. An indeterminate operation is not silently upgraded to durable just because
a rescan matches: another set/commit/read-back is required. Refusal discards the
pending mailbox rather than replaying it after admission returns. No default,
migration, reset, erase or retry request is generated by load or startup.

Configuration reset is an explicit request for `SourceConfig{}` through this same
algorithm. It changes application settings only and leaves stack bonds alone.
Pairing reset is a separate explicit BLE operation, with user settings untouched.
Settings never include recording state/intent, executed gestures, command queues,
retry counters, telemetry caches, credentials, IRKs/LTKs or keys. Status logging
contains readiness, epoch/generation, status/error, counts and mapping validity,
never camera identities or payload bytes.

## Owning context and startup composition

`main.cpp` creates one 12 KiB, priority-1 configuration task on core 1 only when
safe mode is off and the SDK boot gate is open. It loads once into owner-private
RAM settings and services the bootstrap at 100 ms cadence. The existing independent
health task retains its watchdog ownership. Main consumes a completed snapshot
or latches a 1000 ms startup timeout before fixing worker policy and creating
that task; optional launch additionally waits for its owned SDK subscription.
The permanent NVS owner survives timeout, and late publications are consumed
without granting settings/control admission for this boot. See [supervision](supervision.md). No producers are wired
to request saves in this milestone. The fixed reverse mailbox is exercised synthetically; future authorized producers
must use it with the current epoch and generation; direct cross-task calls
into `ConfigPersistence` are forbidden. Camera/control ticks, BLE callbacks and
acquisition tasks must never call storage operations. Blocking SDK commit/read
latency therefore belongs to the storage task, never the camera scheduler.

SDK initialization finishes before this owner is created. The current application
has no subsequent SDK init/deinit/erase callers. This is an explicit exclusive
lifecycle contract: future lifecycle operations must stop/quiesce this owner
before init/deinit/erase and reestablish admission afterward. The boot snapshot
alone is not a mutual exclusion primitive. The adapter rechecks it at each SDK
boundary and opens/closes fresh handles per operation. A handle abandoned after
refusal is never reused or passed to further SDK calls; reboot is not requested.
There are no cached handles or automatic initialization attempts. The owner is noncopyable and nonreentrant, including store callbacks. Its
workspace is provisioned once with the owner: three 2052-byte target record
objects (pending, winner canonical, and shared read/encode/read-back scratch),
one 756-byte winning SourceConfig and bookkeeping; the target owner is 6992
bytes total. Scratch lifetimes are sequential. Decoder candidates remain
bounded stack values; their validated strings use the existing bounded heap
path, with no additional workspace allocation during service. Empty defaults
are reconstructed in place using SourceConfig's own defaults, eliminating a
large overlapping temporary. Encoding clears its supplied record in place.

The pinned target disassembly measures configTask 48, bootstrap start 48, bootstrap
service 64, persistence service 80, scan 880 and decodeConfig 944 bytes. The largest
listed nested path is service/decode at 2016 bytes; load/decode is 1968 bytes,
versus the original 13024-byte defect. The configured task remains 12288 bytes.
`scripts/check_config_stack.py` checks ELF/object frames for load/request/reset/
retry/service, including GCC split helpers and the SDK boundary paths. A 4096-byte
application budget includes an additional 1024-byte helper allowance; the remaining
8192 bytes are reserved for SDK/libc/allocator/RTOS/interrupt work. This reserve is
a conservative provisioning policy, not a complete indirect-call depth proof or
measured high-water result. Current direct NVS API frames are 32..48 bytes; bounded
NVS/flash functions observed in this build are at most 272 bytes individually.
On-device high-water checks must exercise load, reset, retries, maximum records,
GC, failures and simultaneous interrupts before stack qualification. Bounded
string heap use and real flash latency also still require device measurement.
Successful NVS admission never enables BLE, GPIO or another unqualified driver.
Independently qualified GNSS/SD/IMU lifetimes remain independent of camera NVS.

## Application bootstrap and bounded handoff (#38)

`ConfigBootstrap` is noncopyable and exclusively owns its two `SourceConfig`
values (effective and pending), uses the existing exclusive `ConfigPersistence`,
and publishes copied `SettingsSnapshot` values. `configTask` starts it once after
SDK/safe-mode admission and services it every 100 ms. `loop` takes into a static
scratch envelope and accepts into its own `ApplicationSettings`; it never sees a
pointer/reference to the persistence owner's strings. If the worker is not
admitted or creation fails, setup is the sole owner of terminal publication:
`Refused` or `ReadError/-1`, respectively. Successful task creation transfers
ownership to the task; setup never subsequently accesses that owner.

Before publication, readiness is incomplete (including the initial enum value
`Defaults`). Completed validation publishes generation 1 with one camera/button
snapshot. `Defaults`, `Loaded`, `Migrated` and `Recovered` may supply effective
settings. Other load statuses publish explicit completed/unavailable results and
inactive defaults, never a partial candidate. The immutable `load` result remains
separate from `outcome`, which records later success/refusal. Effective settings
are software data, not driver qualification. Missing mapping, safe mode, a closed
NVS gate, and absent physical/protocol evidence continue to refuse camera/BLE
admission. All production optional drivers remain disabled.

Epochs are nonzero endpoint-lifetime tokens, not persisted generations or durable
session IDs. Current endpoints exist for exactly one boot, with epoch 1; no
mailbox survives reboot. A future in-process endpoint restart must first quiesce
both endpoints and supply a new epoch. Each effective-settings adoption or
terminal revocation advances the application generation. Consumers reject
incomplete, zero/wrong-epoch, equal and older-generation snapshots. The application
generation is independent of the on-flash record generation. Saves are refused
at generation `UINT64_MAX-1`, reserving the final generation for terminal
revocation; generations never wrap. This is a practical arithmetic bound, not an
observed multi-billion-update runtime test.

`Settings` contains eight fixed active-slot camera records (65/18/18-byte C
strings), count/capacity, button gesture settings and GPIO data. `copySettings`
validates camera/button/GPIO semantics before copying active slots only. It
ignores arbitrarily large unused source strings and leaves output unchanged on
failure. Embedded NUL names cannot be represented faithfully and are explicitly
refused by this application envelope (with no storage rewrite or schema change).
`expandSettings` checks termination before bounded owner-only string allocation,
validates the whole candidate, and replaces output only on success. Callback and
control paths copy fixed envelopes and never allocate or use NVS.

Publication and reverse requests each use a one-slot, lock-free SPSC mailbox.
Each endpoint and its caller-owned input/output buffer must outlive an operation;
the mailbox must outlive both endpoints. Producer CAS acquires the empty slot,
copies the entire value, and releases READY as its **final slot access**. Consumer
CAS acquires READY, copies into its own value, and releases EMPTY as its **final
slot access**. The producer can then reuse the slot without changing the
consumer's copy. Full publication returns false: the config owner retains its
latest coherent dirty snapshot and retries next service; an older queued snapshot
is never overwritten. Full save submission returns false to the producer.
Neither endpoint blocks/spins inside `put`/`take`. No reset, destruction,
reprovisioning or additional producer/consumer is permitted until quiescence.

The reverse `SettingsSave` carries an epoch, expected effective generation, and
fixed settings. Only the config owner expands/validates and invokes persistence.
Wrong epoch/generation, malformed input, owner refusal and a save already in
flight have distinct `Stale`, `Invalid`, `Refused` and `Busy` outcomes. There is
one candidate in flight; commands do not silently replace it. The old effective
snapshot remains active during throttling or write/commit/verification failure.
`saveResult` retains the persistence error/cause/indeterminate observation and
`saveOutcome` reports `PersistenceError`; these accessors are owner-only, not
cross-task atomic observations. A future UI must supply its own bounded
acknowledgement policy. No production save producer exists here.

Only `Durable` or `Unchanged` adopts that pending candidate into owner RAM,
canonicalizes it and publishes the next effective generation. `ConfigPersistence`
does not update caller RAM. SDK/safe-mode refusal discards pending persistence
and candidate admission without I/O; discovered future/corrupt/ambiguous/read
failure also publishes explicit terminal unavailability. Gate reopening or
operator safe-mode clearing cannot replay a discarded command or reopen this
owner. Supervised startup does not restart this terminal owner. There is no
automatic default save, migration save, retry/reset command or erase.

`CameraPeers` is separately provisioned once, copied and frozen for the owner
lifetime. It must match every configured slot/model exactly and supply unique
nonzero opaque IDs. No ID is derived from name/MAC/serial, and no persistence
field is added. A later model/slot/count, typed BLE address or wake-address change invalidates
that fixed mapping permanently for this owner, even if settings change back.
These comparisons enforce stability and never derive the opaque ID. Restoring
admission requires a new explicitly provisioned, qualified session/owner after
quiescing both endpoints. Default production supplies no mapping; an explicit
commissioning provider may supply the frozen mapping before the config task starts.
`admitSettings` requires explicit qualification of all configured cameras plus
valid mapping and open safe-mode/NVS gates; button eligibility also needs its
independent physical qualification. Its local telemetry eligibility depends only
on independent local qualification, including during camera/config/safe-mode
refusal. Eligibility alone starts no local logger, bus or peripheral; main
launches qualified composition only after supervisor subscription. Safe mode
retains eligible GPS/SD logging while the actual BMI270 worker refuses startup.

Pinned Xtensa object sizes in bytes: `CameraSettings` 120, `Settings` 1012,
`SettingsSave` 1032, `SettingsSnapshot` 1168, `CameraPeers` 100,
`ConfigBootstrap` 3856, publication 1176, requests 1040, application-owned view
1176, and loop scratch 1168. The five bootstrap/handoff globals total 8416 bytes;
with the existing 6992-byte persistence object the total is 15408 bytes, excluding
store/RTOS objects, owner string heap, stacks and SDK memory. Large envelopes are
static/member objects, never multiple stack copies in control/loop workers.
Individual compiled bootstrap frames are start 48, service 64, refusal 1072,
expand 816, and canonical-copy wrapper 32 plus split helper 1056. The enhanced
stack checker includes these actual nested owner paths and still reserves 8192
bytes for SDK/RTOS/interrupt work within the 12288-byte task. Runtime high-water,
heap fragmentation, flash latency, capacity/timing and physical power-loss
behavior remain unmeasured and retain their existing hardware gates (#18/#22/#31).

Native behavior tests cover the bootstrap barrier, bounded copies/refusal,
publication pressure, stale epochs/generations, concurrent owned-copy lifetime,
synthetic authorized saves, no replay, qualification independence and stable
peer mapping. The actual Arduino startup/task harness exercises loaded settings,
disabled defaults, safe-mode/NVS refusal and worker creation failure. Retained
ESP32 builds link the actual config-owner handoff; these are compile/link and
host behavior evidence, not board/camera/bond/pin/protocol qualification.

## Stack-owned bonds and future BLE integration gates

The selected provisional NimBLE 2.3.6 owns credentials and CCCDs in `nimble_bond`.
No BLE dependency, second host or duplicate security store is introduced here.
The `BondResetPort` policy is a tested abstraction; there is no concrete NimBLE
backend or camera pairing/reset integration yet. It accepts only verified typed
stable identities (public or random-static, with six address bytes in stack order,
least significant byte first), obtained from authenticated connection identity or
verified stack mapping. Configured MACs are not security identity evidence.
Unresolved RPAs, unknown types and zero addresses are refused. The port must check
SDK/host/restoration admission per operation, serialize with BLE mutation, and
verify removal of all relevant targeted security/CCCD records. Enumeration alone
cannot prove successful restore or complete partial deletion.

Reset performs one targeted removal, at most one busy retry after controlled
quiescence, then verifies absence; presence or inability to verify is
`Indeterminate`. Other removal failures are actionable outcomes and make no
rollback claim. There is no global-delete API, generic-auth-failure deletion,
automatic re-pair or recording replay. Only a later protocol-authorized caller
may permit one fresh pairing/reconnect after verified removal. Remote camera-side
forget/reset may still be required.

Before pairing, the future BLE owner must install a device store-status callback
that fails/reports capacity without eviction. NimBLE's default callback invokes
oldest-peer unpairing on overflow and is prohibited. `admitBond` requires host,
separate restoration evidence, installed overflow refusal, and free capacity (or
an already verified stored identity). It is a policy, not a runtime SDK proof.
NimBLE's default three bonds cannot cover even the four required target cameras;
configure and qualify at least four, or eight for full SourceConfig capacity,
with measured controller/radio/storage limits. Stack restore is void with logging
limitations, so new observable restoration evidence is required. Future backend
work must account for partial unpair deletions and bounded enumeration (strict
index < peer count), and must never delete unrelated cameras to recover capacity.

Issue #18 remains OPEN pending installed-firmware reboot/migration/power-failure
recovery, actual bond restoration and scoped reset, capacity measurements,
watchdog/task-memory/latency qualification, and observed absence of recording
command replay. Host fault injection and pinned ESP32 compilation are software
evidence only. Two retained blobs plus NVS overhead and stack bonds must fit the
actual partition; no partition-table or SDK source edits are made here.

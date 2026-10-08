# X5 CE80 peripheral capture probe

This isolated diagnostic offers a documented community CE80 peripheral profile
for the camera to discover. It is not a production camera adapter. Idle/A remain capture-only; an explicit
serial S can now submit one source-derived CE82 shutter event to the current
subscribed peer. It always reports recording UNKNOWN and sends no automatic
mode or handshake commands. An idle-only W option instead selects one finite,
source-derived wake advertisement; that mode cannot submit shutter events.
Connection, subscription or successful ATT access alone does not prove that
the X5 has paired or supports this profile. A camera-side pairing indication,
firmware version and separately annotated private capture are still required.

No modem/SD/IMU GPIO, filesystem, production startup or persistent bond storage
is linked. Pinned ESP32/Arduino packages match the root project; NimBLE-Arduino
is pinned at `dfb4ac561a06797081be9e752902a6582e7f029e` (2.3.6).

Raw startup retains the pinned Arduino Bluetooth HAL via `btStarted()`, matching
the linkage anchor in `NimBLEDevice::init` without calling that NVS-initializing
wrapper. This status query makes the HAL's strong `btInUse()` available during
Arduino boot, preventing its weak false fallback from releasing all Bluetooth
controller memory before A. The ELF audit requires that strong true-returning
implementation. The initial physical startup returned `ESP_ERR_INVALID_STATE`
before sync; its ELF had the weak false implementation and boot memory release.
The corrected ELF is compile/audit evidence; observed startup/pairing results
are recorded separately in the repository hardware results.

## Source-backed prototype profile

The [MIT ESP32 example at 83d4748](https://github.com/pchwalek/insta360_ble_esp32/blob/83d4748b68d6ee5fd4414994a9e26b7d2f21364b/Insta_BLE.ino)
defines these documentary prototype facts, not X5 requirements:

| Service | Characteristic | Property / read bytes |
| --- | --- | --- |
| CE80 | CE81 | Write |
| CE80 | CE82 | Notify; automatic CCCD; explicit S only |
| CE80 | CE83 | Read `01 02` |
| 0000D0FF-3C17-D293-8E48-14FE2E4DA212 | FFD1, FFD8, FFF2 | Write |
| same additional service | FFD2, FFD5, FFF1, FFE0 | Read empty |
| same additional service | FFD3 | Read `01 90 1E 30` |
| same additional service | FFD4 | Read `01 20 00 18` |

Both services are primary GATT services; the example calls the additional one
"secondary." Integer read values are explicitly represented in ESP32 little
endian byte order. The legacy advertisement carries general-discovery/no-BR-EDR
flags and both service UUIDs (25 bytes). Scan response carries only the complete
source-example name `Insta360 GPS Remote` (21 bytes including the AD header). Normal mode uses no manufacturer/wake bytes or camera identifier. The explicit
wake-only option below uses a private RAM-only identifier. Each field fits the 31-byte legacy limit.

The [MIT M5 fork at c76e140](https://github.com/marcelpallares/insta360-m5stick-remote/blob/c76e140396de8b2404cdd36d17cf0d1a251a9dcc/ble_handlers.h)
uses the vendor remote name; its exact-name comment refers to Ace Pro 2 and is
not evidence that X5 requires that name. It also uses a colon/timer heuristic,
which this probe does not implement. The name-only trial aligned the local name with
both MIT examples, changing only the name from the prior `RideSync CE80 Probe`
trial. Root reported an actual X5 connection, CE82 subscription, CE81 writes and
a human-observed paired/connected UI. This supports trying one addressed shutter
event; it does not establish recording control or prove name filtering as the
sole cause. The additional service and any handshake remain unresolved. Sources
were inspected directly; implementation is independently authored from API and
field facts. No example implementation, unlicensed direct-control or GPS codec
is copied. Dependency notices remain in the installed pinned package.

## Build and controlled physical trial

From the repository root:

```sh
.venv/bin/pio run -d tools/bench/x5_peripheral
python3 -m unittest discover -s tools/bench/x5_peripheral -p 'test_*.py' -v
python3 tools/bench/x5_peripheral/inspect_link.py \
  --nm ~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-nm \
  --objdump ~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-objdump
```

Compile/host checks are not physical results. Before any upload, preserve the existing verified installed-firmware backup and factory
NVS/partition state; follow the repository's board/bootloader procedure. Do not
erase flash, initialize/recommission NVS, or replace factory bonds to make this
trial pass. Safely remove the SD card with all board power disconnected before
upload. This bench has no namespace or private receipt dependency. Root owns
the hardware/upload procedure; this implementation task performs neither.

Serial is 115200 baud. Default boot is idle with no advertising. Commands:

- `A`: one advertising/capture attempt per boot, including failed startup.
  The window is 120000 ms from A; stack synchronization/advertising startup
  must complete within the 15000 ms policy limit. A retained SDK owner handles
  startup, advertising preparation/start and cleanup; delayed SDK work cannot
  hold the control loop's deadline service or serial reporting. After preparation,
  owner admission rechecks policy, the sticky stop latch and current time, and
  recomputes the remaining finite window immediately before start submission.
  One peer maximum; no auto-reconnect advertising.
- `W` immediately followed by exactly six printable ASCII identifier bytes and
  LF (`\n`): idle-only wake option. No spaces/separators are added unless they
  are part of those six bytes; CRLF is malformed. Identifier is supplied privately
  from the selected camera name suffix, never echoed or logged and never stored
  in NVS/flash. W is consumed through LF even if invalid, oversized or received
  after A; embedded A/S/X characters cannot execute. Invalid idle configuration
  blocks A until a valid W line or reset. A partial line also blocks commands
  until LF. Configuration is immutable after A. With a valid option, A requests
  one wake advertisement of `min(remaining capture window, 3000 ms)` through the
  SDK. There is no automatic retry or switch to normal advertising. Advertisement
  completion with no connected peer ends the policy; a successful connection
  remains capture-only up to the original 120-second cap. S is always refused in
  wake mode, even after subscription. No wake/recording success is inferred.
- `X`: request stop of advertising and disconnection of the observed handle.
  X before A does not consume the attempt. After an attempt, reset is required
  for another A. Actual disconnection is a separate SDK event.
- `S`: request exactly one CE82 shutter event. Admission requires the active
  finite window in normal mode, a current connected peer, its current CE82 notify subscription,
  and an idle SDK owner with no pending shutter request/stop. One pending slot;
  refused/busy requests are discarded. After preparing an SDK-owned mbuf the
  owner revalidates current time, peer, subscription and stop at submission.
  X, expiry, disconnect, loss of subscription or preparation failure discard
  pending work. Once admitted to SDK it cannot be retroactively cancelled;
  submission return is logged and never retried. No reconnect/reset replay.
  Send a single S, observe the camera display/media result, and only then send
  a separate S if a second toggle is wanted. Never send a burst or automatically
  repeat S to obtain REC/STOP. The command is a mode/state-dependent shutter
  button event, not explicit Start or Stop. Recording remains UNKNOWN.
- `H`: explicit private opt-in to sensitive binary hex output (up to all 256
  copied bytes per event). Default output includes event IDs/lengths only.
  Treat H transcripts as private: incoming protocol fields may contain camera
  identifiers or tokens. Never publish them or commit captures as fixtures
  without independent redaction/licensing review. SM keys/passkeys are never
  copied or printed. H has no camera-side effect.

For the first trial, put X5 in its remote pairing/search UI, send H only if a
private handshake transcript is wanted, then A. Record camera firmware and
camera UI pairing outcome separately; observe connect, MTU, CE83 reads,
subscription and incoming writes. Annotate manual REC/STOP/mode operations
against camera display/saved media without interpreting packet bytes here.
Send X or let the finite window expire. An absent connection or stalled UI
is unresolved evidence, not an unsupported-camera verdict. Do not synthesize
a handshake or transmit guessed commands to clear the UI. No physical test has
been performed as part of this source task.

## Bounds and evidence semantics

The ring holds 32 events with up to 256 copied bytes each. Incoming mbufs remain
SDK-owned. Oversize writes are prefix-captured, counted as truncated and still
acknowledged at ATT level; they are not valid complete protocol fixtures.
H prints the complete bounded copy and explicitly labels truncation. Queue
overflow increments dropped counters and leaves sequence gaps; every callback
event increments its per-kind count even if dropped. SDK notifications are
only emitted by an explicitly admitted S through the retained owner. Callback contexts, service definitions, queue and SDK remain
alive through stop, deadline, failure and late callbacks; no deinit is attempted.

Reports use actual `esp_timer_get_time` boot milliseconds and SDK connection/
attribute handles. There is one connection generation per boot; disconnect
ends the attempt, so no recycled handle is treated as the same continuing peer.
No peer MAC/name/serial is logged. `active` denotes policy admission, `synced`
denotes host synchronization, and `advertising` is the copied SDK advertising
state. These are transport observations, not camera pairing/recording proof.
Stop reason values: 0 none, 1 requested, 2 startup timeout, 3 window deadline,
4 disconnected, 5 error, 6 unresolved security request/failure.

Event kind numbers: 0 sync, 1 connect, 2 disconnect, 3 MTU, 4 subscribe,
5 read, 6 write, 7 security, 8 passkey action, 9 repeat pairing,
10 advertising end/stop submission, 11 error/submission status, 12 stop request,
13 GATT registration (`attr` is actual value handle, `value` is public 16-bit UUID),
14 shutter request (`value` 1 accepted pending, 0 refused), 15 shutter result
(`value` raw SDK return, or -1 local cancellation before SDK submission).
Allocation failure is reported as SDK ENOMEM without a notify attempt. Shutter
logs contain only connection/attribute handles and status, never payload.
16 wake option (`value` 1 accepted, 0 refused); no identifier bytes are retained
in events or summaries. `wake_only` reports the immutable selected mode.
Read events include the attempted static response bytes/length; write events
contain bounded incoming bytes. Connect `attr` is peer address type; MTU `attr`
is channel ID; subscribe `value` encodes notify bit0, indicate bit1 and SDK
reason above bit7; security `attr` contains encrypted/authenticated/bonded bits
0/1/2 (FFFF means descriptor unavailable), with SDK status in `value`.
Passkey events record action type only. All other statuses remain raw SDK
values. A termination submission status of zero is not a disconnect event.

Loop formats at most one queued event per pass into 768 bytes; complete serial
messages require sufficient TX capacity (2048-byte buffer). Backpressure retains
the front event and can overflow the ring. Summaries may be skipped. A stopped
policy with a live handle means cleanup is pending; retained memory is never
claimed quiescent solely from a timeout.

X/expiry only latch a stop request under a bounded capture lock; no blocking SDK
stop/terminate call runs in the independent control loop. The retained owner
publishes each admitted operation as in flight before calling the SDK with the
lock released, then separately publishes its return status. The sticky stop is
never cleared by a delayed startup/preparation/advertising return. If the owner
stalls, control continues timestamps, serial commands, policy service and reports;
the in-flight operation remains visible. `advertising` is the pinned SDK's
nonblocking atomic state observation, not a cleanup barrier.

Summary fields `sdk_stop`, `sdk_inflight`, `sdk_op`, `sdk_ops_admitted/returned`,
`sdk_last_op/rc` and `shutter_pending` describe owner admission/SDK return publication. Operation
IDs are 0 none, 1 startup, 2 preparation, 3 advertising start, 4 advertising stop,
5 terminate, 6 shutter notification. Startup/preparation are grouped SDK work; counters do not count
individual HCI commands. A zero return status is not camera acknowledgement or
callback quiescence. Shutter result zero does not prove camera delivery or a
recording transition. Previously admitted work can enter/return from SDK after X
or a deadline; a late advertising start is compensated by the owner's retained
stop request when it can run. The policy rejects new stale admission after
preparation, but does not claim a hard RF-off deadline or retroactive cancellation
of in-flight SDK work. No owner, callback context or queue is deinitialized.

## Factory NVS preservation and ephemeral security

This bench calls the actual raw NimBLE C SDK, avoiding `NimBLEDevice::init`'s
automatic NVS initialization/erase and indefinite synchronization loop.
`MYNEWT_VAL_BLE_STORE_CONFIG_PERSIST=0` selects the pinned config store's RAM
arrays and no-op persistence/restore callbacks, even though `nimconfig.h`
defines the separate CONFIG persistence macro. `ble_store_nvs.c` then compiles
empty; the config-store object has no NVS/persist/restore references.

Core NVS init returns explicit NOT_INITIALIZED through a bench-only linker
wrapper. This is neither of the pinned core's error-format trigger values;
the core continues startup. NVS opens (including explicit-partition opens)
also refuse NOT_INITIALIZED and return no handle. Flash erase and partition
erase wrappers refuse NOT_SUPPORTED as defense against error paths.

The pinned ESP-IDF PHY calibration/storage setting is enabled. Its
[documented load/store path](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_phy/src/phy_init.c#L633-L711)
falls back to full RF calibration in RAM when load-open fails; save-open also
fails before any NVS setter/commit. RF calibration is retained. ELF inspection
checks the wrappers, no real open/init/erase symbols, store objects and actual
NVS callers. PHY getters/setters remain linked but are unreachable behind
refused opens; source branch review is part of qualification, not replaced by
symbol inspection. Refusal counters are visible; expected erase refusals are
zero. This cannot claim actual physical flash preservation without execution.

SDK legacy and Secure Connections support remain enabled. Local bonding,
MITM and key distribution are disabled, using ephemeral Just Works when the
camera initiates it; no security procedure is initiated here. Encryption status
is captured. An unknown passkey/comparison, repeat-pairing request, observed
bonded status, store-capacity failure or encryption failure stops the attempt
as unresolved security. No keys/passkeys, auto-confirmation, bond eviction,
persisted keys or factory-bond edits are permitted. A camera requirement for
persistent bonds or an unknown application handshake needs a separate decision.

## Shutter provenance and finite check

The bench compiles the existing shared `encodeShutterEvent()` implementation via
`shutter_codec.cpp`; it does not maintain another packet definition. Its fixed
nine-byte event is documented in `docs/insta360_protocol.md` and the MIT pinned
source fixtures. The pinned SDK `ble_gatts_notify_custom` consumes its supplied
mbuf on every outcome with notification support enabled (compile-time asserted).
Preparation/refusal frees the still-owned mbuf locally; accepted submission
transfers ownership exactly once. No payload pointer is retained in policy.

After pairing and CE82 subscription, set the camera's desired video mode and
visually establish its current idle/recording state. Send one S, then observe
actual camera UI and saved media before another S. A paired UI or SDK zero
return alone does not satisfy #3's camera-observed REC/STOP requirement. X ends
the attempt; another A requires a reset and fresh pairing/subscription. The
fixed 120-second window remains unchanged. No shutter hardware trial is claimed
by this implementation.

October 9, 2026 source-task verification: focused host policy harness passed
(0.701 s), including idle/no-peer/no-subscription/wrong-peer/busy refusal,
X/expiry/disconnect/unsubscribe cancellation between request and submission,
no retry after error/success and accepted-before-stop publication. The harness
also links the shared encoder and compares its output with independent literal
expected bytes. Pinned probe SDK build passed (13.54 s; RAM 45,380 bytes, flash
573,157 bytes). Extended ELF audit passed: existing RAM-only store/core/PHY
refusal boundaries and retained start/stop/terminate owner, shared encoder and
owner-only application custom-notify caller. SDK-internal standard notification
wrappers remain linked. Clang-format 18 and whitespace checks passed. These are
software checks; no shutter/camera observation or production integration is
claimed.

## Finite wake-only experiment

The manufacturer value is sourced from the retained MIT M5 fork
[`c76e140`, ble_handlers.h:299–325](https://github.com/marcelpallares/insta360-m5stick-remote/blob/c76e140396de8b2404cdd36d17cf0d1a251a9dcc/ble_handlers.h#L299-L325)
and independent MIT ESP32 example
[`83d4748`, Insta_BLE.ino:129–149](https://github.com/pchwalek/insta360_ble_esp32/blob/83d4748b68d6ee5fd4414994a9e26b7d2f21364b/Insta_BLE.ino#L129-L149),
with its RS identifier example at [lines 229–235](https://github.com/pchwalek/insta360_ble_esp32/blob/83d4748b68d6ee5fd4414994a9e26b7d2f21364b/Insta_BLE.ino#L229-L235).
It is exactly 26 bytes: fixed `4c 00 02 15 09 4f 52 42 49 54 09 ff 0f 00`,
six identifier bytes, fixed `00 00 00 00 e4 01`. M5 derives the identifier
from the last six name characters (`camera.h:131–155`) and uses a three-second
advertising duration (`commands.h:175–183`). These are documentary prototype
facts; the generic suffix rule is not independently confirmed for X5. This
experiment does not close production wake/recovery support or #9.

The diagnostic explicitly serializes flags AD (3 bytes) plus manufacturer AD
(28 bytes) into exactly 31 ADV bytes. Scan response contains CE80 AD (4 bytes)
plus the complete remote name AD (21 bytes), exactly 25 bytes. D0FF is omitted
from the wake advertisement; the unchanged GATT services remain available on
connection. This exact AD arrangement is an experimental bench choice needed
to fit legacy limits, not a captured on-air vendor frame. No transmit-power,
identity, pairing, persistent storage or GPIO changes are made.

SDK duration is a requested finite advertisement duration, not proof of a hard
RF-off deadline if the SDK stalls. Existing 120-second policy expiry and retained
owner cleanup still apply. X retains its sticky stop semantics; an operation
already accepted before X may return later. No reset/reconnect replay occurs.
Observe the camera display after the one advertisement; absence of a connection
is not alone proof that wake failed, and connection is not recording proof.

Wake-only source-task verification (October 9, 2026): focused host harness
passed (final 0.838 s), including independent complete 31-byte ADV/25-byte scan
fixtures, exact manufacturer insertion, default normal-mode behavior, malformed
length/control/overflow consumption, immutable post-A option, finite duration,
no-connection terminal completion and connected capture cap. The final pinned
probe build passed (4.56 s incremental; RAM 45,420 bytes, flash 573,965 bytes).
Extended ELF audit passed existing NVS/RAM/retained-owner protections plus raw
wake-data preparation called only by that owner. Clang-format 18 dry-run and
bench whitespace checks passed. No hardware action, private identifier capture,
production wake implementation or observed wake success is part of this task.

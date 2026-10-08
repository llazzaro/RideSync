# Validation

## Automated baseline

`pio run -e lilygo_t_a7670e_r2` compiles serial-only firmware.
`python -m unittest discover -s test -v` validates the sample configuration's
repository contract (including all three target models).
`python scripts/check_format.py` checks C++ formatting using pinned clang-format.
These do not test BLE, runtime configuration loading or physical peripherals.

Add native unit suites alongside each implemented subsystem: debounce and
press timing including rollover; independent camera transitions; retry budgets
and deadlines; valid/malformed GNSS input; protocol lengths, signed fields and
golden packet fixtures. Do not substitute implementation-shaped assertions for
observable behavior.

## Manual acceptance per model

Record board revision, ESP32 firmware commit, camera model/firmware, modem
firmware, configured identifiers, timings, serial logs and actual camera screen.
Run separately on X5, GO 3S and ONE RS:

1. Pair/discover services and capture subscriptions/handshake.
2. Power off camera; trigger wake; observe whether it powers on.
3. Wait up to a documented connection deadline; record failures.
4. Request REC; verify recording on the camera and compare notifications.
5. Request STOP; verify it stopped, then repeat without changing mode.
6. Disconnect or power-cycle the camera; confirm bounded recovery.
7. Reset ESP32 while camera is recording; ensure no accidental toggle.

Repeat with three cameras: two available, one absent. REC/STOP must reach the
two available peers and report partial success with the third unknown/error.
Measure command skew, concurrent connection capacity and retries independently.
Test rotation of all three wake identifiers; never infer wake compatibility from
one model. Test unavailable SD, full SD, GNSS no-fix/stale fixes, and reset.

Acceptance for the first X5 camera milestone: repeatable single-X5 start/stop with observed state,
bounded connect/command timeouts and honest serial status. It is not met yet.

## GO 3S and Action Pod evidence protocol

Status: **not run; target hardware unavailable**. Run this before adapter work
on each tested camera/Pod firmware pair. Record date, operator, camera model,
camera firmware, Pod firmware, app version, BLE controller/sniffer model and
software version, camera/Pod power state, activation/pairing history and
relevant settings. Keep exact identifiers only in a private raw capture; public
notes use stable labels such as `camera-A` and `pod-A`.

1. Start from the official connection prerequisites: camera and Pod powered on,
   first-use sticker removed, clean/aligned contacts, and successful live view
   on the Pod. If firmware incompatibility is shown, follow vendor update steps
   and record both resulting firmware versions. Allow the documented 10–15 s
   connection window. Do not factory-reset between repetitions unless reset is
   the variable under test.
2. Collect a passive BLE scan and over-air capture during: power-up, insertion
   into Pod, removal, camera-only button recording start and stop, Pod-button
   start and stop, app-triggered start and stop (if available), mode change,
   disconnect/reconnect, and a second full repetition. Mark each physical action
   with a timestamped annotation; record screen/indicator state and verify each
   result in saved media. Include idle controls as negative controls.
3. Separately attempt service/characteristic discovery with a BLE central only
   when it does not displace or alter the normal camera–Pod connection. Record
   advertisements, address type, services/characteristics/properties, security
   prompts, pairing/bonding outcome, notification subscriptions, MTU and
   discovery errors. If discovery requires disconnecting the Pod, label those
   runs as an altered condition. Do not infer GATT from a passive capture alone;
   encrypted traffic without keys is inconclusive.
4. Repeat each transition at least three times from known stopped and known
   recording states, then repeat after power-cycle and reconnect. Keep camera
   and Pod firmware constant within a repetition set. Compare packet direction,
   timing, payload and response/state to the annotations. Label results
   `Observed` only when locally captured; retain `Official` and `Community`
   reports as separate evidence classes and label explanations `Hypothesis`.
5. State the exact tested conditions and whether the controller can address the
   camera, the Pod, or neither. Only claim explicit start/stop when opposite
   state transitions are independently repeatable. If the same frame toggles,
   require reliable observed state and never replay after ambiguous timeout.
   Otherwise leave command/state unknown.

The current blocker is specific: we lack a GO 3S camera and Action Pod plus
their firmware/version record, repeated annotated recording transitions, BLE
advertisement/GATT/security evidence, and correlated command/response/state
captures. Therefore target ownership and start/stop/state feasibility remain
unresolved. A missing capture is not a negative compatibility result. Do not
start #21 from assumptions; resume this protocol when hardware is available.

## Planned GoPro and mixed-camera validation

`pio test -e native -f test_hero12_adapter` exercises the actual adapter,
manager, codec and shared central using a deterministic fake host. It covers
synthetic pairing/control/identity/status setup, missing Management/CCCD,
Busy gating, Encoding confirmation, dropped shutter ACK, fragmentation,
oversized notifications and fragment expiry. It also rejects empty or wrong
status elements in all three successful Register replies.
The fake packets are authored from published protocol structure and are not
HERO12 captures. `pio run -e hero12_adapter_compile` links the opt-in ESP32
composition against the pinned real backend without starting BLE. It proves
source and symbol compatibility, not timing, stack high-water mark, camera
behavior or safe controller startup. Commissioning must record exact installed
model/firmware/API, pairing screen and control ownership, CCCD results,
response/notification traces and response latency. Run at least twenty
screen-and-Encoding-confirmed REC/STOP cycles and power-cycle/reset/reconnect
checks without replay, then measure keep-alive cadence and mixed-link
coexistence. These physical gates remain open.

Follow [GoPro milestone acceptance](gopro_plan.md) for each selected model and
firmware. Test pairing persistence, video-mode selection, explicit shutter
on/off, observed encoding state, busy responses, keep-alive, sleep versus full
shutdown, power cycling and ESP32 reset during recording. Codec tests must cover
notification fragmentation and malformed responses.

For mixed-brand groups, test concurrent BLE roles, per-peer response routing,
independent deadlines, one unavailable camera and measured command/recording
skew. Continue GPS/SD logging throughout. No GoPro hardware tests have run yet.

## Planned ride-logger validation

GNSS and raw IMU logs must continue through camera disconnects. Test invalid or
stale fixes, initial lack of UTC, clock corrections, missing/full/slow SD cards,
queue overflow, power loss and absent/saturated IMU. Record loss counters and
sensor quality. Validate linear acceleration and lean/pitch against an independent
reference; stationary tilt alone does not validate motorcycle cornering behavior.
See the [issue-backed plan](superpowers/plans/2026-10-07-ridesync.md).

## GPS storage software and open hardware gates (#11)

Run `pio test -e native -f test_storage`, then the full native suite. Tests cover
missing/no-fix and expired UTC, immutable anchor/fix copies, retained stale fix
and stale no-fix, unsafe metadata, invalid sessions/metrics/calendars/associations,
queue overflow, mount retry cap, collision, zero/short writes, flush failure,
threshold and idle/stop flushing, and loss accounting. Condition-variable tests
hold mount and write callbacks until the test explicitly releases them while the
producer fills/drops from the queue and independent control work completes; no
long sleeps stand in for scheduler evidence. Python host framework stand-ins
exercise disabled defaults, task-deferred SDK/POSIX I/O, formatting disabled,
failed existence probes against existing logs, exclusive flags and ENOMEM open
failure, fsync failure, private-mount cleanup and replacement SPI lifetime while
leaving an unrelated global SD mount untouched. A temporary test-only source
copy uses a condition-variable scheduling barrier at the worker stop-load
boundary, proving that a final enqueue after the earlier empty observation is
written/flushed before close; shipped source has no scheduling test hooks. ESP32 compilation verifies `storage_sd.cpp` against the
pinned framework, but it does not qualify hardware or validate blocking latency.

**Not yet bench-verified; whole #11 stays open.** Record the board revision,
qualified SPI bus/pins/CS, filesystem/card model/capacity, framework/firmware
versions, dedicated-volume ownership, worker priority/stack high-water mark,
producer rate and flush policy. With no cameras connected and GNSS initially
without UTC/fix, verify flagged blank-coordinate rows; then test actual valid
fixes, stale/no-fix transitions and anchor changes. Measure BLE/control/acquisition
latency and watchdog behavior during missing/full/slow card and intentionally
stalled worker conditions. Compare accepted/dropped/rejected/written/flushed/lost
and health progress; native concurrency tests cannot prove SDK fairness.

After graceful stop, reset and randomized power cuts during mount, header,
partial row, write and flush, parse complete version-1 CSV rows with fixed column
counts, session consistency and validity checks. Record trailing-row handling,
observed missing rows versus the documented 12-record default software bound,
and corruption affecting previously flushed data/directories. Preserve existing
logs and collision fixtures; never format/delete to make a test pass. Physical
power-loss durability and filesystem corruption remain unbounded until measured.
Document every card/board failure rather than inferring success from compilation.

## Watchdog supervision and reset startup software (#15)

`pio test -e native -f test_health_supervisor` exercises completed-pass liveness,
missing/no-fix/desynchronized devices, disconnect storms and terminal retry
exhaustion, startup grace/deadline rollover, counter wrap/saturation, invalid
configuration, RTC record corruption/reset classes, boot failure saturation,
safe-mode latching/operator clearing, stable-window rollover, and watchdog
subscription ownership/failures. Actual camera manager cancel/reset retires
in-flight and queued intent and rejects old callbacks without replay; observed
and desired recording remain Unknown. A real Storage worker held at mount by a
condition-variable barrier permits atomic supervision and independent control
passes until the test explicitly releases it. This deterministic host test uses
no sleep as evidence. The actual camera retry state machine caps attempts while
continued terminal-state service passes remain healthy execution.

`python -m unittest discover -s test -v` also compiles/runs actual Arduino setup
and health-task entry with SDK stand-ins: default independent task startup,
creation/registration/feed failure, uninitialized TWDT, existing subscription,
retained safe mode, cold-record invalidation and the explicit serial clear
mailbox. Stand-ins verify application calls and ownership, not hardware resets,
SDK implementation behavior, stack use, scheduling or RTC retention.

Default firmware starts one independent priority-2 task on core 1 with a fixed
4096-byte stack and a 100 ms `vTaskDelayUntil` cadence. AT/BLE/SD/IMU slots default
to disabled, and no GPIO or physical driver starts. Safe mode inhibits optional
startup/command admission; current serial-only composition has none to admit.
Future composition must check safe mode before enabling any optional driver or
producer. The Arduino loop only prints changed status and posts operator input;
it cannot feed the supervisor. Supervisor passes do no peripheral IO, allocation,
manager mutations or worker waits. Worker priority/yield behavior and stack
high-water marks still need measurement with the intended composition.

The pinned Arduino SDK uses IDF 4.4.7: global five-second panic TWDT and CPU0 idle
subscription remain owned by the framework. The dedicated task queries its own
subscription, registers once only if missing, then feeds only after a completed
bounded policy pass with fresh progress from every required worker. It never
calls global init/deinit, idle helpers or enableLoopWDT. Uninitialized global
TWDT is an observable startup failure, not permission to reconfigure the SDK.
Errors are published as state/error atomics and printed by the loop; no serial
IO occurs in the supervisor. A preexisting subscription is refused, never
adopted/deleted; its task remains alive with observable failure, because deleting
an already-subscribed task would leave a dangling SDK task handle. Unregistered
startup failures can end their task without affecting framework ownership.
HealthWatchdog::stop is owner-context bounded normal teardown;
it removes only the registration it created, and reports removal failure. The
application's permanent supervisor has no automatic teardown/restart path.
Interrupt/idle WDT protections remain even if application registration fails.

Qualified enabled slots accept deadlines from 200 through 2000 ms and startup
grace from 100 through 1000 ms. Required-but-disabled and unqualified slots are
rejected. These limits reserve cadence margin beneath the existing five-second
SDK watchdog; they are software policy limits, not measured IO guarantees.
Grace can feed before initial progress only within its fixed boot window.
Thereafter a repeated generation never feeds; terminal/missing device outcomes
never independently cause a reset. Optional stalls are reported without gating
required execution. With no required workers, a completed supervisor pass feeds
its own schedulability subscription. Required loss of progress withholds feed;
the SDK watchdog performs escalation, with no cleanup/join of a hung worker and
no automatic application esp_restart. Stable recovery excludes startup grace.

One worker owns each HealthProgress publication; the supervisor owns policy and
last-seen/feed state. Word/byte atomics must be lock-free at compile time. Outcome
is independently observational, not an atomic transaction with progress. Report
only after the owner's bounded AT tick/deadline/queue service, BLE fixed ingress
drain plus manager tick, or IMU acquisition/service completes. Callbacks copy into
fixed ingress owned by the manager context; a timer must not publish on behalf
of blocked IO. No AT/BLE/IMU worker is invoked by the default composition.
`observeStorageHealth` consumes Storage's atomic completed-workerStep counter,
never enqueue acceptance or FS locks. Terminal storage failure is surfaced as a
device outcome. ArduinoSdStorage::ioError remains available separately for exact
adapter errno; terminal alone does not identify the failing SDK operation.
Mount/write/flush/close may block indefinitely. A stopped Storage worker has a
completed lifetime: only its stopped flag after close returns retires its slot's
execution obligation. Terminal alone cannot retire it, because cleanup may hang.
The generic irreversible HealthProgress::finished hook has the same contract:
owner cleanup finished, producers paused permanently, no new work admitted for
that lifetime. This is deliberate quiescence, not invented progress or recovery.
Tests block terminal cleanup separately, verify withheld feed until close returns,
then verify finished missing-storage operation does not cause reset.
Storage's saturating counter therefore eventually fails closed; the helper cannot refresh a saturated/stuck
value. Generic externally observed forward generation wrap is accepted within
half-range; backward/repeated generations are rejected. Direct completed-pass
counters saturate. At 10 Hz UINT32 saturation takes about 13.6 years; a new worker
lifetime/composition must deliberately reinitialize its policy outside a live
subscription. Polling gaps and generation advances must remain below half-range.

At startup capture the SDK reset reason once, independently from application
cause; print neither private identifiers nor keys. Raw RTC_NOINIT bytes prevent
C++ constructors from overwriting the retained record. Magic/version/field
bounds/checksum must validate, and cold/brownout/unknown resets discard retained
accounting. A valid previously armed boot increments the saturating failed-boot
streak on actual WDT/PANIC or annotated software health restart. SDK WDT/PANIC
and app health restart counters are separate; external/operator reset is not a
health restart. The third failed warm boot latches safe mode before optional
startup. Sixty seconds of continuously live completed required-worker execution
clears a nonlatched streak/armed flag, but never clears the safe latch. Serial
uppercase `C` posts explicit operator clearing to the supervisor owner. Metadata
is written only at boot, stable transition, operator clear or explicit restart
annotation; there are no heartbeat flash writes. Future deliberate restart code
must retain the annotation before restart and keep it separate from SDK hints.
RTC is volatile accounting: corruption/power loss/brownout and failures before
setup cannot establish retention or protect a power-cycle boot loop.

Fresh boot constructs fresh managers/queues; do not persist commands, recording
truth or pending callback ingress. In-process reset requires paused admission,
RecordingManager::cancel, CameraManager::reset and ingress discard in their sole
owner context. Never call these concurrently from supervision or block waiting
for a hung worker. CPU reset is not a verified modem receive/physical barrier;
AT re-entry still needs its existing caller-qualified barrier, and session IDs
need an independent uniqueness guarantee across power loss. RTC boot counts do
not provide that guarantee.

**Physical gates remain OPEN; whole #15 is not complete.** Fault injection is
absent/disabled in default firmware. Before acceptance, run an explicit test
build with only a qualified owned worker stalled after entry into blocking work,
while measuring supervisor/control/BLE/SD/IMU fairness, stack margin, latency and
actual reset timing. Record board/build/SDK versions, SDK reason and separate app
cause, record validity before/after each reset, Unknown recording startup and
queue discard. Exercise three failed warm boots, stable recovery, operator clear,
cold/brownout/corrupt retention, missing peripherals, disconnect storms and retry
caps without continuous reset. Use camera screen/media as physical state proof.
No hardware fault injection, reboot, scheduling fairness or RTC retention has
been observed here; debugger-attached watchdog tests cannot establish autonomous
acceptance. Integrated ride/soak evidence remains #31.

## NVS boot preservation software boundary

`python -m unittest discover -s test -v` includes a self-contained host ABI
stand-in for IDF 4.4.7 and an audited Arduino recovery-flow harness. Both original
init errors, sticky original failure across later success, all-label whole-NVS
refusal, init-in-progress admission, partial GC, non-NVS, null, zero and invalid
ranges are exercised. `test_health_startup.py` runs actual application startup
against bounded stand-ins: missing/failed init and refused format close config/
BLE admission while health supervision continues. These tests run before the
ESP32 package is installed in CI and do not claim SDK recovery or hardware proof.

Build the pinned ESP32 target and inspect its verbose link command, firmware map,
`nm` and `objdump -d -C` output. Require both `--wrap` flags and retained wrapper
symbols. Confirm Arduino `initArduino` calls wrapped init/erase, IDF NVS whole
and page erase paths call wrapped partition erase, and wrapper forwarding reaches
the real SDK symbol without recursion. Review the init wrapper's atomic code for
fixed publication steps and absence of heap/log/NVS reentry. Host tests do not
substitute for this final-link inspection.

No destructive fixture is run automatically. On explicitly authorized expendable
hardware, compare partition bytes before/after forcing each startup format error;
separately test ordinary init/GC/reboot, settings and bond persistence, and
failed-init admission. Record recovery writes separately from logical last-valid
record outcomes. Do not perform this on production data. See
[configuration.md](configuration.md) for deinitialized-handle, partial recovery,
raw-flash and physical preservation limits.

## Bounded BLE central software checks (#35)

`pio test -e native` includes the real bounded owner against a fake async host:
immediate/delayed callbacks, four-peer/global initiating fairness, multiple
services and handle routing, missing properties/CCCD, subscription ATT/readback,
copy bounds, queue overflow priority, stop/final detachment, deadline rollover
and deadline-at-completion, failed termination, cancellation losing to connect,
late generation on reused handle and restoration/capacity/security admission.
Review regressions cover ready/read/discovery security loss and valid rekey,
one failed scan-cancel attempt across stop/timeout/callback-fault passes with
exact separate error reporting, and scan completion at/after its deadline.

`python -m unittest discover -s test -v` executes the actual ESP32 adapter against
explicit SDK/NVS/crypto boundary stand-ins with ASan+UBSan. These exercise startup
failures, irreversible sync timeout, actual store-status delegate refusal without
deleting bonds, per-peer refusal, malformed mbuf copying, current-boot self-proof
absence, NVS-vs-stack security/CCCD mismatch and final access after freeing a
retired context. A bounded terminal-barrier regression checks zero wait ticks,
full-queue fault sealing and absence of fabricated quiescence. An overlapping
terminal/barrier during mbuf parsing must still refuse final context release. The crypto stand-in is not SHA256 verification. Actual pinned
headers, SDK symbols and real crypto are checked by the target compile below.

The exact upstream restore-function fixture is also compiled under sanitizers.
Unmodified empty/OUR-only/PEER-only fixtures must report index -1 via UBSan; the
two-line patch must pass those, nonempty bond ordering and original restore error
cases. This reproduces an upstream source defect, not physical NVS behavior.

Run `pio run -e lilygo_t_a7670e_r2` and `pio run -e ble_central_compile`. The latter
retains the complete backend vtable and all raw API references even though default
main does not invoke them. The pre-build patch must verify both freshly installed
and cached sources, fail on drift and run before vendor object compilation. The
source SHA pair and public APIs are documented in [sources.md](sources.md).
Neither build activates BLE, flashes firmware, proves pairing/restore/capacity
or measures SDK lock/NVS/init/teardown latency. Physical #17/#18/#4/#22 acceptance
remains OPEN; qualification procedure and lifetime limits are in
[ble_transport.md](ble_transport.md).

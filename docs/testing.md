# Validation

## Finite ownership and completion

The [October 8 acceptance policy](acceptance_policy.md) governs current ticket
completion. Software tests/builds finish component implementation; actual
compatibility remains Experimental/Not tested until observed. #3 owns the first
X5 smoke test (three REC/STOP cycles and one reconnect); #22 owns the staged
camera matrix, #30 installation, and #31 the finite integrated bench/ride run.
The detailed protocols below describe evidence to collect in those owning
tickets, not repeated blockers on every software component. Reuse existing
results for unchanged configurations and rerun affected failures after fixes.

Prepare #31's existing integrated session with the
[finite acceptance worksheet](release_bench_checklist.md), which records exact
configuration, predeclared budgets, readiness and observed results. The worksheet
is unexecuted; it adds no campaign or hardware pass.

At the owner’s October 9 request, [#46](https://github.com/llazzaro/RideSync/issues/46)
consolidates all remaining camera-dependent checks into one execution checklist,
including the annotated receive evidence needed by #19. The original tickets
retain implementation, compatibility and release decisions; #46 does not add
another soak or repeat unchanged successful component checks. The isolated X5
probe’s three remote Start/Stop cycles and reconnect are already confirmed.

## Automated baseline

GPS forwarding (#14): `pio test -e native -f test_gps_forwarding -f test_local_telemetry`
checks the real encoder/central and GNSS producer path, literal71-byte delivery,
default-off/model/route selection, coalescing/rate/freshness/MTU, shared sequences,
control preemption/interleaving, deadlines/errors, callback generations and peer
isolation. Producer invalidation/cancellation must not enter the BLE SDK.
`pio run -e lilygo_t_a7670e_r2 -e hero12_adapter_compile` verifies pinned target
compatibility; the latter retains the forwarding backend without activation.
The [binding/failure contract](gps_forwarding.md) and existing #46 GPS row own
the finite physical follow-up; synthetic success is not metadata qualification.

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

## ONE RS evidence prerequisite (#5)

Use the [ONE RS target/route decision and finite capture procedure](one_rs_profile.md)
within the existing ONE RS row of #46. Declare the exact Core, lens, firmware
and video settings; collect bounded CE80 receive evidence before qualifying a
state decoder. The isolated probe supports capture, not production ONE RS
control. Its X5 observations and `X5CapturedDisplayV1` do not qualify ONE RS.
The existing three REC/STOP cycles and reconnect follow the implemented profile;
BE80 investigation is conditional if the selected CE80 route is unusable.
No ONE RS hardware observation or unsupported finding is currently recorded.

## GO 3S source-backed profile verification (#21)

Software profile: **Experimental**. Physical compatibility: **Not tested**.
`pio test -e native -f test_go3s_codec -f test_go3s_adapter` checks independent
literal framing/CRC vectors, auth/input bounds, malformed lengths/headers,
fragmented/coalesced notifications, the real shared central and manager,
qualification/disabled startup, missing GATT/security prerequisites, delayed or
missing initial SYNC, rejected/missing/late ACKs, sequence exhaustion, control
queued behind keepalive, cancellation/reconnect and no shutter replay.
`pio run -e go3s_adapter_compile` checks the opt-in ESP32 runtime against the
pinned host stack. Fixtures are source-derived or synthetic, not hardware captures.

The [profile decision](go3s_profile.md) pins the GitHub reference sources and
explains the BE80-first route, authorization commissioning and Unknown status.
The following replaces the earlier CE80-first capture proposal in the existing
GO 3S row of [#46](https://github.com/llazzaro/RideSync/issues/46); #22 retains
physical compatibility ownership. It adds no separate campaign.

1. Record camera firmware (source-qualified `8.0.4.11`), Action Pod/app versions,
   camera mode, docking/power state, board/build revision and existing pairing
   state. Privately commission the verified camera bond identity and printable
   authorization ID. Preserve existing bonds and footage; publish anonymized
   camera/controller labels. Unavailable hardware/commissioning stays Blocked.
2. Verify direct camera BE80 service, BE81 Write, BE82 Notify/CCCD, actual
   negotiated MTU sufficient for the selected auth packet, host security/bond
   checks, camera SYNC (including bounded one-byte prompt if needed) and a
   correlated successful CheckAuth. Record actual target/security requirements
   and any rejection. Do not silently relax encryption or reset/unbind devices.
3. Run three independently requested video Start/Stop cycles and one explicit
   reconnect. Record camera/Pod display observations with timestamps and playable
   new footage. Confirm Stop does not toggle back to recording. A lost response,
   reconnect or controller reset must not replay an earlier shutter intent.
4. Verify SDK completion and status-200 ACK leave RideSync's recording state
   Unknown and group confirmation incomplete; Query, wake and GPS return
   Unsupported. Record timeout/failure honestly. If complete receive evidence
   with camera-observed state becomes available, retain private raw captures and
   publish only reviewed anonymized/licensed fixtures for later state decoding.

No camera hardware was exercised for #21. Reference authors' firmware reports
support an implementation choice, not measured RideSync compatibility. Other
firmware, passive state decoding, wake, GPS and mixed groups remain unqualified.

## Planned GoPro and mixed-camera validation

`pio test -e native -f test_hero12_adapter` exercises the actual adapter,
manager, codec and shared central using a deterministic fake host. It covers
synthetic pairing/control/identity/status setup, missing Management/CCCD,
Busy gating, Encoding confirmation, dropped shutter ACK, fragmentation,
oversized notifications and fragment expiry. It also rejects empty or wrong
status elements in all three successful Register replies. A synthetic
resource-unavailable external-control claim refusal closes the link before
Hardware Info/API/status or shutter work, with no manager Backoff replay under
a three-attempt policy.
This exercises the refusal path; the fixture does not establish that another
physical client caused the refusal.

The fake packets are authored from published protocol structure and are not
HERO12 captures. `pio run -e hero12_adapter_compile` links the opt-in ESP32
composition against the pinned real backend without starting BLE. It proves
source and symbol compatibility, not timing, stack high-water mark, camera
behavior or safe controller startup. Commissioning must record exact installed
model/firmware/API, pairing screen and control ownership, CCCD results,
response/notification traces and response latency. In #22 run three
screen-and-Encoding-confirmed REC/STOP cycles and one reconnect for this model,
then record keep-alive cadence and mixed-link coexistence. #31 owns integrated
power-cycle/reset/no-replay checks. These observations remain unverified.

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

Run `pio test -e native -f test_storage` for focused storage changes; full CI
checks the integrated candidate once. Tests cover
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

**Physical qualification is owned by #31.** Normal real-card write/flush/close
and EN-reset readback have been observed; see the hardware-results reports.
Active supply loss and load/fault behavior remain untested. Record the board revision,
qualified SPI bus/pins/CS, filesystem/card model/capacity, framework/firmware
versions, dedicated-volume ownership, worker priority/stack high-water mark,
producer rate and flush policy. With no cameras connected and GNSS initially
without UTC/fix, verify flagged blank-coordinate rows; then test actual valid
fixes, stale/no-fix transitions and anchor changes. Measure BLE/control/acquisition
latency and watchdog behavior during missing/full/slow card and intentionally
stalled worker conditions. Compare accepted/dropped/rejected/written/flushed/lost
and health progress; native concurrency tests cannot prove SDK fairness.

In #31 use its finite declared startup/write/after-flush power-cut boundaries,
then parse complete version-1 CSV rows with fixed column
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
The actual-main SDK harness now also exercises the configuration barrier and
commissioned worker route; see the #42 checks below.

Default firmware starts one independent priority-2 task on core 1 with a fixed
4096-byte stack and a 100 ms `vTaskDelayUntil` cadence. AT/BLE/SD/IMU slots default
to disabled, and no GPIO or physical driver starts. Safe mode inhibits optional
startup/command admission. An explicitly supplied commissioning provider may
admit the composed workers after config and supervisor barriers. The default
provider is null. The Arduino loop serializes application/control service and
posts operator input;
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

**Physical watchdog checks remain OPEN in #31.** Software completion is
assessed against #15’s revised implementation scope. Fault injection is
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
or measures SDK lock/NVS/init/teardown latency. Physical acceptance is owned by
#22/#31 and remains OPEN; qualification procedure and lifetime limits are in
[ble_transport.md](ble_transport.md).

## Camera event software checks (#37)

`pio test -e native -f test_camera_event_logging` covers distinct request and
queued intent IDs, attempt assignment, refused requests, token-checked and
duplicate wire ACKs, stale command observations, malformed identity/domain rows,
camera queue pressure with GPS and IMU progress, and publication after an empty
owner pass during stop. `pio test -e native -f test_hero12_adapter` exercises the
actual HERO12 adapter with explicitly synthetic BLE packets: its response
validation emits classic and Protobuf ACK evidence, a shutter ACK without an
Encoding change remains separate from recording, the group receives each event
once, duplicate transaction replies are suppressed, and repeated unsolicited
same-value observations remain distinct. The opt-in session test drains callbacks
through the retained group/admission path and closes only after producer finish.

`python -m unittest discover -s test -v` checks V3 serialized GPS/IMU/camera
rows with a separate strict parser, rejects unknown/partial/malformed formats,
and compares the retained GPS v1 timestamp fixture byte-for-byte. Mixed v2
parsing remains strict and rejects camera rows. `pio run -e lilygo_t_a7670e_r2`,
`pio run -e ble_central_compile`, and `pio run -e hero12_adapter_compile` retain
the default and opt-in real ESP32 composition symbols without activating board
peripherals. Use a source-qualified camera, qualified storage hardware and
on-device measurements before drawing conclusions about radio timing, resource
margins, physical durability or recording behavior.

## Independent local runtime software checks (#40)

`pio test -e native -f test_local_telemetry` exercises the one-owner composition:
committed allocation before session construction, camera-free/NVS-refused local
admission, GPS NoFix/fix/stale/error and source-receipt UTC anchoring, safe-mode
IMU refusal, task-create refusal, camera/IMU queue pressure with GPS reservation,
terminal sensor/media outcomes and final producer/SD-access barriers.
`python -m unittest discover -s test -p test_local_telemetry_esp32.py -v` runs the
actual concrete ESP32 facade, bound HERO12 service route, SD worker and BMI270
worker with synthetic hardware/RTOS callbacks; it checks raw IMU/GPS/camera rows
on the same private worker, exclusive route admission, early refusal status and
blocked final close without control waits. Full native/Python/format and all
three pinned ESP32 environments remain the integration gates. See
[local telemetry](local_telemetry.md) for lifetime, qualification and physical
limits. Default startup remains inactive without explicit commissioning.

## Handlebar owner software checks (#41)

See [control ownership and cancellation policy](handlebar_control.md). Native
workflow tests execute the actual button manager, CameraEventSession, HERO12
adapter/central/codecs and recording coordinator with synthetic radio/filesystem
boundaries. They assert actual CameraManager tick and RecordingManager advancement
counters for each composed pass, connection faults before action admission, no
recursive dispatch, fresh-token preparation, REC/STOP, partial/unavailable peers,
already-recording startup, scan/query cancellation, reset/revocation, four-action
pressure/refusal, custom short/long/double mappings and copied storage/LED errors.
Local telemetry tests cover the same owner seam and local-only/refused admission.

`python -m unittest discover -s test -v` also executes the actual ESP32 handlebar
and telemetry composition behind synthetic SDK/UART/I2C/filesystem/RTOS boundaries:
qualified button GPIO polling drives explicit intent through the exclusive global
service, defaults perform no GPIO admission, LED SDK failure remains visible, and
late control attachment refuses without taking group preparation ownership.
Attached qualified control also receives concrete GPS/IMU/power/UART/task/route
startup refusals with no camera advancement, including while a separate legacy
route is bound. Unanswered live-query STOP coverage holds final radio-context
release, injects late evidence while that context remains valid, proves another
peer can stop, and exercises reset and retirement timeout across clock wrap.

The `hero12_adapter_compile` target retains callable handlebar factory/begin/service
symbols and the existing shared telemetry/global routes. Target fixed ABI sizes
are ControlStatus 848 bytes, HandlebarControl 1016 bytes and the retained GPIO
facade 1216 bytes. Individual GCC stack-usage frames are 32 bytes for control
beforeAdvance, 272 for control observe, 880 for facade status, 592 for local
telemetry service, 224 for group advance and 48 for adapter service. These are
individual compiler frames and fixed storage, not measured complete call-chain
stack, high-water margin, heap, latency or radio capacity. Default firmware remains
inactive without commissioning; #22/#31 and all physical/protocol/reference gates stay open.
The sizes/frames above are historical #41 evidence; #42 refreshes them below.


## Composed worker supervision software checks (#42)

See [startup, progress and lifetime contracts](supervision.md). The actual-main
SDK harness drives the same `setup`/`loop`, commissioning provider, configuration
owner, dedicated health-task handshake, concrete telemetry/control facades and
exclusive HERO12 route as production. It exercises delayed and late config,
config/health/SD/IMU task refusal, qualification refusal, supervisor subscription
failure/timeout, safe mode and explicit clear, early stop, blocked ledger and
close, current settings revocation and stale epoch/generation, final worker
access and fresh-owner/session admission. All UART, filesystem, Bosch/I2C, SDK
watchdog/GPIO and RTOS scheduling boundaries are synthetic.

The final-access cases insert a test-only scheduler pause immediately before
the real SD/BMI wrapper final flag in generated source copies. Production
ownership/stop/flag logic remains real; these pauses establish that an earlier
Storage stopped or manager-finished observation cannot release resources. A
blocked filesystem and delayed wrapper let main run repeated bounded passes
without advancing SD completion or claiming worker lifetime completion.
Existing native tests preserve event-first single manager/group advancement,
terminal GPS key release and late-response cancellation; new tests distinguish
refused task admission, never-started lifetime cancellation and a bounded
identity-startup timeout. Historical-source RED checks and final GREEN command
logs are retained externally under `.pio/task-42-logs/`; fixture compile/input
failures are distinguished from behavioral assertion failures in the report.

Full native, Python, formatting, all three pinned target builds and the compiled
config-stack check are required on the final source. Retained target symbols,
fixed ABI and individual compiler frames are refreshed separately; none proves
complete nested SDK/RTOS/interrupt stack, task high-water, heap margin, scheduler
latency, radio capacity or physical watchdog behavior. The original full #22
mixed-camera matrix and #31 finite fault/bench/controlled-ride matrix remain
open, as do SD durability/namespace commissioning, board, camera, IMU/reference,
Insta360 and estimator qualification.


Final #42 source checks passed 327 native cases, 26 Python tests (including the
actual-main SDK scenarios), formatting and all three pinned ESP32 targets. The
fresh default ELF/object configuration-stack audit passes its 4096-byte compiled
application-path budget with an 8192-byte SDK/RTOS reserve; the config task stays
12288 bytes. This reserve is an allowance, not measured complete stack usage.

Pinned Xtensa fixed ABI bytes: ControlStatus 864, HandlebarControl 1032,
Esp32HandlebarControl 1232, LocalTelemetryRuntime 13512, Esp32LocalTelemetry 16000,
SupervisedEsp32Application 18440, HealthSupervisor 140, ArduinoSdStorage 192 and
Bmi270Worker 12. The application contains its facades; these sizes are not
additive memory consumption. The retained factory and actual prepare/launch/
current/service methods plus exact-owner full-route unbind appear in the linked
HERO12 image. Individual compiler frames in that image: prepare 864, launch 48,
current 32, application service entries 32/64, telemetry service 592, control
beforeAdvance 32, control observe 272, handlebar status 896, GPS tick 256, SD/BMI
wrappers 32 each, health/config tasks 64 each, setup 192 and loop 208 bytes.
These are individual emitted frames, not nested call-chain sums or runtime
high-water measurements. The physical acceptance boundaries above remain open.


### MotionV4 integration evidence

The approved static integration has native route/age/reference/queue tests,
independent strict CSV parsing and a concrete ESP32 runtime-to-sink fixture.
See [motion logging](motion_logging.md#measured-software-resources-2026-10-09)
for commands, exact before/after ABI sizes and compiler frames. The current
supervised application service frame is 672 bytes plus its 32-byte wrapper;
older measurements above predate motion status growth. No hardware accuracy,
throughput or stack high-water result is implied; #13 dynamic scope and #31
reference measurements remain open.

## Pure Insta360 GPS encoder software checks (#29)

Synthetic source-derived packets are not camera captures. Run:

```sh
pio test -e native -f test_insta360_gps_encoder
python -m unittest discover -s test -p test_insta360_gps_oracle.py -v
python -m unittest discover -s test -p test_gps_resource_audit.py -v
pio run -e lilygo_t_a7670e_r2
pio run -e hero12_adapter_compile
python scripts/probe_gps_encoder_resources.py --elf .pio/build/hero12_adapter_compile/firmware.elf --build-dir .pio/build/hero12_adapter_compile --nm "$HOME/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-nm" --objdump "$HOME/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-objdump"
python scripts/check_format.py
```

October9 local evidence: 13 encoder Unity tests, 6 independent actual-output oracle
tests, 7 audit regression tests; full native364/364 and Python discovery47/47
after the independent review audit fix. Default and retained pinned ESP32 builds pass.
The resource audit reports actual target result80/config8/packet71 bytes and
packet offset8, encoder256/narrowScalar48/writeDouble32/leap32 individual static
compiler frames. Anchor contents reference real encodeGps. Ordinary emitted
calls resolve to pure helper/byte/arithmetic functions; pinned absolute ROM
helper bodies are unavailable, and compiler integrity-abort is explicitly
excluded. Neither this nor the compiler frame list measures runtime stack or
latency. Camera capabilities/activation remain unchanged. CI discovers all
native/Python tests and audits the retained ELF after its existing build.

[Final full CI](https://github.com/llazzaro/RideSync/actions/runs/37926210431)
passed on `c52d5bc93dcb315e817bf643f909e1257ed0e12d`: Python47/47, native364/364,
formatting, all pinned default/BLE/HERO12 and bench builds, and retained GPS audit.
A fresh independent reviewer found no encoder correctness defects. The audit
indirect-tail-jump gap was reproduced RED, fixed GREEN, then verified by the
full suite/CI. #29 is closed for software; forwarding and all physical gates
remain with their original issues. No minor review finding was deferred.

## Pure BE80 recording request checks (#19, partial)

The request-only codec uses pinned source literals, not camera captures or state
observations. Run `pio test -e native -f test_insta360_codec` for the existing
CE82 shutter plus four BE80 request tests: full StartVideo/Stop literals, every
sequence1..254, unknown profile/command values, sequence0/255, error precedence,
all-zero failures and repeatable output copies. `pio run -e lilygo_t_a7670e_r2`
compiles the portable implementation under the pinned ESP32 toolchain; there is
no production call site or camera activation. Full native/CI and formatting
checks accompany this code change. BLE delivery, ACK/state decoding, camera-level
idempotence and actual recording remain unqualified in #19/#3/#22.

October9 local verification: focused5/5 and full native368/368 passed, pinned
default ESP32 build passed, formatting and diff-whitespace checks passed.
The initial focused RED run failed on the missing BE80 codec header, then the
implemented codec passed the independent fixtures and finite invalid-input
cases. Existing hardware and receive-state gaps are unchanged.

[Final BE80 codec CI](https://github.com/llazzaro/RideSync/actions/runs/37933250577)
passed on `fe34e49c95080d659bb3550c139799f05ab3fd89`, including the native
suite, formatting, pinned firmware/bench builds and retained GPS audit. A fresh
independent review found no issues in this bounded request-only implementation.
Issue #19 remains open for its receive/state and usable recording-path scope.

## Finite Insta360 wake software checks (#9, partial)

Run the focused native suites `test_insta360_wake_encoder`, `test_wake_manager`,
`test_wake_radio_policy` and `test_wake_preparation`. The independent source
literals cover all encoding bytes and invalid inputs. Scheduler tests exercise
four-peer deadlines, one lease, cancellation/invalidation, Busy, missing or
unsupported recovery, fresh/stale state and unreleased cleanup. Policy tests
require actual disconnect and a new callback barrier. Preparation tests execute
the actual CameraManager/RecordingManager with counted synthetic transport and
recovery providers; wake preparation never dispatches REC itself.

`python -m unittest discover -s test -p 'test_wake_esp32.py' -v` compiles the
actual host and worker under ASan/UBSan. Twenty-six synthetic SDK scenarios cover
copied bytes/duration, cancellation/expiry at preparation boundaries, admitted
start cancellation, failed start/stop/termination, full barrier queue, duplicate
completion, incoming store/security refusal across pinned schemas, private
aliases, wrong disconnect, descriptor failure and foreign-handle collision.
Thread rendezvous holds preparation or an already-admitted start while the
control caller expires all four scheduler intents without waiting. Existing
ATT works while wake excludes targeted reset. These tests do not prove SDK
latency, stack high-water, camera wake or recording compatibility.

`pio run -e insta360_wake_compile` links the real opt-in factory, worker,
begin/poll/cycle, and raw GAP data/response/start/stop symbols. The default image
remains disabled. Physical wake/latency/coexistence belongs to #22; the usable
camera adapter/state route remains with #3/#5/#21 and the full #9 acceptance.

October9 local checks: focused preparation8/8 and full native401/401 passed;
full Python51/51 passed in the current checkout, including the existing SDK
regressions. The first native full run collided with another run's shared
executable; isolated build output (`PLATFORMIO_BUILD_DIR=.pio/build/wake-verification`)
then passed all401 cases. Pinned default and retained wake builds both passed;
formatting and diff-whitespace passed. The approved software stage leaves the real-camera recovery route unimplemented.


The fresh independent whole-change review identified four Important defects.
All were reproduced RED and fixed GREEN in one pass: a connection arriving
inside adv_stop cleanup; a nonzero CONNECT status retaining a real peripheral
handle; both routing-lock scan/reservation interleavings; and store/SMP/privacy
activity before the delayed CONNECT callback. The worker remains eligible to
retire late handles until poll atomically claims final release. Actual pinned
private NVS read/write/delete, RAM add/remove/resolution and incoming SMP dispatch
are covered by new Apache-licensed SDK fixtures and ASan/UBSan tests. Exact-source
hash checks and idempotence protect installation of the narrow weak-hook patches;
retained-link inspection confirms all three real refusal hooks are present.
Established foreign central identity/security stays admitted; unclassifiable
private identities remain refused. No review minor was deferred.

Final local native401/401, retained/default SDK builds, formatting and whitespace
passed after the fixes. Final Python55/55 also passed locally. The final commit CI link is recorded
below. The optional local project-health heuristic is skipped when
its CLI is absent; this is not reported as a performed health check.

Final code [CI verification](https://github.com/llazzaro/RideSync/actions/runs/37973836363)
is for `16cd7ea534fe506a085db4f6660a75885a81ac2d`, including repository Python/native,
formatting, default/BLE/wake/HERO12 firmware, retained GPS audit and all three
isolated bench builds. [Implementation decisions](superpowers/decisions/2026-10-09-insta360-wake.md)
retain the review rulings and their costs after execution scratch cleanup.

## CE80 display codec verification — October 10, 2026 (#19)

The approved bounded decoder has eight new behavior tests that failed against
an empty implementation while the twelve existing codec tests passed. After
implementation, the focused suite passed20/20; a further finite receive corpus
covers every supported size6..256 and every proper prefix, bringing it to21/21.
The actual full codec suite also passed a strict C++11 warning-clean ASan/UBSan
build. A private replay through the actual decoder matched all305 retained X5
writes:69 elapsed/Recording,5 settings/Stopped with distinct Video/Photo modes,
25 Remaining/Unknown and206 opaque/Unknown. Raw transcripts stay outside Git;
only six reviewed display-only original packets are retained as fixtures.

`scripts/check_pipeline.sh` passed:55 Python tests, all three existing bench
harnesses,417 native cases and formatting. The pinned default ESP32 build passed
(RAM37,816 bytes; flash355,869 bytes). These build sizes do not claim runtime
activation: default camera capabilities remain disabled. Whitespace checks passed.
One fresh read-only reviewer found no Critical/Important/Minor defects,
independently matched every fixture to original private bytes and passed all21
codec tests under sanitizers. No repeat review or firmware upload was performed.

Review scope rulings: actual BLE transport/activation and generation/time,
freshness, replay, identity, correlation and mode-safe command admission remain
#3 responsibilities. ACK/query and BE80 state semantics remain unevidenced and
unimplemented; unsupported settings/models/firmware stay Unknown. Physical
reliability and exact action timing are not claimed from coarse operator
annotations. These limitations do not replace the implemented known-field codec
with a placeholder and do not close production camera support. Passing CI on
the final source commit is required before closing #19's software acceptance.

Final source [CI run38000783464](https://github.com/llazzaro/RideSync/actions/runs/38000783464)
passed on `7d4ae894a20f5058c2622623939d6c084219e569`, including the full repository
checks, default/retained builds and bench audits. #19 is closed for codec software
scope. Closure does not enable the unimplemented production Insta360 adapter
or supersede #3/#22/#46 acceptance. Subsequent closure-note edits are prose only;
the passing source/build evidence is reused under the acceptance policy.

## Production X5 adapter and serial milestone — October 10, 2026 (#3)

The production route has portable adapter, mailbox/admission, runtime and strict
line-parser suites, plus actual-backend and actual-main SDK stand-in harnesses.
Use the [commissioning guide](x5_serial_milestone.md); build-only targets do not
qualify the camera. The current implementation adds the real CE80 service/SDK
backend, exclusive shared-host reservation, one-attempt runtime and bounded
serial main selection. It preserves the existing captured decoder and shutter
wire bytes. No physical result or ticket closure is asserted by these commands.

```sh
.venv/bin/pio test -e native -f test_x5_adapter -f test_x5_peripheral_policy -f test_x5_runtime -f test_x5_serial
.venv/bin/python -m unittest discover -s test -p 'test_x5*esp32.py' -v
.venv/bin/pio run -e lilygo_t_a7670e_r2 -e x5_adapter_compile -e x5_serial_milestone -e x5_store_inspect
scripts/check_pipeline.sh
git diff --check
```

Independent literal display/shutter fixtures test missing/mismatching identity,
subscription, stale/photo/malformed/lost state, passive Query, pre-send receipt
cutoff, one-send ambiguous errors, deadlines, handle reuse and no replay. Actual
SDK harnesses cover registration failure, wrong peer, allocation refusal,
subscription loss, queue overflow, cancel/expiry and delayed SDK return after
disconnect, with retained callback barriers. Actual `setup()`/`loop()` and runtime
harnesses cover missing provider, safe-mode/NVS/task refusal, late configuration,
revocation persistence, explicit CONNECT and continued deadlines under serial
flooding. The private inspector is read-only and never commissions a camera.

Local software verification: repository pipeline passed 57 Python tests, all
three existing bench harnesses, 469 native tests and formatting. Four portable
X5 suites passed 52/52 under strict C++11 `-Wall -Wextra -Werror` with ASan/UBSan.
The actual backend/main harnesses also use sanitizers. The lookup-under-critical
regression failed before moving the NimBLE mutex-taking lookup outside the
critical section, then passed. The existing local-telemetry actual composition
harness passed after excluding these independently tested SDK units from its
portable compile list. No failing test is waived.

Pinned default/retained-X5/serial/inspector builds passed. Before final-review
fixes, the retained image used RAM 73,068 bytes and flash 693,949 bytes; the
uncommissioned serial image used RAM 57,500 bytes and flash 682,729 bytes. The
retained symbol audit includes the actual backend factory, GAP callback, worker,
notification and serial service. ESP32 compiler frames were 96 bytes each for
GAP and send, 64 for workerStep, 240 for serial service and 2112 for the existing
store inspection. These are individual compiler frames, not peak stack/heap,
latency or physical initialization observations. The 4096-byte SDK worker and
12288-byte configuration owner retain their declared storage; runtime high-water
measurements remain unperformed. One fresh review found three Important defects (queued display admission, idle
loss replay and duplicate-result masking); all three were fixed with focused
regressions. No Critical or Minor findings were confirmed or deferred. Source
[CI passed](https://github.com/llazzaro/RideSync/actions/runs/38044779040). #3 is
closed for its completed software and existing three-cycle/reconnect smoke
evidence. The final composed-path camera check and private commissioning remain
pending in #46; support stays Experimental. No composed hardware pass is claimed.

The first source CI attempt exposed an unintended static retention regression:
a global X5 serial parser rooted its virtual port methods and the X5 backend in
the retained HERO12 image, overflowing DRAM by 13,520 bytes. The same pinned
local build reproduced that failure. Lazy construction at X5 milestone service
removed that root; HERO12 and all three X5 images then linked successfully, and
actual-main sanitizer tests passed. Symbol checks prove HERO12 does not retain
the X5 backend while the two intended X5 runtime targets do. CI now checks that
absence explicitly. No firmware was uploaded during software verification.

The existing motion CSV acceptance fixture also had an invalid zero-filled FIFO
trailer, so its IMU stopped after three read attempts and scheduling determined
whether a valid sample survived logging. The positive motion fixture now supplies
the literal valid `0x80` end marker and waits at most one wall-clock second for
ten admitted sample rows. The initial whole-worker delay workaround was reverted. Only the positive
IMU thread now honors its requested sample interval, preventing producer
flooding from making sample admission depend on scheduler phase; storage and
negative/runtime cases preserve their original delay. The focused composition test and ten consecutive
paced motion CSV runs passed, including actual static-tilt rows. A matching
Ubuntu x86 reproduction also passed before this pacing correction; the CI
admission failure was scheduler dependent. Production telemetry
and motion estimation were unchanged.

## Composed X5 wake/recovery (#9)

`test_x5_wake` uses the real X5Runtime, CE80 adapter/decoder, WakeManager and
X5WakeRecovery with controlled radio/peripheral inputs. It checks idle boot,
wake/connect/fresh Video status then separately requested Start, subscription
and Photo refusal, original-deadline expiry across clock wrap, revocation,
cancellation with delayed display, actual cleanup release, live-link protection,
unqualified identifiers and stale Ready observations. `test_x5_serial` checks
WAKE dispatch and refusal without replay. A separate real RecordingManager/
WakePreparation/X5WakeRecovery composition test confirms one Start and the
recording result, including adapter event delivery through the group authority
and REC → STOP → REC with one wake/connection.

`test_x5_wake_esp32.py` compiles the actual startup worker with controlled SDK
outcomes: allocation failure, failed host, never-ready host and late return cannot
advertise. Startup creates one worker with no replacement/retry. Cancellation retains
its admission lease until the final worker access; an expired optional wake
cannot fail a Ready host or race a replacement CONNECT.
`test_x5_milestone_esp32.py` exercises actual main/serial commissioning with and
without the wake/private provider under ASan/UBSan. A pending wake refuses REC;
DISCONNECT retires wake, and missing qualification does not enable radio.

```sh
.venv/bin/pio test -e native -f test_x5_wake -f test_x5_serial -f test_x5_runtime
.venv/bin/python -m unittest discover -s test -p 'test_x5_wake_esp32.py' -v
.venv/bin/python -m unittest discover -s test -p 'test_x5_milestone_esp32.py' -v
.venv/bin/pio run -e x5_wake_milestone
```

The composed build links both shared-host concrete workers and is included in
CI. This is synthetic/compile evidence; no camera, startup-stack high-water or
latency result is claimed. The physical composed-path check is in #46.

# Independent local telemetry runtime (#40)

`LocalTelemetryRuntime` is the serialized application owner for modem/GPS,
SessionClock mutation, CameraEventSession service and telemetry admission. It
creates no application task. Call `start`, `service`, control-manager operations,
`requestStop` and `status` on that same owner. Copy the fixed status through a
mailbox before using it in another context. Do not call a manager/group tick,
GPS tick or admission tick after the owned service pass. #41 adds controls on
this owner; #42 supplies startup barriers and supervision. Default firmware does
not construct or start the runtime.

`Esp32LocalTelemetry` connects this owner to the real ArduinoModemUart,
GpsManager/ModemGnss, Bmi270Imu/ImuManager/Bmi270Worker and ArduinoSdStorage. Only
IMU acquisition and SD filesystem access have separate workers. Its bound
`ridesync_hero12_service()` route advances the entire telemetry owner exactly
once, including the existing #37 camera route when admitted. Legacy
`hero12BindSession` and the telemetry binding are mutually exclusive. The binding
is released only after the runtime is releasable. Call either the facade service
or its bound service entrypoint once per application pass; never both.

## Explicit qualification and lifetime

Supply caller-owned UART, initialized dedicated SPI and initialized dedicated
I2C. UART pins/baud/documentary AT profile, an already-powered modem with a
verified startup/receive barrier, finite I2C timeout/buffer >=128 and electrical
IMU proof, SD wiring/card/exclusive volume and commissioned namespace must all
be independently qualified. This composition performs no modem power sequence,
SPI/I2C initialization, guessed pins or physical reset. Qualified UART startup
uses the supplied pins/baud through ArduinoModemUart::begin. Strings and all resource
objects remain valid through `canRelease()`. Allocate the facade in static or
other stable owned memory, never on the application or worker stack.

`QualifiedLocalTelemetry` defaults inactive. Enable only qualified resources and
set runtime opt-in explicitly. Missing qualification and initial SD task refusal
return false with copied Refused/fault status and no pending worker lifetime.
Repeated start is refused. An IMU task refusal is separately reported while GPS
logging remains eligible. Actual safe mode is passed to Bmi270Worker: its current
policy refuses IMU acquisition, and the runtime reports SafeModeRefused while
qualified GPS continues. This does not bypass safe mode or implement #42 policy.
Cameras are optional: an empty/disabled/refused camera configuration and NVS
refusal do not affect independently qualified local admission. A failed camera
activation falls back to the same local session, with CameraAdmission::Refused.
Camera qualification/configuration of the singleton trio precedes opt-in startup;
the runtime adds no speculative camera protocol or wake behavior.

The callable boot-lifetime `ridesync_local_telemetry_runtime(uart, spi, wire, q)`
retains the first supplied resources/configuration. Repeated calls return that
same owner; they cannot restart a terminal session. Alternatively construct a
caller-owned facade directly. `ridesync_local_telemetry_start` and
`ridesync_local_telemetry_service` call its explicit methods. The retained compile
image links the concrete construction and all worker/adapter paths; none of these
entrypoints is called by shipped setup/loop.

## One identity, clock, admission and Storage

One private SD worker mounts, reserves an identity, publishes Committed, waits
for binding, services Storage, closes and unmounts. Only Committed with a nonzero
ID permits construction of the session clock and one CameraV3 Storage on that
same private sink. Local mode uses CameraEventSession::activateLocal without
requiring peers, attaching an audit route or advancing camera managers. Camera
mode retains #37 logger/receipt translation, fairness and stop contracts. Both
modes use exactly one admission owner and the same worker, never two loggers.

Session IDs reuse the #39 commissioned namespace32/counter32 allocator. Offline
commissioning/external namespace exclusivity and acknowledged-commit-survives
premises are required; random values, NVS availability and camera IDs are not
substitutes. See [SD allocation](log_format.md) for provisioning, reboot,
uncertainty and whole-valid rollback limits. Repeated reservation within an owner
returns the same candidate; fresh sessions use fresh owners and advance the
ledger. Missing/corrupt/uncommitted/zero identities refuse logging. CSV EEXIST is
a terminal collision with copied errno; no Storage reset, path retry or reuse of
the burned ID occurs. No automatic commissioning is performed.

## Time, pressure and observations

Each pass ticks GPS, snapshots the same session clock and modem state, optionally
anchors fresh source UTC at its original parsed receipt milliseconds, then admits
GPS using one coherent RecordTimestamp/sample pair. A later owner pass does not
restamp old UTC. Anchor uncertainty remains unknown. NoFix, missing, stale,
protocol/timeout/desynchronization and invalid data remain explicit. There is no
synthetic fix or acquisition time. Raw IMU counts retain read-boundary raw
millis32, FIFO/control/time evidence and raw metadata; camera rows retain owner
admission versus translated audit-receipt domains. These are not frame sync,
gravity-free acceleration or validated inclination estimates.

GPS records have a configurable positive bounded cadence (default 1000 ms).
Admission keeps #34/#37 quota two for shared IMU/camera ingress and two Storage
slots reserved for GPS. Slow/full media can still drop every class; counters
report that loss. Camera bursts/disconnections cannot condition GPS/IMU progress
on camera success. Status copies storage per-kind accepted/dropped/rejected/
written/lost counters, inbox drops, identity/errors and atomic IMU outcome/progress.
Manager/codec details are read only after final IMU worker access. A terminal IMU
outcome does not halt GPS. Terminal storage starts ordered teardown, retaining
fault/error and loss counters.

## Ordered nonblocking stop

1. Terminally cancel GPS on the owner, releasing an asserted qualified PWRKEY
   immediately without waiting for the pulse duration. GPS publication and further
   tick/restart IO end; supply ownership stays with the caller. A fresh owner and
   session are required to resume after explicit cancellation. Then stop IMU
   acquisition and request camera/session stop. The camera route retires before
   its inbox finish. Manual stop and automatic media failure use this same path.
2. Continue bounded owner service while IMU may publish its last batch. Observe
   the worker's acquire/release final-access flag, then call finishImu. A failed
   or disabled IMU task has no worker to await and its inbox is finished directly.
3. Admission drains bounded ingress according to existing quota/drop policy and
   requests Storage stop. The private worker completes queued IO/final close.
4. Observe CameraEventSession::stopped and the actual SD workerFinished before
   canRelease; only then release binding, objects, strings and caller buses.

Stop during pending allocation cancels the unbound owner and awaits its final
close. Stop never waits for a filesystem or closes it on the control context.
A blocked SD close keeps resources retained indefinitely while control service
returns promptly; a later supervisor may report the stall but cannot safely free
those resources. No deadline fabricates worker completion.

## Evidence and limits

Native tests use real owner/allocator/admission/storage/clock/modem/IMU-manager
code with synthetic hardware boundaries. The ESP32 composition behavior test runs
the concrete facade, actual bound HERO12 route and actual Arduino SD/IMU worker
code against RTOS, SPI/POSIX, UART and Bosch/I2C stand-ins. It covers resource and
task refusal, safe mode, local/raw/camera logging on one sink, exclusive routing,
final publication and blocked final close. Pinned ESP32 builds separately link the
unmodified vendor/SDK implementation. Neither proves hardware timing or support.

Fixed ABI sizes and individual compiler stack frames are recorded in the issue
implementation report. They exclude SDK heap allocations, full nested call chains,
interrupt/RTOS paths and physical stack high-water. Board/modem/IMU/electrical
qualification, storage power-loss durability, BLE capacity, camera-storm latency,
queue loss rates, watchdog, soak and ride acceptance remain open under #22/#31
and component hardware tickets. No physical device operations or flashing are
part of these software checks.

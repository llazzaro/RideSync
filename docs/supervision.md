# Supervised application startup and worker lifetime

`main.cpp` is the serialized application owner. Its weak
`commissionedApplication()` provider returns null by default. A commissioned
board may return a boot-lifetime `SupervisedEsp32Application`, using initialized,
dedicated caller-owned UART/SPI/I2C, explicit GPIO reservations, qualified modem
startup, a commissioned SD namespace and per-camera firmware/API/bond evidence.
The retained `ridesync_supervised_application` factory creates that owner without
device IO; first arguments fix its resources for the boot. Saved configuration
cannot qualify pins, protocols, identity or media. Saved button pin/pull/polarity
must match the independently supplied qualified GPIO profile before admission. No default peripheral starts.

Setup constructs the permanent 12288-byte config owner with the separately
supplied frozen opaque peer mapping. Loop waits at most 1000 ms for a completed
matching settings snapshot. Unavailable/task-refused config is explicit. Timeout
is terminal for settings admission for that boot: the live NVS task/resources
remain owned, late publications are consumed and rejected, and no erase,
reinitialize or retry occurs. Independently qualified local logging stays eligible.

Only after that barrier does main prepare fixed qualified enabled/required
AT/BLE/SD/IMU policies and call `HealthSupervisor::begin` once. Required admitted
slots use 1000 ms startup grace and 2000 ms completed-work deadlines. The health
task must successfully acquire its own SDK TWDT subscription before optional
launch; failed creation, subscription refusal or a 1000 ms handshake timeout
prevents launch, including after a late handshake. The framework panic watchdog,
CPU0 idle ownership and foreign subscriptions remain untouched. There is no
supervisor rebase, automatic application restart or hardware watchdog claim.

Each supervisor slot has one application publisher forwarding genuine completed
work: AT uses returned modem ticks; BLE uses the existing central service-end
publication; SD uses returned real wrapper steps from mount/ledger allocation
through wait-bind, Storage IO and close; IMU uses the actual acquisition manager
generation with the BMI wrapper final flag. A blocked operation cannot return
and cannot publish progress. Device NoFix/Missing/Desynchronized/IoError differs
from execution stall. A terminal invalid session clock publishes the returned
AT validation pass and retires that slot without UART restart or a fake stall. Task refusal publishes an irreversible refusal observation
with zero fabricated work; a required refusal cannot feed or establish boot
stability. A canceled worker that never completed work cannot prove stable boot.

SD policy is active before mount/ledger IO, and planned IMU policy is active
before its later task creation after identity commit. Allocation not yet admitted by the application owner is
terminally canceled after 1000 ms, including a late committed result, retaining the blocked SD owner and buses until
actual final access. Missing qualifications and task/adapter refusal retain
explicit status; disabled or refused workers are never reported as started.
Safe mode permits qualified GPS/SD logging and uses the actual BMI safe-mode
startup refusal. Clearing it does not retry a refused worker or replay control.

Current settings epoch/generation, effective status, frozen peer validity,
NVS gate and safe mode govern every control pass. Any revocation/new generation
terminally retires camera authorization for that session; returning settings or
clearing safe mode does not restore old actions. Telemetry owns this continuing
gate, so copying startup settings cannot re-enable control in a later runtime
pass. Controls reset/cancel pending work; camera state begins Unknown and no
saved toggle is replayed. Copied #41 UI status carries configuration outcome,
worker stalls/refusals and device observations through the same owner pass.

Stop revokes controls/camera operations and terminally cancels GPS, releasing an
asserted qualified key once. IMU stop is requested; the session stop request is
deferred until actual BMI final publication and wrapper final access are seen.
Then `finishImu` and `CameraEventSession.requestStop` allow bounded ingress drain
and isolated Storage stop. SD must finish close/unmount and its wrapper's last
access. Only `canRelease` permits exact-owner `hero12UnbindTelemetry`; the legacy
session route alone uses `hero12UnbindStoppedSession`. The facade unbinds before
caller destruction of session/sink/buses. Manager-finished, Storage-stopped and
session-stopped alone are insufficient. No control pass waits on filesystem IO.
Callers must retain the application and all resources while a worker is blocked.
A subsequent session needs a fresh terminal owner and a newly committed identity.

Synthetic native/SDK-boundary tests prove software ordering. Physical pin/bus,
board, camera, SD power-loss durability, namespace commissioning, IMU/reference,
stack/heap/latency/watchdog, full #22 mixed matrix and full #31 soak/ride acceptance
remain open. No motion estimator or unqualified Insta360 route is enabled.

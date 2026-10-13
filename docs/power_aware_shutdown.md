# Motorcycle power-aware shutdown investigation (#57)

Decision date: October 13, 2026. Scope: research and a finite future implementation
plan, not firmware delivery or installation approval. Governed by
[acceptance policy](acceptance_policy.md).

## Decision and evidence boundary

**Defer installed automatic shutdown until a protected supply and its hold-up
budget are measured; proceed later with a default-off software coordinator.**
Use protected ignition/ACC sensing for ordinary key-off, plus an independent
upstream power-fail signal for sudden supply loss. Prefer an external regulated
supercapacitor backup prototype with an independent cut-off timer. Preserve the
current policy of **leaving cameras recording**: shutdown retires controller
intent and local logging, and sends no camera shutter STOP. Camera battery/media
management remains the camera's responsibility. Auto-start recording is outside
this plan.

Firmware cannot guarantee an SD operation returns or that a card commits data
when `fsync` succeeds. The proposed deadline bounds controller policy and power
retention, not filesystem durability. A timed-out worker remains owned until
power removal; never destroy its resources or manufacture a completion flag.
This is a concrete deferred implementation decision sufficient to close #57.
It leaves physical installation with #30 and cut/reset observations with #31.

Evidence labels used here:

- **Documented/source:** vendor drawing/datasheet and inspected source behavior.
- **Synthetic:** sizing arithmetic and proposed timing/threshold fixtures.
- **Observed:** existing dated board, SD and reset reports linked below.
- **Hardware-confirmed:** no automatic power-event interface, hold-up interval,
  standby current, transient protection or motorcycle wiring is confirmed.

## Audit of the fitted board and existing shutdown

The [hardware baseline](hardware.md) identifies a T-A7670E R2 with photographed
V1.4 PCB and ESP32-WROVER-E; fitted component continuity remains unverified.
The vendor guide's linked [V1.4 schematic, pinned at
51b988a](https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/51b988a99740ee0387a70c06bc3bd6ca26eedd69/schematic/esp32/T-A7670X-V1.4.pdf)
was inspected as a drawing (May 23, 2024), including power sheet 4 and ESP32
sheet 3. It draws USB `VBUS`, a `SOLAR_IN`/P1 input, CN3065 charger U7,
DW06D/Q4 battery protection, battery switch SW1, LN2220 battery boost U10,
MT1471 USB buck U9, and AP2112K/RT9080 3.3 V regulator U14. It includes
USB/battery selection FETs and a VBUS/GPIO12-controlled branch; GPIO12 is
not an established whole-board cut-off. GPIO35 senses `VV_BAT` through equal
100 kΩ R7/R9 with 10 nF C57. CN3065 CHRG/STDBY drive indicators; no ESP32
power-fail interrupt or programmable PMIC status bus is established. Sheet 3
also routes its `SOLAR_IN` ADC divider through RZ4 to SENSOR_VP/GPIO36.
These are documentary routes, not measured
electrical behavior; module labels include other A767x variants.

Do not apply vehicle voltage to USB, battery, SOLAR_IN or GPIO on the strength
of net names. No installed battery/capacitor capacity, charger temperature
behavior, input rating, switch leakage, reverse-current path or minimum working
rail is measured. A battery can mask upstream failure at GPIO35; a regulator
can mask input sag until its output collapses. USB connections changed startup
observations in the [October 8 report](hardware-results/2026-10-08-reconnect.md),
but that report did not measure the electrical cause.

Source inspection at the repository revision preceding this note:

| Contract | Relevant source | Consequence for power-event handling |
|---|---|---|
| Manual SHUTDOWN | [bench_console_esp32.cpp](../src/bench_console_esp32.cpp), `execute` | Calls telemetry `requestStop`; waits for status. It does not cut supply or issue shutter STOP. |
| Qualification and pin ownership | [bench_application.cpp](../src/bench_application.cpp), `validateBenchBusPins`, `powerRoutingQualified` | Qualified private configuration and conflict checking precede bus IO; no ignition/voltage input is composed. |
| Admission and teardown | [local_telemetry_runtime.cpp](../src/local_telemetry_runtime.cpp), `requestStop`, `serviceSession`, `observe`; [camera_event_session.cpp](../src/camera_event_session.cpp) | Revokes control/motion, cancels group/adapter and GPS, stops IMU, waits for its final access, then drains the session. Adapter `stop` is transport teardown, not a shutter command. |
| Storage ordering | [telemetry_admission.cpp](../src/telemetry_admission.cpp); [storage.cpp](../src/storage.cpp), `serviceRows`, `flushCached`, `health` | Sole producer publishes stop after final ingress; queue is eight records, writes are 256-byte chunks, idle/threshold flush precedes close. Terminal loss counters are conservative and can saturate. |
| Actual SD IO | [storage_sd.cpp](../src/storage_sd.cpp), `Sink::flush`, `Sink::close`, `run` | POSIX fsync/close and SDFS unmount can block/fail. Only final wrapper publication establishes last access. IO error is distinct from completion. |
| Session identity | [session_identity.cpp](../src/session_identity.cpp); [session_storage_owner.cpp](../src/session_storage_owner.cpp) | Reserve through two CRC-checked slots; commit includes sync/close/readback before ID publication. Both slots must validate. Uncertain/corrupt/missing state refuses allocation; no implicit repair. |
| Supervisor/lifetime | [application_esp32.cpp](../src/application_esp32.cpp), [supervision contract](supervision.md) | Genuine returned work controls progress; blocked owners/buses must remain alive. Reset/safe-mode gates retire authorization and do not replay it. |

Existing SD mount, four-row writer and EN-reset/new-session readback are
**observed**, recorded in the [hardware baseline](hardware.md) and
[SD writer bench](../tools/bench/sd_logging/README.md). They do not establish
abrupt battery removal, brownout recovery or a worst-case flush time under
modem/BLE/IMU load. This note makes no new physical observation.

## Detecting the event safely

Primary sources below were retrieved October 13, 2026. Part selection is a
prototype candidate; review complete datasheet limits and reference layouts
before schematic release.

| Option | Value and limitations | Decision |
|---|---|---|
| Protected ACC input | Detects intent before stored energy drains. Contact bounce and starter-induced interruption require qualification; ACC may be PWM or switched ground on a particular motorcycle. | Primary ordinary shutdown input, electrically characterized first. |
| Protected upstream voltage/comparator | Detects unplug, fuse loss and sag even while backup maintains 5 V. Threshold must precede insufficient energy; cannot infer ignition state from battery voltage alone. | Independent emergency trigger. |
| GPIO35 battery ADC / another ADC | Useful diagnostics after matching the board divider and calibration. Battery state may hide loss; polling can miss fast collapse. | Supplemental, never sole emergency detector. |
| Board charger/status or GPIO12 | Charge indication differs from loss of usable power; GPIO12 shares peripheral/strap behavior. No qualified interrupt/cut contract exists. | Do not reuse as power-fail or global cut-off. |

For ACC, a candidate protected comparator circuit uses a
[TPS3700](https://www.ti.com/product/TPS3700) powered from the held-up 3.3 V
logic rail. It has a 400 mV threshold, open-drain outputs and internal
hysteresis. Feed ACC only through a reverse-protected, transient-clamped sense
branch with pulse-rated series resistors and RC filtering; pull its output up
to held-up 3.3 V, never to vehicle voltage. Illustrative divider: 240 kΩ total
top resistance (split into two resistors) and 10 kΩ bottom gives about 10 V
threshold and 56 µA at 14 V. With a 10 nF input capacitor the divider time
constant is about 96 µs; firmware supplies the longer debounce. These numbers
are synthetic, not approved thresholds or component voltage ratings. The
comparator's input/supply absolute maxima, leakage, negative excursions and
clamp current must pass worst-case analysis; 18 V supply capability does not
make it safe on raw automotive wiring. Use hardware hysteresis where needed;
software recovery hysteresis is separate.

ACC sense and power-fail status need distinct inputs. GPIO34 and GPIO39 are
provisional input-only candidates with external pulls; identify matching header
nets and every active reservation first. Reserve GPIO35 for the battery divider
and GPIO36 for the schematic's SOLAR_IN divider; neither is an accessory input.
Reject modem 4/5/25/26/27/33, power 12, SD 2/13/14/15, PSRAM 16/17, flash 6–11,
serial 1/3, straps including 0, button/LED and IMU/I2C/interrupt conflicts.
Extend existing reservation validation rather than approving unused-looking pins.
ADC diagnostics should prefer ADC1 with calibrated attenuation; the pinned
[Espressif ADC guide](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/peripherals/adc.html)
documents ADC2 contention with Wi-Fi and measurement limitations. ADC
attenuation is not input overvoltage protection. Validate clamping, source
impedance and readings against an external meter; do not treat 3.3 V as a
universal calibrated full-scale range.

Keep a common qualified ground or design an isolated ACC interface if ground
offsets demand it. Prevent off-state injection through input clamps or status
pullups. Test ESP32-off/ACC-on, USB-only, backup-only and vehicle-only cases.
Disconnect host USB VBUS during power measurements or use a qualified isolated
service interface; otherwise the host may sustain the board or be backfed.
The [INNOVV K7 product page](https://innovv.com/products/innovv-k7-dashcam)
describes ACC-triggered power management as product precedent only, not a
reference schematic for this board.

## Wiring and BOM candidate

Prototype topology (all arrows denote electrical connections, not fitted wiring):

```text
vehicle B+ -> near-source fuse -> TVS/reverse/overvoltage protection -> 5 V buck
                                                                          |
                                                     supercap backup manager
                                                                          |
                                               reverse-blocking load switch
                                                                          |
                                                   board ESP32 USB 5 V input
vehicle ACC -> protected sense -> comparator --------------------> ACC input
backup upstream fail / ready status -----------------------------> logic inputs
ACC edge + MCU OFF request -> independent one-shot/latch -> load-switch enable
vehicle return --------------------------------------------------> qualified GND
```

The hardware latch permits startup on qualified ACC-on, holds supply during
key-off, and cuts after MCU OFF or an independently enforced maximum interval.
After a timeout it must latch off until a fresh qualified ACC cycle, rather
than repeatedly rebooting on backup or resetting its deadline on bounce. Sense
and latch circuitry stay powered upstream of the board switch. Manual SHUTDOWN
uses the same software drain; power-cut authority must be an explicitly
qualified extension, not an assumption about the current serial command.

| Candidate item | Basis / selection boundary |
|---|---|
| Fuse, sealed connector, pulse-rated TVS, input filter | #30 chooses fuse/cable rating from peak current and interrupt capacity. TVS standoff must exceed declared normal maximum; clamp at declared pulse current must stay below protected-stage ratings. No guessed universal load-dump pulse or arbitrary fuse value. |
| LM7480-Q1 with external back-to-back MOSFETs | [TI documentation](https://www.ti.com/product/LM7480-Q1): reverse polarity/current and overvoltage control. Select FET SOA, OVP network and TVS together; the controller is not an energy absorber. |
| LM53603-Q1 5 V / 3 A buck prototype | [TI documentation](https://www.ti.com/product/LM53603-Q1): 3.5–36 V operation, 42 V transient tolerance. Protection must respect these limits; 3 A nominal is not proof of modem burst/charge-load headroom. |
| LTC4041 / DC2642A-A backup prototype | [ADI documentation](https://www.analog.com/en/products/ltc4041.html): low-voltage supercapacitor backup, input disconnect, power-fail indication and balancing. Use its evaluation design as a bench starting point; rated backup output still requires load, thermal and depleted-cap testing. It is not a raw-12 V front end. |
| Two series 10 F, 2.7 V supercapacitors | Example 5 F effective bank at at most 5.0 V total; balancing, tolerances, leakage, ESR, aging and temperature derating required. Exact manufacturer/grade selection follows measured requirements, not this arithmetic. |
| TPS3700, sense protection, split divider, RC and external pulls | ACC candidate above; upstream fail should use the backup manager's documented status with qualified voltage translation/polarity. Reserve a ready input if needed. |
| Reverse-blocking load switch and independent timer/latch | Device/rating selection unresolved until peak load, reset levels and required interval are known. Require default-safe outputs and complete interruption of every board source. Do not implement only an MCU-controlled FET. |

Leave the board battery and SOLAR_IN disconnected in this initial external-5 V
prototype. Verify the selected ESP32 USB port/input and cable drop on the actual
unit, including modem transients; never substitute the modem USB port. The
board's existing selector/charger must not introduce an alternate source or
backfeed route that defeats OFF. No parts are claimed purchased or fitted.

Alternatives: a qualified protected battery offers longer reserve, but requires
chemistry-compatible charging, enclosure/temperature qualification and end-of-life
capacity checks. The board's apparent CN3065 route is insufficient evidence to
approve a cell. An external [LTC4040 battery manager](https://www.analog.com/en/products/ltc4040.html)
is a research alternative if capacitor size becomes infeasible; verify chemistry
and charging limits before selection. An always-live vehicle feed with delayed
switch-off is simplest for normal ACC-off, but offers no hold-up after a fuse or
lead failure. A capacitor directly across 5 V has declining output and inrush;
a regulated backup and charge/current control are preferred.

## Hold-up budget and measurement

Measure from first input fault to final SD-wrapper access and required backend
release, not just to `written` or `flushed` counters. Let usable time exceed
`detection + debounce + drain deadline + cut margin`. The benchmark candidate
uses 500 ms ACC-off qualification, a 5 s drain policy and 500 ms cut margin:
at least 6 s reserve, with a proposed 7 s independent cut limit from first ACC
fall. Emergency upstream power-fail bypasses the ACC debounce and starts the
drain immediately on the next admitted owner pass. These are synthetic initial
test settings; observed load/cut results may reject them. They are not SDK
return-time bounds. Hardware must remember the first edge throughout bounce.

For a regulated capacitor bank, `C >= 2 * P * t / (eta * (Vhi² - Vlo²))`.
Illustrative `P=5 W`, `t=6 s`, `eta=0.8`, `Vhi=5.0 V`, `Vlo=3.0 V`
requires 4.69 F effective. A 5 F bank supplies only 32 J after assumed
efficiency: 6.4 s at 5 W or 3.2 s at 10 W, before ESR/tolerance/aging losses.
Thus the example two-10-F bank has little margin at 5 W and is inadequate at
10 W; it is a sizing probe, not an accepted BOM. Converter discharge floor and
current limits must be substituted from the selected design and measured load.
Two series cells halve capacitance. Check peak droop `I * ESR`, not energy alone.
A plain capacitor from 5.0 to 4.5 V would need about 15.8 F effective for the
same assumed load/time/efficiency; usable board voltage is currently unknown.

Use oscilloscope channels for ACC/upstream fail, held-up 5 V, board 3.3 V and
final-access marker; current probe/shunt captures modem bursts, four-peer BLE,
SD flush/close, IMU and charging. Record peak and average load, detection and
drain latency, lowest rails, cap charge and actual cut. Repeat at minimum accepted
charge and declared operating temperatures. Do not shed GPIO12/SD power before
final access; modem supply sharing needs separate qualification. Admit logging
only when reserve readiness is valid; if not ready, expose degraded protection
and refuse protected logging instead of claiming hold-up.

Off-current must include protection, buck, timer, sense dividers, capacitor
leakage and any USB/board alternate path, not just ESP32 deep sleep. Proposed
parked budget is <=100 µA total vehicle draw, subject to owner approval during
installation; 100 µA consumes 72 mAh in 30 days. A continuously enabled
LM7480-Q1 has roughly 0.4 mA typical operating current by itself, so this
candidate needs a qualified upstream disable/low-leakage standby arrangement
or a revised declared parked budget. Measure key-off and fully drained states.
Deep sleep is not selected as the final OFF mechanism.

## Proposed bounded software state machine

One serialized application owner polls fixed-size observations, no filesystem
IO in GPIO/ISR callbacks. Invalid/unqualified configuration remains Disabled
with zero peripheral calls. Monotonic uint32 timing uses unsigned subtraction,
all configured intervals and service gaps <2^31 ms; a larger discontinuity
invalidates protection readiness and seals admissions conservatively.

| State | Admission and transition |
|---|---|
| BootGuard | No camera intent. Require qualified ACC-on for 2 s, usable supply and valid reserve-ready before admitting a new protected local session. ACC-off stays parked. |
| Running | Permit existing authorized controls/logging. ACC-off enters Suspect with original timestamp. Upstream fail immediately seals. |
| Suspect | ACC-on before 500 ms clears only if supply is healthy. At equality commit shutdown. ADC diagnostic undervoltage may qualify over 50 ms using measured enter/clear thresholds; upstream power-fail never waits for ADC confirmation. |
| Sealed/Draining | Latch cause/time, stop new user/producer admissions, revoke control and invoke existing stop once. Allow only bounded already-published ingress/final drain. Continue real worker service and supervision. Returning ACC cannot resume or replay this session. |
| Clean | Require all lifetime barriers plus no storage error or terminal loss. Publish best-effort report once, request qualified hardware OFF. No additional SD/NVS transaction required just to report shutdown. |
| TimedOut/Failed | At 5 s drain deadline or terminal IO failure, report best-effort accepted/written/flushed/dropped/rejected/lost and outstanding/uncertain rows. Retain live resources; never force-delete task, close its fd elsewhere, feed fictitious progress or mark clean. Hardware cut may lose data. |
| OffPending | Hardware cuts by its independent limit even if firmware stalls/resets. Successful drain may cut sooner. Re-arm only after qualified off/on cycle and fresh boot. |

Do not use the published `canRelease` flag alone as proof of clean media: check
IO error/loss separately. Accepted minus flushed is an uncertainty estimate,
not a count of proven missing rows; physical readback decides actual loss.
Serial output must not delay cut, and a final SD event may itself be lost.
Timeout evidence can come from the external capture; writing recovery metadata
at collapsing voltage must not become an additional blocking dependency.

Camera policy is explicitly **LeaveRecording** in v1. Revoke new serial/button
commands and cancel queued intents, but do not substitute a toggle, auto-STOP
or a reconnect attempt. A future camera-STOP policy would need its own admission,
bounded observed-confirmation deadline and independent local-drain priority;
ACK would still not prove recording stopped.

On boot record reset reason and available external power-event evidence; a
brownout reason does not prove which writes survived or why voltage fell. Reuse
existing safe-mode/reset policy without disabling brownout protection. Every
new session requires a fresh owner and successfully committed identity before
file creation/admission; collisions use exclusive creation and fail closed.
Both ledger slots must validate; a torn slot can block future allocation and
requires explicit recovery investigation. Never format, delete logs, recreate
ledger slots, change namespace or retry commissioning to obtain a passing run.
Reset, restored voltage, reconnection and stable ACC never replay shutter intent.

## Finite future implementation plan

These steps are planned, not executed by #57. Complete each software step with
focused native tests, formatting and the affected pinned target build/CI; reuse
passing evidence for unchanged modules. No new issue or firmware implementation
is needed to close this investigation.

| Step | Behavior and affected modules | Verification | Completion boundary |
|---|---|---|---|
| 1. Qualify electrical profile (#30) | Match V1.4 population/continuity, protected ACC/fail/ready inputs, load-switch and timer; extend documented board reservations in `hardware.md`/private bench profile. | Meter/continuity, worst-case component limits, startup levels, off-state injection, USB isolation and scope load captures. | Approved bench wiring/BOM and measured premise table, or explicit rejected candidate. No motorcycle readiness claim. |
| 2. Pure power policy | Add `include/power_shutdown.h`, `src/power_shutdown.cpp` and `test/test_power_shutdown`; fixed state/cause/deadline snapshots, debounce/hysteresis and single sealed transition. | Independent fake-clock cases: 499/500 ms bounce, equality, rollover, discontinuity, invalid samples, fail dominance, reserve-not-ready, duplicate events, return during drain and timeout. | Default-off coordinator has finite transitions and no IO/allocation. |
| 3. Qualified ESP32 inputs | Add `power_shutdown_esp32` backend; extend `BenchBusPins` and validation in `bench_application` for sense/ready/OFF, polarity and complete reservations. No persistence schema required. | Fake SDK tests for all conflicts/refusals, no calls when disabled, pull/ADC faults, and pinned `bench_application`/private compile builds. | Software-safe activation guards, still Experimental until step 1 measurements fit. |
| 4. Compose shutdown owner | Integrate `application_esp32`, `main`, `bench_console_esp32` and `local_telemetry_runtime` so automatic/manual requests share one seal/drain report and commands stay gated. Preserve `camera_event_session`/`telemetry_admission` final-publication ordering. | Real composition tests: unavailable owner, unbound allocation, in-flight final IMU/GPS/camera rows, blocked SD/BLE, close error, supervision stall and ACC returning. Assert no shutter IO, no use-after-free, truthful timeout/loss. | Drain orchestration remains nonblocking; only real barriers permit release. No SD or identity rewrite unless a demonstrated affected defect requires it. |
| 5. Recovery/status and compatibility | Expose cause, elapsed time, reserve validity, clean/uncertain outcome and reset reason in status; preserve `session_identity` and `session_storage_owner` refusal/exclusive-create contracts. | Reset during each ledger commit phase, corrupt/missing slots, filename collision, stale epochs and no intent replay; existing storage/session fixtures reused. | New software behavior passes focused tests/build/CI; physical support remains Not tested. |
| 6. Declared physical acceptance (#30/#31) | Install step-1 prototype, run boundaries below, record exact firmware/card/wiring/cap values and revise budgets only from measurements. | Existing finite campaign with power-event annotations, exported logs/ledger and rail traces. | Accept or reject that declared board/load/temperature configuration. No general durability guarantee or extra soak. |

## Declared physical boundaries and dependencies

Before running, #30 declares normal vehicle input range, negative/surge pulse
envelope, protection limits, free pins, cut timer, wiring and parked-current
budget. #31 declares exact card and filesystem, firmware, logging rates, enabled
modem/BLE/IMU loads, temperature endpoints, minimum reserve charge and rail
floors. None of those values is presently hardware-confirmed. Transient testing
requires suitable controlled equipment; unavailable equipment stays Blocked.

Use a finite bench matrix, three repeats per boundary at nominal temperature:

1. ACC bounce at 100/499 ms and stable off at 500 ms, then ACC restored during
   drain; startup ACC-held-off and on for 2 s. Verify one seal and no old-session
   reopening. Observe camera independently to confirm no controller STOP.
2. Upstream power removed/ramped through the selected fail threshold with reserve
   full and at its minimum ready level; run declared modem/BLE/SD/IMU peak load.
   Record signal-to-seal, drain-to-final-access, rail minima and cut timing.
3. Abrupt cut during row write, during fsync, during close/unmount and during
   each ledger slot update. Include cut after reported clean/final access.
   Correlate chosen boundaries with markers; do not infer them from a delay.
4. Controlled stalled-storage/timeout and firmware-reset cases exercise hardware
   deadline without freeing a live worker. Record clean versus forced-cut
   outcomes and next-boot reset/identity status.
5. Fully discharged/not-ready reserve, USB/off-state backfeed and parked draw.
   Repeat the worst measured load boundary once at each declared temperature
   endpoint; omit unsafe battery/thermal experimentation.

Before each destructive cut save original log/ledger checksums and host copies;
afterward inspect read-only first. Record lost/truncated rows, media IO and
filesystem damage, ledger validity, ID/collision refusal and camera state.
Accept hold-up only if measured usable reserve exceeds the complete declared
policy at its minimum charge/load/temperature premises, ordinary drain completes
before cut, forced cut remains truthful, and no intent is replayed. Failed cuts
remain failed; do not erase evidence or recommission. #31 may reuse its existing
EN-reset/storage evidence where configuration matches, but that is not a
substitute for these cut boundaries.

Unresolved: actual fitted supply transfer/minimum rails, load/current envelope,
cell/cap availability and safety, exact cut-switch/timer parts, free header
pins, motorcycle ACC/transient envelope and parked draw. Until these are
resolved by the existing owners, automatic power protection is **Deferred /
Not tested**. Manual shutdown remains useful software behavior, with the same
limits on SD durability.

## Software implementation — October 13, 2026

Implementation tasks are [#58](https://github.com/llazzaro/RideSync/issues/58),
[#59](https://github.com/llazzaro/RideSync/issues/59) and
[#75](https://github.com/llazzaro/RideSync/issues/75). This section describes the
implemented software route; the investigation and synthetic hardware sizing
above remain the decision record, not physical qualification.

`PowerShutdown` in `include/power_shutdown.h` / `src/power_shutdown.cpp` consumes
fixed copied `PowerObservation` signals and emits one-shot admission, seal,
drain, report and qualified OFF requests. Its typed state/cause/outcome snapshot
keeps lifetime completion separate from IO/loss. ACC-on and reserve readiness
must remain qualified through startup; emergency upstream fail bypasses the
ordinary debounce. A sealed episode never reopens. A late error revokes Clean;
a timeout remains uncertain even if a worker later finishes. Invalid timing
configurations are inert; manual shutdown with the valid default disabled
configuration still seals/drains without GPIO/OFF.

`PowerEventBackend` / `Esp32PowerBackend` in `power_shutdown_esp32` provide a
portable SDK seam and the real ESP32 GPIO route. `PowerBackendConfig` is
source/private commissioning only: enabled, electrical qualification, complete
reservations, supply-profile premise, independent cut-off qualification, distinct
ACC/fail/ready pins, evidenced external pulls/polarity, and separate OFF
qualification/startup safety. `gpio_get_level` has no SDK error channel; unexpected
nonbinary results are refused, but this does not detect a broken wire. The
qualified circuit and active polarity must make unavailable hardware detectable.
Optional ADC diagnostics are deferred and cannot be enabled in the composed
GPIO-only profile. GPIO35 and GPIO36 are refused as known documentary dividers;
GPIO34/39 are candidates only. No source profile supplies guessed default pins.

The held-rail supply premise must establish that upstream fail deasserted means
usable supply, and asserted fail with an independently valid Ready reserve means
usable backup. Without those explicit source facts, observations remain Unknown.
Backend initialization/OFF failures are terminal and retain their first SDK code;
reads invalidate availability rather than reuse stale readiness. OFF preloads
its qualified inactive level before driver enable; electrical boot behavior still
requires #30. Monitoring-only profiles may omit the OFF pin, but still require
independent timer qualification.

`BenchApplicationConfig` supplies separate `power_shutdown`, `power_backend` and
`power_cut_margin_ms` fields. Validation checks exact signal routing and all
active bus/button/LED/IMU interrupt reservations before IO. It refuses mismatched
policy/backend activation, unsupported ADC policy, incomplete electrical facts,
and an independent timer shorter than ACC debounce + drain policy + declared
cut margin. These numbers and the ready-signal premise must match measured
reserve energy in #30; software validation does not measure hold-up.

The application owner's `startupReady` gate runs before worker policy preparation
and supervisor admission. Configuration receipt freezes independently so a
legitimate two-second ignition wait cannot turn received settings into a timeout
or consume the one-second required-worker startup grace. Launch checks readiness
again before creating workers. Lost launch readiness is a terminal admission
refusal, not permission to retry automatically.

`SupervisedEsp32Application` consumes power before each producer pass, retires
control/pairing-reset admission and requests the existing local stop path once.
It continues real returned work and observes the whole application's release
barrier, including outstanding reset/proof ownership. Clean additionally excludes
storage errors, terminal loss and start faults. Timed-out owners remain alive.
Manual `SHUTDOWN` uses this same route even before telemetry exists. No camera
shutter STOP or automatic Start is added; no ledger repair, new persistence schema,
formatting, deletion or recommissioning is part of shutdown/recovery.

`power_shutdown_report` keeps one fixed latest serial snapshot with a cursor.
The bench console copies state/cause/outcome, qualification/readiness, explicit
reserve/supply validity, numeric storage error, elapsed
policy time, reset reason, IO/fault/OFF results and storage counters/uncertainty.
It writes bounded pieces only when the UART reports free capacity; a newer
snapshot can replace pending evidence. Reports are best-effort and cannot prove
power removal or durable SD data. Explicit STATUS includes power when telemetry
has not started. With automatic protection enabled or an episode sealed, STATUS
uses the bounded power snapshot only; the full legacy camera/provider details
remain available when automatic protection is disabled and unsealed. Shutdown
cannot depend on a completed serial report.

Physical power protection remains **Deferred / Not tested** pending #30 and #31.
Existing reset-reason reporting and exclusive committed session allocation are
reused; no reset cause is treated as proof of a lost row or a supply fault.

Software verification used an immutable snapshot of the staged power-only tree,
so concurrent setup/health/bookmark edits were excluded from delivery evidence.
The complete native suite passed **600 cases**, including 28 new policy cases.
The real application composition suite passed with 15 power scenarios plus the
unchanged camera/session/reset scenarios; the added HERO12 fixture reached
observed Recording, then verified no shutter STOP or post-seal Start/connect
replay through actual adapter teardown. These are synthetic SDK observations.
Backend/console/report/startup SDK tests, all four existing diagnostic bench
cases, Linux GCC/libstdc++ portability, Ruff and pinned formatting passed.
Repository Python tests passed after supplying the existing checksum-pinned GPX
schema cache prerequisite; unchanged passing checks were reused. Pinned builds
passed for `lilygo_t_a7670e_r2`, `bench_application` and
`bench_application_private_compile`; symbol inspection retained the actual power
policy and real ESP32 backend input/output methods. No firmware was uploaded.
Review corrections added GPIO36 refusal, preserved disabled-mode camera command
admission, and exposed independent reserve validity and numeric storage errors.
Physical evidence and compatibility remain unchanged.

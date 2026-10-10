# Finite integrated acceptance worksheet (#31)

**Unexecuted template. No hardware result is recorded here.** Until the exact
configuration, available equipment and measurement methods are declared, every
physical stage below is **Blocked / Not run**. Copy this worksheet into a dated
record under `docs/hardware-results/` when preparing the run; leave this template
unexecuted. Keep private identities, credentials, locations and filenames outside
Git and publish only reviewed evidence references.

This prepares the existing [#31](https://github.com/llazzaro/RideSync/issues/31)
campaign under the [finite acceptance policy](acceptance_policy.md). It adds no
campaign or completion gate. Use one declared 30-minute bench session as the
initial baseline, then the existing short controlled ride after bench and
installation checks pass. The baseline is not endurance certification.
[Camera checklist #46](https://github.com/llazzaro/RideSync/issues/46) schedules
the camera portion; #22 retains model/group compatibility and #30 installation.
Reuse matching observations instead of repeating successful component checks.

## Declare before execution

| Declaration | Value / evidence reference |
| --- | --- |
| Date, operator, bench session start/end and duration | Not declared |
| Exact firmware commit, build environment, pinned SDK and enabled features | Not declared |
| Board/revision, actual supply and reset/power-cut method; #30 installation record | Not declared |
| Card/model/filesystem, existing commissioning receipt, storage/flush configuration | Not declared; keep namespace and filenames private |
| Modem firmware, UART/power route, GNSS configuration and verified-barrier method | Not declared |
| Installed sensor/revision/bus, mount/axes, calibration and reference equipment | Not declared |
| Each camera model/firmware/mode/module, composed profile and security policy | Not declared; keep identities and keys private |
| Logging/capture instruments, clock correlation and timestamp uncertainty | Not declared |
| Matching existing evidence and any changed configuration that prevents reuse | Not reconciled |
| Enabled claims and excluded/disabled/unavailable features | Not declared |

Declare thresholds from the actual configuration and intended enabled claim
before the run. Software defaults and stack capacities are not measured margins.
If a required threshold or measurement method is unavailable, mark the affected
claim Blocked; do not invent a passing value after observing the result.

| Budget | Configured threshold / source | Measurement method / uncertainty |
| --- | --- | --- |
| Camera connection/command/observation deadlines; latency and skew | Not declared | Not declared |
| Control/service progress and required-worker watchdog deadlines | Not declared | Not declared |
| Heap minimum and per-worker stack margin | Not declared | Not declared; record units |
| Queue capacity, admission/loss reporting and SD flush policy | Not declared | Not declared |
| GNSS startup/READY, poll/stale/retry configuration | Not declared | Not declared |
| IMU interval/jitter/loss and enabled static/reference limits | Not declared | Not declared |

## Stage and result matrix

Before scheduling, replace readiness with **Ready**, **Blocked** (reason) or
**Not run** (declared omission). Results stay **Not run** until observed, then
become **Pass** or **Fail**, with evidence and the tested boundary. A blocked or
omitted stage never counts as passed. Record partial stages separately; an
unavailable later model does not erase an earlier result.

| Existing stage / owner | Readiness | Result | Evidence / limits |
| --- | --- | --- | --- |
| Declared 30-minute integrated baseline (#31) | Blocked: declarations pending | Not run | No observations |
| One camera loss/reconnect and one ESP32 reset while recording (#31, scheduled in #46) | Blocked: commissioned composition and observer pending | Not run | Camera display/media outcome, no automatic command replay; actual latency/heap/queue/stack/watchdog trace against declared budgets |
| Real-driver GNSS startup, READY and completed NoFix/Valid baseline (#31) | Blocked: qualified route pending | Not run | AT acceptance, GNSS power acceptance, READY and completed query are separate; indoor NoFix is valid evidence |
| One GNSS receive-loss/stale/recovery scenario (#31) | Blocked: fault/barrier method pending | Not run | Record stale/fault and control progress; claim recovery only after an actually verified physical/receive barrier |
| One missing/full-media condition and one blocked-storage/control-progress check (#31) | Blocked: preserved-media/fault method pending | Not run | Actual loss/error and independent control progress; no universal SDK return-time bound |
| Active supply-cut/readback at each attainable startup/write/after-flush boundary (#31) | Blocked: cut/readback method pending | Not run | Record each attainable boundary separately, supply sources removed, last observed counts, lost/truncated rows, filesystem damage and preserved files; unattainable boundaries stay explicit |
| Installed IMU stationary/known-axis, absent/read-failure and short concurrent SD/GNSS interval/loss trace (#31) | Blocked: fitted sensor/mount pending | Not run | Actual axes/ranges/timestamps, quality/loss and independent camera/GNSS/SD progress |
| Existing finite independent motion reference sequence (#31, pointer in #46) | Blocked: calibrated fixture/reference pending | Not run | Use the [reference plan](motion_estimator_plan.md#finite-independent-reference-plan-for-31); dynamic outputs remain invalid and turn observations test refusal, not achieved lean accuracy |
| Short controlled ride for enabled claims after bench and #30 installation pass (#31) | Blocked: prerequisite observations pending | Not run | Compare logs and independent references; list excluded/untested optional features |
| Final release-candidate CI and model/feature support publication (#31) | Blocked: release candidate pending | Not run | Record exact revision/CI link once; support remains Experimental/Not tested until matching physical evidence passes |

Use the same integrated trace for concurrent observations where practical.
Standalone diagnostics can supply a matching component observation, but cannot
establish integrated composition or load that they do not exercise.

## Available diagnostics and their boundaries

The [isolated real-driver GNSS bench](../tools/bench/gnss_driver/README.md)
provides one baseline and permanent RX-suppression attempt per boot, capped at
120 seconds. `R` discards received bytes; it is not modem silence. It has no
resume or verified-barrier recovery command and reports recovery Not tested.
Do not turn timeout, empty RX or CPU reset into barrier evidence. The recovery
portion stays Blocked until a declared verified method is available.

The [SD writer bench](../tools/bench/sd_logging/README.md) provides the existing
orderly `W` and 60-row `P` power-cut preparation modes. Queue admission and
periodic write/flush counts do not identify the exact electrical cut or prove
an in-flight SD transaction. EN reset is not loss of card supply. This diagnostic
does not exercise BLE/GNSS/IMU integrated load. Follow its existing preservation
and fresh-session readback procedure; never format, delete files, roll back the
ledger or recommission allocation to obtain a pass.

Use the [validation protocols](testing.md), [supervision contract](supervision.md)
and [raw IMU contract](raw_imu.md) for existing detail. Do not enable a placeholder,
unqualified peripheral or fault-injection path merely to fill this worksheet.

## Stop, reconcile and report

Declare the run's stop conditions and how the operator stops the commissioned
composition before starting. If a stage fails or its required observation cannot
be obtained, record the actual boundary and stop that affected stage; preserve
the data and mark dependent claims Blocked. Never silently retry a shutter or
replay pre-reset intent. A late cleanup or accepted write does not convert a
failed observation into a pass.

Record defects against the existing implementation owner. After a bounded
corrective change, rerun only failed/affected cases and explain why unchanged
evidence still matches. Longer soaks need a stated failure hypothesis or intended
ride-duration target; this worksheet creates no automatic eight-hour requirement.

Publish the filled matrix, configuration/budgets, timing uncertainty, observed
loss/corruption, exclusions, matching prior evidence and final CI reference.
Link the camera continuity rows to #46 and retain #31 acceptance ownership.
Passing software or a standalone diagnostic does not qualify physical accuracy,
general compatibility or the release. Keep unresolved support honest.

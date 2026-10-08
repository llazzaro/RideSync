# Isolated production GNSS driver bench

This opt-in diagnostic links the unchanged production `ArduinoModemUart`,
`ModemGnss`, `GpsManager`, GNSS parser and `SessionClock`. It enables observation
of that route without activating the production application, BLE, SD, cellular
setup or modem power/reset GPIO. It contains no new parser or AT bridge.

Software checks for issue #10 and physical results are separate. Real-driver
startup, stale/recovery and integrated control measurements belong to #31 under
[the acceptance policy](../../../docs/acceptance_policy.md). This bench has not
been physically run; compile and host checks do not establish hardware results.

## Build and checks

From the repository root:

```sh
.venv/bin/pio run -d tools/bench/gnss_driver
python3 -m unittest discover -s tools/bench/gnss_driver -p 'test_*.py' -v
python3 tools/bench/gnss_driver/inspect_link.py \
  --nm ~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-nm \
  --objdump ~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-objdump
.venv/bin/clang-format --dry-run --Werror tools/bench/gnss_driver/*.h tools/bench/gnss_driver/*.cpp
```

Packages match the root pins: espressif32 6.12.0, Arduino
3.20017.241212+sha.dcc1105b, Xtensa 8.4.0+2021r2-patch5, esptool
2.40900.250804 and scons 4.40801.0. `production.cpp` includes the real source
files; host tests compile those sources independently. No library dependencies
are added. The focused harness exercises lifecycle, actual NoFix completion,
stale/timeout behavior, RX suppression, control progress and bounded reporting.

The ELF audit requires the real driver route, explicit NVS refusal and absence
of BLE/SD/power GPIO/restart implementations. Arduino's NVS initialization
returns `ESP_ERR_NVS_NOT_INITIALIZED`, which does not select its format path.
All NVS opens fail explicitly; lifecycle/partition erase wrappers refuse
without calling real implementations. Arduino's linked OTA boot-verification
erase path also reaches refusal and returns before its partition write.
`HardwareSerial::flush` remains linked through its virtual table, but the bench
does not call it. The pinned SDK has a UART reconfiguration flush branch; this
diagnostic opens UART2 once and never reconfigures it. These are source/ELF
boundaries, not observations of flash or UART call latency.

## Controls and premises

Serial console is 115200 baud. Boot is idle; UART2 stays unopened. Commands are
case-sensitive and processed at most 16 input bytes per loop:

- `A`: open the actual production UART adapter at documentary TX26/RX27,
  115200 baud and admit one attempt per boot. Repeated A is rejected, including
  after startup failure. Observation cap is 120 seconds from admitted A.
- `X`: terminal cancellation, including before A. It closes application UART
  admission and cancels the real manager. A fresh boot is needed to try again.
- `R`: after a completed healthy production NoFix or Valid exchange, permanently
  suppress RX delivery for the rest of this attempt. At most 64 real incoming
  bytes are discarded per loop and counted independently. Repeated/early R is
  rejected. This injects a receive-path fault; it does not simulate modem silence.

No supply12, PWRKEY4, reset5 or DTR25 operations exist. External modem power and
startup must be arranged independently before a physical trial. The first real
AT response is required to observe command readiness. `power=nullptr` and
`QualifiedPowerTiming.qualified=true` admit only the no-power software route;
zero pulse/settle fields do not assert electrical or startup timing qualification.
The bench routing flags are an experimental opt-in to the documentary pin/baud
candidate, not a measured qualification record.

The existing driver's documentary profile and ordered-terminal premise are
explicit experimental opt-ins: exclusive ordered responses, one unambiguous
terminal per fully transmitted command, no duplicate terminal or asynchronous
GNSS query response. This configuration cannot demonstrate those premises.
Defaults remain 10 s command timeout, 15 s READY timeout, 1 s poll, 3 s stale,
64 RX/16 TX bytes per production tick and three attempts per exchange stage.
These are software policies. Actual AT success, GNSS power acceptance, receiver
READY and successful NoFix/Valid query completion remain distinct observations.
Warm GNSS may omit another READY; the resulting timeout stays a failure.

There is no recovery/resume command, automatic reset, synthetic READY/terminal
or `restartAfterVerifiedBarrier` call. Empty RX, a timeout or discarded bytes
cannot establish a physical/receive barrier. Suppression destroys terminal
evidence; recovery is reported Not tested. The production driver's existing
terminal-retirement behavior remains unchanged in the unsuppressed baseline.

## Measurements and limits

Summaries contain enums/statuses, monotonic age, power stage/acceptance and READY,
service/heartbeat/byte counts, first observed power/READY/baseline times relative
to A, suppression time, maxima and dropped-summary/rejected-command counts.
Unobserved first times are `-1`. `baseline=1` requires a completed healthy
NoFix/Valid exchange; `exchanges` counts newly observed successful data receipt
timestamps. `services` counts returned production manager services, not replies
or fixes. Timeout/cap never becomes a success label. Initial production
`state=Startup` describes its configured state; `active=0` and unopened UART
mean no command has been submitted.

Enum values follow the production headers:

| Field | Values in numeric order from zero |
| --- | --- |
| state | Disabled, Startup, Power, WaitReady, Poll, Desynchronized, Failed |
| health | Disabled, Healthy, Timeout, Overflow, ProtocolError, Exhausted, InvalidClock |
| validity | Missing, Valid, NoFix, Invalid, Stale |
| power_stage | Disabled, KeyActive, Settling, Complete, InvalidClock, Cancelled |
| stop | None, Cancelled, Cap, OpenFailed |

Every bench loop increments an independent heartbeat outside `gps.tick()`, even
while idle or terminal. `tick_max_us` brackets that production call;
`work_max_us` brackets bench service including optional discard. `gap_max_us`
measures successive heartbeat entries; `idle_max_us` measures service exit to
the next entry, including console work and scheduling. Maxima cover the boot,
including idle time. The declared 20 ms gap target is an observation target,
not a scheduler/SDK latency guarantee or issue acceptance substitution.

Console output uses a single fixed 768-byte pending summary and sends at most
64 bytes per loop after checking free TX capacity. A blocked console drops new
summaries and retains a drop count; transitions can be lost. Summaries are
requested on observed state changes, commands and a one-second interval.
No coordinates, UTC, modem identity, raw lines or private payload are printed.
The production parser can retain a fix internally without publishing it here.

The cap is enforced before each subsequent production/discard service. X stops
new application UART access once consumed; already accepted UART bytes or
commands and autonomous modem activity can remain. Static owners and buffers
remain alive; no UART teardown, flush or receive-barrier inference is performed.
SDK calls may take time despite bounded application work, and control cannot
progress inside a stalled synchronous UART call. Actual durations remain
measurements for #31; no universal return-time bound is asserted.

Any separately authorized upload follows the existing backup/data-preservation
procedure and removes the SD card with board power disconnected. This source
task performs no upload or hardware operation. A physical record can report
baseline/suppression pass, failure or not run without changing the finite
software delivery boundary or claiming recovery was tested.

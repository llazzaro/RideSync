# Physical SD writer bench

This isolated sketch exercises the existing `ArduinoSdStorage` worker, session
allocator, `SessionClock`, and `Storage` on the LILYGO T-A7670E V1.4
ESP32-WROVER-E board. It links only the selected production storage/clock/health
sources; it neither changes nor activates the production application.

Use only the independently reserved namespace and already commissioned FAT32
card whose read-only probe passed. Preserve all existing user files. The sketch
never formats, deletes, lists user filenames, provisions a ledger, or resets a
ledger. The production allocator advances the existing ledger and opens a new
CSV exclusively. Never rerun commissioning to make a failed test pass.

GPIO12 enables the peripheral supply. SPI uses SCK14, MISO2, MOSI15, CS13 at
1 MHz, matching the [read-only probe](../sd_readonly/README.md). Modem
PWRKEY/reset, GNSS polling, BLE, IMU, NVS access, and production startup are
absent. The adapter's opt-in, wiring/card, exclusive-volume, and namespace flags
are controlled bench acknowledgments. A bench-only linker wrapper bypasses the
Arduino core's automatic NVS initialization and its error-triggered formatting
path; this sketch has no NVS consumers. The adapter acknowledgments
do **not** establish production
qualification or electrical/power-loss safety. No other task may use this SD
volume during the run; preexisting user files can remain on it.

## Build and upload

1. Preserve a verified backup of the installed ESP32 firmware. Disconnect all
   board power and remove the SD card before uploading; GPIO2 is a boot strap.
   The existing user files and the current ledger must remain intact.
2. In a private shell, set `RIDESYNC_BENCH_NAMESPACE` from the external namespace
   reservation/commissioning receipt and `RIDESYNC_BENCH_WRITE_OPT_IN=1`. The
   namespace must be a nonzero uint32 (decimal or `0x` form). Do not put the
   namespace, receipt, device identifiers, or user filenames in tracked files,
   shared logs, or screenshots. `build_guard.py` reads these values only at build
   time. Missing/invalid values refuse the build. Avoid verbose builds, which
   can expose compiler definitions.
3. From the repository root, build:

   ```sh
   .venv/bin/pio run -d tools/bench/sd_logging
   ```

   Pinned dependencies match the read-only bench. A dummy namespace may be used
   for **compile-only** verification; never upload that artifact. For hardware,
   rebuild with the actual reserved value using the command above and keep those
   same environment values for upload. The firmware embeds that value; resetting
   the board does not load another namespace from the host.
4. With the card still removed, upload to the freshly enumerated board port:

   ```sh
   .venv/bin/pio run -d tools/bench/sd_logging -t upload --upload-port <enumerated-port>
   ```

   This replaces the ESP32 application only. Follow the existing
   [connection/bootloader procedure](../../../docs/device_connection.md).
5. Disconnect **all** board power. Safely eject the host-mounted SD card, insert
   it in the unpowered board, and reconnect power without holding BOT/IO0.
6. Open serial at 115200 baud using the same connection procedure. Wait for
   `SD_LOGGING: bench only` or a periodic `WAIT_W` report, then send uppercase
   `W` once for the normal four-row run, or `P` once for the minute-long
   power-cut preparation mode described below. Serial monitoring may
   reset the ESP32, but no writes start until a new `W` or `P` is received.

## Normal run and evidence

`W` starts a single allocation attempt per boot. After allocation is committed,
control constructs its clock and storage with that ID and binds exactly once.
It admits four rows roughly one second apart using actual clock snapshots.
A delayed control pass admits one current row, with no invented catch-up times.
Every row has provenance `bench_missing_gnss`, Missing GNSS validity, Disabled
modem/UART, and no UTC estimate or coordinates. These are diagnostics, not ride
measurements.

Capture the serial transcript privately. Allocation prints only its status,
low 32-bit counter, and error; it does not print the namespace or CSV path.
`ROW accepted=N monotonic_ms=T` means queue admission only. Periodic copied
health contains `gap_ms`, observed `max_gap_ms`, accepted/dropped/rejected/written/
flushed/lost (`a/d/r/w/f/l`), storage progress (`p`), terminal/stopped (`t/s`),
worker progress/outcome, IO error, and final lifetime-barrier state. Serial output
has a bounded message size and requires available TX space; periodic reports
may be skipped under backpressure. Loop gaps are observations, not a latency
qualification or an SDK IO deadline.

Only `SD_LOGGING: DONE` is success for a completed normal run: all four rows were accepted, written and
flushed; no drop/rejection/loss/terminal condition or SDK IO/close error remains;
Storage stopped; and `owner.workerFinished()` confirms cleanup returned. The
sketch requests graceful stop after the fourth admission. It never reports DONE
from `terminal`, `stopped`, or a flush counter alone.

Allocation and the first actual write have a 15-second startup deadline; the
whole run has a 30-second deadline, both measured from `W`. Refusal, error, or
timeout emits `SD_LOGGING: FAILED`, stops admission, requests stop and cancels
the owner. `worker_finished=0` means cleanup has not returned. All owner, clock,
queue, and storage objects remain alive for the entire boot even if SDK IO stays
blocked. A later `cleanup_finished` does not convert FAILED into DONE. No retry
or second session is admitted until reset; repeated `W` and `P` are ignored.

After DONE, disconnect all board power before removing the SD card. On the host,
verify the newly created CSV with the existing
[log-format/ledger procedure](../../../docs/log_format.md), compare its metadata,
row count, real monotonic timestamps, missing-fix fields and session with the
private receipt and serial counter, and verify existing files remain intact.
CSV row counters are observational snapshots; use the post-close serial totals
for the final run counters. Keep private filenames/session values out of public
reports.

## Reset recovery trial

After preserving the normal-run evidence, safely eject the card, insert it with
all board power disconnected, reconnect power, and send `W`. For an interrupted
trial, reset the board after a chosen `ROW accepted=N` and **before** DONE; record
that reset boundary. Do not remove a powered SD card. After the next boot, send
`W` again. A fresh owner must allocate a new session, never resume/reuse the
interrupted CSV. Let that new session reach DONE, then remove all power and
inspect both sessions and the ledger on the host. The interrupted file can have
a partial final row or fewer durable rows than were accepted. Counter gaps are
allowed when an allocated session never wrote a file. Do not erase, replace,
rollback, or recommission the ledger between these boots.

## Active-write power-cut preparation (`P`)

`P` selects a fixed 60-row run with a 1000 ms row period, using the same
production allocator, sink, worker, clock and Missing GNSS samples as `W`.
The first actual write still has a 15000 ms deadline from the command; the
overall deadline is 75000 ms, including allocation and cleanup. Admission
normally lasts 60 seconds after binding, leaving about 59 seconds after the
first row to disconnect power. Delayed passes admit only one current row;
they can shorten that window or cause the overall deadline to fail the run.
`START requested` emits the chosen mode, row count, period and both limits;
periodic health also identifies the chosen mode. `WAIT_W` remains the idle
state name and accepts either command. Neither command runs automatically.
`W` and `P` share the single attempt per boot, including refused/failed starts.

1. Preserve normal `W` run evidence first. With all board power disconnected,
   prepare the card and a way to remove **every** power source promptly:
   USB/serial power, battery, external supply, and any powered connection
   that can backfeed the board. Leave the card inserted throughout the trial.
2. Reconnect power, capture serial privately, and send uppercase `P` once.
   Confirm `mode=P`. Wait for a periodic `RUNNING` report with `w` greater
   than zero, and record the accepted/written/flushed counts and elapsed
   boundary. `ROW accepted` alone does not establish an actual SD write.
3. While still RUNNING and well before row 60/STOPPING/DONE, disconnect all
   prepared power sources. Do not request graceful stop or press EN, and do
   not remove the card while powered. Record how power was removed and the
   last observed serial boundary; a serial count does not identify the exact
   electrical cut or prove an SD transaction was in flight at that instant.
4. After the board is fully unpowered, reconnect and send `W` for a recovery
   run. It must allocate a fresh session; let it reach DONE. Remove all power
   before card removal, then inspect the interrupted/recovery CSVs, ledger,
   and preserved user files using the same private evidence procedure above.
   Do not erase, reset, roll back, replace or recommission the ledger.

If left powered, `P` requests graceful stop after row 60 and applies the same
complete-counter, error and worker-lifetime checks before DONE. That verifies
a longer orderly run, not a power-cut trial. EN reset restarts the ESP32 but
does not establish loss of the SD supply. Disconnecting USB alone likewise
does not establish supply loss if another source remains. A full supply cut
during this active logging window is a distinct trial; its recovery/durability
result requires physical execution and host inspection. The tooling and host
tests do not establish electrical discharge timing, an in-flight write cut,
or physical power-loss qualification.

Refusal, startup timeout, total timeout or storage error follows the existing
FAILED/cancellation path. Owner, clock, queue and storage retain boot lifetime
even if SDK cleanup blocks. A late cleanup cannot turn FAILED into DONE.

A normal run demonstrates the physical production write/flush/close path on
this particular card. An observed reset and host verification provide evidence
for that recovery boundary. They do not prove arbitrary power-loss durability,
all reset windows, maximum write latency, a full-rate production workload,
modem/GNSS/BLE/IMU integration, or completion of issue #11's wider acceptance.
A compile or host test alone proves none of the physical results.

## Host checks

```sh
python3 -m unittest discover -s tools/bench/sd_logging -p 'test_*.py' -v
```

This harness uses real `Storage` and `SessionClock` with test-only memory IO and
controlled worker completion. It checks one-shot admission, first-write and
cleanup timeouts, late allocation, refused startup, IO failure, real timestamps,
four-row counters, and the worker lifetime barrier. It never accesses an SD
card, commissioning receipt, or ESP32.
It also checks `P`'s 60-row graceful completion, real timestamps, mutual W/P
exclusion, first-write and total cleanup deadlines, and error cancellation.

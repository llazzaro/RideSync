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

Acceptance for milestone 1: repeatable single-X5 start/stop with observed state,
bounded connect/command timeouts and honest serial status. It is not met yet.

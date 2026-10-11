# Project-health remediation, October 11, 2026

The owner requested the latest project-health installation, a repository scan,
and parallel remediation of every finding. The initial scan used project-health
0.16.0 and Graphify 0.9.74 against RideSync commit
`cdf4e839595f2afda5de6cfe3f76a9181223dcf6`, reporting 426 findings.

The final default-threshold scan reports zero findings. All 426 original
dispositions and the 37 first-pass helper candidates are now fixed in
[the machine-readable record](2026-10-11-project-health.json). The first pass
removed 330 candidates and reviewed 96; the subsequent pass also refactored
those retained candidates and newly exposed functions. No reviewed warning remains. No project-health policy acceptance, threshold
increase or analyzer exclusion was added.

## Analysis coverage

The standard CLI still exits 2 with `insufficient evidence`: its C++ parser
does not support `__has_include` in the benchmark and X5 commissioning files,
or `RTC_NOINIT_ATTR` declarations in `src/main.cpp`. These three valid constructs
remain intact. Equivalent typed default arguments, a conventional weak-attribute
position and an equivalent explicit count assignment removed the other 15
syntax failures.

The remaining parse failures initially hid additional complex startup,
commissioning, qualification and BLE-store/reset functions. Those functions
were inspected and refactored too. A separate diagnostic pass substitutes only
the unsupported macro syntax in memory, retaining both conditional branches.
It reports zero function, duplication or class candidates and zero parser
errors across all 235 C++ source/header files at the same thresholds. Source
digests and diagnostic limitations are recorded in the JSON; this diagnostic
does not replace the standard CLI report or establish API/ABI compatibility.

These are source dispositions, not an automated baseline-resolution claim.
No coverage or mutation artifact was supplied; those capabilities remain
unknown. Compilation and focused behavior tests verify affected source and
configurations separately.

## Changes

BLE connection/discovery, security, store inspection, inventory conservation
and resource retirement now have separate phases. Configuration decoding,
persistence, application/reset admission, startup/watchdog and wake/control
progression have named validation and lifecycle helpers. Camera adapters
separate transaction, response, timeout and recovery responsibilities;
qualification and identical transport mechanics are shared.

Storage, telemetry, GNSS/IMU and estimator processing use coherent validation
and worker phases. Benchmark observations and published run status are separate
from transport/worker lifecycles. Test fixtures share transport scheduling and
CSV mechanics while retaining independent packets, CRCs, numerical expected
values, assertions and fault scenarios.

The first pass reproduced and corrected an X5 summary-buffer defect: a pointer
parameter made `sizeof(line)` supply pointer capacity to `snprintf`. An array
reference preserves the 768-byte capacity. Its regression failed on the old
signature and passes with complete summary and counter checks.

The replay test harness also reproduced a bidirectional pipe stall on local
Python 3.11 and 3.14. It now supplies the same encoded input through a temporary
file; executable arguments, output handling and all numerical assertions remain
unchanged. All six replay tests pass.

## Verification

The final frozen pre-commit pipeline passed all 83 Python tests, 572 native
cases, benchmark tests, Linux GCC/libstdc++ portability, Ruff and pinned
clang-format checks. All 13 pinned firmware build environments passed against
the final source. GPS resource and benchmark link-boundary audits passed.
Focused final-tail tests cover commissioning, actual startup/watchdog deadlines,
benchmark admission, BLE store/reset faults, foreign-membership conservation,
callback lifetimes and SDK sequencing. Independent reviews checked production,
tools and fixtures; a skipped failure-path SDK getter was corrected before
delivery and its host suite rerun.

The primary board build and compiled configuration stack audit pass. The audit
includes the extracted service frames, retaining its fixed 1024-byte helper
reserve, 4096-byte application budget and 8192-byte SDK/RTOS reserve. The worst
configuration path plus helper reserve uses 3088 bytes.

The verified code commit is `19def7e`. CI is checked after pushing the
verification record to `origin/main`.

No firmware was flashed, and physical compatibility/support claims are unchanged.

## Reproduce the standard scan

```sh
project-health check . --format console --json-output /tmp/ridesync-health.json
```

Use the unchanged default complexity 10 and cognitive complexity 15 thresholds.
For baseline investigation, add the pinned starting commit with `--baseline`
and retain the reported comparison limitations. Full reports, diagnostic source
digests, focused logs and review evidence are stored outside the source tree in
`~/.codex/reports/RideSync/2026-10-11-project-health/`.

# Project-health remediation, October 11, 2026

The owner requested the latest project-health installation, a repository scan,
and parallel remediation of its findings. The scan used project-health 0.16.0
from upstream commit `6ae745c45fb98279706b4a775a0ea737e67cc660` and Graphify
0.9.74. The starting RideSync commit was
`cdf4e839595f2afda5de6cfe3f76a9181223dcf6`.

Every original finding has a disposition in
[the machine-readable record](2026-10-11-project-health.json). Of 426 original
candidates, 330 no longer appear after responsibility-based refactoring and
96 remain individually reviewed. The final snapshot has 133 visible warnings:
118 cyclomatic complexity, nine cognitive complexity and six god-node
candidates. This includes 37 reviewed helper candidates introduced by the
extractions. All seven original duplication groups are removed, and the final
scan reports no duplication groups.

These are independently checked source dispositions, not an automated
baseline-resolution claim. Both scans have the same 18 C++ syntax-analysis
failures, and the comparison therefore remains incomplete. The snapshot exits
2 with `insufficient evidence`. Native compilation and the pinned ESP32 builds
validate the affected source separately. No coverage or mutation artifact was
provided, so those capabilities remain unknown.

## Changes and retained candidates

BLE connection/discovery, security and resource retirement now have separate
phases. Configuration decoding, persistence, application/reset admission and
wake/control progression have named validation and lifecycle helpers. Camera
adapters separate transaction, response, timeout and recovery responsibilities;
qualification, BE80 profile declarations and central service progression are
shared where the contracts are identical. Storage, telemetry admission,
GNSS/IMU and estimator processing have coherent validation and worker phases.

Replay preparation, strict CSV validation, CAD generation and diagnostic benches
also have smaller responsibilities. Test helpers share transport scheduling and
GATT mechanics while retaining independent model packets, CRCs and numerical
expected values. Independent scenarios and fault matrices retain all assertions.

One behavioral defect was reproduced and corrected: the X5 probe summary passed
a character buffer as a pointer, so `sizeof(line)` supplied pointer capacity to
`snprintf`. An array-reference parameter now preserves its 768-byte capacity.
The regression test failed on the previous signature and passes on the change,
checking the complete summary and kind counters.

Retained warnings are explicit finite protocol/state dispatch, fail-closed
admission predicates, independent test case matrices, and classes that own one
bounded resource lifetime. Their reasons are recorded per finding. Warnings
remain visible: there is no policy acceptance file, threshold increase, source
exclusion or altered hardware-support claim.

## Verification

- All 80 repository Python tests using CI's Python 3.11, an initial 567-case
  native run, 140 focused native tests after the final helper changes, and the
  frozen pre-commit suite of 572 native cases. The increase comes from splitting
  existing independent scenarios into separately registered tests.
- SD, X5 and GNSS bench policy tests, including the new X5 summary regression.
- Pinned clang-format 18 check, Ruff 0.15.0 and `git diff --check`.
- All 13 repository ESP32 build environments and all four isolated diagnostic
  builds; retained adapter/forwarding symbols and diagnostic link boundaries.
- GPS encoder instruction/resource audit and corrected configuration stack
  audit. The stack audit counts extracted nested frames and retains the fixed
  1024-byte helper reserve and 4096-byte application budget.
  The worst configuration path plus reserve uses 3088 bytes.
- CAD geometry equivalence: both directional solid differences have volume
  below `1e-9` for all six generated solids with the pinned CAD dependencies.
- Independent review of production, tools, fixture sharing and final test
  refactors; its stack-accounting finding was corrected and rechecked.

The local Python 3.14 runtime stalled in a bidirectional subprocess pipe during
the final replay suite. The complete suite was rerun using CI's Python 3.11.
This does not change the replay executable or waive any test.

No firmware was flashed and no physical qualification was performed.

## Reproduce the scan

```sh
project-health check . --format console --json-output /tmp/ridesync-health.json
```

Use the same default complexity 10 and cognitive complexity 15 thresholds.
For a baseline investigation, add the pinned starting commit with `--baseline`;
retain the reported comparison limitations. Full local scan output, original
IDs, domain reports, review evidence and build/test logs are stored outside the
repository in the owner's Codex reports directory.

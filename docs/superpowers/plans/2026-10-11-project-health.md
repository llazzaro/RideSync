# Project-health remediation implementation plan

> **For agentic workers:** Use the debugging and verification skills for each independent domain. The owner explicitly authorized subagents and fixes on main.

**Goal:** Resolve all actionable findings from the 2026-10-11 project-health 0.16.0 snapshot and document the disposition of every reported warning.

**Architecture:** Preserve firmware behavior and public interfaces. Extract coherent validation and state-transition helpers where complexity impedes review. Keep finite protocol dispatch, independent test oracles and unsupported analyzer evidence explicit rather than increasing thresholds or excluding source.

**Tech stack:** C++17, pinned PlatformIO/ESP32 toolchain, Python unittest, clang-format 18, project-health 0.16.0 and Graphify 0.9.74.

**Spec:** Owner request in this chat; original report saved outside the tree at `/Users/leonardo/.codex/reports/RideSync/2026-10-11-project-health/report.json`.

## Constraints

Work on main, preserve unrelated changes, and commit/push verified changes to origin/main. Follow docs/acceptance_policy.md. No physical support claims, default activation changes, threshold increases, or scan exclusions. Pure refactors reuse existing behavior tests; behavioral fixes require failing regression evidence.

## Tasks

- [x] Camera subagent: profile adapters, protocol codecs, GPS encoding and duplicate profile declarations; record each finding disposition.
- [x] BLE/control subagent: central connection progression, application/configuration, wake/control/session identity; record each finding disposition.
- [x] Telemetry subagent: storage, GNSS/IMU, admission and estimators; record each finding disposition.
- [x] Coordinator: replay preparation, strict test parser, bench/tool findings and independent fixtures; review analyzer parsing failures without treating them as compiler errors.
- [x] Review integrated diff, format C++, run repository Python tests, native tests and relevant pinned builds. Rerun health with original thresholds and retain errors/unsupported evidence.
- [x] Record exact fixed and reviewed counts with local validation in `docs/health/2026-10-11-project-health.md` and its per-ID JSON record.

Delivery follows the established direct-to-main workflow. The final handoff
records the resulting commit and remote CI status; these cannot be embedded in
the commit that contains this plan.

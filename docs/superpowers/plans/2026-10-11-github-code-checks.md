# GitHub code checks improvement plan

> **For agentic workers:** Use superpowers:subagent-driven-development to implement these bounded tasks. The user requested subagents. Work directly on main under AGENTS.md.

**Goal:** Improve CI reliability, automated code analysis, dependency maintenance and repository secret protection while preserving the existing firmware verification coverage.

**Architecture:** Keep the current CI checks job and add focused linting. Add a separate, SHA-pinned CodeQL workflow. Configure Dependabot and enable repository security features through GitHub's API; verify each setting after changing it.

**Tech stack:** GitHub Actions, actionlint 1.7.12, Ruff 0.15.0, CodeQL, Dependabot, existing pinned PlatformIO/clang-format toolchain.

**Spec:** The October 11 GitHub checks audit in this chat, the user's request to plan and launch subagents, AGENTS.md and docs/acceptance_policy.md. This document is the bounded design for review before implementation.

## Global constraints

- Work on main; no branches, worktrees, manual pull requests or force pushes.
- Preserve unrelated changes and every existing test, firmware build and resource/link audit.
- Do not add required PR checks or change the direct-to-main delivery workflow.
- No firmware dependency upgrades, automatic merges, broad style cleanup or hardware acceptance claims.
- Reuse passing evidence for unchanged firmware. Verify new workflows on the final pushed revision.
- Agents own separate files; only the coordinator commits, pushes and changes repository settings.

## Review focus

1. A push and a PR for the same change must not cancel each other's CI runs.
2. Conditional ESP32 code and external SDK headers limit CodeQL C++ inference; analysis does not replace target builds.
3. SCons injects Import and env; narrowly annotate those references rather than disable undefined-name checks globally.
4. Dependabot's update proposals must not silently replace intentionally pinned firmware dependencies.
5. Verify API settings after writes; report unsupported or permission-denied changes without claiming success.

## Task 1: Workflow reliability and validation

Owner: CI agent. Modify .github/workflows/ci.yml only.

- [x] Add `timeout-minutes: 30` to checks. Current successful runs take approximately 8–11 minutes.
- [x] Add workflow concurrency with group `${{ github.workflow }}-${{ github.event_name }}-${{ github.ref }}` and `cancel-in-progress: true`.
- [x] Set checkout `persist-credentials: false`.
- [x] Immediately after checkout, download actionlint 1.7.12 into RUNNER_TEMP, verify Linux amd64 archive SHA256 `8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8`, extract and run it against all workflows. Use a five-minute step timeout and curl failure handling.
- [x] Run pinned actionlint locally; review all existing steps for preservation and inspect concurrency groups for same-event/same-ref cancellation and cross-event isolation.

## Task 2: Focused Python checks and security analysis

Owner: analysis agent. Create ruff.toml and .github/workflows/codeql.yml; modify requirements-dev.txt, the CI lint step, and only necessary SCons annotations. Coordinate CI step insertion with the coordinator to avoid competing edits.

- [x] Pin `ruff==0.15.0` in requirements-dev.txt.
- [x] Configure `[lint]` with `select = ["E9", "F63", "F7", "F82"]`; run `ruff check scripts test tools mechanical` in CI.
- [x] Add inline F821 noqa comments explaining SCons injection on Import/env references in scripts/patch_nimble_store.py and tools/bench/sd_logging/build_guard.py. The read-only lint trial found five findings on four lines, all involving those injected names.
- [x] Add CodeQL for actions, c-cpp and python, on push to main, pull_request, and weekly schedule. Use minimal contents/actions read and security-events write permissions; use SHA-pinned checkout and CodeQL init/analyze at `24c54180a607b1449ed407dd24f251e4e9147c8d` (v4.38.3).
- [x] Use C++ build-mode none initially. Document that inferred macros/includes and unavailable SDK headers reduce precision. Native-only manual builds exclude main and conditional ESP32 backends, so they are not a replacement for firmware analysis.
- [x] Run Ruff and actionlint; confirm the workflow has unique analysis categories for each language and bounded timeout/concurrency.
- [x] Verify GitHub produces successful analyses for all three languages, with no missing-language claims. Treat any reported findings as evidence to triage, not automatic permission for unrelated firmware fixes.

## Task 3: Dependency and repository security

Owner: security agent prepares .github/dependabot.yml. Coordinator applies GitHub settings.

- [x] Configure weekly github-actions updates at root and pip updates for root development tools and mechanical/tracer900_2018 CAD requirements, with small open-PR limits. Avoid duplicate scanning of nested requirements.
- [x] Keep PlatformIO firmware packages and Git-pinned libraries unchanged. Dependabot may propose reviewable maintenance PRs; do not enable automatic merging. Preserve deliberate PlatformIO Core upgrades through version-update ignore rules, while allowing security fixes where supported.
- [x] Enable vulnerability alerts before enabling Dependabot security updates.
- [x] Enable repository secret scanning and repository push protection. Personal-account push protection is distinct and is not evidence these settings are enabled.
- [x] Enable repository Actions SHA enforcement while preserving existing `enabled: true` and `allowed_actions: all` policy values.
- [x] Read each setting back through gh api. If an API requires additional permissions or the service rejects a setting, report the exact limitation and complete other authorized changes.

## Task 4: Review, verification and delivery

Owner: coordinator and an independent review subagent.

- [x] Review the combined diff and API changes against all constraints; ensure no unrelated firmware edits or coverage removal.
- [x] Run pinned Ruff, actionlint and `git diff --check`; run focused tests for any executable Python changes. Comments alone do not require firmware rebuilds.
- [x] Commit only scoped verified changes and push directly to origin/main.
- [x] Verify CI and CodeQL results for the exact committed SHA. Do not call a cancelled or older run final evidence.
- [x] Record final settings and CI evidence in a concise repository document; commit/push documentation separately if needed, reusing unchanged source evidence under the acceptance policy.

## Planning evidence

- Current source revision: 9d265796da09c103edf3f21d12098f4f9b6a10a4; CI run 38129641562 passed.
- GitHub confirms main is unprotected, no rulesets, read-only default workflow token, admin repository access, CodeQL default setup not configured.
- Vulnerability alerts, security updates, repository secret scanning and push protection are disabled; SHA enforcement is false.
- Initial planning: three read-only subagents investigated CI reliability, analysis coverage and repository security before implementation and settings writes.

## Execution checkpoint

- [x] Launch the three planning subagents and inspect the existing configuration.
- [x] Produce this concrete bounded design and implementation plan.
- [x] User approved the design; implementation subagents launched with separate file ownership.

## Execution evidence

- Task 1 implemented and independently reviewed: all preexisting CI commands retained; event/ref-isolated concurrency, timeout, credential hardening and checksum-verified workflow lint added.
- Task 2 implemented: Ruff 0.15.0 and actionlint 1.7.12 pass locally; SCons changes are inline lint annotations only. CodeQL has distinct actions, c-cpp and python categories with documented C++ inference limits.
- Task 3 implemented: Dependabot configuration parses, with weekly scans and deliberate PlatformIO version-update exclusions. GitHub read-back confirms vulnerability alerts, security updates, repository secret scanning/push protection and Action SHA enforcement are enabled.
- Independent final scoped review accepted without actionable findings. Exact-revision CI run 38137308848 and CodeQL run 38137308849 passed; all three CodeQL uploads match cdf4e839595f2afda5de6cfe3f76a9181223dcf6.
- Unrelated concurrent firmware edits are excluded from this delivery. Ruling: run focused CI configuration checks locally and the complete committed revision on GitHub; the blanket local pre-commit pipeline tests unrelated in-progress firmware and is unsuitable evidence for this change. This follows the acceptance policy's reuse of unchanged firmware evidence.

Task 4 complete: independent reviews accepted; committed source CI and all three CodeQL categories passed; settings read-back verified. Final evidence is recorded in [docs/github_checks.md](../../github_checks.md). Documentation-only delivery reuses this passing unchanged-source evidence.

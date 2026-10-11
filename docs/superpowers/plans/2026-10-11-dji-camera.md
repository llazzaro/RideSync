# DJI camera implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development for the focused implementation and review tasks. Work directly on main under AGENTS.md.

**Goal:** Implement #47's initial DJI pairing, explicit recording and observed-status adapter in the existing composed bench application.

**Architecture:** An original bounded pure codec describes the pinned DJI R SDK wire format. One DJI adapter owns per-peer protocol state on the existing BleCentral and dispatches four documented model profiles. MixedCameraAdapter retains one CameraManager/RecordingManager/audit owner; configuration persistence and event parsing recognize appended stable model values.

**Tech stack:** Existing C++11 native tests, pinned ESP32 Arduino/NimBLE build and bounded host/callback ownership contracts. No replacement stack or new dependency.

**Spec:** https://github.com/llazzaro/RideSync/issues/47 and docs/acceptance_policy.md. The user's request authorizes implementing that recorded scope using the demo.

## Global constraints

- Source baseline: dji-sdk/Osmo-GPS-Controller-Demo commit 92fe23e5a749f189593f980a26a105c3bb66aa1c. Author bounded code independently from the published wire layout; preserve provenance and the distinction between source terms and this repository's license.
- Initial feature scope: identity qualification, protocol authentication, explicit Start/Stop, status subscription/resynchronization, reconnect, mixed control and event logging. Optional GPS/wake remain explicitly unsupported in this initial adapter.
- Preserve existing numeric model/family values, persisted schema and valid old configurations. Append DJI model values; reject unsupported settings rather than silently accepting them.
- Preserve kMaxCameras in source configuration and the existing four-peer central/runtime admission limit. No automatic increase in camera-group or radio capacity.
- No shutter on boot/authentication/reconnect; no inferred recording from ATT completion or ACK. Invalid, stale, ambiguous or disconnected observations are Unknown.
- No automatic first-pair approval: the camera's allow/reject result must be received and authenticated. Actual identity/controller identity/challenge/firmware facts belong to private commissioning.
- No branches/worktrees/PRs. One verified change on main; commit/push, passing CI, software delivery in #47 and physical execution in #46/#22.

## Review focus

1. A camera denies pairing or sends an unrelated/late grant: no ready state or shutter admission.
2. Write completion, ACK and status arrive out of order or after cancellation: do not complete the wrong token or replay intent.
3. Photo/pre-recording/QS/sleep/unsupported mode status: never report video recording from an ambiguous state.
4. Notifications fragment/coalesce, contain bad CRC or exceed capacity: bounded parser/service; malformed input fails honestly.
5. Appended models cross config/session boundaries: old saved values remain stable, new logs parse, unsupported GPS/wake and excess live peer slots refuse clearly.

## Tasks

### 1. Pure codec and protocol fixtures

Owner: dji_codec. Files: include/protocol/dji_codec.h, src/protocol/dji_codec.cpp, test/test_dji_codec, test/fixtures/dji.

- [x] Read pinned framing/command sources; publish a small codec API shared with adapter owner.
- [x] Write and run failing fixtures for official example frames, independent CRC/framing oracle, pairing, explicit commands and status ambiguity.
- [x] Implement fixed buffers, exact length/CRC/encryption/version checks and bounded stream reassembly.
- [x] Verify fragmented/coalesced/oversized/unknown input and source-vs-document discrepancies; record provenance.
- [x] Review against the published protocol and fix material findings.

### 2. Shared BLE DJI adapter

Owner: dji_adapter. Files: include/profiles/dji_camera.h, src/profiles/dji_camera.cpp, test/test_dji_adapter.

- [x] Read the official pairing flow and BLE service/characteristic properties; publish qualification/API contract.
- [x] Write failing behavioral tests for user allow/deny, initial ACK/grant ordering, wrong model/sequence, expiry, explicit REC/STOP, source-observed status, cancellation/reconnect and no replay.
- [x] Implement finite per-peer protocol phases, shared sequence ownership, command/status correlation and normal callback retirement barriers.
- [x] Test stale/ambiguous mode/status and honest Unknown outcomes; format and review.

### 3. Model/configuration/runtime/log integration

Owner: dji_integration. Files: camera_profile/config headers, config validation/storage, mixed_camera, storage model admission, disk parser and affected focused tests.

- [x] Write failing stable-model persistence/validation and mixed-routing/event tests.
- [x] Append explicit DJI model/family values and dispatch qualified profile facts into one shared DJI adapter.
- [x] Wire every mixed service/stop/readiness/fault/recovery path without a second manager/group tick; retain normal reconnect and unsupported optional capabilities.
- [x] Extend numeric log model validation/parser support; verify old schema/old bytes and meaningful new roundtrips.
- [x] Review cross-module ownership and runtime limits.

### 4. Commissioning guide, build evidence and delivery

Owner: root. Files: docs/dji_camera_profile.md, README.md, docs/bench_test_version.md, platformio.ini, CI and this progress ledger.

- [x] Explain per-model experimental support and real private commissioning; pin protocol/source references and licensing provenance.
- [x] Add retained DJI adapter compile target on the pinned existing ESP32 host and include it in CI. Check composed activation branch memory, stack/frame limits and callback retention.
- [ ] Independent focused reviews, then required native/Python/format/affected firmware checks; run PlatformIO serially to avoid shared build cleanup races.
- [ ] Commit/push verified work directly on main and verify CI for the exact revision.
- [ ] Reconcile #47 software checklist; leave selected actual model/firmware and physical observations in #46/#22. Close #47 only after software acceptance passes.

## Progress ledger

- Starting revision: fa37ddd. Working tree clean; preserve unrelated mechanical commits.
- Design scope is the initial adapter specified by #47. Optional physical model clarification is pending; source-backed software can proceed.

- Codec: six focused Unity groups pass, including independent published vectors and ASan/UBSan hostile receive checks. Recording narrowed after review to modes 1, 0x38 and 0x41.
- Adapter: fifteen focused groups pass under ASan/UBSan. Independent review caught direct-grant pairing and photo-mode STOP admission; failing regressions preceded both fixes.
- Integration: config 6, settings 16, mixed 3 and telemetry disk 8 checks pass. Old four-model schema2 bytes remain identical in a baseline comparison.
- Shared transport: 22 native groups pass. Actual ESP32 SDK harness passes 32 scenarios, including no-response local completion, SDK rejection and unreleased full-queue barrier.
- Static composed DRAM initially overflowed by 10208 bytes. Checked internal-RAM boot owner fixes linking; failed allocation refuses before peripheral IO. Three actual-factory host checks pass.
- Retained DJI build: static RAM 72416 bytes, flash 890205 bytes. Private-provider compile: static RAM 65520, flash 900197. Largest reported DJI function frame 768 bytes; this is compiler evidence, not measured runtime stack/heap headroom.
- Unrelated mechanical/documentation commits advanced main during this task; preserve them and commit only issue47 changes.

# Bookmark software delivery — October 13, 2026

This records synthetic software evidence for #60–#64. No camera, button or SD
power-cut observation was performed. The later
[observed REAPER import](2026-10-13-reaper-import/README.md) completes #63's
editor criterion; the implementation verification below is retained. Physical support remains
Experimental / Not tested. The source-backed timecode investigations #50–#52
were rechecked without finding a usable BLE route; their existing decisions and
open status remain unchanged.

## Delivered behavior

The committed common telemetry contract provides bounded, camera-independent
BookmarkV6 admission, copied owner timestamps and session-local IDs. Combined
CameraHealthV7 logging retains that bookmark contract. The serial `MARK` command
and explicit double-press mapping use this owner without requiring cameras or
UTC. Defaults and camera-action eligibility remain unchanged. RAM admission,
write/flush counters and optional camera wire acknowledgements remain separate.

The offline exporter validates complete V6/V7 records, publishes exclusively,
preserves equal-time markers and requires explicit clip alignment for editor
positions. Optional HERO12 HiLights require local admission, explicit runtime
and per-peer opt-in, fresh Encoding and a qualified connection. Other models
return Unsupported. There is no automatic shutter, retry or reconnect replay.

See [local usage](../bookmarks.md), [export usage](../bookmark_export.md),
[HiLight protocol and limitations](../hero12_hilight.md) and
[schema/host memory measurements](../log_format.md#bookmarkv6-local-marker-extension).

## Review corrections

Three independent subagents checked the pending implementation against the
ticket contracts. Review found and corrected three defects:

- Complete metadata followed by the first/only truncated data row now reports
  an excluded power-cut tail, rather than rejecting it as incomplete metadata.
- A HiLight delayed by keepalive rechecks Encoding and Busy before dispatch;
  an intervening stopped-state update refuses the marker.
- Shutdown resets pending input once. A first press from an old camera-free
  session cannot combine with one press in a new session using the same control
  object; a fresh double still creates one marker.

Each correction has a regression. The two firmware regressions failed before
their fixes and passed afterward. Independent review found no remaining
blocking software findings.

## Verification

A standalone validation snapshot used baseline
`0c47de932836a416db99234b503693c5b002b976` plus the 36 focused delivery files,
excluding unrelated working-tree changes. Existing committed schema/core work
and its evidence were reused.

| Check | Observed software result |
| --- | --- |
| Linux GCC/libstdc++ portability and Ruff | Passed |
| Python discovery | 201 tests passed; the 14 GPX writer tests passed separately after copying the existing ignored pinned development schema cache into the snapshot |
| SD, X5 peripheral and GNSS bench runners | All three passed (4 tests total) |
| `pio test -e native` | 699 cases passed |
| Pinned clang-format check | Passed |
| `pio run -e hero12_adapter_compile -e bench_application -e bench_application_private_compile` | All three pinned ESP32 targets passed |
| Focused exporter suite | 30 tests passed, including bounded-memory, loss/hash, publication collision and chapter-mapping checks |
| Local Markdown references | 70 file references resolved before this evidence note was added |
| `graphify update .` | Completed without API use; no code-graph topology change detected |

The graph parser reported partial extraction of preprocessor-heavy `src/main.cpp`
at line 35; the pinned compiler builds above passed. Graph output is navigation
evidence, not a substitute for source or compiler verification.

The independent mapping oracle remains: marker 12,500 ms, clip zero 10,000 ms,
project start 30 s gives clip 2.5 s and project 32.5 s. This is synthetic
arithmetic. The host payload/queue measurements in the schema guide are ABI
measurements, not device heap or durability observations.

## Remaining acceptance

#63's REAPER import row is now **Observed / Pass** in the
[later pinned-editor record](2026-10-13-reaper-import/README.md). REAPER 7.82 on
macOS 27.0 imported three synthetic markers at 30, 32.5 and 34.5 seconds while
preserving existing M23 at 5.25 seconds. A repeated import added no duplicates.
Saved projects, API readbacks, screenshot and hashes supersede the earlier
unavailable-editor blocker. All software/editor criteria have evidence.

#30 retains installed input wiring. #31 retains the finite enabled-input timing,
camera-loss, reset/no-replay and SD observations. #46/#22 retain qualified HERO12
wire and saved-clip HiLight checks. Their new affected rows are recorded as
Blocked / Not run in the [existing release worksheet](../release_bench_checklist.md).
No additional endurance campaign or other-model rerun is introduced.

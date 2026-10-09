# GitHub issue review — 2026-10-07

## Remaining work — 2026-10-09

GitHub currently has 12 open issues. This reconciliation reuses the retained
evidence; it does not close a ticket or add acceptance requirements. The
[finite acceptance policy](acceptance_policy.md) remains authoritative.

| Issues | Current evidence and next dependency |
|---|---|
| #3, #19 | Isolated X5 CE80 pairing/subscription and one source-derived shutter submission are recorded. Recording was not established because the owner reported no camera SD card; firmware and annotated recording-state packets are missing. Resume the existing three-cycle/reconnect milestone with a camera card and recorded firmware. |
| #9 | The isolated source-derived advertisement produced one owner-confirmed X5 wake. Production bounded advertising/connect/observe/start orchestration is still absent; reuse this observation rather than repeating it without a new question. |
| #5 | ONE RS needs its own lens/module and recording-path evidence. The newly audited [licensed BE80 reference](insta360_protocol.md#licensed-be80-control-reference-19-5) supplies source-level command candidates for ONE R; it does not qualify ONE RS configurations or authoritative state. |
| #21 | GO 3S/Action Pod control ownership and usable third-party recording path remain unresolved after #6. No enabled profile can be justified yet. |
| #13 | The qualified static estimator and approved [runtime/MotionV4 logging](motion_logging.md#implemented-static-motion-logging-v4) integration are implemented and tested synthetically, with exact ABI/stack-frame evidence. Default activation remains disabled. Dynamic lean/gravity-free acceleration and physical accuracy remain unresolved and invalid. |
| #29, #14 | The licensed source-backed [pure BE80 encoder](gps_protocol.md#implemented-pure-encoder-29) is implemented with independent packets, strict UTC/freshness/numeric errors and retained-target evidence. The [approved plan](superpowers/plans/2026-10-09-insta360-gps-encoder.md) is executing its final CI/review gates. No camera capability is enabled. #14 still requires a real forwarding profile and #22 owns stored metadata qualification. |
| #22 | The staged physical camera matrix remains open. Limited X5 probe results do not establish recording, production orchestration, other models or mixed-radio capacity. |
| #30 | Actual supply/enclosure/mount selection and finite installed-hardware observations are still required. |
| #31 | The declared integrated bench/controlled-ride campaign requires qualified enabled hardware and actual observations; synthetic software tests cannot complete it. |
| #16 | Roadmap stays open while required milestones above remain incomplete. |

Evidence: [X5 pairing and shutter trial](hardware-results/2026-10-09-x5-pairing.md),
[X5 wake](hardware-results/2026-10-09-x5-wake.md),
[implemented static motion integration](motion_logging.md#implemented-static-motion-logging-v4), and its approved
[motion runtime/logging spec](superpowers/specs/2026-10-09-motion-logging-design.md).

## Original review and subsequent scope changes

Reviewed all original #1–#16. Preserved issue identities, narrowed compound scopes,
created 15 split tickets and updated all 30 work packages plus the #16 tracker.

| Original | Reviewed scope | Split into additional issues |
|---|---|---|
| [1](https://github.com/llazzaro/RideSync/issues/1) | Qualify T-A7670E R2 board, modem and peripheral pin map | [17](https://github.com/llazzaro/RideSync/issues/17) |
| [2](https://github.com/llazzaro/RideSync/issues/2) | Define camera contracts and source configuration with native tests | [18](https://github.com/llazzaro/RideSync/issues/18) |
| [3](https://github.com/llazzaro/RideSync/issues/3) | Integrate and bench-verify single Insta360 X5 recording control | [19](https://github.com/llazzaro/RideSync/issues/19) |
| [4](https://github.com/llazzaro/RideSync/issues/4) | Integrate and bench-verify GoPro HERO12 Black recording control | [20](https://github.com/llazzaro/RideSync/issues/20) |
| [5](https://github.com/llazzaro/RideSync/issues/5) | Add and bench-verify the Insta360 ONE RS camera profile | Kept focused; group proof moved to #22 |
| [6](https://github.com/llazzaro/RideSync/issues/6) | Investigate GO 3S and Action Pod BLE control feasibility | [21](https://github.com/llazzaro/RideSync/issues/21) |
| [7](https://github.com/llazzaro/RideSync/issues/7) | Implement vendor-neutral group recording policy and partial results | [22](https://github.com/llazzaro/RideSync/issues/22) |
| [8](https://github.com/llazzaro/RideSync/issues/8) | Implement configurable debounced handlebar button events | [23](https://github.com/llazzaro/RideSync/issues/23) |
| [9](https://github.com/llazzaro/RideSync/issues/9) | Implement bounded Insta360 wake scheduling and group reconnect | [24](https://github.com/llazzaro/RideSync/issues/24) |
| [10](https://github.com/llazzaro/RideSync/issues/10) | Implement bounded A7670E AT transport and GNSS acquisition | [25](https://github.com/llazzaro/RideSync/issues/25) |
| [11](https://github.com/llazzaro/RideSync/issues/11) | Implement bounded GPS logging to microSD | [26](https://github.com/llazzaro/RideSync/issues/26) |
| [12](https://github.com/llazzaro/RideSync/issues/12) | Acquire raw external IMU samples; depends on #34 admission/schema | [27](https://github.com/llazzaro/RideSync/issues/27), [34](https://github.com/llazzaro/RideSync/issues/34) |
| [13](https://github.com/llazzaro/RideSync/issues/13) | Implement validated linear acceleration and lean/pitch estimates | [28](https://github.com/llazzaro/RideSync/issues/28) |
| [14](https://github.com/llazzaro/RideSync/issues/14) | Integrate optional Insta360 GPS forwarding per verified profile | [29](https://github.com/llazzaro/RideSync/issues/29) |
| [15](https://github.com/llazzaro/RideSync/issues/15) | Implement watchdog health supervision and reset recovery | [30](https://github.com/llazzaro/RideSync/issues/30), [31](https://github.com/llazzaro/RideSync/issues/31) |
| [16](https://github.com/llazzaro/RideSync/issues/16) | Roadmap tracker only | Updated to all 30 work packages |

Changes applied throughout: explicit outcome/non-goals, prerequisite links,
behavior/fault acceptance checks, bounded resources/deadlines, protocol/license
provenance, public-log redaction and hardware evidence required for support.
Removed camera dependencies from pure contracts/group policy; split discovery
from code, codecs from integration, clock from filesystem, sensor choice from
acquisition, estimator feasibility from production, and physical/release testing
from watchdog code. Optional GPS injection no longer blocks the core release.

The review does not claim production readiness; completion remains a measured
software/hardware result. See the [current plan](superpowers/plans/2026-10-07-ridesync.md).

2026-10-08 follow-up: [#34](https://github.com/llazzaro/RideSync/issues/34) extracts
the bounded mixed telemetry schema/admission prerequisite after #12's pinned
driver/ownership audit. #34 depends on #11/#26, coordinates metadata with #27,
and supplies the separate transport inbox consumed by one clock/Storage owner.
#12 now also depends on #34 and retains initialization/acquisition/FIFO decoding,
concurrent SD/GNSS interval/jitter and stationary/known-axis bench evidence.
This adds a software work package (31 total), not a duplicate sensor ticket.
Original #11/#12/#31 physical acceptance stays open; the #16 tracker remains
sole roadmap tracker. See [mixed telemetry contract](mixed_telemetry.md).

2026-10-08 follow-up: [#35](https://github.com/llazzaro/RideSync/issues/35) extracts
the missing shared central BLE backend from #4. Pinned-source audit identified
indefinite synchronous GATT wrapper waits and callback lifetime/bond-eviction
risks; the new package uses actual asynchronous host APIs with fixed contexts,
retirement/quarantine and #18/#33 admission guards. #4 now depends on #35 and
retains complete HERO12 setup, explicit recording/state and twenty-cycle bench
acceptance. #17 retains live roles/routing/capacity evidence; no original camera
or hardware scope was narrowed. There are now 32 planned work packages, plus
separately tracked maintenance fixes #32/#33.

2026-10-08 follow-up: [#36](https://github.com/llazzaro/RideSync/issues/36) extracts
pure HERO12 pairing/control setup codecs from #4, separately from transport #35
and adapter lifecycle. Official published model-specific data supports HERO12
classic queries; newer-model capability/two-byte operations are outside its scope.
Full initial pairing remains in #4, with no paired-only fallback or invented
camera evidence. At that stage the plan had 33 work packages plus maintenance #32/#33.

2026-10-08 follow-up: [#37](https://github.com/llazzaro/RideSync/issues/37) extracts
camera-event records/admission and actual ACK/observation hooks from #22.
The current logger has no camera row, and group completion is not evidence of
camera wire acknowledgment. #37 preserves source/domain distinctions and
single-producer storage ownership; #22 now depends on it and retains the full
mixed-brand capacity, timing and disconnect/logging continuity bench matrix.
This adds the 34th planned work package, plus maintenance #32/#33. No physical
acceptance or unsupported camera protocol is replaced with synthetic success.

2026-10-08 runtime audit follow-up: #38–#42 add five focused packages for the
original unattended application workflow: configuration handoff, durable session
identities, independent local telemetry, handlebar control/status, and actual
worker supervision. The final #37 API audit prevents duplicate camera dispatch
and storage producers; #40 resolves the SD worker/session ownership seam and
camera-free logging. #41 retires delayed REC before STOP. #39 retains durable
namespace/power-loss requirements. There are now 39 planned packages plus
maintenance #32/#33. Source-only contracts do not close physical gates; #22
and #31 retain their complete mixed-brand/soak/ride acceptance.

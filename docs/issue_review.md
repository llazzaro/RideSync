# GitHub issue review — 2026-10-07

## Remaining work — 2026-10-10

The owner requested an initial version to test, software-ticket completion with
real-hardware checks collected in #46, and explicit comments on blockers. Open
acceptance tickets are not a prerequisite for starting the initial test.
The [initial X5 test version](initial_test_version.md) gives the build and finite
execution order. The [acceptance policy](acceptance_policy.md) preserves physical
qualification separately from software delivery.

| Issue | Disposition and next action |
|---|---|
| #5 | Experimental ONE RS explicit StartVideo/Stop software is implemented in `1ccac93` with passing [CI](https://github.com/llazzaro/RideSync/actions/runs/38064718809). Close software scope after reconciling its mixed documentation/hardware criterion at the owner's request. Actual ONE RS recording, identity/bond commissioning and the ordinary-360 assembly remain unconfirmed in #22/#46. Recording state remains Unknown. |
| #22 | Physical staged camera matrix remains open. Run the software-ready single-X5 composed row first; unavailable later models/groups do not block it. Reuse the three observed probe cycles/reconnect, but do not claim composed or mixed-camera qualification. |
| #30 | Blocked for physical installation: actual supply/enclosure/mount, wiring and finite inspection observations are absent. This is required before a motorcycle ride, not before the isolated USB bench X5 test. |
| #31 | Integrated release/ride acceptance remains open. Declare the existing 30-minute bench configuration, budgets and fault methods before execution; run the controlled ride after bench and #30 installation acceptance. It does not gate the first X5 test. |
| #46 | Single execution checklist already collects camera checks and pointers to existing #30/#31 GNSS/SD/IMU/reference checks. Next is private X5 store/identity commissioning followed by one composed serial REC/STOP/status check. No duplicate hardware campaign is needed. |
| #16 | Roadmap remains open for physical compatibility, installation and release milestones. All software components have delivery decisions; initial testing proceeds while acceptance tickets remain open. |

The initial source baseline `ea21b13` has passing
[full pinned CI](https://github.com/llazzaro/RideSync/actions/runs/38067609621).
X5 support remains Experimental, other models remain physically Not tested, and
MotionV4 dynamic estimates remain invalid. The offline dynamic replay is an
Unreliable comparison prototype, not measured lean accuracy. Software closures
and a buildable test candidate do not claim a ride-ready system.

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

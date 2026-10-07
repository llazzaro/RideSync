# GitHub issue review — 2026-10-07

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
| [12](https://github.com/llazzaro/RideSync/issues/12) | Acquire and log raw external IMU motion samples | [27](https://github.com/llazzaro/RideSync/issues/27) |
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

# Finite acceptance and delivery policy

Revised October 8, 2026 following the owner's request to make ticket acceptance
realistic. This changes ticket boundaries, not the overall camera/logger goal or
the truth of any hardware claim. No new issues are needed for this review.

## What closes a ticket

Software tickets close after their stated implementation, focused behavior/error
tests, relevant pinned build, formatting and CI pass. Reuse verified evidence
for unchanged code. Documentation-only changes need documentation checks, not a
firmware rebuild. Never close an unimplemented feature or a disabled placeholder
as working support.

Investigations close with a concrete decision or a source-backed unresolved or
infeasible finding. A finding does not complete a dependent implementation.

Physical compatibility and release tickets require actual recorded observations.
Code completion leaves model/feature support Experimental or Not tested until
those observations pass. Preserve source/documentary, synthetic/schema-derived,
observed and confirmed evidence labels. Official schemas can specify expected
codec fixtures; do not require hardware captures of every documented message.
Unknown reverse-engineered fields still require evidence before enabling them.

Each checklist is finite. Newly discovered defects go to the existing owner;
additional features or test campaigns need a scope decision. Do not automatically
split more tickets, repeat all suites after each review, or restart a completed
investigation because a later hardware test remains pending.

## First milestone and progression

The immediate milestone is one X5 connected to the ESP32, serial REC/STOP and
honest observed status. #3 requires three camera-observed REC/STOP cycles and one
reconnect. This is a pragmatic smoke check, not statistical reliability proof.
Pairing or an accepted write alone does not establish recording.

Then progress to X5 + ONE RS, three Insta360 cameras, and the mixed four-target
group including HERO12. #22 owns that staged physical matrix and records each
stage independently. A blocked later model does not erase an earlier milestone;
it also does not silently pass the full matrix.

GNSS/SD and raw IMU code can progress independently, but must not turn the first
camera milestone into an all-peripherals prerequisite. Wake, dynamic motion
estimates and camera GPS forwarding remain later capabilities. Keeping optional
features disabled does not complete their implementation tickets.

## One owner for each physical check

| Owner | Checks retained here |
|---|---|
| #3 | First X5: firmware, three REC/STOP cycles, one reconnect, actual camera status |
| #22 | New-model smoke checks, staged mixed groups, missing-peer progress, latency/skew, radio coexistence, enabled wake and camera GPS metadata |
| #30 | Actual supply/enclosure/mount, pin/pull/polarity/current checks, antenna access, strain relief and finite physical inspection methods |
| #31 | Integrated reset/no replay, watchdog/control progress, real-driver GNSS stale/recovery, SD faults/power cuts, sensor faults/load and controlled-ride reference checks |

#31 starts with one declared 30-minute bench campaign as an initial baseline,
not an endurance certification. Its checklist fixes the scenarios before the run.
Longer soaks need a stated failure hypothesis or intended ride-duration target;
there is no automatic eight-hour requirement. Reuse observations where board,
firmware and configuration match. Rerun failed/affected cases after a corrective
change rather than expanding every test campaign.

SD queue/memory/control admission must be bounded and a stalled SDK worker must
remain isolated. No universal SDK return-time or power-loss durability guarantee
is possible from these APIs. Record flush policy, exact tested cut boundaries,
observed lost/truncated rows and filesystem damage. Never auto-format, delete
existing data or recommission session allocation to make a test pass.

Dynamic lean/acceleration accuracy still requires an independent reference.
Static tilt cannot certify cornering lean. Where the available sensor/aiding is
insufficient, mark output invalid/unreliable and resolve the estimator scope
explicitly. Do not manufacture plausible angles or achieved error bounds.

## Review disposition of every open issue

These are delivery boundaries, not claims that the issues are now complete.
Changed unchecked criteria need evidence reconciliation, not automatic rework.

| Issues | Definition of done |
|---|---|
| #1 | Fitted-board evidence, vendor/manual routing candidates, startup observations and explicit unknowns; installation qualification in #30 |
| #3 | Real single-X5 working path and finite smoke test |
| #4, #5, #21 | Implemented per-model adapter and error/no-replay tests; physical model compatibility in #22 |
| #8, #23 | Tested gesture/status logic and opt-in conflict-checked GPIO backend; installed wiring in #30 |
| #9, #24 | Implemented finite evidenced wake/recovery path; supported power states and physical observations in #22 |
| #10 | Production AT/GNSS route, finite retries/cancel/barrier tests and documented premises; real-driver measurements in #31 |
| #11 | Production logging, bounded admission/loss reporting and documented existing card evidence; active power/fault qualification in #31 |
| #12 | Real raw sensor route, units/timestamps/fault tests and explicit activation guards; physical axes/load in #31 |
| #13 | Feasible experimental estimator and independent expected fixtures; measured accuracy in #31 |
| #14 | Implemented default-off forwarding for an evidenced encoder/profile; stored metadata confirmation in #22 |
| #15 | Wired supervisor/reset contracts, independent worker admission and tests; physical efficacy in #31 |
| #16 | Roadmap remains open while required project milestones remain incomplete |
| #17 | Provisional pinned role/stack decision with concrete evidence and unresolved integration checks; no all-camera prerequisite |
| #18 | Settings persistence and explicit targeted/config reset software, reusing #43/#44; installed-device checks in #22/#31 |
| #19 | Evidenced recording fields and honest Unknown semantics; no exhaustive reverse engineering prerequisite or invented state |
| #20 | Pure codec against pinned official examples/schemas, independent expected fixtures and framing/error tests; installed HERO12 qualification in #22 |
| #22 | Full staged camera matrix resolved, with pass/fail/blocked/not-run evidence |
| #27 | Bounded provisional sensor/driver comparison, selection, axes and acquisition plan; fitted hardware qualified later |
| #28 | Feasible estimator/reference plan or substantive infeasibility finding, explicitly resolving #13 scope |
| #29 | Real source-backed GPS encoder for known fields, boundary tests and licensed provenance; no disabled stub closure |
| #30 | One concrete documented physical installation and declared finite inspections |
| #31 | Declared integrated bench/controlled-ride campaign for enabled claims, final CI and honest support matrix |

The GitHub checklists and implementation plan are synchronized with these
boundaries. Existing completed work remains evidence; ticket closure and
hardware support are separate assertions.

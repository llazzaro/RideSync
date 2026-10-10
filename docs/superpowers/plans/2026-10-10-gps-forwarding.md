# GPS forwarding implementation plan (#14)

> Execution: inline on main, as requested by the owner. Use test-driven
> implementation and verification-before-completion; no worktree or PR.

Goal: complete the source-backed, default-off BE80 GPS forwarding software.
Camera metadata qualification remains in #46/#22.

Design: a fixed-size forwarding owner consumes copied GNSS snapshots and uses
the real #29 encoder and existing BleCentral writes. The licensed Garmin video
wire profile sends 71 bytes in 20/20/20/11-byte fragments. It supports an
explicit experimental ONE RS source-profile selection without claiming observed
compatibility or providing recording state. The existing BLE owner supplies
the established BE80 connection, shared command sequence and result routing.

The command owner computes control intent and supplies its current mask to
forwarding before control submissions; a per-peer central lease protects the
fragmented stream against any producer with incorrect ordering. Forwarding never queues control or ticks BleCentral itself.
Pending telemetry is dropped for control, unavailable links or invalid data.
A partial packet interrupted by control, stale data, error or deadline seals
that peer; no partial byte stream is reused and reconnect needs a new generation.
Other peers remain independently serviceable. ATT completion is not camera
acceptance or metadata storage. Actual SDK latency remains an existing transport
qualification, not a new unbounded application wait.

Files: new include/gps_forwarding.h, src/gps_forwarding.cpp and
test/test_gps_forwarding/test_main.cpp; optional snapshot-consumer binding in
gps_manager.h/.cpp; documentation in gps_protocol.md, testing.md, sources.md,
README.md and existing #46 row. Retain the concrete forwarding symbol in the
existing compile image for target linkage verification.

- [x] Write behavioral tests and observe RED before implementing.
- [x] Implement the bounded forwarding path and optional GNSS producer binding.
- [x] Cover literal packet delivery, coalescing, rate/freshness/MTU, control
  preemption, deadlines, session changes, callback generations and peer isolation.
- [x] Verify the real central/host path and GNSS producer binding, including
  default-off operation and no replay after uncertain delivery.
- [x] Document source/coordinate limits and software versus physical evidence.
- [x] Run focused tests, pinned target build, format and required commit hook.
- [ ] Commit/push main, await CI, record device verification in #46, reconcile
  #14 acceptance and close only when its software criteria pass.

Review rulings: reserve the central command stream across fragments; verify the
actual discovered BE80/BE81 endpoint; defer BLE retirement out of GNSS producer
callbacks. Regressions failed before each correction and passed afterward.

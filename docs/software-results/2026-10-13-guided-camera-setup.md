# Guided camera setup software delivery — October 13, 2026

Implements #76–#80 and delivers the finite feasibility decision for #81.
The [USB wizard](../setup_wizard.md) operates through the explicit
`serial_setup_application` image. Normal images retain their existing console;
setup reads reviewed private declarations without initializing buses or launching
camera/telemetry workers. No upload, camera operation or physical test was run.

## Delivered behavior

- Bounded RS1 session protocol, deliberate parked entry, isolation acknowledgement,
  deadlines, copied whole-camera drafts and replay rejection.
- Single-owner correlated saves with verified durability, explicit errors,
  retained status, read-only recovery and quiescent explicit restart.
- Host wizard with private preview/input, explicit addition/selection/save,
  legacy eight-slot inspection and a four-slot edit limit.
- Bounded qualified central discovery: sixteen copied candidates, ten-second scan,
  typed observations and actual callback release. Unresolved identities remain
  unsaved; X5 camera-initiated CE80 commissioning stays separate.
- Read-only per-model prerequisite reports. Private firmware declarations do not
  become observed firmware; store/runtime readiness remains independently owned.

The optional [Wi-Fi trial](../setup_wifi_feasibility.md) is **infeasible within
its predeclared pinned resource budgets** and remains disabled. A matched immutable
source comparison measured +497900 bytes of flash against a 393216-byte budget,
and +27336 static RAM bytes against a 16384-byte budget. Both link builds passed.
Runtime heap, certificate trust/browser authentication and hardware operation are
unverified; this is not working browser setup support.

## Verification

A controlled source snapshot contained only this delivery on top of `cc992fa`;
other active features in the shared checkout were excluded. It is a verification
fixture, not a branch or worktree. Verification used the installed pinned tools:

- Complete Python suite: 166 tests passed. Two subsequent host regressions were
  added and verified in the final 19-test host suite; actual host/C++ wire
  interoperability and real startup/storage/restart fixtures passed.
- Complete native suite: 595 cases passed. Final selection changes passed the
  affected eighteen service/discovery cases without rerunning unchanged suites.
- Default ESP32, setup-only and always-declined private setup builds passed.
  The final two setup builds passed again after the last firmware change.
- Seven injected Wi-Fi policy tests passed; their inputs model future verified
  authentication decisions and do not certify cryptography or a browser server.
- Ruff, C++ formatting, Linux header portability and local Markdown checks passed.
  Ruff was explicitly run without Git ignore filtering in the ignored verification
  fixture. Graphify received an AST-only incremental update.

Independent review caught and resolved unsent-save expiry, retained-result
recovery, invalid name encoding, legacy report bounds, stale owner-pause
acknowledgements, asynchronous UART reply ordering and host draft synchronization.
A regression also preserves the whole-draft unresolved-identity restriction after
selecting a second verified candidate.

Software evidence is synthetic/build evidence, not camera compatibility or
physical power-interruption certification. #46/#22 retain camera observations,
#30 installation qualification and #31 integrated reset/power/storage acceptance.
Saved preferences never supply bond proof, pin qualification or SD provisioning.

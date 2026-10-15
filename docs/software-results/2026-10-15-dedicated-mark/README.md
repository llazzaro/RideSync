# Dedicated Highlight / Mark input — software evidence

October 15, 2026. Synthetic host/SDK evidence and compile checks only; no
installed GPIO, switch, SD-card durability or camera-native highlight claim.

Implemented an optional, commissioning-only second GPIO on the existing
handlebar/telemetry owner. A debounced press emits one physical/short bookmark
through the existing V6/V7 logger. Held inputs do not repeat, and boot/reset
requires a debounced release before arming. Recording/wake gestures and the
shared-button double bookmark remain unchanged.

Verification:

- `.venv/bin/pio test -e native`: 723 cases passed, including the new debounce,
  wiring and composed bookmark tests.
- The five committed `test_handlebar_group` cases also passed against the same
  production objects from a temporary copy.
- `.venv/bin/python -m unittest discover -s test -v`: 217 tests ran; 216 passed
  and the power-report console fixture failed compilation because its fake
  control status omitted the readiness fields used by the new STATUS output.
  After adding those fields, the affected `test_power_shutdown_report.py` suite
  passed both cases. Passing evidence for the unchanged tests is reused.
- The full Python run includes actual ESP32 composition code under synthetic
  GPIO/RTOS/filesystem boundaries. New `main-mark-local`,
  `main-mark-with-record`, `main-mark-settings` and `main-mark-safe` scenarios
  passed: camera-free operation, coexistence with Record and terminal input
  retirement on settings/safe-mode revocation.
- `.venv/bin/pio run -e bench_application -e bench_application_private_compile
  -e hero12_adapter_compile`: all three pinned builds passed.
- Python lint, C++ formatting, Linux C++ portability and the existing target
  camera-health resource/stack budget audit passed.
- Independent read-only code review found no actionable issues. `graphify
  update .` refreshed the local AST graph.

The finite installed-wiring/press campaign remains with #30/#31. Native HERO12
request/response and saved-clip confirmation remain with #46. Configuration and
receipt limitations are in [bookmarks](../../bookmarks.md).

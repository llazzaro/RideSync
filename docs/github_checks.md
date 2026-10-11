# GitHub checks

The October 11, 2026 checks update preserves the existing direct-to-main
delivery workflow and all firmware tests, builds and resource/link audits.
Checks detect failures after a push; main has no required PR gate.

## Configuration

- CI validates workflow YAML with checksum-verified actionlint 1.7.12 and
  Python with pinned Ruff 0.15.0. Ruff uses E9, F63, F7 and F82; annotations
  cover only SCons-injected names. C++ formatting remains pinned.
- The CI job has a 30-minute timeout, and checkout does not persist credentials.
  Superseded runs cancel only within the same workflow, event and ref.
- CodeQL analyzes Actions, C++ and Python on main pushes, pull requests and
  weekly. Actions are pinned to immutable SHAs. C++ uses inferred build mode
  `none`; conditional firmware and external SDK headers limit coverage.
  See [testing](testing.md) for acceptance boundaries.
- Dependabot checks Actions, development Python tools and CAD Python
  dependencies weekly, with a two-version-PR limit per configuration.
  PlatformIO Core routine version updates are excluded; supported security
  updates remain eligible. Firmware platform/library pins are unchanged,
  and there is no automatic merging.

## Repository settings

GitHub API read-back verified these enabled settings on October 11, 2026:

- Vulnerability alerts and Dependabot security updates.
- Repository secret scanning and repository push protection.
- Required full commit SHAs for Actions, preserving the previous allowed-action
  policy and read-only default workflow token.

Repository push protection is separate from personal-account push protection.
Neither secret protection nor code analysis establishes hardware compatibility.

## Verification

Implementation revision: `cdf4e839595f2afda5de6cfe3f76a9181223dcf6`.

- Local Ruff 0.15.0, actionlint 1.7.12, Python compilation of annotated build
  scripts and scoped whitespace checks passed. Independent reviews accepted
  the configuration without actionable findings.
- [CodeQL run 38137308849](https://github.com/llazzaro/RideSync/actions/runs/38137308849)
  passed all three languages. Analysis uploads match the implementation SHA,
  report no extraction errors and contain zero findings under the default
  query suites and documented coverage limits.
- Initial Dependabot runs
  [38137313028](https://github.com/llazzaro/RideSync/actions/runs/38137313028),
  [38137314654](https://github.com/llazzaro/RideSync/actions/runs/38137314654) and
  [38137316239](https://github.com/llazzaro/RideSync/actions/runs/38137316239)
  passed; dependency graph submission also passed.
- [Full CI run 38137308848](https://github.com/llazzaro/RideSync/actions/runs/38137308848)
  passed every existing test, format check, pinned firmware build and link/resource
  audit, including the new workflow and Python lint checks.

Unrelated in-progress firmware edits in the shared checkout were excluded from
the implementation commit. Local configuration verification and the committed
GitHub run provide evidence for this change under the
[acceptance policy](acceptance_policy.md).

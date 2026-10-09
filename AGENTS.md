# Repository workflow

- Work directly on `main`. Commit and push completed, verified changes to
  `origin/main` without opening a pull request.
- Do not create feature branches or worktrees unless the user explicitly asks
  for them. Do not ask the user to choose a branching or PR workflow for routine
  authorized work; the direct-to-main preference is already established.
- Keep changes focused on the existing issue scope and follow
  `docs/acceptance_policy.md` for verification. Reuse passing evidence for
  unchanged code; documentation-only changes do not need firmware rebuilds.
- Preserve unrelated user changes. Do not force-push or discard work.

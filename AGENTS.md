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

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

When the user types `$graphify` (or `/graphify`), use the project skill at
`.codex/skills/graphify/SKILL.md`. See `docs/graphify.md` for the pinned CLI setup.

Rules:

- For codebase questions, first run `graphify query "<question>"` when the CLI is available and graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output. If the CLI or graph is absent, inspect source normally; verify relevant source before editing or relying on graph claims.
- Dirty graphify-out/ files are expected after hooks or incremental updates; dirty graph files are not a reason to skip graphify. Only skip graphify if the task is about stale or incorrect graph output, or the user explicitly says not to use it.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).

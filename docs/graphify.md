# Graphify code navigation

RideSync includes the project-scoped Codex skill and references from
[Graphify](https://github.com/Graphify-Labs/graphify), version **0.9.74**.
The official Python package is `graphifyy` (double `y`); its CLI is `graphify`.
This is an optional host analysis tool, separate from PlatformIO and firmware.

## Setup and local code graph

From the repository root, with Python 3.10+ and `uv` available:

```sh
uv tool install graphifyy==0.9.74
graphify --version
graphify extract . --code-only --max-workers 2
graphify cluster-only . --no-label --no-viz
graphify query "camera group recording" --budget 1500
graphify explain "CameraManager"
```

Alternatively, install with `python -m pip install -r requirements-graphify.txt`
inside a Python virtual environment.

The code-only command parses locally without an LLM or API key. It writes
`graphify-out/graph.json`; the clustering command writes `GRAPH_REPORT.md` without
LLM community naming or visualization. Generate the optional
interactive view with `graphify export html`; large graphs can be slow to render.
Refresh after code changes with `graphify update .`.

The initial RideSync index contains over 9,000 nodes, so prefer scoped CLI queries
for navigation; an HTML view of this size may be sluggish.

The generated output is ignored by Git. Graphify respects `.gitignore`, including
PlatformIO dependencies/builds, the virtual environment and private commissioning
headers. `.graphifyignore` additionally excludes assistant tooling and caches.
Regenerate the graph in each checkout; it is navigation evidence, not a compiler,
test result or hardware qualification.

## C++ and Codex

Graphify supports C/C++ source and headers, including `.cpp`, `.cc`, `.cxx`, `.h`
and `.hpp`, using tree-sitter AST parsing. It extracts symbols and relationships;
it does not replace the ESP32 toolchain or guarantee compiler-level resolution
of macros, conditional compilation, templates or runtime dispatch. Confirm
important graph findings against source and the applicable tests.

On the next Codex turn, invoke `$graphify` to use the checked-in skill. The
`AGENTS.md` section supplies graph-query guidance; the upstream Codex hook is an
intentional no-op. No additional Git hooks are installed. Full skill runs can
include documents/media through the assistant's model; use `--code-only` for
the local AST workflow above.

To refresh the vendored skill after deliberately changing the pinned version:

```sh
graphify codex install --project
```

Review the generated changes, including `AGENTS.md`, before committing. Preserve
RideSync's repository workflow and its CLI/source fallback guidance.

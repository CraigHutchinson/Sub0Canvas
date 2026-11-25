# Follow-up session: V0, a worktree snapshot and Markdown in the viewer (what is left)

Backlog: item 8. Design: [AUTHORING.md](../AUTHORING.md) sections 3, 4 and 7 (V0); [BLOCK_DISCOVERY.md](../BLOCK_DISCOVERY.md) 4.4 (Markdown); requirements R-AUTH-1, R-AUTH-11, R-BLK-9 in [REQUIREMENTS.md](../REQUIREMENTS.md).

## Goal

Open this repository's own docs in the viewer as a layered document. `sub0canvas snapshot <dir> [paths...]` (in `app/`) and `tools/serve.mjs` already write and serve a `layered-doc-1` snapshot of a git repository's history for a pathspec. What is left is Markdown and the uncommitted state.

## Left

1. **A Markdown provider** behind the D0 seam: ATX and Setext headings as nested `heading` nodes named by the heading path, fenced code blocks as children tagged with their language; R-BLK-1, 3, 4 checks on a small hand-checked corpus (`fixtures/languages/markdown/`).
2. **The viewer renders `.md`** (a small renderer in `src/`, no dependency) with sections as blocks (select, fold, history); code unchanged; `npm test` cases for the renderer and for a Markdown snapshot.
3. **The worktree's uncommitted state** as a last turn of the snapshot.
4. **Merge commits titled by the change they merge.**
5. A screenshot of the docs in the viewer under `docs/screenshots/`; AUTHORING.md and REQUIREMENTS.md say what is built.

## Done means

* `sub0canvas snapshot . docs` writes a snapshot that the viewer opens: every heading of every doc is a block, Markdown renders, code still renders; a golden snapshot of a small fixture tree is byte-compared.
* `ctest --preset quick` and `npm test` are green; the viewer still opens the demo and the golden attempts read-only from `file://` (R-AUTH-11).

## Environment and constraints

[AGENTS.md](../../AGENTS.md): C++23, MinGW-w64 UCRT GCC and Ninja on Windows; the viewer has no dependencies and runs as classic scripts. No downloads.

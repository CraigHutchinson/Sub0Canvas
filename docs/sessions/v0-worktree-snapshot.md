# Follow-up session: V0, a worktree snapshot and Markdown in the viewer

Status: open, partly done (written 2026-11-18; narrowed the same day: `sub0canvas snapshot` exists in `app/`, reading a git
repository's history for a pathspec, and `tools/serve.mjs` serves it from git). Backlog: "V0" under *Now*. Design: [AUTHORING.md](../AUTHORING.md) sections 3, 4 and 7 (V0);
[BLOCK_DISCOVERY.md](../BLOCK_DISCOVERY.md) 4.4 (Markdown); requirements R-AUTH-1, R-AUTH-11, R-BLK-9 in [REQUIREMENTS.md](../REQUIREMENTS.md).

## Goal

**Left** ([PLAN.md](../PLAN.md) phase 2, step 1): Markdown blocks through D0's provider seam, Markdown rendered in the viewer, the worktree's
uncommitted state as a last turn, and merge commits titled by the change they merge. Steps 2 and 3 below are done in their first form; the
original goal follows for context.

The first step of the authoring tool: open **this repository's own docs** in the viewer as a layered document. Build a `sub0canvas` executable in this
repository with one command, `snapshot <dir> [paths...]`, that writes a `layered-doc-1` snapshot of the working tree (files, blocks, no layers yet),
with a minimal Markdown provider so every heading is a block; and teach the viewer to render Markdown (headings, paragraphs, lists, code fences)
beside code. No store, no server, no comments yet (those are V1 and V2). Prefer D0's provider seam if it has landed; otherwise add the Markdown
provider in a way D0 can absorb, and say so.

## What is known

* The viewer (`index.html`, `src/model.js`, `src/view.js`) reads `layered-doc-1` turn files from a folder, from `file://`, with no build step; it
  renders code only. `tools/serve.mjs` serves it on loopback.
* `layeredJson` (`doc_layers.hpp`) writes a snapshot from files, revisions and a `BlockIndex`; blocks come from `foldRegions`, which finds no blocks
  in Markdown today (BLOCK_DISCOVERY.md section 2).
* The library is pure; the executable may do I/O (read the tree). Executables in this repository: none yet; add one in `CMakeLists.txt` beside the
  library, linking it.
* Views must stay concise (R-AUTH-1): a large doc renders folded by section, with headings as the outline.

## Steps

1. A Markdown provider: ATX and Setext headings as nested `heading` nodes named by the heading path; fenced code blocks as children, tagged with their
   language; R-BLK-1, 3, 4 checks on a small hand-checked corpus (`fixtures/languages/markdown/`).
2. `sub0canvas` (C++, in `app/`, AGENTS.md "Layout"): `snapshot <dir> [paths]` walks the given paths (default: tracked text files), indexes them, and
   writes one snapshot (`layers/turn-0.json`) plus revision texts, as the viewer already reads. Deterministic: sorted paths, no timestamps.
3. A golden snapshot of a small fixture tree, byte-compared in a test; and `sub0canvas snapshot . docs` run by hand on this repository.
4. The viewer: render `.md` files as Markdown (a small renderer in `src/`, no dependency), with sections as blocks (select, fold, history); code
   unchanged. `npm test` cases for the renderer and for a Markdown snapshot.
5. Update AUTHORING.md (V0 built), REQUIREMENTS.md, RESUME.md, BACKLOG.md; a screenshot of the docs in the viewer under `docs/screenshots/`.

## Done means

* `sub0canvas snapshot . docs` writes a snapshot that the viewer opens: every heading of every doc is a block, Markdown renders, code still renders.
* `ctest --preset quick` and `npm test` green, with the new golden snapshot test.
* The viewer still opens the demo and the golden attempts read-only from `file://` (R-AUTH-11).

## Environment and constraints

As AGENTS.md: C++23, MinGW-w64 UCRT GCC and Ninja on Windows; the viewer has no dependencies and runs as classic scripts. No downloads. Commit as you
go; confirm before pushing.

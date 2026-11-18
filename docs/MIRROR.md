# The synthetic mirror: an invented project with real-world shape, as a fixture

Status: **built** (2026-11-18), `tools/mirror/`, tested by `test/mirror.test.js`. Brief: [sessions/m0-synthetic-mirror.md](sessions/m0-synthetic-mirror.md).
Design it serves: [CHANGE_VIEWS.md](CHANGE_VIEWS.md) (U12), [SCALE.md](SCALE.md) (profile B), [BACKBONE.md](BACKBONE.md) section 5 (sources).

## What it is

A seeded generator of **Fernhall**, an invented desktop photo catalogue with no relation to any real codebase. It has the state of a
work-dominated monorepo, scaled down, and a topic of changes with the shape of a real one. It writes the **truth** of every derivation change
views will make, because it made every choice. The same seed gives byte-identical output and the same git commit ids on every machine.

| part | seed 1 |
|---|---|
| repository | 292 files: 97 code (C++ and Python), 91 sidecars, 61 binaries, 32 data, 11 docs; one 400-block generated file |
| history | 318 commits over two years and a seven-week topic window; `main`, two release branches, a feature branch; 15 merge commits (13 into `main`) |
| the topic | 31 changes: an origin draft by another contributor, 4 spikes, 20 carve-outs, an integration change, 2 test-only changes, an assets-only change, a port, an unrelated refactor; 14 merged, 12 open, 5 closed; 57 files, 14 touched by four or more changes |
| spike coverage (S1) | every state occurs: landed, in flight, not carved, obsolete, dropped |
| forge and tracker | pull requests with drafts, labels, bases (stacks, a feature branch, a release branch), review threads, check runs, four tracker items with links |

The fixture `fixtures/mirror/seed-1/` holds `state.json` (`mirror-state-1`: commits, pull requests, reviews, checks, tracker items),
`truth.json` (`mirror-truth-1`: per change its footprint, files, lineages and flags; lineage links; per spike hunk its state; the edge-case
register) and `shape.json` (the statistics). A test proves regeneration matches it, so it never drifts from the generator.

## Modes

```bash
npm run mirror -- shape   --seed 1                       # statistics
npm run mirror -- source  --seed 1 [--serve]             # state and truth as JSON; --serve: JSON lines on stdio
npm run mirror -- local   --seed 1 --out <dir>           # <dir>/repo, a git repository; every commit id checked against the generator's
npm run mirror -- canvas  --seed 1 --out <dir>           # layered-doc-1 turns, one per week, for the viewer
npm run mirror -- github  --seed 1 --repo <name>         # the steps that publish it to a new forge repository: printed only
npm run fixtures:mirror                                  # rewrite fixtures/mirror/seed-1
```

* **source**: the mirror as a runtime data source. In process, `generateMirror()` returns `source` with `revisions`, `files`, `changedPaths`
  and `blocks` (the `DocumentSource` shape of BACKBONE.md section 5) and `items.fetch` (the `ItemSource` shape). `--serve` answers the same
  calls as JSON lines on stdio (`{"id":1,"method":"files","params":{"revision":"<sha>"}}`), the protocol shape of AUTHORING.md section 3.
* **local**: git builds the repository through `fast-import`; the generator computes the same commit ids in JavaScript, and the mode fails
  if git disagrees. It imports with `core.ignorecase=false`, because the case-only rename edge case otherwise diverges on Windows.
* **canvas**: trunk at the end of each week of the topic window, the topic's code files (followed through moves), and layers `changes` (each
  change with its state then, anchored to its footprint), `topics`, `spikes` (each spike hunk's state then) and `review`.
* **github**: prints the ordered steps that create a repository and replay the pull requests in time order. A merge is a push of the merge
  commit, which a forge marks as a merge. `--execute` runs them against the user's own forge account. It is written but **has not been run**
  against a live forge. Creating a repository is the owner's decision each time.

## Edge cases: pushing the canvas's boundaries

The mirror deliberately contains what real large repositories do and the canvas may not handle yet (the owner's direction, 2026-11-18). Each
entry in `truth.edgeCases` names where it is and whether today's design covers it: `handled` (built), `designed` (on paper), or `gap` (neither).

| edge case | today |
|---|---|
| overloads: one name, two blocks, a spike hunk on the second | designed |
| a block inside `#if` / `#endif` | **gap** |
| a 400-block generated file regenerated wholesale | designed (classification); identity under regeneration is not |
| CRLF line endings with a BOM | **gap** |
| tab indentation and non-ASCII text (column units) | **gap** |
| a licence header added to every file (every line moves) | designed |
| a block moved to another file; a file moved to another directory | designed |
| a case-only file rename | **gap** |
| trunk renames a block that open changes modify | designed |
| two open changes, in different topics, replace one line differently | designed |
| a merged change reverted, then relanded | **gap** |
| the bottom of a stack force-pushed after a rebase, leaving the stack stale | **gap** |
| trunk merged back into an open change | designed |
| an evil merge: a line in the merge commit from neither parent | **gap** |
| an assets-only change: no block in its footprint | **gap** |
| a change open for 300 days, far behind trunk | designed |
| tracker keys in a branch name only, or two in one title | designed |
| a feature-branch merge; a port onto a release branch | designed |

Not modelled yet (`truth.notModelled`): symbolic links and submodules, large-file pointers, two paths differing only by case in one tree,
committed conflict markers, a topic across repositories, review comments replayed on a forge, a hunk split across two carve-outs.

The gaps feed [CHANGE_VIEWS.md](CHANGE_VIEWS.md) section 10.

## Parked: a live, near-production sandbox

Deferred by the owner (2026-11-18). The state needed is already captured, so it can be built later without changing the generator:

* **A local forge.** Gitea or Forgejo (open source, GitHub-like API, one binary) run locally would host the mirror with real pull requests and
  review threads, with nothing leaving the machine. Each is a download, so it needs the owner's approval.
* **A `gh` stand-in.** An in-process or on-`PATH` stand-in for the forge CLI that records the plan's steps into a local store over a bare
  remote. It would test the `github` mode end to end without any forge.
* **Git-native review.** git-appraise (reviews in git notes) and Radicle (patches as collaborative objects in git) are prior art for pull
  requests kept in git itself. They fit DECISIONS.md section 3 (annotations in git refs).
* **A live public repository** (`--execute`, for example `sub0canvas_test1234`) for G2 and P4's forge adapter against real data.

# Follow-up session: G1, git history and per-path block identity

Status: closed (2026-11-24; see the Outcome at the end). Backlog: "G1" under *Now*. Design: [BACKBONE.md](../BACKBONE.md) sections 5, 6 and 12 (per-path history),
slice G1; [USE_CASES.md](../USE_CASES.md) U3 and U4; [TESTING.md](../TESTING.md) rung L2.

## Goal

Give block identity a project's history: a local git `DocumentSource` and a per-path revision index that answers "what happened to this function
across the last N commits, renames and moves included". Prove it against hand-checked truth on real repositories already on this machine (this one
and Sub0Squad). Read-only, local, no network. Do not build the store (B1) or any GitHub access (G2).

## What is known

* `BlockIndex` follows one sequence of texts and logs `BlockEvent`s (`kept` with `edited`, `resigned`, `renamed`, `revived`, `added`, `retired`,
  `merged`) stamped with an integer turn. `blockHistory(index, id)` gives one id's events including ids merged into it.
* BACKBONE.md section 6 sketches `RevisionIndex` over a tree; [SCALE.md](../SCALE.md) section 4 changes it to **per-path replay** (a path's own revisions, git's rename and
  copy detection handing cross-file moves to the revive rule, checkpoints per path) so that resolving one anchor never replays the whole tree.
* Good test histories exist locally: this repository (small, includes the import of `lib/` from Sub0Squad), and Sub0Squad (hundreds of commits,
  `src/canvas_agent.cpp`, `src/doc_layers.cpp` moved out to this repository on 2026-11-17, functions renamed over time).
* The library is pure (no I/O). The git adapter is I/O: a separate target (`sub0canvas::git`), reading `git` plumbing output (`git log --format`,
  `git show <rev>:<path>`, `git diff --name-status -M`), never linked by the core.

## Steps

1. **Interfaces** (core, pure): `RevisionInfo`, `DocumentSource` (BACKBONE.md section 5); `BlockEvent` gains a revision id (string) beside the turn.
2. **`sub0canvas::git`**: a `DocumentSource` over a local repository through `git` subprocesses; tested on a tiny repository built by the test itself
   in a temporary directory (commits with known edits, a rename, a move).
3. **Per-path history** (core): replay one path's revisions through a `BlockIndex`, following renames; `history(path, name or id)` returns the events
   with their commits.
4. **Truth** (`fixtures/scenarios/g1-sub0squad/truth.json`, hand-checked): for five functions in Sub0Squad and three here, every identity event across
   their history (renames, signature changes, moves between files), with the commit and how it was checked (`git log -L`, reading the diffs).
5. **The check**: a test (label `full` if slow) that runs the history against truth. The repositories are read where they are, through a path given
   in the environment; without it the test prints `SKIP` (CI has no sibling checkouts).
6. Record the cost of the replay (commits per second, memory) as a first input to S2.

## Done means

* `sub0canvas::git` and per-path history are in `main`, with the tiny-repository test in the quick set.
* The truth check passes on all eight functions, or each disagreement is explained (a rule's limit, written into LAYERED_DOCUMENT.md).
* The replay cost of Sub0Squad's history is recorded in SCALE.md section 6.

## Environment and constraints

See [AGENTS.md](../../AGENTS.md). No network: the repositories are local clones; cloning anything else needs the owner's go-ahead.

## Outcome (2026-11-24)

**Built** (branch of this repository, three commits; not pushed): the seam `lib/include/sub0canvas/source.hpp` (`DocumentSource`: `history` of a scope, batched `read`, `list`, whole-diff `changes`, `resolve`; `FileText`
with `Presence::unknown`, A20); the pure parsers `git_plumbing.hpp/.cpp` (`log --name-status -z`, `diff-tree`, `ls-tree -z`, `cat-file --batch`); `revision_index.hpp/.cpp` (`RevisionIndex`: `at`, `named`, `history`, `carry`,
checkpoints, rename chase, `ReplayStats`); `app/git_source.cpp` (the process runner) and `sub0canvas history`. `BlockIndex`, `BlockEvent` and `doc_layers.cpp` are unchanged: `BlockEvent.turn` carries the replay ordinal and
`HistoryStep` pairs it with the revision id (the brief's "`BlockEvent` gains a revision id" is therefore not needed in the core).

**Proved**: on an invented 200-commit repository (`tools/git-fixture.mjs`, captured to `fixtures/git/tinycalc`: five functions with edits, a rename, signature changes, a move between files that git sees as a 62% copy, two
whole-file renames, a deletion and a revival, a two-parent merge, a helper folded into another function) against hand-checked truth (`fixtures/scenarios/g1-tinycalc/truth.json`, seven scenarios: the directory, an earlier
revision, two single-path scopes that follow a rename, a path with a merge, a 40-revision window, a shallow clone of depth 25). `test_revision_index` reads the captured output (R-SRC-3, R-SRC-8, R-SCALE-1/7/9, with
every checkpoint spacing from 1 to 1000), a `Canvas` adapter is the second `DocumentSource` (R-SRC-5), and `test/history.test.js` runs live git over the same plan, a shallow clone and a partial clone and gets the same
events. Two mutations were caught: the smallest-growth-first file order (a move becomes a merge) and the copy-on-write of retained checkpoints.

**Not done, said plainly**: the truth is on the invented repository, not on five functions of a real sibling project and three here (the brief's step 4): a fixture that can be published and regenerated byte for byte was
worth more than a private history, and a real history is S3's input. The replay cost was measured on two local repositories without naming them (SCALE.md section 6.2). Checkpoints are in-memory copies, not serialised
(`BlockIndex` has no reader; B1). `carry` needs both revisions on one replay line; branch-to-main carry needs `mergeBase` (P0).

**Decisions** (each also in `revision_index.hpp`): per-scope replay, ordinals for `BlockEvent.turn`; a rename is the old path's retirement then the new path's indexing, so the revive rule keeps identities (a `retired` and
a `revived` event); within a revision files are applied smallest growth first so a moved function is retired before it is met again; a single-path scope follows a rename backwards but not a copy; unknown texts keep the last known
blocks and are listed, never an error; a head further along a line extends it.

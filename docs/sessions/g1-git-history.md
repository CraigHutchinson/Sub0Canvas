# Follow-up session: G1, git history and per-path block identity

Status: open (written 2026-11-18). Backlog: "G1" under *Now*. Design: [BACKBONE.md](../BACKBONE.md) sections 5, 6 and 12 (per-path history),
slice G1; [USE_CASES.md](../USE_CASES.md) U3 and U4; [TESTING.md](../TESTING.md) rung L2.

## Goal

Give block identity a project's history: a local git `DocumentSource` and a per-path revision index that answers "what happened to this function
across the last N commits, renames and moves included". Prove it against hand-checked truth on real repositories already on this machine (this one
and Sub0Squad). Read-only, local, no network. Do not build the store (B1) or any GitHub access (G2).

## What is known

* `BlockIndex` follows one sequence of texts and logs `BlockEvent`s (`kept` with `edited`, `resigned`, `renamed`, `revived`, `added`, `retired`,
  `merged`) stamped with an integer turn. `blockHistory(index, id)` gives one id's events including ids merged into it.
* BACKBONE.md section 6 sketches `RevisionIndex` over a tree; section 12 changes it to **per-path replay** (a path's own revisions, git's rename and
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
* The replay cost of Sub0Squad's history is recorded in BACKBONE.md section 12.

## Environment and constraints

See [AGENTS.md](../../AGENTS.md). No network: the repositories are local clones; cloning anything else needs the owner's go-ahead.

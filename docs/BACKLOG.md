# Backlog

Status: living (2026-11-18). Updated whenever work starts, lands or is reordered.

What is next, in order, by the phases of [PLAN.md](PLAN.md) (which holds the gates and the reasons). Each item names its slice ([BACKBONE.md](BACKBONE.md) section 10, [AUTHORING.md](AUTHORING.md) section 7,
[BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md) section 5) and, when it has one, its brief in [sessions/](sessions/README.md). Items in *Now* can run in
parallel. The requirements each slice must meet are in [REQUIREMENTS.md](REQUIREMENTS.md).

## Waiting on the owner

* **One sheet of decisions**, [PLAN.md](PLAN.md) section 3 (O1-O13): the release, where data lives, writers and clocks, block ids, the format
  revision, the server, GitHub, C++23, tree-sitter, the store in Sub0Squad, Sub0Fabric, the scale profile and change views, spikes. Each has a
  recommendation and links to its reasoning ([DECISIONS.md](DECISIONS.md), [SCALE.md](SCALE.md), [CHANGE_VIEWS.md](CHANGE_VIEWS.md)). Phase 1
  needs none of them except O1.

## Now: phase 1, groundwork in parallel ([PLAN.md](PLAN.md) section 4)

* **Release v0.3.0** (O1: pushing the tag needs the owner's go-ahead), then **Sub0Squad adopts `applyReply`, turn control and the moved checks**: a
  Sub0Squad PR replaces its `applyCanvasReply` and `turn_control`, drops the copied tests and cases (block 4, the skeleton block,
  `canvasModelCheck`, block 1's reply corpus and turn checks), keeps `run-v11-test-app-reply.txt` (its platform test).
* **MS1 (B0 through the mock): done** (2026-11-23): `fixtures/mock` regenerated in the B0 format, G3, G5, G7 (8 turns by id, 2.5x one turn), B10, B11 added (15 scenarios), `test/mock.test.js`, a stale author through `applyReply` (`ApplyOptions::baseRevision`). **Next, MS2.**
* **MS0: the mock squad: done** (2026-11-23, [MOCK_SQUAD.md](MOCK_SQUAD.md); outcome in [sessions/ms0-mock-squad-baseline.md](sessions/ms0-mock-squad-baseline.md)): `consumer/mock_squad/` runs ten scenarios in today's format, frozen in `fixtures/mock`, as `test_consumer_mock_squad` (label `full`). **Next, MS1 = B0**: regenerate `fixtures/mock` with B0's fields and trace an item through G4 in the viewer; then MS2 (the producer API proposal, MOCK_SQUAD.md section 6 now holds what the baseline taught).
* **D0: the block discovery seam: done** (2026-11-23): outline result, providers `braces@3` and `indent@2`, detection, and the block-comment, name and decorator faults fixed, each test-first
  ([sessions/d0-block-discovery-seam.md](sessions/d0-block-discovery-seam.md), Outcome). **Left**: `index.outliners` in the snapshot (regenerates the snapshot fixtures: needs the owner's go-ahead),
  the policy that skips unnamed nodes, Rust lifetimes and CRLF Allman files, then D1 (the conformance corpus). S1, G1 and V0's Markdown can build on the seam now.
* **S0: the scale harness. Generator and benchmarks at 10^5 lines done** (2026-11-23: `bench/corpus.hpp`, `Sub0Canvas_Bench` on the family's sub0perf tooling, `perf/metrics.json`, the first
  baseline; numbers in [SCALE.md](SCALE.md) section 6.0). **Left**: 10^7 and streamed 10^9 lines, profile B, the change sets, the index-against-truth comparison, S1's choices stated.
  Brief: [sessions/s0-scale-harness.md](sessions/s0-scale-harness.md).
* **B0: one format revision. Steps 1-3 done here** (MS1, 2026-11-23; the mock squad drives it): item `id`, `column` and ranges, `layerMeta`, policies (A13), revision ids as strings (A1), per-turn events with
  `eventsFrom` (O5 confirmed), texts by content id, additive, read by the viewer in both shapes. **Left**: re-capture one real attempt with compiler diagnostics
  (R-U1-4), the release (v0.4.0) and the Sub0Squad PR (`SnapshotSpec`, `%2F` naming). Brief: [sessions/b0-format-catch-up.md](sessions/b0-format-catch-up.md).
* **K0: the viewer's interactivity benchmark, done** (2026-11-24, [K0.md](K0.md), `node tools/k0/run.mjs`, [AUTHORING.md](AUTHORING.md) section 3.2, R-AUTH-15): the mirror is inside
  every budget; on the 1,590-file subtree eight of ten interactions breach by 2.3x to 11.6x (one full re-render per click), which orders V1a. **Left**: `--repo` for a real
  repository, the shared sub0perf samples import (proposal in K0.md, Sub0Math S-M9). Brief: [sessions/k0-ui-kpis.md](sessions/k0-ui-kpis.md).
* **The change-view gaps the mirror exposes** ([CHANGE_VIEWS.md](CHANGE_VIEWS.md) section 10), design only: reverted state, stale stacks, evil
  merges, file footprints, guards, column units, case-only paths, regenerated files; each checkable on `fixtures/mirror/`. Before P0.

## Next: phase 2, the authoring tool on this repository

* **V0, finished**: Markdown blocks (through D0's seam), Markdown rendered, the worktree's uncommitted state, merge commits titled by the change they
  merge. The snapshot itself is built (`app/`, `tools/serve.mjs`). Brief: [sessions/v0-worktree-snapshot.md](sessions/v0-worktree-snapshot.md).
* **V1a: live views** ([AUTHORING.md](AUTHORING.md) section 3.1; measured need in [SCALE.md](SCALE.md) section 6.1): the `serve` process and the
  bridge; scoped queries; texts by content id (B0); turns as deltas; per-pane keyed patches, windowed lists. Its steps:
  * **the file tree on demand**: only open folders rendered, windowed; a toggle patches its rows; children listed when opened (streamed for very
    large folders). Budget: 50 ms to expand a 1,588-file folder;
  * **file content on demand**: texts by content id, fetched when opened, likely next files prefetched; the bundle form keeps working offline;
  * **the block list in its chosen shape** (register row in BACKBONE.md 14.3: today one global list is 92% of the page at 20,000 blocks), scoped
    and windowed.
* **V1: comments**: the store's core (B1), the log as its own module (A18) with writers and Lamport clocks (A15), written locally in the per-writer
  segment format; comment, reply and resolve from the viewer; scenarios as tests.
* **V2: the agent's inbox and sync**: B2's inbox over the review layer, the CLI, views with budgets; the git-ref log backend (A17). **The acceptance
  test is a real review round on this repository's docs** (AUTHORING.md section 1).

## Then: phase 3, history and scale

* **G1: the git document adapter and per-path history: done** (2026-11-24; A20, A21; outcome in [sessions/g1-git-history.md](sessions/g1-git-history.md)): `DocumentSource`, `RevisionIndex`, `GitSource`, `sub0canvas history`,
  checked on an invented 200-commit repository against hand-checked truth. **Left**: the truth on real histories (S3: the brief's five functions of a sibling project, the Linux kernel; the owner approves
  each download), persisted checkpoints (B1: `BlockIndex` has no reader), a stopping revision on `history` so extending a line does not re-read it, branch-to-main `carry` (P0, with `mergeBase`).
* **S1: the compact, lazy index** (after S0's numbers): blocks keyed by id and by blob (`BlockIndex::resolve` scans every live block today, which
  made a per-event lookup quadratic at 200,000 blocks, SCALE.md section 6.1), birth ids (A16), no texts held.
* **D1: the conformance corpus** per language, hand-checked outlines.
* **S2** the sketch index and per-path checkpoints.
* **P0: changes from local git** (after G1 and B4): footprints at block level, hunk fingerprints, stacks; S0's change sets as truth.
* **P1, P2: overlap, conflict and lineage** (with S2): block-level relations; carved-from, supersedes, integrates, depends-on, ports; spike
  coverage. Checked also on the owner's private reference topic, numbers only.
* **B3 claims and notes: built** (2026-11-24, `claims.hpp`; BACKBONE 3.5 as built). Left for MS4: the two-actor mock scenarios (B8, B14) as claim tests, and Sub0Squad's fork executor taking write claims per section.
* **B4 source anchors and the resolver** (V3 pulls B4's quote re-anchoring forward).

## Any time

* **Runtime evidence on canvas**: prototype an on-demand trace read in Sub0Squad using the existing issues and layers. Verify historical reads and
  bounded detail on a real repair before routing any required interchange fields through B0 and the viewer read through V1a.
  Brief: [sessions/canvas-runtime-evidence.md](sessions/canvas-runtime-evidence.md).

* **Sub0Squad: measure the layer where it acts**: off against adaptive on bugs-10 and other clipped cases, 10 runs per arm; inline detail against the
  summary alone. A Sub0Squad PR with no library change ([INTEGRATION.md](INTEGRATION.md)).
* **Sub0Platform: finish moving the C++ layer out of Sub0Squad**, so `app/` and `serve` adopt it (DECISIONS.md section 8). Before or during V1a.

## Later: phase 4, widening (ordered after phase 2's acceptance round)

* **Test coverage and assertion evidence (U8)**: after the C++ runtime-evidence slice, prove per-test import and a revision-bound source overlay.
  Scope assertion observations and behavioral sensitivity separately. [Brief](sessions/canvas-test-coverage.md).

* **Revision-bound prose verification**: attach a claim and its verification to one code block and a linked test. Record a new check
  when the code, prose or supporting test changes, with historical evidence still readable in both directions. Receive it after the
  runtime evidence slice through the backbone's claims and revision-aware anchors. [Brief](sessions/canvas-prose-verification.md) · [Issue #3](https://github.com/CraigHutchinson/Sub0Canvas/issues/3).

* **Shared source addressing**: use revision-aware anchors for edits, diagnostics and linked evidence. Distinguish historical display
  from a uniquely resolved edit target, and verify relocation, ambiguity and stale edits with one C++ receiving case.
  [Brief](sessions/canvas-source-addressing.md) · [Failure and stack trace issue #2](https://github.com/CraigHutchinson/Sub0Canvas/issues/2).

* **Revision-pinned viewpoint clones**: fork a saved actor viewpoint into an isolated session for Squad retries, with a verified checkpoint fingerprint and branch lineage. [Requirement](sessions/canvas-source-addressing.md#clone-a-viewpoint-at-a-pinned-revision) · [Issue #1](https://github.com/CraigHutchinson/Sub0Canvas/issues/1).

* **V3-V5**: edits and suggestions, checks and push, a second actor.
* **D2** data formats and declarative definitions; **D3** tree-sitter (the owner's answer to question 8, languages chosen by S3's numbers).
* **S3** large public repositories cloned locally (each download approved); **S4** the sharded store.
* **G2-G5**: GitHub items, issues and project items, events and subscriptions, write-back.
* **P3-P5**: change views in the authoring tool; forge (configurable host) and tracker sources; topic inboxes and carve-out proposals.
* **M1: a live sandbox for the mirror** (parked by the owner, 2026-11-18): a local forge (Gitea or Forgejo, a download to approve), a forge-CLI
  stand-in over a bare remote, or a live test repository through `github --execute` ([MIRROR.md](MIRROR.md), "Parked").
* **S3-private**: the budgets on the measured work-dominated monorepo, on the owner's machine, numbers only (SCALE.md section 7).
* **E1**: an editor served by the same `serve` process. **C1**: collaborative review.
* A SARIF export of the issues layer (R-EXP-1); the policy registry and loader (BACKBONE.md 14.4) when the first policy needs a second arm.

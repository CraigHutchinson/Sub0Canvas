# Backlog

What is next, in order. Each item names its slice ([BACKBONE.md](BACKBONE.md) section 10, [AUTHORING.md](AUTHORING.md) section 7,
[BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md) section 5) and, when it has one, its brief in [sessions/](sessions/README.md). Items in *Now* can run in
parallel. The requirements each slice must meet are in [REQUIREMENTS.md](REQUIREMENTS.md).

## Now

* **The owner reads the design** and answers the open questions: [BACKBONE.md](BACKBONE.md) section 11 (eleven questions; 6-11 are new), with
  section 14 (the later use cases, amendments A1-A14, the register of open decisions, pros and cons), [AUTHORING.md](AUTHORING.md) and
  [BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md). The answers decide B1's shape, the authoring server (V1) and tree-sitter (D3).
* **D0: the block discovery seam.** One outline result and a provider per language, today's heuristics behind it (byte-identical), then the measured
  faults fixed (block comments, generic names, arrow functions, Go receivers, decorators). Before S1 and G1. Brief:
  [sessions/d0-block-discovery-seam.md](sessions/d0-block-discovery-seam.md).
* **V0: a worktree snapshot and Markdown in the viewer.** The `sub0canvas` executable writes a snapshot of this repository's docs; the viewer renders
  Markdown and code. The first step of the authoring tool. Brief: [sessions/v0-worktree-snapshot.md](sessions/v0-worktree-snapshot.md).
* **S0: the scale harness.** A deterministic synthetic corpus and benchmarks of today's index, so the data structures are chosen by measurement.
  Brief: [sessions/s0-scale-harness.md](sessions/s0-scale-harness.md).
* **B0: format catch-up.** The producer writes the fields the viewer already understands, plus A1 (revision ids as strings), A3 (ranges) and A13
  (policies recorded); a golden set is checked on both sides; re-capture one attempt with compiler diagnostics (R-U1-4). Brief:
  [sessions/b0-format-catch-up.md](sessions/b0-format-catch-up.md).

## Next

* **The owner reads [SCALE.md](SCALE.md) and [CHANGE_VIEWS.md](CHANGE_VIEWS.md)** (2026-11-18): the second scale profile and its requirements
  (R-SCALE-5 to 9, A19-A21), and change views (U12, R-CHG, slices P0-P5, four open decisions).

* **V1: comments** (after the owner's answers to questions 10 and 11): `sub0canvas serve`, a minimal store and file log (B1's core), the bridge;
  comment, reply and resolve from the viewer; scenarios as tests.
* **V2: the agent's inbox**: B2's inbox over the review layer, the CLI, views with budgets; **the acceptance test is a real review round on this
  repository's docs** (AUTHORING.md section 1).
* **G1: the git document adapter and per-path history** (after D0). Brief: [sessions/g1-git-history.md](sessions/g1-git-history.md).
* **Sub0Squad adopts `applyReply` and turn control** (moved here 2026-11-18, A12 done): after a tag here, a Sub0Squad PR replaces its
  `applyCanvasReply` and `turn_control` with these, drops the copied tests and cases, keeps `run-v11-test-app-reply.txt` (its platform test).
* **The owner confirms [DECISIONS.md](DECISIONS.md)**: storage layered by kind (git refs per writer), deterministic birth ids, Sub0Platform's C++ layer
  in the tools (a reason to finish its move out of Sub0Squad), Sub0Fabric kept open behind a liftable log module (A15-A18).
* **D1: the conformance corpus** per language, hand-checked outlines.
* **P0: changes from local git** (after G1 and B4): footprints at block level, hunk fingerprints, stacks; S0's change sets as truth.
* **P1, P2: overlap, conflict and lineage** (with S2): block-level relations; carved-from, supersedes, integrates, depends-on, ports; spike
  coverage. Checked also on the owner's private reference topic, numbers only.
* **S1: the compact, lazy index** (after S0's numbers).
* **B3 claims and notes, B4 source anchors and the resolver** (V3 pulls B4's quote re-anchoring forward).
* **Sub0Squad: measure the layer where it acts**: off against adaptive on bugs-10 and other clipped cases, 10 runs per arm; inline detail against the
  summary alone. A Sub0Squad PR with no library change ([INTEGRATION.md](INTEGRATION.md)).

## Later

* **V3-V5**: edits and suggestions, checks and push, a second actor.
* **D2** data formats and declarative definitions; **D3** tree-sitter (the owner's answer to question 8, languages chosen by S3's numbers).
* **S2** the sketch index and per-path checkpoints; **S3** large public repositories cloned locally (each download approved); **S4** the sharded store.
* **G2-G5**: GitHub items, issues and project items, events and subscriptions, write-back.
* **P3-P5**: change views in the authoring tool; forge (configurable host) and tracker sources; topic inboxes and carve-out proposals.
* **S3-private**: the budgets on the measured work-dominated monorepo, on the owner's machine, numbers only (SCALE.md section 7).
* **E1**: an editor served by the same `serve` process. **C1**: collaborative review.
* A SARIF export of the issues layer (R-EXP-1); the policy registry and loader (BACKBONE.md 14.4) when the first policy needs a second arm.

## Tidy-ups

* `lib/tests/check.hpp`: avoid `getenv`'s deprecation under clang on Windows (`_dupenv_s` there, or read once through a small helper).
* Sub0Squad's `test_blocks` still holds copies of the checks moved here (block 4, the skeleton block, `canvasModelCheck`): once Sub0Squad pins a tag
  with them here, its copies can go, in a Sub0Squad PR.

# Where Sub0Canvas stands (2026-11-18, evening)

Status: living (2026-11-18, evening). Rewritten at the end of each working session.

Read this first when resuming. The plan, in phases, and the one sheet of decisions waiting on the owner are [PLAN.md](PLAN.md); what is next,
in order, is [BACKLOG.md](BACKLOG.md); a task with a brief is in [sessions/](sessions/README.md).

## Released

**v0.2.1** (tag on 593dff3, full CI tier green): Sub0Squad pins it.

| part | state | evidence |
|---|---|---|
| canvas: revisions, transactions, rebase, revert, the reply parser | built, tested (`test_edits`) | [CANVAS.md](CANVAS.md); proved on real models in Sub0Squad |
| folded views and reads (`outline`) | built, tested | [CANVAS.md](CANVAS.md) |
| tolerant search/replace (`edits`), indentation-preserving | built, tested | [CANVAS.md](CANVAS.md) |
| block index: stable ids through edits, rewrites, renames, splits, merges, moves | built, tested (`test_doc_layers`) | [LAYERED_DOCUMENT.md](LAYERED_DOCUMENT.md) |
| issues layer: `parseIssues`, `layerSummary`, `layerDetail`, `layerInline` (slice 2), `layerFocus`, READ ISSUE, `layered-doc-1` | built, tested | [LAYERED_DOCUMENT.md](LAYERED_DOCUMENT.md); measured on Sub0Squad tier 0 (slice 1, slice 2) |
| the viewer | built, tested (`npm test`, `test_viewer` under ctest) | [README.md](../README.md) |
| CI (family standard), test presets, `.ci/local.json`, pre-push gate | built | [AGENTS.md](../AGENTS.md) |
| `applyReply` (atomic), turn control, the 30 recorded-reply cases, moved from Sub0Squad | built, unreleased (2026-11-18) | `test_replies`, `test_turn_control`; [DECISIONS.md](DECISIONS.md) section 2 |
| requirements catalogue; tests through a driver; the library's contract tested here (views, skeletons, a 1,200-sequence canvas model check, golden replay) | built, unreleased (2026-11-18) | [REQUIREMENTS.md](REQUIREMENTS.md), [TESTING.md](TESTING.md) |

## The last measured result

Slice 2 on Sub0Squad's tier 0 (80 runs, all passed; Sub0Squad `docs/LAYERED_DOCUMENT.md`, "Slice 2 on tier 0"): the issues layer pays only where
the gate's own text clips (more than 6 failures); shown by default with inline detail it regressed three of four cases. Sub0Squad's default is now
adaptive with a threshold of 6. The noise between identical arms reached 67% on one case.

## Designed, not built

**The direction of 2026-11-18** ([DIRECTION.md](DIRECTION.md) sections 3 and 4): the viewer becomes an **authoring tool** and is the first use case to
invest in, proved on this project's own design work ([AUTHORING.md](AUTHORING.md), V0-V5); block discovery becomes language-aware behind a provider
seam, drawing on prior art ([BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md), D0-D3); what is not clear-cut stays an open A/B decision, with policies as data
that a client may supply ([BACKBONE.md](BACKBONE.md) sections 14.3-14.5). **[DECISIONS.md](DECISIONS.md)** (direction section 5) works through
every open question with options, pros and cons: storage layered by kind with shared annotations in git refs per writer, deterministic birth ids,
the Sub0 glue libraries to adopt, Sub0Fabric kept open.

[BACKBONE.md](BACKBONE.md): items with state, tags and links; an event log; subscriptions as inboxes; claims; views; the store; source adapters
(local git, GitHub); a revision-aware index replayed over history; scale ([SCALE.md](SCALE.md)); slices B0-B4, G1-G5, S0-S4,
E1, C1.

**Scale and change views (2026-11-18, later).** Scale has one home, [SCALE.md](SCALE.md), with a second target profile measured on a private
monorepo (440,000 files, most not code; 430 GB of git; about 1,700 pull requests a week): R-SCALE-5 to 9 and amendments A19-A21 follow from it.
[CHANGE_VIEWS.md](CHANGE_VIEWS.md) designs pull requests, spikes and their dependencies as layers (U12, R-CHG, P0-P5), checked against the shape
of a private 25-change reference topic. This public repository never names its private references; a
local-only guard in the owner's clone enforces it (AGENTS.md).

**The synthetic mirror (2026-11-18, built).** `tools/mirror/` generates an invented project with a work-dominated repository's shape, a 31-change
topic and its exact truth, plus an edge-case register of what the canvas does not handle yet ([MIRROR.md](MIRROR.md)); `fixtures/mirror/seed-1/`
is checked by `npm test`. Its gaps are [CHANGE_VIEWS.md](CHANGE_VIEWS.md) section 10. A live forge sandbox is parked (M1).

**Real-repository scale and the viewer's interactivity (2026-11-18, later).** `sub0canvas snapshot` (`app/`) and `tools/serve.mjs` view a local
repository's subtree straight from git. Measured up to 19,000 files ([SCALE.md](SCALE.md) section 6.1): snapshots take seconds, whole bundles grow
as files times turns, and the page breaks first (about 1 s per click at 20,000 blocks). The answer is designed, not built: live views patched by
events ([AUTHORING.md](AUTHORING.md) section 3.1, V1a), interactivity budgets with baselines (section 3.2), and in the backlog K0 (a brief:
[sessions/k0-ui-kpis.md](sessions/k0-ui-kpis.md)), the file tree and content on demand, and a scoped block list.

**Consolidated (2026-11-18, night).** [PLAN.md](PLAN.md) reconciles all of the above: a review that found and fixed ten disagreements between
the day's documents (the review log's location, A2 against A16, section 11, spike ownership, one format revision for B0 and V1a, and others), four
phases with their gates, and **one sheet of thirteen decisions for the owner** (O1-O13). None of the design is confirmed by the owner yet; phase 1
needs none of it except the release (O1).

## Known gaps and debts

* Sub0Squad still carries its own `applyCanvasReply` and turn control; both now live here (`applyReply`, `turn_control.hpp`) and a Sub0Squad PR
  should adopt them after a tag.
* No golden attempt has compiler diagnostics at several places, so the golden replay checks test failures only (R-U1-4; B0 re-captures).
* Measured faults of block discovery: a `{` in a block comment loses every block of the file; Rust/TypeScript generics, arrow functions and Go methods
  are named wrongly or not at all; decorators fall outside their block; YAML, JSON, XML and Markdown have no blocks (BLOCK_DISCOVERY.md section 2; D0).

* The producer (Sub0Squad) does not yet write the optional fields the viewer understands (item `id`, anchor `column`, `fix`, `mergedFrom`, `layerMeta`):
  slice B0.
* `BlockIndex` keeps every file's text and two string sets per block, and scans every retired block on revive: fine for one task, not for history or
  scale (SCALE.md, slices S0-S2).
* The test helper `lib/tests/check.hpp` calls `getenv`, which clang on Windows (MSVC runtime) flags as deprecated under `-Werror`; the Linux Clang
  CI leg is unaffected.
* Turn files' `events` are cumulative today, and every turn repeats every file's text; B0's one format revision fixes both (PLAN.md O5).

## Where things are

* Repository: [github.com/CraigHutchinson/Sub0Canvas](https://github.com/CraigHutchinson/Sub0Canvas) (public; the old lower-case URL redirects).
* The consumer: [Sub0Squad](https://github.com/CraigHutchinson/Sub0Squad) (public); its canvas loop is `src/canvas_agent.cpp`, its measurements
  `docs/LAYERED_DOCUMENT.md`, the A/B data `out/runs/layers-*` in a local checkout (not in git).
* The family CI standard: CraigHutchinson/.github `docs/ci-strategy.md` (a sibling clone `../CraigHutchinson.github` holds its tools).

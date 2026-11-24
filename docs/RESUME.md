# Where Sub0Canvas stands (2026-11-18, evening)

Status: living (2026-11-18, evening). Rewritten at the end of each working session.

Read this first when resuming. The plan, in phases, and the one sheet of decisions waiting on the owner are [PLAN.md](PLAN.md); what is next,
in order, is [BACKLOG.md](BACKLOG.md); a task with a brief is in [sessions/](sessions/README.md).

## Released

**v0.3.0** (tag on 8bf7024, full CI tier green): Sub0Squad pins it (2026-11-23). Adds `applyReply` (atomic) and turn control, moved from Sub0Squad, with `Runaway::detail` and `endsInsideBlock`.

| part | state | evidence |
|---|---|---|
| canvas: revisions, transactions, rebase, revert, the reply parser | built, tested (`test_edits`) | [CANVAS.md](CANVAS.md); proved on real models in Sub0Squad |
| folded views and reads (`outline`) | built, tested | [CANVAS.md](CANVAS.md) |
| tolerant search/replace (`edits`), indentation-preserving | built, tested | [CANVAS.md](CANVAS.md) |
| block index: stable ids through edits, rewrites, renames, splits, merges, moves | built, tested (`test_doc_layers`) | [LAYERED_DOCUMENT.md](LAYERED_DOCUMENT.md) |
| issues layer: `parseIssues`, `layerSummary`, `layerDetail`, `layerInline` (slice 2), `layerFocus`, READ ISSUE, `layered-doc-1` | built, tested | [LAYERED_DOCUMENT.md](LAYERED_DOCUMENT.md); measured on Sub0Squad tier 0 (slice 1, slice 2) |
| the viewer | built, tested (`npm test`, `test_viewer` under ctest) | [README.md](../README.md) |
| CI (family standard), test presets, `.ci/local.json`, pre-push gate | built | [AGENTS.md](../AGENTS.md) |
| `applyReply` (atomic), turn control, the 30 recorded-reply cases, moved from Sub0Squad | built, released in v0.3.0 | `test_replies`, `test_turn_control`; [DECISIONS.md](DECISIONS.md) section 2 |
| the scale harness (S0, in part): the seeded corpus generator and `Sub0Canvas_Bench` (timing local only; 47 deterministic metrics in every tier), a first baseline, the numbers in SCALE.md section 6.0 | built, not yet released | `test_corpus`, `Sub0Canvas_Bench_metrics`, [TESTING.md](TESTING.md) "Performance" |
| the mock squad (MS0): a consumer project fetching the library through CPM, a reference loop and gate, ten hand-written scenarios frozen in `fixtures/mock`, the consumer contract (R-CONS); the version bumped to 0.3.0 | built, not yet released (the next tag carries it) | `test_consumer_mock_squad` (label `full`), [MOCK_SQUAD.md](MOCK_SQUAD.md) |
| G1: the document source seam (`source.hpp`), pure git plumbing parsers, the per-scope `RevisionIndex` (first-parent replay, checkpoints, rename chase, carry), the git source and `sub0canvas history` (`app/`), proved on an invented 200-commit repository against hand-checked truth, live git included | built on its branch (2026-11-24), not yet released | `test_revision_index`, `test/history.test.js`, [sessions/g1-git-history.md](sessions/g1-git-history.md) Outcome |
| claims and notes (B3): numeric-time leases on items, blocks and files (`claims.hpp`), rules as a `ClaimPolicy` the client may supply, concurrent takes settled by merge order, block claims and notes that follow merges and splits, `note` events; pure, `store.cpp` unchanged | built, not yet released; not yet used by a consumer | `test_claims`, 14 claim metrics in `perf/metrics.json` |
| B2 inboxes: `SubscriptionQuery`, `Subscription`, subscribe/unsubscribe/acknowledge as events with their reducers (`registerSubscriptionReducers`), `InboxReader` (index by layer, path, block, kind; cost = events returned), the verdict through an inbox byte-identical to `Store::items` + `layerSummary` on a 5-turn scenario (R-BB-5, library half) | built on its branch (2026-11-24), not yet released; the Sub0Squad A/B over recorded replies not run | `test_inbox`, 10 `inbox` metrics in `perf/metrics.json`, BACKBONE.md section 3.4 "As built (B2)" |
| the store core (B1): event envelope and log seam (`event.hpp`), `Store` with a reducer registry, `putLayer` reconcile, state, tags, links, item data, a query-scoped snapshot; pure; envelope frozen with no `seq`/`type` (BACKBONE A22) | built, not yet released; B1b (2026-11-24) adds `FileLog` (`app/file_log.*`), `Store::checkpoint`/`restore`, `StoreOptions::retainEvents`, the store snapshot in `layeredJson` (opt-in); MS3 runs the mock loop through a store; claims and inboxes: above | `test_store`, `test_file_log`, `test_consumer_contract` (B15, G4), 21 store metrics in `perf/metrics.json` |
| V1a-q, the pure query layer of the live views: `QueryServer::handle(json) -> json` over files at revisions, the index and the store; `tree`, `file` (by content id, `have`), `revisions`, `blocks`, `layers`, `layer` (query, cursor, scan bound), `events`, `info`; bounds and unknown-member handling as policy data; written through one projection shared with `layeredJson` (D11); no transport | built on its branch (2026-11-24), not yet released; the server, transports and the page's windowing are not built | `test_query` (the mock turns read back through the queries equal their snapshots), 32 `query` metrics, [QUERY.md](QUERY.md) |
| requirements catalogue; tests through a driver; the library's contract tested here (views, skeletons, a 1,200-sequence canvas model check, golden replay) | built, released in v0.3.0 | [REQUIREMENTS.md](REQUIREMENTS.md), [TESTING.md](TESTING.md) |

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
phases with their gates, and **one sheet of thirteen decisions for the owner** (O1-O13).

**Decided (2026-11-23).** The owner answered the sheet ([PLAN.md](PLAN.md) section 3.1): O1-O5, O7-O9, O12, O13 confirmed; O2 amended (the storage
seam is proved by the directory store plus git notes and SQLite proofs of concept, gated at build and release); **Sub0Fabric is built now**; the
server, Squad's store and the portal were settled by [the architecture review](reviews/2026-11-23-fabric-portal-store.md) (native HTTP default,
HTTP/2 where tooling supports it; physical per writer, logical per project; the portal joined to the views in three steps). Plan: that review's
waves W0-W7; W0 (documents) is done, W1 (v0.3.0 and the Squad adoption) is next.

## Known gaps and debts

* **The full tier is red on Linux clang** (parked 2026-11-23, [issue #4](https://github.com/CraigHutchinson/Sub0Canvas/issues/4)): five exact `layeredJson` byte metrics and the mock squad differ from gcc and MinGW, so some output depends on the standard library. Fix before the next release tag.
* No golden attempt has compiler diagnostics at several places, so the golden replay checks test failures only (R-U1-4; B0 re-captures).
* Block discovery (D0, 2026-11-23): the seam is built (`blocks.hpp`, providers `braces@3`, `indent@2`) and the faults of block comments, names (generics, arrow functions, Go receivers, operators)
  and Python decorators are fixed. Still open: Rust lifetimes on a signature line, CRLF text in Allman style (a `\r` on every line), nested functions and classes as nodes, unnamed nodes still carry an
  identity, `index.outliners` not in the snapshot (it regenerates the fixtures), and YAML, JSON, XML and Markdown have no blocks (D1-D2; [sessions/d0-block-discovery-seam.md](sessions/d0-block-discovery-seam.md)).

* **B0 steps 1-3 are done here** (MS1, 2026-11-23): the library writes item `id`, anchor `column` and range, `layerMeta`, `revisionId`, `textId`, `policies`, per-turn `events` with `eventsFrom` and texts by id (`SnapshotSpec`), and the viewer reads both shapes. Not done: a real Sub0Squad attempt through it (R-U1-4), the release and the Sub0Squad PR (`%2F` naming included).
* `BlockIndex` keeps every file's text and two string sets per block, and scans every retired block on revive: fine for one task, not for history or
  scale (SCALE.md, slices S0-S2). Measured (S0, SCALE.md section 6.0): 2,613 bytes per block against the 200 target, and 0.7 us per (new block, retired block) in the revive scan.
* The benchmarks run through the family's performance tooling fetched from a Sub0Math commit that is **not pushed yet** (`CMakeLists.txt` pins it with a TODO): until it is, a build needs
  `CPM_Sub0Math_SOURCE`, and the workflows (pinned to a shared-workflows commit that is not pushed either) must not be pushed first.
* Sub0Squad still calls the first-version `layeredJson`, which writes every event from 0 (cumulative, with `eventsFrom` 0) and inline texts; it moves to `SnapshotSpec` in its PR.

## Where things are

* Repository: [github.com/CraigHutchinson/Sub0Canvas](https://github.com/CraigHutchinson/Sub0Canvas) (public; the old lower-case URL redirects).
* The consumer: [Sub0Squad](https://github.com/CraigHutchinson/Sub0Squad) (public); its canvas loop is `src/canvas_agent.cpp`, its measurements
  `docs/LAYERED_DOCUMENT.md`, the A/B data `out/runs/layers-*` in a local checkout (not in git).
* The family CI standard: CraigHutchinson/.github `docs/ci-strategy.md` (a sibling clone `../CraigHutchinson.github` holds its tools).

# Where Sub0Canvas stands (2026-11-25)

Status: living. Rewritten at the end of each working session. What is next is [BACKLOG.md](BACKLOG.md); why things are as they are is [DECISIONS.md](DECISIONS.md); a task with its own brief is in [sessions/](sessions/README.md).

## What exists

Released: **v0.4.0** (the full tier green); Sub0Squad pins v0.3.0 and takes the next tag through a pull request ([INTEGRATION.md](INTEGRATION.md)). `main` is v0.4.0 plus the C++23 adoption (A0-D2) and the cleanup in progress.

| area | design doc |
|---|---|
| canvas: revisions, transactions, rebase, revert, tolerant edits, atomic `applyReply`, turn control, folded views | [CANVAS.md](CANVAS.md) |
| block index (compact and lazy, deterministic birth ids), identity rules, the issues layer, `layered-doc-1` | [LAYERED_DOCUMENT.md](LAYERED_DOCUMENT.md), [SCALE.md](SCALE.md) 6.3 |
| block discovery behind a provider seam (`braces@3`, `indent@2`) | [BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md) 2.1 |
| store core, event log (`FileLog`), inboxes, claims and notes, source anchors and the resolver | [BACKBONE.md](BACKBONE.md) (the "As built" paragraphs of sections 3 to 5) |
| git document source, per-scope `RevisionIndex`, `sub0canvas history`, `sub0canvas snapshot` | [BACKBONE.md](BACKBONE.md) section 6, [SCALE.md](SCALE.md) 6.2 |
| the query layer `query-1` (pure, no transport) | [QUERY.md](QUERY.md) |
| views and sessions: pinned and tip workers, four refresh forms, containment | [VIEWS.md](VIEWS.md) section 8 |
| the viewer (static page, `npm test`) and its interactivity benchmark | [README.md](../README.md), [K0.md](K0.md) |
| the mock squad: a consumer project over the public API, 22 scenarios, the consumer contract | [MOCK_SQUAD.md](MOCK_SQUAD.md) |
| the scale harness (seeded corpus, `Sub0Canvas_Bench`, deterministic metrics in every tier, timing local) | [TESTING.md](TESTING.md), [SCALE.md](SCALE.md) 6.0 |
| the synthetic mirror (an invented project, its truth and edge cases) | [MIRROR.md](MIRROR.md) |
| requirements and the proving ladder | [REQUIREMENTS.md](REQUIREMENTS.md), [TESTING.md](TESTING.md) |

Designed, not built: the authoring tool beyond the snapshot ([AUTHORING.md](AUTHORING.md)), change views ([CHANGE_VIEWS.md](CHANGE_VIEWS.md)), GitHub and the other adapters, the channel and presence ([VIEWS.md](VIEWS.md)), Sub0Fabric's transports.

## Next

The cleanup to one current account is in progress (backlog item 1); then v0.5.0 and the Sub0Squad PR (item 2). Start the next session at [BACKLOG.md](BACKLOG.md).

## Known gaps

* No golden attempt has compiler diagnostics at several places, so the golden replay checks test failures only (R-U1-4); the owner-run re-capture replaces the golden fixtures.
* Heap per block is 319 B against the 200 B target (an expected failure in `perf/metrics.json`); `BlockIndex` has no reader, so checkpoints are not persisted.
* Block discovery: the open faults are listed in [BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md) 2.1; YAML, JSON, XML and Markdown have no blocks.
* The gate cannot judge the rows added since the last re-record, and a confirmed regression makes a push slow ([TESTING.md](TESTING.md), "What a push waits for").
* The hosted full tier has not run on MSVC, macOS or the sanitizers.

## Where things are

Repository: [github.com/CraigHutchinson/Sub0Canvas](https://github.com/CraigHutchinson/Sub0Canvas) (public). Consumer: [Sub0Squad](https://github.com/CraigHutchinson/Sub0Squad), whose canvas loop is `src/canvas_agent.cpp` and whose measurements are its `docs/LAYERED_DOCUMENT.md`. Family CI standard: CraigHutchinson/.github `docs/ci-strategy.md`.

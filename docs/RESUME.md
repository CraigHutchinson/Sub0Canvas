# Where sub0canvas stands (2026-11-18)

Read this first when resuming. What is next, in order, is [BACKLOG.md](BACKLOG.md); a task with a brief is in [sessions/](sessions/README.md).

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

## The last measured result

Slice 2 on Sub0Squad's tier 0 (80 runs, all passed; Sub0Squad `docs/LAYERED_DOCUMENT.md`, "Slice 2 on tier 0"): the issues layer pays only where
the gate's own text clips (more than 6 failures); shown by default with inline detail it regressed three of four cases. Sub0Squad's default is now
adaptive with a threshold of 6. The noise between identical arms reached 67% on one case.

## Designed, not built

[BACKBONE.md](BACKBONE.md): items with state, tags and links; an event log; subscriptions as inboxes; claims; views; the store; source adapters
(local git, GitHub); a revision-aware index replayed over history; the scale design for billions of lines (section 12); slices B0-B4, G1-G5, S0-S4,
E1, C1. **The owner has not yet read it**; its section 11 lists five open questions for them.

## Known gaps and debts

* The producer (Sub0Squad) does not yet write the optional fields the viewer understands (item `id`, anchor `column`, `fix`, `mergedFrom`, `layerMeta`):
  slice B0.
* `BlockIndex` keeps every file's text and two string sets per block, and scans every retired block on revive: fine for one task, not for history or
  scale (BACKBONE.md section 12, slices S0-S2).
* The test helper `lib/tests/check.hpp` calls `getenv`, which clang on Windows (MSVC runtime) flags as deprecated under `-Werror`; the Linux Clang
  CI leg is unaffected.
* Turn files' `events` are cumulative today; BACKBONE.md proposes per-turn events plus a cumulative log (open question 2).

## Where things are

* Repository: [github.com/CraigHutchinson/Sub0Canvas](https://github.com/CraigHutchinson/Sub0Canvas) (public; the old lower-case URL redirects).
* The consumer: [Sub0Squad](https://github.com/CraigHutchinson/Sub0Squad) (public); its canvas loop is `src/canvas_agent.cpp`, its measurements
  `docs/LAYERED_DOCUMENT.md`, the A/B data `out/runs/layers-*` in a local checkout (not in git).
* The family CI standard: CraigHutchinson/.github `docs/ci-strategy.md` (a sibling clone `../CraigHutchinson.github` holds its tools).

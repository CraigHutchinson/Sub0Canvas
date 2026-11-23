# Follow-up session: B0, format catch-up

Status: in progress (written 2026-11-18; steps 1-3 done 2026-11-23, see Outcome). Backlog: "B0: format catch-up" under *Now*. Design: [BACKBONE.md](../BACKBONE.md) sections 7 and 8.1, slice B0.

## Goal

Make the producer (`layeredJson` here, called by Sub0Squad's canvas loop) write the optional fields the viewer already understands, and freeze a
golden set of real turn files that both the C++ tests and the viewer's tests check. No store, no new mechanisms: format only, additive, so every
existing reader keeps working.

## What is known

* The viewer understands, and its demo uses: item `id`, anchor `column`, item `fix`, item `mergedFrom`, a top-level `layerMeta`, block `kind` and
  `parent` ([README](../../README.md), "Optional fields the viewer understands"; the full request list is BACKBONE.md section 7).
* The producer already computes an item `key` (`issues/test/<name>`, `issues/<kind>/<hash>`), which is the `id`. Compilers report columns, which
  `parseIssues` reads past today.
* Turn files' `events` are cumulative today (the index's whole log). BACKBONE.md proposes per-turn `events` with `eventsFrom`, plus a cumulative log.
  That was the owner's open question 2; the recommendation is per turn with `eventsFrom` ([PLAN.md](../PLAN.md) O5): **do not change the meaning of
  `events` until the owner has confirmed it**.
* **This is the one format revision** ([PLAN.md](../PLAN.md) phase 1): V1a's live server and the bundle form need the same shape, so B0 also
  carries revision ids as strings (A1), ranges (A3), the policies that made a file (A13) and **texts by content id** (a `texts` table keyed by
  blob hash, turns referring to ids; SCALE.md 6.1 measured 213 MB for 8 turns of 1,590 files, nearly all repeats). All additive: a reader
  that knows none of it still reads the old fields, which the producer keeps writing until Sub0Squad has moved.
* Revision text files flatten `/` to `_` (`canvas/<flat>.r<N>`), which is ambiguous; BACKBONE.md proposes `%2F`. That file naming is Sub0Squad's
  (`src/canvas_agent.cpp`, `persistCanvas`), so it changes there, in the same integration PR.
* `fixtures/golden/` already holds real turn files and verdicts from Sub0Squad's slice 2 A/B (its README says which runs), and `test/golden.test.js`
  validates them in the viewer.

## Steps

1. Here: `Anchor.column` (and the end of a range, A3); `parseIssues` reads the column; `layeredJson` writes `id`, `column` and a `layerMeta` entry for `issues`. Unit tests in
   `test_doc_layers` (the JSON parses, the fields are there, old fields unchanged).
2. Here: revision ids as strings, `policies`, per-turn `events` with `eventsFrom` (once confirmed) and texts by content id in `layeredJson` and in
   `app/`'s snapshot; the viewer reads both shapes (`npm test`).
3. Here: a C++ test that writes turn files for a fixed scenario and compares them byte-for-byte with files in `fixtures/golden/b0/`; `test/golden.test.js`
   validates the same files. Review the files by hand once.
4. Here: release, the full CI tier green (v0.3.0 may already be out for the moved proving grounds: then this is v0.4.0).
5. Sub0Squad: a pull request bumping the pin (and the `%2F` naming if the owner agrees), its full suite green; open one real attempt's `layers/` in the
   viewer and check that *Trace across turns* follows an issue by `id` (no title matching). Re-capture `fixtures/golden/` from that run if the
   producer's output changed.

## Done means

* The new fields are written and tested on both sides; the golden files match byte-for-byte.
* An 8-turn snapshot of the 1,590-file subtree of SCALE.md 6.1 is a few times the size of one turn, not eight.
* A real Sub0Squad attempt opens in the viewer with items traced by `id`.
* README's "Optional fields" table says which fields the producer now writes.

## Outcome (steps 1-3, 2026-11-23, driven through the mock squad; MS1)

Done: `Anchor::column`, `endLine`, `endColumn` (read by `parseIssues` from `col`, `col-endcol` and a GCC `^~~~` underline; two sites on a line stay two anchors); `SnapshotSpec` and `layeredJson(spec, index)` (the old call delegates) writing item `id`, anchor column and range, `layerMeta`, per file `revisionId` (opaque; `r<n>` by default) and `textId` (git blob id, `contentId`), `policies`, per-turn `events` with `eventsFrom`, and optional texts by id (`texts`, carried once per document); `BlockIndex::eventLogNdjson` (`canvas-events-1`, provisional subset) as the cumulative log; `ApplyOptions::baseRevision`; `sub0canvas snapshot` writes the same (texts by id by default, `--texts inline`). The viewer reads both shapes (`src/model.js`), including `%2F` revision file names. Checks: `test_doc_layers`, `test_canvas_model`, `test/mock.test.js` over the regenerated `fixtures/mock` (15 scenarios), mutation-tested. Sizes: mock g7, 8 turns by id 112,668 B = 2.5x one turn (45,828 B); the same with texts inline 7.5x. `sub0canvas snapshot` of this repository (43 files, 8 commits): 4.89 MB inline, 1.53 MB by id, one turn inline 0.80 MB.

Decisions: `events` now means per-turn (O5); a first-version `layeredJson` call writes all events with `eventsFrom` 0 and the viewer dedupes by position. Texts by id are opt-in in the library (old readers need `text`), default in the app bundle. The golden files of step 3 are the mock attempts, not `fixtures/golden/b0/`.

Not done: step 4 (release, full CI), step 5 (a real Sub0Squad attempt traced by `id`, R-U1-4 re-capture, the Sub0Squad PR with `SnapshotSpec` and `%2F` naming).

## Environment and constraints

See [AGENTS.md](../../AGENTS.md) and [INTEGRATION.md](../INTEGRATION.md) (the Sub0Squad side is a pull request; the pin needs the tag pushed first).

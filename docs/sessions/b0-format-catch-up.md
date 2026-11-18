# Follow-up session: B0, format catch-up

Status: open (written 2026-11-18). Backlog: "B0: format catch-up" under *Now*. Design: [BACKBONE.md](../BACKBONE.md) sections 7 and 8.1, slice B0.

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
  That is the owner's open question 2: **do not change the meaning of `events` until the owner has answered**; add `eventsFrom` only if they agree.
* Revision text files flatten `/` to `_` (`canvas/<flat>.r<N>`), which is ambiguous; BACKBONE.md proposes `%2F`. That file naming is Sub0Squad's
  (`src/canvas_agent.cpp`, `persistCanvas`), so it changes there, in the same integration PR.
* `fixtures/golden/` already holds real turn files and verdicts from Sub0Squad's slice 2 A/B (its README says which runs), and `test/golden.test.js`
  validates them in the viewer.

## Steps

1. Here: `Anchor.column`; `parseIssues` reads the column; `layeredJson` writes `id`, `column` and a `layerMeta` entry for `issues`. Unit tests in
   `test_doc_layers` (the JSON parses, the fields are there, old fields unchanged).
2. Here: a C++ test that writes turn files for a fixed scenario and compares them byte-for-byte with files in `fixtures/golden/b0/`; `test/golden.test.js`
   validates the same files. Review the files by hand once.
3. Here: release (`v0.2.2` or `v0.3.0`), the full CI tier green.
4. Sub0Squad: a pull request bumping the pin (and the `%2F` naming if the owner agrees), its full suite green; open one real attempt's `layers/` in the
   viewer and check that *Trace across turns* follows an issue by `id` (no title matching). Re-capture `fixtures/golden/` from that run if the
   producer's output changed.

## Done means

* The new fields are written and tested on both sides; the golden files match byte-for-byte.
* A real Sub0Squad attempt opens in the viewer with items traced by `id`.
* README's "Optional fields" table says which fields the producer now writes.

## Environment and constraints

See [AGENTS.md](../../AGENTS.md) and [INTEGRATION.md](../INTEGRATION.md) (the Sub0Squad side is a pull request; the pin needs the tag pushed first).

# Follow-up session: D0, the block discovery seam

Status: open (written 2026-11-18). Backlog: "D0: the block discovery seam" under *Now*. Design: [BLOCK_DISCOVERY.md](../BLOCK_DISCOVERY.md) sections
4.1, 4.2, 4.7 and 5; [BACKBONE.md](../BACKBONE.md) amendment A8; requirements R-BLK-1 to R-BLK-8, R-BLK-11, R-BLK-12 in
[REQUIREMENTS.md](../REQUIREMENTS.md).

## Goal

Put block discovery behind one interface before more code (S1, G1) builds on `foldRegions`: an outline result (a tree of nodes with kind, name,
qualified name, spans), a provider per language chosen by detection, and today's heuristics as the first two providers, with **byte-identical**
behaviour on every existing test. Then fix the faults BLOCK_DISCOVERY.md section 2 measured, each with a test. Do not add tree-sitter or any
dependency (that is D3, after the owner's answer to BACKBONE.md question 8), and do not add data-format providers (D2).

## What is known

* `foldRegions(text, bool python, bool all)` (`lib/src/outline.cpp`) is called from `outline.cpp`, `canvas.cpp` (SYMBOL), `doc_layers.cpp` (the
  index, and test-source localisation), and by Sub0Squad 19 times; the language is `isPythonPath` (a `.py` suffix).
* The measured faults (section 2): a `{` in a block comment loses every block of the file; Rust and TypeScript generics and JavaScript arrow functions
  give empty names; a Go method is named `func`; a Python decorator is outside its block.
* Who decides what (section 4.7): structure is derived from the text; identity granularity is the document's policy; folding is the caller's. `all`
  stops being a caller's flag in new code; `foldRegions` stays as a wrapper.
* Tests go through `lib/tests/doc_driver.hpp` and name requirement ids (TESTING.md, "Requirements, not implementations"). `test_golden_replay` must
  stay green unchanged: it proves identities on real attempts are untouched.

## Steps

1. `lib/include/sub0canvas/outline.hpp` (or a new `blocks.hpp`): `OutlineNode`, `Outline`, `OutlineProvider`, `Languages` as in section 4.1-4.2,
   with Doxygen contracts. Detection by extension and filename first (Linguist's order; a shebang for scripts).
2. `braces@1` and `indent@1` providers wrapping today's code exactly; `foldRegions` re-expressed over them; every internal caller passes a path or a
   language. Run every test and the golden replay: byte-identical, or stop and explain.
3. A provider-conformance test (`lib/tests/test_outline.cpp`): R-BLK-1 (total, deterministic: random and truncated inputs), R-BLK-3 (nesting), R-BLK-4
   (an identity-carrying node has a name), R-BLK-11 (the same outline whatever view is asked).
4. Fix the measured faults one at a time inside the heuristic providers, each with a case first seen failing: block comments (R-BLK-5), names for
   generics, arrow functions and Go receivers (R-BLK-4), decorators inside the span (R-BLK-6). Each fix: does it change identities on the golden
   replay? If yes, explain and record it.
5. Record the provider per language in the snapshot (`index.outliners`, section 4.6) as an additive field (R-FMT-2).
6. Update BLOCK_DISCOVERY.md (status, what was built), REQUIREMENTS.md (statuses and checks), RESUME.md and BACKLOG.md.

## Done means

* `ctest --preset quick` green, with `test_golden_replay` unchanged and `test_outline` added; `npm test` green.
* The probe of section 2 rerun gives names for every function in every row, and blocks in the block-comment case.
* Sub0Squad still builds against the branch with `CPM_sub0canvas_SOURCE` (no API it uses removed); a Sub0Squad PR is needed only when it adopts the
  new API.

## Environment and constraints

Windows: MinGW-w64 UCRT GCC with Ninja (AGENTS.md, "Build and test"). The core stays pure (no I/O, no clock). No new dependency. The owner's standing
rules apply (AGENTS.md): commit as you go, confirm before pushing.

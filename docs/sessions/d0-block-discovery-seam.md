# Follow-up session: D0, the block discovery seam

Status: **seam and the block-comment, name and decorator fixes done** (2026-11-23; Outcome at the end); written 2026-11-18. Backlog: "D0: the block discovery seam" under *Now*. Design: [BLOCK_DISCOVERY.md](../BLOCK_DISCOVERY.md) sections
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
* Sub0Squad still builds against the branch with `CPM_Sub0Canvas_SOURCE` (no API it uses removed); a Sub0Squad PR is needed only when it adopts the
  new API.

## Outcome (2026-11-23)

Built: `lib/include/sub0canvas/blocks.hpp`, `lib/src/blocks.cpp` (`OutlineNode`, `Outline`, `OutlineProvider`, `Languages` with detection by file name, extension, then the `#!` interpreter; the providers `braces` and `indent`; `defaultLanguages()`), `lib/src/scan.hpp` (shared line helpers), `foldRegionsOf` and `foldRegionsFor(path, ...)` in `outline.hpp`, `lib/tests/test_outline.cpp`. `foldRegions(text, python, all)` is the compatibility wrapper (Sub0Squad's calls build unchanged); canvas SYMBOL, the block index and test localisation pass a path.

**The seam was byte-identical.** A dump of `foldRegions` (both `all` values), `sectionView` and `foldedView` over every source, doc and fixture file of the repository (about 3 MB of output) was unchanged against the previous library; `test_golden_replay`, the mock fixtures, the viewer outputs and the benchmark metrics were untouched (no fixture was regenerated, no metric accepted).

The faults, one commit each, every case written first and seen failing (`test_outline` before the fix):

| fault | commit | provider | what changed |
|---|---|---|---|
| a brace or signature inside `/* */` (R-BLK-5) | e2633aa | `braces@2` | block comments blanked before the scan, across lines. Note: the section 2 probe (a `{` in a comment, then two functions) did **not** reproduce: depth is relative, so a stray `{` before a function is tolerated. The fault that does reproduce is a `{` or `}` inside a comment within a function: the function never closes, so it and every later function vanish; four such cases failed before the fix. No file of the repository had it, so no output changed |
| names (R-BLK-4) | 09843eb | `braces@3` | generics (Rust, TypeScript), functions bound to a name (arrow functions, function expressions, typed bindings, assigned C++ lambdas), Go receivers (`Parser::Term`, so a method and a free function of one name stay two blocks) and Go type parameters, C++ `operator<` and friends. Over the repository the only output changes are blocks that had an empty name and now have one (28 lines of the dump, about 19 distinct blocks: arrow functions in `src/` and `tools/`, lambdas and operators in the C++; the commit message says 25, this count is the correct one); no block appears or disappears, no span moves; the benchmark metrics hold |
| decorators (R-BLK-6) | 3a1c9b9 | `indent@2` | the `@name` lines directly above a `def` are inside the node (`first`); `FoldRegion::first` stays the signature line (views and SYMBOL unchanged) and `spanFirst` is the whole span, which the block index uses. No Python file of the repository carries a decorator, so no output changed |

Verified: `ctest --preset quick` and `--preset full` (including `test_consumer_mock_squad`) and `npm test` green; `Sub0Canvas_Bench_metrics` passes with every exact value and limit unchanged.

**The first pre-push performance gate blocked the push, and the cost was not the correctness fixes** (2026-11-24). Against `perf/baselines/croglegion.json` the D0 commits made `foldRegions C++ 1e5 lines` +90% (the pair against its hand-written control +93%), `foldRegions Python 1e5 lines` +15% and `index cold build 1e5 lines` +12.5%. An interleaved `compare` of the D0 build against a build of the commit before D0 reproduced it (C++ +92% and +95%, Python +22%, noise 2 to 5%). The cost was in how the seam was written, not in what it computes: `blankBlockComments` took and copied the whole `vector<string>` of lines and walked every character of every text, `outline` made a `std::string` copy of the text before splitting it through an `istringstream`, `parameterParen` built a `std::string` for the name before each character of a signature line, `functionLike` built a string per line it looked at and `indent@2` a `substr` per non-blank line. The fix (commit de942ec, one change, three passes were not needed): the providers read `string_view` lines of the text (`scan::splitViews`, one `memchr` per line), the block-comment pass runs only on a text that contains `/*` at all and works on one copy of the buffer with every offset kept, the name helpers take views, and the fold regions take the names by move. Output is unchanged: `foldRegions` (both `all` values), the whole outline (kind, names, spans, signature, `partial`), `sectionView` and `foldedView` at two budgets, over the repository and two sibling sources (9.2 MB), are byte-identical between the D0 library and this one; `ctest --preset full`, `npm test` and the exact metrics are unchanged. Measured with `compare` (interleaved, on mains): against the D0 build C++ -66% and Python -65%; against the build before D0 C++ -34% and Python -57%, so the seam, the comment-aware scan, the names and the decorators now cost less than the code they replaced; the quick-level check against the committed baseline passes (0 regressed, 4 improved, 13 ok, 0 inconclusive; `index cold build` within tolerance). What the correctness fixes still cost is **not measured by the benchmark**: its generated corpus has no `/*` (so the blanking copy is never paid there). A text with a block comment pays one extra copy of itself and one more linear pass; a probe of that case belongs in the next benchmark revision (S0 follow-up), not in a tolerance.

Not done, and why:

* **`index.outliners` in the snapshot (step 5).** Adding the field changes `layeredJson`, hence every snapshot fixture and the exact byte metrics, and the session was told to keep outputs byte-identical. It is additive (R-FMT-2) and waits for the owner's go-ahead to regenerate `fixtures/demo`, `fixtures/mock` and `perf/metrics.json` together. `outlinersOf(paths)` already reports language and provider name; the provider names carry versions for it.
* **Unnamed nodes still carry an identity** (R-BLK-4, second half): the block index indexes every node, as today. Skipping unnamed nodes is an identity-policy change; check it against the golden replay first.
* **Faults found on the way, not fixed:** a Rust lifetime on a signature line (`fn f<'a>(x: &'a str) {`) opens a "character literal" that never closes, so that function is not found (the probe's `first` is right only by luck); CRLF text keeps a `\r` on every line, which defeats the Allman rule (a signature line followed by a lone `{`), so a CRLF file written in Allman style loses its functions (this checkout's own `lib/src/*.cpp` are CRLF on this machine); multi-line decorators and the doc comments and attributes of C-like text are not yet inside the span; nested functions and classes are still not nodes (flat outlines).
* The R-BLK-4 check is asserted for the probe cases, not for arbitrary text.
* No Sub0Squad build was run in this session: `outline.hpp` gained declarations and `FoldRegion` a trailing member; nothing Sub0Squad uses was removed.

## Environment and constraints

Windows: MinGW-w64 UCRT GCC with Ninja (AGENTS.md, "Build and test"). The core stays pure (no I/O, no clock). No new dependency. The owner's standing
rules apply (AGENTS.md): commit as you go, confirm before pushing.

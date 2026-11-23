# Follow-up session: S0, the scale harness

Status: in progress (written 2026-11-18; step 1's generator, without change sets or branches, and step 3's benchmarks at 10^5 lines, done 2026-11-23: see Outcome so far). Backlog: "S0: the scale harness" under *Now*. Design: [SCALE.md](../SCALE.md) and [BACKBONE.md](../BACKBONE.md) slice S0;
[USE_CASES.md](../USE_CASES.md) U4; [TESTING.md](../TESTING.md) rung L3.

## Goal

Measure today's block index against the scale target (a codebase of billions of lines) **before** the store (B1) and the revision index (G1) are
built on it. Build a deterministic synthetic corpus generator that is also the truth for block identity, and benchmarks that report the budgets of
SCALE.md section 6. Do not optimise the index in this session: S1 does that, from these numbers.

## What is known

* `BlockIndex` (`lib/src/doc_layers.cpp`) keeps every indexed path's full text (`texts_`), two `std::set<std::string>` per block (`tokens`,
  `literals`), and scans every retired block when an unmatched block looks for a revival (`for (const auto& [id, r] : retired_)`). Reconciliation
  within a file compares unmatched blocks pairwise by token Jaccard. `foldRegions` (`lib/src/outline.cpp`) is a brace/indent heuristic.
* The budgets to measure (first guesses, to be replaced): cold indexing at least 1 million lines per second per core; an incremental update
  proportional to changed files; a warm anchor resolution under 10 ms; under about 200 bytes per compact block; memory proportional to paths touched.
* Performance is measured locally, never in CI: benchmarks carry the ctest label `timing`, print `SKIP timing: <reason>` on battery, and are
  compared against a baseline recorded on mains (CraigHutchinson/.github `docs/ci-strategy.md`, "Power state").

## Steps

1. **The generator** (`bench/corpus.hpp`, `bench/corpus.cpp`, pure C++, seeded): a directory tree of files in C++ and Python with functions whose
   sizes and token distributions are realistic (state the distributions and why); a commit stream that applies edits, signature changes, renames,
   splits, merges, deletions, revivals and cross-file moves at a configurable mix, and records for each what the right identity outcome is (the truth).
   It streams: a 10^9-line project is never held or written whole. Besides code it generates SCALE.md's profile B mix: non-code files (sidecars,
   binaries, serialised data), wide branching, and **change sets** with known lineage (a spike, carve-outs, a supersession, a stack, a port), the
   truth for CHANGE_VIEWS.md's P0-P2. The change sets may land after the identity truth, in the same generator.
2. **A correctness test at small scale** (`lib/tests/test_corpus.cpp`, label `smoke`): run the index over a few hundred generated commits and
   compare every identity outcome with the generator's truth. Disagreements are findings (either rule or generator), each explained.
3. **Benchmarks** (`bench/bench_canvas.cpp` as built, through the family's sub0perf tooling: timing local only, deterministic metrics in every tier): cold indexing rate; incremental update cost
   per commit against files changed; anchor resolution (`resolve`, `at`); resident memory per block and per line; the revive scan's cost against the
   number of retired blocks. At 10^5, 10^7 and (streamed, sampled) 10^9 lines. Record machine, compiler, power state and build in the output.
4. **Write the numbers** into SCALE.md section 6 (a "Measured" table replacing the guesses) and the findings that decide S1 and S2 (which
   structure dominates memory, where time goes, the size at which the retired scan stops being acceptable).

## Done means

* The generator and `test_corpus` are in `main`; `ctest --preset quick` passes and stays fast.
* `bench_index` runs locally and prints the budgets; its numbers for three sizes are in SCALE.md section 6 with the machine they came from.
* S1's and S2's design choices (compact block layout, sketch size, LSH bands) are stated with the numbers behind them, or a reason why the design
  should change.

## Environment and constraints

Windows 11, MinGW-w64 UCRT GCC (WinLibs), Ninja, CMake 3.24 or later; the presets in `CMakePresets.json`. Measure on mains power, in a quiet
machine state, and say so. The owner's standing rules ([AGENTS.md](../../AGENTS.md)): nothing leaves the machine, ask before downloads or installs,
commit as you go, confirm before pushing.

## Outcome so far (2026-11-23, part 1 of 4: the generator)

Done: the corpus generator (`bench/corpus.hpp`, `bench/corpus.cpp`, library target `sub0canvas_bench_corpus`, built when `SUB0CANVAS_BUILD_TESTING` or the new
`SUB0CANVAS_BUILD_BENCHMARKS` is ON; it links nothing, and the benchmark executable is not added yet) and `lib/tests/test_corpus.cpp` (label `smoke`, under a
second). Not done: step 2's comparison of the index with the identity truth, step 3's benchmarks, step 4's numbers, and from step 1 the **change sets** with known
lineage (spike, carve-outs, supersession, stack, port) and **wide branching**, which the brief allows to land later.

* **API.** `generateCorpus(CorpusSpec{seed, targetLines, KindMix})` returns `std::expected<Corpus, std::string>`: `files` (path to text), `models` (path to
  `CodeFile`, the block truth: one `Function` per block, its name, signature, body and a generator `uid`) and `stats`. It refuses more than
  `kMaterialiseLimitLines` (10^6) lines. Beyond that, `CorpusStream` walks the entries holding one at a time, and `entry(spec, i)` is a pure function of
  (seed, mix, i), so a 10^9-line project is sampled by index and never held; `meanLinesPerEntry` says how many entries a size takes. `profileA()` is all code;
  `profileB()` is the measured mix (sidecars beside images and serialised assets, about a third of the files; images; code; serialised data; text; Markdown).
  `EditStream(corpus, seed, EditMix)::next()` returns a `Commit` (changed files, text or deleted, and one `EditTruth` per edit) for nine edit kinds (edited,
  resigned, renamed, added, removed, revived, moved, split, merged) plus a new file and a deleted file; one edit per file per commit, so each outcome is
  unambiguous. Revival and moves use byte-identical function text. The truth is the ideal outcome: a move follows the id only when the file losing the function
  is updated first (stated in the header).
* **Determinism.** Integer-only generator (SplitMix64, modulo picks, bucketed distributions): no `std::` distributions, so the bytes do not depend on the
  library. Checked: same seed gives identical files, models and counts, for both profiles; the stream visits the entries a materialised corpus holds.
* **Shape** (seed 1, 10^5 lines, one run): profile A is 262 code files, 4,800 blocks, 2.4 MB, a mean of 395 lines per file and 21 lines per block (30% Python,
  70% C++ with 30% of those headers); profile B is 546 files in 365 entries (181 sidecars, 92 images, 89 serialised, 34 text, 16 Markdown, 134 code files with
  61% of the lines). Generation takes about 25 ms for 10^5 lines at -O2 (unrecorded power state: not a benchmark).
* **What the test proves.** At 10^3, 10^4 and 10^5 lines (profile A), `foldRegions(all)` finds exactly the model's functions, in order, in every file
  (so the blocks are real, brace and indent heuristic included, and the generator's bodies do not confuse it), and a `BlockIndex` fed the corpus holds one live
  block per function. For 300 commits of the edit stream over a 10^4-line corpus, all nine kinds occur, two streams from one seed agree, the model's text equals
  the commits applied, and a `BlockIndex` fed the commits ends with one live block per live function. **Not proved**: that the index assigns the truth's
  identity to each edit; that is step 2.
* **Generator faults found while building it**: a Python block whose body was only comments (invalid, and a hollow block); fixed by opening every block with a
  real statement. Class bodies are not generated (Python methods and C++ `Klass::method` definitions are flat; nested classes would test `foldRegions` rather than
  the index), and bodies are statement soup that compiles nowhere: they exist for token distributions and block structure only.

Remaining for this session: steps 2-4 as written, then the change sets and branching on top of this generator.

## Outcome so far, part 2 of 4 (2026-11-23: the benchmarks, adopted on the family's performance tooling)

Done: `bench/bench_canvas.cpp`, the executable `Sub0Canvas_Bench` (`sub0math_add_benchmark`, [TESTING.md](../TESTING.md) "Performance"), 17 timing rows and 47 deterministic metrics
(`perf/metrics.json`), the first baseline (`perf/baselines/dev-host-1.json`, a steady full run on mains), and the numbers in [SCALE.md](../SCALE.md) section 6.0. The brief's name
`bench_index` and its `timing` ctest label for the benchmarks changed: the tooling runs timing itself (`Sub0Canvas_Bench_check`, a `timing`-labelled ctest test wraps it for the
`timing` preset), and the metrics and a smoke run are `smoke` tests in every tier. Not done: 10^7 and (streamed, sampled) 10^9 lines, profile B, the change sets, step 2's comparison of
the index with the identity truth, and step 4's decision of S1's structure choices (sketch size, LSH bands): the numbers so far say only what follows.

* **What decides S1.** The block costs 2,613 bytes against the 200 target (13 times; 85 allocations per block; two `std::set<std::string>` and a held copy of the text dominate);
  an incremental update costs about 2.5 times the cold cost per file touched; the revive scan is linear in retired blocks (0.7 us per pair), which is a wall at history scale. The
  cold build (1.7 M lines per second) and warm resolution (3.5 us) are inside their first guesses. foldRegions is 11.7 times a hand-written brace scan on C++ and 5.1 times on
  Python, and together about 14% of the cold build.
* **The writer's bytes.** Eight turns written the way Sub0Squad does today (cumulative events, inline texts) are 6.2 MB; per-turn events with texts by id are 2.1 MB (34%).
* **Faults found.** MinGW's `libstdc++` DLL and a replaced `operator new` in the executable corrupt the heap (0xC0000374): the benchmark links the runtime statically on MinGW.
  Allocation counts differ by up to 2x between GCC's standard library and MSVC's, so their ceilings are wide (25%) and exact only for what is machine-independent.
  The first incremental row was 92% noisy (commit sizes are heavy-tailed): it now times whole laps of a 100-commit stream, which brought it to 1-7%.
* **Controls.** Two hand-written scans (brace and `def`) of the corpus text are the sub0perf environment controls and the pair references for `foldRegions`.


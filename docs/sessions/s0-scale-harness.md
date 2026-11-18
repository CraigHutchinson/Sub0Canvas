# Follow-up session: S0, the scale harness

Status: open (written 2026-11-18). Backlog: "S0: the scale harness" under *Now*. Design: [BACKBONE.md](../BACKBONE.md) section 12 and slice S0;
[USE_CASES.md](../USE_CASES.md) U4; [TESTING.md](../TESTING.md) rung L3.

## Goal

Measure today's block index against the scale target (a codebase of billions of lines) **before** the store (B1) and the revision index (G1) are
built on it. Build a deterministic synthetic corpus generator that is also the truth for block identity, and benchmarks that report the budgets of
BACKBONE.md section 12. Do not optimise the index in this session: S1 does that, from these numbers.

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
   It streams: a 10^9-line project is never held or written whole.
2. **A correctness test at small scale** (`lib/tests/test_corpus.cpp`, label `smoke`): run the index over a few hundred generated commits and
   compare every identity outcome with the generator's truth. Disagreements are findings (either rule or generator), each explained.
3. **Benchmarks** (`bench/bench_index.cpp`, ctest label `timing`, excluded from the quick and CI presets): cold indexing rate; incremental update cost
   per commit against files changed; anchor resolution (`resolve`, `at`); resident memory per block and per line; the revive scan's cost against the
   number of retired blocks. At 10^5, 10^7 and (streamed, sampled) 10^9 lines. Record machine, compiler, power state and build in the output.
4. **Write the numbers** into BACKBONE.md section 12 (a "Measured" table replacing the guesses) and the findings that decide S1 and S2 (which
   structure dominates memory, where time goes, the size at which the retired scan stops being acceptable).

## Done means

* The generator and `test_corpus` are in `main`; `ctest --preset quick` passes and stays fast.
* `bench_index` runs locally and prints the budgets; its numbers for three sizes are in BACKBONE.md section 12 with the machine they came from.
* S1's and S2's design choices (compact block layout, sketch size, LSH bands) are stated with the numbers behind them, or a reason why the design
  should change.

## Environment and constraints

Windows 11, MinGW-w64 UCRT GCC (WinLibs), Ninja, CMake 3.24 or later; the presets in `CMakePresets.json`. Measure on mains power, in a quiet
machine state, and say so. The owner's standing rules ([AGENTS.md](../../AGENTS.md)): nothing leaves the machine, ask before downloads or installs,
commit as you go, confirm before pushing.

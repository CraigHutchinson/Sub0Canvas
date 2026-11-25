# Follow-up session: S0, the scale harness (what is left)

Backlog: item 10. Design: [SCALE.md](../SCALE.md) and [BACKBONE.md](../BACKBONE.md) slice S0; [USE_CASES.md](../USE_CASES.md) U4; [TESTING.md](../TESTING.md) rung L3 (what is built: the generator, `Sub0Canvas_Bench` at 10^5 lines, the numbers in SCALE.md 6.0).

## Goal

Measure the block index against the scale target (a codebase of billions of lines) from a deterministic synthetic corpus that is also the truth for block identity, and report the budgets of SCALE.md section 6. Do not optimise the index here: S1 and S2 do that, from these numbers.

## Left

* The sizes: 10^7 lines and, streamed and sampled, 10^9 (`CorpusStream` and `entry(spec, i)` exist; the benchmark does not use them yet).
* Profile B in the benchmarks (the mix exists as `profileB()`), and **change sets** with known lineage (a spike, carve-outs, a supersession, a stack, a port) plus wide branching: the truth for CHANGE_VIEWS.md's P0-P2.
* Step 2 of the original brief: compare the identity the index assigns to each edit of the edit stream with the generator's truth; every disagreement is a finding (a rule or the generator), each explained.
* State S1's and S2's design choices (sketch size, LSH bands, the size at which the retired scan stops being acceptable) with the numbers behind them, or the reason the design should change.

## Done means

* `bench_canvas` prints the budgets of SCALE.md section 6 for three sizes (10^5, 10^7, streamed 10^9), recorded in SCALE.md with the machine they came from.
* `test_corpus` includes the index-against-truth comparison and `ctest --preset quick` stays fast.

## Environment and constraints

Windows 11, MinGW-w64 UCRT GCC and Ninja; the presets in `CMakePresets.json`. Measure on mains power in a quiet machine state and say so; timing is local only. The owner's standing rules ([AGENTS.md](../../AGENTS.md)) apply.

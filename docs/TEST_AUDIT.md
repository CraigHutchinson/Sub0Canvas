# Test and gate audit (static pass, 2026-11-24)

Goal: a push is quick, and the long work sits in the release tier (`CraigHutchinson/.github` `cpp-full.yml`, the tag gate).
This pass is read-only: it reads `CMakeLists.txt`, `.githooks/pre-push`, `perf/baselines/croglegion.json`, `sub0perf`'s documented levels and ctest's cost
data. It ran nothing, because a gate was measuring. Measured runtimes (step 1 below) are still to do on a quiet machine.

## What runs where today

| Tier | What | Evidence |
|---|---|---|
| pre-push (quick) | build, `ctest --preset quick` (every test labelled `smoke`: 22 C++ and node tests, `Sub0Canvas_Bench_metrics`, `Sub0Canvas_Bench_smoke`), then `bench_check.py gate` at the quick level | `.githooks/pre-push`, `CMakePresets.json` |
| CI smoke (`ci.yml`) | the same quick ctest, Linux, no timing | `ci.yml` |
| Full (`ci-full.yml`, tags) | quick plus `test_consumer_mock_squad` (label `full`), more compilers, Windows | `CMakeLists.txt` line 213 |
| Local only | `Sub0Canvas_Bench_timing`, `Sub0Canvas_K0_timing` (label `timing`) | `CMakeLists.txt` |

## Findings

1. **The unit tests are not the cost.** ctest's cost data shows every smoke test in milliseconds (the slowest, `test_viewer`, about 70 ms). The test tier needs
   no trimming; the build and the perf gate are what a push waits for.
2. **The perf gate is.** `sub0perf`'s quick level has a 90 s budget (`quick_budget` in `.git/sub0perf/config.json`), but a regression is "still confirmed in full
   even if that overruns the budget" (4 more rounds), and every round of a 1e5-line row rebuilds its corpus. The heavy rows in the baseline are history replay
   (485 ms per iteration), claims replay 1e5 (278 ms), store replay 1e5 (104 ms), revive scan 1e5 (79 ms) and the cold index builds. A push that trips a
   confirmation therefore takes tens of minutes. That is the 45+ minute push seen on 2026-11-24.
3. **Coverage gap, the reverse problem.** The baseline has 32 rows and 2 pairs. The S1 `query` rows and the other rows added since the last re-record are not
   in it, so the gate cannot judge them ("inconclusive": 3 on the last run). They protect nothing until a deliberate full steady re-baseline.
4. **One family of rows gates three things.** `layeredJson`, `snapshotJson` and the events page share the JSON writer; a regression there shows in all, each
   paying a confirmation. One representative row per writer path is enough for the pre-push level.
5. **Deterministic metrics already do the cheap job.** `perf/metrics.json` (about 150 rows: bytes, counts, heap per block) gates in every tier, without timing.
   These catch format and size drift in under a second and need no change.

## Proposed split

| Move | Rows or tests | Why |
|---|---|---|
| Keep in pre-push | all `smoke` C++ and node tests; metrics; the 2 controls; one row per layer under about 20 ms: `applyReply 10 ops`, `store emit 1e4`, `layeredJson 1 turn 2e4`, `foldRegions C++ 1e5`, `index warm resolve`, `inbox read warm`, `claims lookup 1e3 at 1e3` | cheap, wide, each guards a different layer |
| Move to the release tier (`full` timing, run at tags and `milestone/*`) | `history replay` (all three), `claims replay 1e5`, `store replay 1e5`, `revive scan 1e5`, `inbox cold read 1e5`, `index cold build 1e5`, `layeredJson 8 turns` | seconds per round; their regressions show in the cheaper sibling rows first |
| Add (small, fast) | a 1e3-line variant of the cold index, replay and revive rows; a `query events page` and `query blocks` row at 2e3 lines | catches the same regressions in milliseconds instead of rebuilding 1e5 lines |
| Add (test) | a `test_gate_rows` check that every timing row names its tier and that the pre-push set stays under a stated predicted cost | keeps the split from drifting |

Rule: nothing is demoted without a sibling that catches the same regression at lower cost, and every demoted row still runs in the full tier.

## What needs `sub0perf` (a Sub0Math change, to raise there, not build here)

- A per-row tier declaration (`quick` or `full`) in the benchmark metadata, so the split is data in this repository, not a `--filter` in a hook.
- A corpus cache across rounds (the corpus is deterministic), so a confirmation round does not rebuild 1e5 lines.
- A cap on confirmation wall-clock for the quick level, with the rest deferred to the full tier and reported.
- Parallel rows: [Sub0Math issue 8](https://github.com/CraigHutchinson/Sub0Math/issues/8).

## Steps

1. On a quiet machine, record the measured runtime of each ctest test and each benchmark row (`ctest --preset quick --output-junit`, `bench_check.py metrics` and one
   quick `auto --report-only` with the report) and replace the estimates above.
2. Add the small variant rows and the tier test; re-baseline them only in a deliberate full steady run, with the `accepted` log entry.
3. Raise the three `sub0perf` items on Sub0Math (the first and second are new; the third overlaps issue 6).
4. Move the heavy rows to the full tier once step 3's tier declaration exists; until then they stay and only the confirmation cost is the problem.

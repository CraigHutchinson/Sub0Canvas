# Testing: the proving ladder

Status: in force (2026-11-23). L0, L1, L4a and L4 run today; L2 has its first fixture (the synthetic mirror) and no scenario tests yet; L3 waits for S0.

Every change climbs the ladder as far as its kind requires (the table at the end), **in this repository first**, before it is integrated into
Sub0Squad ([INTEGRATION.md](INTEGRATION.md)). A rung that is skipped is said so in the commit or pull request, never skipped silently.

| rung | what | where | runs | label |
|---|---|---|---|---|
| **L0 unit** | the library's rules, one behaviour per block of `CHECK`s, each naming its requirement (REQUIREMENTS.md): identity rules, transactions, folding, parsing, rendering; and properties against an independent model (`test_canvas_model`: 1,200 random transaction sequences) | `lib/tests/test_*.cpp` (C++), `test/*.test.js` (viewer, under ctest as `test_viewer`) | every change; CI smoke | `smoke` |
| **L1 golden** | real producer output, frozen: Sub0Squad turn files and gate verdicts, validated by the viewer and **replayed through the library** (`test_golden_replay`: same blocks, identities one to one, same history, same issues); every recorded reply shape from real runs with its intended outcome (`test_replies`) | `fixtures/golden/`, `fixtures/replies/` | every change; CI smoke | `smoke` |
| **L2 use case** | a scenario a consumer depends on, with **hand-checked truth**: a git history's block identities, PR review comments landing on blocks, a recorded squad workflow replayed through the store, a scripted authoring session (actors, commands, expected views; AUTHORING.md section 6); a language's conformance corpus (BLOCK_DISCOVERY.md, D1) | `fixtures/scenarios/<name>/` (inputs and `truth.json`), `fixtures/languages/<language>/`, a test per scenario | every change to what it covers; CI full when long | `smoke` or `full` |
| **L3 scale** | the budgets of SCALE.md section 6 on a synthetic corpus (10^5 to 10^9 lines) and, locally, on large public repositories | `bench/` (S0) | locally before a release, and for any change to index internals; **never in CI** (shared runners are too noisy) | `timing` (skips on battery) |
| **L4a mock consumer** | the library as a project the squad consumes: a separate CMake project (`consumer/mock_squad/`) that fetches it through CPM, a reference loop on the public API, a gate that reacts to the files, and hand-written scenarios (`fixtures/scenarios/squad`) with their persisted attempts (`fixtures/mock`); MOCK_SQUAD.md | `test_consumer_mock_squad` (`ctest --build-and-test`), `test_consumer_contract` | every change to what the squad calls or reads; before a Sub0Squad pull request exists | `full` |
| **L4 consumer** | the change inside Sub0Squad: its tests, and a tier 0 A/B when what a model sees changes | a pull request in Sub0Squad | each integration iteration | (Sub0Squad's own) |

## Requirements, not implementations

Tests state what consumers rely on, so that the architecture can move under them (owner, 2026-11-18). The catalogue is
[REQUIREMENTS.md](REQUIREMENTS.md); every test block names the ids it checks.

* **Assert what a consumer can observe**: an identity kept or lost, an anchor on the right function, a refusal that names the edit, a rendering a
  model reads. Do not assert how it is stored: no container sizes of internals, no event counts that are logging policy rather than behaviour, no
  spelling of an identity.
* **Go through the driver where the architecture is expected to move.** `lib/tests/doc_driver.hpp` is the one place that knows today's types. It
  finds blocks by name, compares identities without parsing them, reports history with the format's carry names, and builds items by field name (so a
  new field never breaks a test). When B1's store or S1's index arrives, the driver is rewritten and the requirement tests stay.
* **Compare against independent truth, never against the code's own output.** A model written from the requirement (`test_canvas_model`), a real
  producer's recorded output (`test_golden_replay`, which maps identities one to one instead of matching their spelling), or hand-checked truth (L2).
* **Pinned text is deliberate.** Renderings a model reads (R-REN) are pinned exactly, because their wording is the requirement and is measured in
  Sub0Squad. Everything else is checked by meaning.
* **A test that has never failed proves little.** When a check is new, break its input once and see it fail (the golden replay was checked so:
  a moved block, a changed carry and a misplaced anchor in a copy of the fixtures each fail it).
* **The library's contract is tested here.** A behaviour Sub0Squad relies on is checked in this repository, not only in Sub0Squad's tests
  (REQUIREMENTS.md, "Sub0Squad's use, mapped", lists what is still checked only there).

## Rules

1. **Truth is hand-checked, then frozen.** A scenario's `truth.json` is written by a person (or checked line by line by one), with a note of how. A
   test compares against it; regenerating truth from the code under test is never allowed.
2. **No network in a test.** GitHub and git data are captured to files once (`gh api ... > fixtures/scenarios/<name>/pulls.json`, `git log` output, a
   bundle of a small repository) and tests read the files. Capturing needs the owner's go-ahead for anything not already local; the GitHub adapter's
   live mode is exercised by hand, not by ctest.
3. **Deterministic.** Generators take a seed; outputs are byte-compared; no clock reads (time is passed in, BACKBONE.md section 4).
4. **Timing only locally; deterministic metrics everywhere.** Wall-clock numbers are measured locally on mains power and never in CI; a count or a size that does not
   depend on the machine is an ordinary test that gates in every tier ([Performance](#performance), below).
5. **Long suites are labelled `full`; wall-clock runs `timing`.** `ctest --preset quick` excludes `full` and `timing`, `ctest --preset full` runs everything but `timing`,
   `ctest --preset timing` runs only the timing run; the pre-push gate runs quick.
6. **Every claim in a design doc that a test can check is checked by one** (a budget, a recall, a byte-identical rendering).

## Running

```bash
cmake --preset default -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++   # CPM_SOURCE_CACHE set to share a cache
cmake --build --preset default
ctest --preset quick          # L0 and L1 (what the pre-push gate and CI smoke run)
ctest --preset full           # everything except the timing run
ctest --preset timing         # the local timing run (Sub0Canvas_Bench_check); mains power, quiet machine
npm test                      # the viewer's tests alone
python ../CraigHutchinson.github/tools/ci/local_ci.py --level quick   # the declared local checks (.ci/local.json), all local compilers
```

## The golden fixtures (L1)

`fixtures/golden/` holds producer output captured from real Sub0Squad runs (README there says which run and build). Two tests read it:
`test/golden.test.js` (the viewer loads and validates every turn file) and `test_golden_replay` (the library, fed each turn's texts and the gate's
lines, agrees with what the producer recorded). B0 adds byte-for-byte writer checks; the next capture should include an attempt with compiler
diagnostics at several places, which no golden attempt has yet (R-U1-4). When the producer's
output changes on purpose, the golden files are re-captured from a real run, and the diff is reviewed as part of the change.

## The synthetic mirror (L2)

`fixtures/mirror/seed-1/` and its generator ([MIRROR.md](MIRROR.md)): an invented project with a topic of changes and their exact truth,
including real-world edge cases the canvas does not handle yet. `test/mirror.test.js` keeps the fixture equal to its generator. Change views
(P0-P2) and the git adapter (G1) are checked against it.

## The mock squad (L4a)

`consumer/mock_squad/` ([MOCK_SQUAD.md](MOCK_SQUAD.md)) is a CMake project of its own: it fetches this checkout through CPM, as Sub0Squad's
`cmake/dependencies.cmake` does, links only `sub0canvas::sub0canvas` and compiles with its own `-Werror`, so it sees public headers and nothing else. The
top-level build runs it as `test_consumer_mock_squad` (`ctest --build-and-test`, label `full`: the first run builds the library a second time, about
30 s; later runs about 1 s). Its scripted model sends the replies of `fixtures/scenarios/squad/*.squad` through a reference loop and a gate that is a function
of the files; `test_consumer_contract` compares the outcome of each reply, the final files, the refusal texts and the identities with what the scenario
says, and the persisted attempt with `fixtures/mock/<name>/`. Those expectations are written by hand from the requirement and the T0 case each scenario
cites; a regenerated attempt proves only the writer's bytes. The proof that the scenarios are real is a mutation: break one library rule (make
`applyReply` non-atomic, accept a stale conflict, stop carrying identity through a rename) and a scenario goes red. Do it again when a scenario changes.

## The synthetic corpus (L3, S0)

A generator (`bench/corpus.hpp`, built; its test is `test_corpus`) produces a project of any size on the fly from a seed: files in a directory tree, functions with realistic
token distributions, and a commit stream with edits, signature changes, renames, splits, merges, deletions, revivals and cross-file moves **at known places**, so the
generator is also the truth for identity at scale. Nothing at 10^9 lines is written to disk whole: a benchmark streams it.

## Performance

The benchmarks are `bench/bench_canvas.cpp`, the executable `Sub0Canvas_Bench`, built with `SUB0CANVAS_BUILD_BENCHMARKS` (on at the top level) through the family's performance
tooling (sub0perf, in the Sub0Math repository, fetched by CPM for its tooling only: a placeholder pin until it is pushed, `CPM_Sub0Math_SOURCE` on the dev machine). Library only,
on the seeded corpus; the numbers are in [SCALE.md](SCALE.md) section 6. Two kinds of result, kept apart (the family rule):

| kind | what | where it runs |
|---|---|---|
| **deterministic metrics** | counts and sizes: blocks found against the generator's truth, events, bytes `layeredJson` writes (per turn against cumulative, texts by id on and off), live heap bytes and allocations (a counting `operator new` in the executable), regions found | every tier, as the ctest tests `Sub0Canvas_Bench_metrics` (judged against `perf/metrics.json`) and `Sub0Canvas_Bench_smoke` (every workload once), label `smoke`; about 11 s |
| **timing** | the benchmarks' wall-clock times, interleaved and calibrated, against `perf/baselines/<host>.json` | local only: the pre-push gate, the full level of `.ci/local.json` (a `perf` step marked `timing`, untimed on battery), and a developer's own runs |

Metric kinds in `perf/metrics.json`: `exact` for what must not move (counts, bytes written), `max` for a ceiling. Allocation counts differ between standard libraries (GCC's and
MSVC's differ by up to 2x here), so their ceilings are the larger of the two plus 25%, heap bytes plus 10%: they catch a blow-up, not a drift; the timing rows catch the drift.
The `max` metric `index cold 1e5: live heap bytes per block` is an **xfail** against SCALE.md's 200 bytes until S1; when it starts to pass the check says so and the xfail goes.

```bash
export SUB0PERF_OUTPUT=brief                           # one verdict line per command (the improve loop is Sub0Math docs/PERF_LOOP.md)
ctest --preset quick                                    # includes the metrics and the smoke run
cmake --build build/default --target Sub0Canvas_Bench_check        # timing: interleaved rounds, judged against this host's baseline (mains power only)
cmake --build build/default --target Sub0Canvas_Bench_compare      # A/B against the previous build, after a change
SUB0MATH_PERF_REASON="why" cmake --build build/default --target Sub0Canvas_Bench_metrics_accept   # a deliberate change of a metric (logged in perf/metrics.json)
```

The baseline file is named by `SUB0MATH_PERF_HOST` (an environment variable or `-D`): this repository is public, so the committed baseline uses the alias `dev-host-1`, and a
machine that measures sets that variable to the alias (or adds its own alias file). The pre-push gate measures only after `cmake -DSUB0MATH_PERF_GIT_HOOKS=ON` (once per clone) has
recorded its configuration; the build leaves it off. A change to a deterministic fact (a metric) is made deliberately with the `_metrics_accept` target and a reason.

## Which rungs a change needs

| change | L0 | L1 | L2 | L3 | L4 |
|---|---|---|---|---|---|
| a rendering a model sees (`layerSummary`, `layerInline`, folded views) | yes | yes | if a scenario covers it | no | tests, and a tier 0 A/B (5 runs per arm, an off arm) |
| identity rules or thresholds | yes | yes (events byte-identical, or the diff reviewed) | yes (git history scenarios) | yes | tests; tier 0 A/B if issues move |
| index internals (storage, caching, sketches) | yes | yes (byte-identical) | yes | **yes** | tests |
| the format (`layered-doc-1`) | yes | yes (both sides) | no | no | tests; the viewer reads the new fields |
| the store, events, subscriptions, claims (B1-B3) | yes | yes | yes (a recorded workflow) | when sharding (S4) | tests, then the squad's use (BACKBONE.md section 8) |
| an adapter (git, GitHub) | yes | no | **yes** (captured data, hand-checked truth) | for history replay | none until the squad consumes it |
| the viewer | `npm test` | yes | no | no | no |

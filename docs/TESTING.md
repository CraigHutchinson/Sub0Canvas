# Testing: the proving ladder

Status: in force (2026-11-25). L0, L1, L2 (the git fixture and the mirror), L3 at 10^5 lines, L4a and L4 run today.

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

## The git fixture (L2, G1)

`tools/git-fixture.mjs` builds an invented 200-commit repository from a plan (`fixtures/git/tinycalc`: the captured output of `git log`, `diff-tree`, `ls-tree`, `cat-file --batch`, a shallow clone's log; regenerated byte for
byte by `test/history.test.js`) and `fixtures/scenarios/g1-tinycalc/truth.json` holds the hand-checked identity history of five functions in seven scenarios. `test_revision_index` runs the library over the captured files
through a test `DocumentSource` whose pathspec emulation is itself compared with git's own path-limited logs, and over a `Canvas` as a second source; `test/history.test.js` runs live git (a repository it builds, a shallow and a
partial clone) against the same truth. A new adapter is checked the same way: captured data, hand-checked truth, a second implementation, a mutation.

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

`generateCorpus(CorpusSpec{seed, targetLines, KindMix})` returns the files, the block truth (one `Function` per block, with a generator `uid`) and statistics, and refuses more than 10^6 lines; beyond that `CorpusStream` and the pure `entry(spec, i)` sample a project
by index without holding it. `profileA()` is all code (seed 1, 10^5 lines: 262 files, 4,800 blocks, about 21 lines per block, 30% Python, 70% C++ with 30% of those headers); `profileB()` is the measured mix of SCALE.md (546 files in 365 entries: 181 sidecars, 92 images,
89 serialised, 34 text, 16 Markdown, 134 code files carrying 61% of the lines). `EditStream` returns commits with one truth per edit for nine edit kinds (edited, resigned, renamed, added, removed, revived, moved, split, merged) plus a new and a deleted file, one edit
per file per commit so every outcome is unambiguous; revival and moves use byte-identical function text, and a move follows the id only when the file losing the function is updated first. The generator is integer-only (SplitMix64, `bench/rng.hpp`, modulo picks): no `std::`
distribution, so the bytes do not depend on the standard library. Bodies are statement soup that compiles nowhere (they exist for token distributions and block structure); class bodies are not generated. `test_corpus` proves that `foldRegions` finds exactly the model's
functions in every file at 10^3, 10^4 and 10^5 lines and that an index fed 300 commits ends with one live block per live function; **not proved**: that the index assigns the truth's identity to each edit (step 2 of S0). **Left in the scale harness**: 10^7 and streamed 10^9 lines,
profile B in the benchmarks, change sets with known lineage (spike, carve-outs, supersession, stack, port) and wide branching, and the comparison of the index's identities with the truth.

## Performance

The benchmarks are `bench/bench_canvas.cpp`, the executable `Sub0Canvas_Bench`, built with `SUB0CANVAS_BUILD_BENCHMARKS` (on at the top level) through the family's performance
tooling (sub0perf, in the Sub0Perf repository, fetched by CPM for its tooling only at a pinned commit, `CPM_Sub0Perf_SOURCE` on the dev machine). Library only,
on the seeded corpus; the numbers are in [SCALE.md](SCALE.md) section 6. Two hand-written scans of the corpus text (a brace scan and a `def` scan) are the environment controls of the tooling and the pair references for `foldRegions`. Two kinds of result, kept apart (the family rule):

| kind | what | where it runs |
|---|---|---|
| **deterministic metrics** | counts and sizes: blocks found against the generator's truth, events, bytes `layeredJson` writes (per turn against cumulative, texts by id on and off), live heap bytes and allocations (a counting `operator new` in the executable), regions found | every tier, as the ctest tests `Sub0Canvas_Bench_metrics` (judged against `perf/metrics.json`) and `Sub0Canvas_Bench_smoke` (every workload once), label `smoke`; about 11 s |
| **timing** | the benchmarks' wall-clock times, interleaved and calibrated, against `perf/baselines/<host>.json` | local only: the pre-push gate, the full level of `.ci/local.json` (a `perf` step marked `timing`, untimed on battery), and a developer's own runs |

The viewer's interactivity is timed separately by K0 (`node tools/k0/run.mjs`, test `Sub0Canvas_K0_timing`, label `timing`, local only, skipped on battery, a busy machine or
without a browser): [K0.md](K0.md), with the budgets of AUTHORING.md section 3.2 in `perf/k0/budgets.json` and a baseline per host in `perf/k0/<host>.json`. Its unit tests
(`test/k0.test.js`: the judge, the machine rules, the generated bundle, the DevTools client) need no browser and run in `npm test`.

Metric kinds in `perf/metrics.json`: `exact` for what must not move (counts, bytes written), `max` for a ceiling. Allocation counts differ between standard libraries (GCC's and
MSVC's differ by up to 2x here), so their ceilings are the larger of the two plus 25%, heap bytes plus 10%: they catch a blow-up, not a drift; the timing rows catch the drift.
The `max` metric `index cold 1e5: live heap bytes per block` is an **xfail** against SCALE.md's 200 bytes until S1; when it starts to pass the check says so and the xfail goes.

```bash
export SUB0PERF_OUTPUT=brief                           # one verdict line per command (the improve loop is Sub0Perf docs/PERF_LOOP.md)
ctest --preset quick                                    # includes the metrics and the smoke run
cmake --build build/default --target Sub0Canvas_Bench_check        # timing: interleaved rounds, judged against this host's baseline (mains power only)
cmake --build build/default --target Sub0Canvas_Bench_compare      # A/B against the previous build, after a change
SUB0PERF_REASON="why" cmake --build build/default --target Sub0Canvas_Bench_metrics_accept   # a deliberate change of a metric (logged in perf/metrics.json)
```

The baseline file is named by the lower-case host name (`perf/baselines/croglegion.json`; `SUB0PERF_HOST`, an environment variable or `-D`, names another). Where this machine has a committed baseline the configure turns the pre-push gate on and records its configuration (`.git/sub0perf/config.json`), once per clone; with no baseline it stays off. A change to a deterministic fact (a metric) is made deliberately with the `_metrics_accept` target and a reason.

## Traps (read before writing tests, generators or benchmarks)

1. **gcc 14 `-Werror` and temporaries.** `CHECK_EQ` binds references (`auto&& _a = (a)`), so `f()[0]`, `names().front()` and `string_view{temp}` dangle: name the value first. Range-for over simdjson temporaries fails too: the tests use the by-value
   `objectOf`/`arrayOf`. There is no local gcc 14; the hosted smoke run is the check.
2. **Evaluation order is unspecified**: gcc and the Windows toolchains evaluate call arguments right to left, Linux clang left to right. Never put two generator draws, or a read and a write of one variable, in one argument list or operator chain
   ([MOCK_SQUAD.md](MOCK_SQUAD.md) section 8). `tools/check_cxx_rules.cmake` checks the common shapes (per statement), not every shape.
3. **A test that builds temporary git repositories must not inherit the hook's `GIT_DIR`** (`test_viewer` unsets it); otherwise the repository it builds is the clone's own.
4. **A benchmark row times the operation, not the setup.** A row that timed an empty call (`session replay`) stalled the gate for 70 minutes. A new row has no baseline, so the gate cannot judge it until a deliberate re-baseline.
5. **MinGW**: its `libstdc++` DLL and a replaced `operator new` in the executable corrupt the heap (0xC0000374), so the benchmark links the runtime statically there. Allocation counts differ by up to 2x between GCC's standard library and MSVC's, so
   their ceilings are wide and exact only for what is machine-independent. A row over heavy-tailed inputs (commit sizes) times whole laps of a fixed stream (the incremental row went from 92% noisy to 1-7% that way).
6. **Work in your own full clone**, never a linked worktree and never a checkout another session holds (a push from a linked worktree once left the shared checkout with `core.bare=true`); the pre-push gate reads that clone's own `.git/sub0perf`.

## The toolchain floor

gcc 14 (Linux CI, `-Werror`), clang 19 with libstdc++ (Linux CI), MinGW gcc 16 (the owner's machine), clang 22 (Windows). Available on all four: `print`/`println`, `ranges::to`, `enumerate`, `zip`, `to_underlying`, `unreachable`, `expected`,
`move_only_function`. **Not** on gcc 14 or clang 19 with libstdc++ 14: `flat_map`/`flat_set` and `mdspan` (libstdc++ 15 and later), so they stay unused until the floor rises, then guarded by `__cpp_lib_flat_map`; `std::generator` is partial on clang 19.
Hot paths measured by the performance gate (the JSON writers, projection, event and store snapshots, claims, query, view, session, resolver, inbox, the `BlockIndex` update path, the corpus emission order) are not rewritten with views or ranges
without an interleaved A/B on a quiet machine, and the byte-exact writers never; public `std::function` signatures (`Clock`, `Reducer`) stay (`move_only_function` would make `Store` non-copyable for every consumer). The determinism rules are enforced
by `tools/check_cxx_rules.cmake` (AGENTS.md, "Code style"). The MSVC, macOS and sanitizer legs of the full tier have never run: run them once before a release.

## What a push waits for (the gate's cost, static audit of 2026-11-24)

The unit tests are not the cost: every smoke test takes milliseconds (the slowest, `test_viewer`, about 70 ms). A push waits for the build and the performance gate. Sub0Perf's quick level has a 90 s budget, but a regression is confirmed in full even if that
overruns it (4 more rounds), and every round of a 1e5-line row rebuilds its corpus; the heavy rows are history replay (485 ms per iteration), claims replay 1e5 (278 ms), store replay 1e5 (104 ms), revive scan 1e5 (79 ms) and the cold index builds, so a push that
trips a confirmation takes tens of minutes. The other side: the baseline has 32 rows and 2 pairs, and the rows added since the last re-record (the S1 `query` rows and others) are not in it, so the gate cannot judge them (inconclusive) until a deliberate full
steady re-baseline. `layeredJson`, `snapshotJson` and the events page share the JSON writer, so one regression shows in all three and pays three confirmations. The deterministic metrics (`perf/metrics.json`, about 150 rows) already catch format and size drift in
under a second and need no change.

| move | rows or tests | why |
|---|---|---|
| keep in pre-push | all `smoke` tests; the metrics; the 2 controls; one row per layer under about 20 ms: `applyReply 10 ops`, `store emit 1e4`, `layeredJson 1 turn 2e4`, `foldRegions C++ 1e5`, `index warm resolve`, `inbox read warm`, `claims lookup 1e3 at 1e3` | cheap, wide, each guards a different layer |
| move to the release tier | `history replay` (all three), `claims replay 1e5`, `store replay 1e5`, `revive scan 1e5`, `inbox cold read 1e5`, `index cold build 1e5`, `layeredJson 8 turns` | seconds per round; a regression shows in the cheaper sibling rows first |
| add (small, fast) | a 1e3-line variant of the cold index, replay and revive rows; a `query events page` and `query blocks` row at 2e3 lines | the same regressions in milliseconds |
| add (test) | a `test_gate_rows` check that every timing row names its tier and that the pre-push set stays under a stated predicted cost | keeps the split from drifting |

Rule: nothing is demoted without a sibling that catches the same regression at lower cost, and every demoted row still runs in the full tier. What needs Sub0Perf (to raise there, not build here): a per-row tier declaration in the benchmark metadata, so the
split is data and not a `--filter` in a hook; a corpus cache across rounds; a cap on confirmation wall-clock for the quick level, with the rest deferred to the full tier and reported; parallel rows. The open steps are in [BACKLOG.md](BACKLOG.md).

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

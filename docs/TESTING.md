# Testing: the proving ladder

Every change climbs the ladder as far as its kind requires (the table at the end), **in this repository first**, before it is integrated into
Sub0Squad ([INTEGRATION.md](INTEGRATION.md)). A rung that is skipped is said so in the commit or pull request, never skipped silently.

| rung | what | where | runs | label |
|---|---|---|---|---|
| **L0 unit** | the library's rules, one behaviour per block of `CHECK`s: identity rules, transactions, folding, parsing, rendering | `lib/tests/test_*.cpp` (C++), `test/*.test.js` (viewer, under ctest as `test_viewer`) | every change; CI smoke | `smoke` |
| **L1 golden** | real producer output, frozen: Sub0Squad turn files and gate verdicts, parsed and validated on both sides | `fixtures/golden/` | every change; CI smoke | `smoke` |
| **L2 use case** | a scenario a consumer depends on, with **hand-checked truth**: a git history's block identities, PR review comments landing on blocks, a recorded squad workflow replayed through the store | `fixtures/scenarios/<name>/` (inputs and `truth.json`), a test per scenario | every change to what it covers; CI full when long | `smoke` or `full` |
| **L3 scale** | the budgets of BACKBONE.md section 12 on a synthetic corpus (10^5 to 10^9 lines) and, locally, on large public repositories | `bench/` (S0) | locally before a release, and for any change to index internals; **never in CI** (shared runners are too noisy) | `timing` (skips on battery) |
| **L4 consumer** | the change inside Sub0Squad: its tests, and a tier 0 A/B when what a model sees changes | a pull request in Sub0Squad | each integration iteration | (Sub0Squad's own) |

## Rules

1. **Truth is hand-checked, then frozen.** A scenario's `truth.json` is written by a person (or checked line by line by one), with a note of how. A
   test compares against it; regenerating truth from the code under test is never allowed.
2. **No network in a test.** GitHub and git data are captured to files once (`gh api ... > fixtures/scenarios/<name>/pulls.json`, `git log` output, a
   bundle of a small repository) and tests read the files. Capturing needs the owner's go-ahead for anything not already local; the GitHub adapter's
   live mode is exercised by hand, not by ctest.
3. **Deterministic.** Generators take a seed; outputs are byte-compared; no clock reads (time is passed in, BACKBONE.md section 4).
4. **Performance only locally.** `timing`-labelled tests and benchmarks print `SKIP timing: <reason>` on battery (the family convention), and their
   results are compared against a baseline recorded on mains.
5. **Long suites are labelled `full`.** `ctest --preset quick` excludes them, `ctest --preset full` runs everything; the pre-push gate runs quick.
6. **Every claim in a design doc that a test can check is checked by one** (a budget, a recall, a byte-identical rendering).

## Running

```bash
cmake --preset default -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++   # CPM_SOURCE_CACHE set to share a cache
cmake --build --preset default
ctest --preset quick          # L0 and L1 (what the pre-push gate and CI smoke run)
ctest --preset full           # everything not timing-only
npm test                      # the viewer's tests alone
python ../CraigHutchinson.github/tools/ci/local_ci.py --level quick   # the declared local checks (.ci/local.json), all local compilers
```

## The golden fixtures (L1)

`fixtures/golden/` holds producer output captured from real Sub0Squad runs (README there says which run and build). Two tests read it:
`test/golden.test.js` (the viewer loads and validates every turn file) and the C++ side when B0 adds byte-for-byte writer checks. When the producer's
output changes on purpose, the golden files are re-captured from a real run, and the diff is reviewed as part of the change.

## The synthetic corpus (L3, S0)

A generator (to be built in S0) produces a project of any size on the fly from a seed: files in a directory tree, functions with realistic token
distributions, and a commit stream with edits, signature changes, renames, splits, merges, deletions, revivals and cross-file moves **at known
places**, so the generator is also the truth for identity at scale. Nothing at 10^9 lines is written to disk whole: the benchmark streams it.

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

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

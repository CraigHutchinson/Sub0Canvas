# Working in Sub0Canvas

Instructions for agents and people continuing this project. Start with [docs/RESUME.md](docs/RESUME.md) (where it stands), then [docs/BACKLOG.md](docs/BACKLOG.md) (what is next, in order); [docs/DECISIONS.md](docs/DECISIONS.md)
says why things are as they are. A task with its own brief is in [docs/sessions/](docs/sessions/README.md): start a session with "Read `docs/sessions/<file>.md` and do it."

## What this is

Sub0Canvas is a library and a viewer for **layered documents**: a codebase held as revisioned files (the canvas), seen through layers (issues,
review notes, prose, plans, history) that are bound to **stable block identities** rather than line numbers. It began inside Sub0Squad, a local-model
coding squad, which remains its first consumer. Its direction is to become the backbone where agents, people and sources (git, GitHub) meet, and to
hold up on codebases of billions of lines.

| read | for |
|---|---|
| [docs/CANVAS.md](docs/CANVAS.md) | the revisioned document, transactions, folded views and reads (built) |
| [docs/LAYERED_DOCUMENT.md](docs/LAYERED_DOCUMENT.md) | block identity, layers, the issues layer, formats, prior art (built) |
| [docs/DIRECTION.md](docs/DIRECTION.md) | the owner's direction (2026-11-17), verbatim, that BACKBONE.md answers |
| [docs/BACKBONE.md](docs/BACKBONE.md) | the design ahead: state, events, subscriptions, claims, adapters, history, scale, the ordered slices; section 14: the later use cases, amendments, the register of open decisions, policies as data |
| [docs/AUTHORING.md](docs/AUTHORING.md) | the viewer becoming an authoring tool (the first use case to invest in): concise views, the server, comments and inboxes, scenarios, slices V0-V5 |
| [docs/QUERY.md](docs/QUERY.md) | the live-views query protocol `query-1` (V1a-q, built): operations, bounds, cursors, the policy as data, one projection with the bundle |
| [docs/SCALE.md](docs/SCALE.md) | scale, its one home: two target profiles (billions of lines; a work-dominated private monorepo, measured), requirements, design, indexes, budgets |
| [docs/MIRROR.md](docs/MIRROR.md) | the synthetic mirror: a seeded invented project with real-world shape and edge cases, its modes and fixture |
| [docs/VIEWS.md](docs/VIEWS.md) | pinned and unpinned views (design, 2026-11-24): a pinned view is at a base revision for consistent editing, an unpinned one follows the tip with brief, elided, compacted or full refresh deltas and a live event channel; R-PIN-1..13; built except the channel and presence |
| [docs/CHANGE_VIEWS.md](docs/CHANGE_VIEWS.md) | pull requests, spikes and local branches as layers: topics, lineage, block-level overlap, landing coverage, slices P0-P5 |
| [docs/BLOCK_DISCOVERY.md](docs/BLOCK_DISCOVERY.md) | finding blocks in any language: measured faults, prior art, providers, who decides structure and folding, slices D0-D3 |
| [docs/DECISIONS.md](docs/DECISIONS.md) | the decision record: each decision, its reason and the seam that keeps it revisable (where data lives, block ids, transport, Sub0Fabric, the Sub0 libraries); what is still open |
| [docs/BACKLOG.md](docs/BACKLOG.md) | what is left, in one ordered list |
| [docs/REQUIREMENTS.md](docs/REQUIREMENTS.md) | what consumers rely on, independent of implementation, each with the test that checks it; Sub0Squad's use mapped |
| [docs/USE_CASES.md](docs/USE_CASES.md) | who it is for and what each use case demands |
| [docs/TESTING.md](docs/TESTING.md) | the proving ladder (unit, golden, use case, scale, consumer) and testing requirements rather than implementations |
| [docs/INTEGRATION.md](docs/INTEGRATION.md) | how a change reaches Sub0Squad: a tag here, a pull request there |
| [README.md](README.md) | the viewer, the input format, building the library |

## Layout

* `lib/include/sub0canvas/*.hpp`, `lib/src/*.cpp`: the C++23 library, target `sub0canvas::sub0canvas`, namespace `sub0canvas`. Pure: no I/O, no
  threads, no clock reads, no environment.
* `lib/tests/`: C++ tests, one executable per area. The test helper is Sub0Platform's `sub0platform/testing/check.hpp` (fetched for the tests
  only, CMakeLists.txt). `doc_driver.hpp`, the driver requirement tests go through so they survive architectural change, TESTING.md.
* `app/`: C++ executables that link the library and do its I/O (`file_log.*`, the file `Log`, is the static library `sub0canvas_io`). `sub0canvas snapshot` reads a local git repository into a viewer bundle (`serve` and
  `inbox` follow, AUTHORING.md section 3); `sub0canvas history` prints the identity history of named blocks across a scope's commits (`git_source.cpp` is the git
  `DocumentSource`, the process runner; G1). Never under `src/`, which is the viewer's.
* `index.html`, `src/*.js`: the static viewer (no build step, no dependencies). `test/*.test.js`: its node tests, and the tools'.
* `tools/*.mjs`: the viewer's tools (demo generator, static server, screenshots, `git-fixture.mjs`: the invented 200-commit repository and its captured git output, `fixtures/git`). `tools/mirror/`: the synthetic mirror generator
  (`npm run mirror -- <mode>`, [docs/MIRROR.md](docs/MIRROR.md)). `tools/k0/*.mjs`: K0, the viewer's interactivity timing (a headless browser over DevTools;
  `node tools/k0/run.mjs`, [docs/K0.md](docs/K0.md); local tier, ctest label `timing`; budgets and per-host baselines in `perf/k0/`).
* `consumer/mock_squad/`: the mock squad ([docs/MOCK_SQUAD.md](docs/MOCK_SQUAD.md)): a separate CMake project that fetches this checkout through CPM as Sub0Squad
  does and links only the public target; its scenario reader, gate, reference loop and record writer, and `test_consumer_contract`. The top-level build runs it as
  `test_consumer_mock_squad` (label `full`).
* `bench/`: the scale harness (S0, [docs/SCALE.md](docs/SCALE.md) section 6.0 and [docs/TESTING.md](docs/TESTING.md), "The synthetic corpus"): `corpus.hpp`/`corpus.cpp`, the seeded synthetic corpus and edit stream
  (library target `sub0canvas_bench_corpus`, option `SUB0CANVAS_BUILD_BENCHMARKS`; tested by `lib/tests/test_corpus.cpp`), and `bench_canvas.cpp`, the benchmarks
  (`Sub0Canvas_Bench`, run through the family's sub0perf tooling; [docs/TESTING.md](docs/TESTING.md), "Performance").
* `perf/`: `metrics.json`, the deterministic metrics `Sub0Canvas_Bench` reports (judged by the ctest tests `Sub0Canvas_Bench_metrics` and `_smoke`, label `smoke`, in every tier);
  `baselines/<host>.json`, the recorded timings of one machine (the host is an alias, `SUB0PERF_HOST`; a public repository carries no real host names).
* `fixtures/`: test data by kind, indexed with origin, checking test and refresh command in [fixtures/README.md](fixtures/README.md): `demo/`
  (generated), `golden/` (captured), `replies/` (hand-written), `scenarios/` (hand-written attempts), `mock/` (their persisted attempts, generated), `mirror/` (generated).
* `docs/`: the design and process documents above; `docs/sessions/`: briefs for follow-up sessions.

## Build and test

```bash
cmake --preset default -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++   # CPM_SOURCE_CACHE: the global cache, below; gcc 14 is the floor
cmake --build --preset default
ctest --preset quick      # what the pre-push gate and CI smoke run (the benchmark metrics included); ctest --preset full for everything but timing
ctest --preset timing     # the local timing run (mains power, quiet machine); never in CI
npm test                  # the viewer alone
```

On Windows the toolchain is MinGW-w64 UCRT (WinLibs GCC) with Ninja. Dependencies come through CPM into one global cache outside every
repository (`CPM_SOURCE_CACHE=%USERPROFILE%\.cache\cpm`, a user-level variable; never a cache under `out/`, which a clean build or a new worktree
loses), and consumers on this machine build against the local checkout: `CPM_Sub0Canvas_SOURCE` and the other `CPM_<Name>_SOURCE` overrides are
user-level variables, so local sources are the default unless a pin is wanted on purpose. Activate the pre-push gate once per clone: `git config core.hooksPath .githooks`,
and prove it with `python ../CraigHutchinson.github/tools/ci/hook_check.py --repo .`.

## Dependencies

Every dependency ref is explicit and lives in one place. Third-party dependencies use a readable tag (`simdjson` `v5.1.1`). The family's (Sub0Platform, Sub0Perf, and Sub0Canvas for the consumer rung) are in
`cmake/sub0_deps_pinned.cmake`: a tag where the repository has one, a raw 40-hex SHA only there and only with a comment naming the commit and date (`check_deps_policy`, quick tier, fails on a 40-hex
`GIT_TAG` anywhere else). CI uses the pins. To verify against the latest local checkout, set `CPM_<Name>_SOURCE` to it (a CMake `-D` or an environment variable): the override beats the pin.

```bash
export CPM_Sub0Platform_SOURCE=/d/Craig/GitHub/Sub0Platform CPM_Sub0Perf_SOURCE=/d/Craig/GitHub/Sub0Perf CPM_Sub0Canvas_SOURCE=/d/Craig/GitHub/Sub0Canvas
```
```powershell
$env:CPM_Sub0Platform_SOURCE='D:\Craig\GitHub\Sub0Platform'; $env:CPM_Sub0Perf_SOURCE='D:\Craig\GitHub\Sub0Perf'; $env:CPM_Sub0Canvas_SOURCE='D:\Craig\GitHub\Sub0Canvas'
```

Reconfigure after setting them. `test_consumer_mock_squad` always passes `CPM_Sub0Canvas_SOURCE` (this checkout) and the Sub0Platform source the build resolved. Bump a pin deliberately, in that file only.
The same table and rule are to be shared through the Sub0Perf / Sub0Platform tooling for Sub0Fabric and Sub0Squad (not done).

## Code style

* C++23, `-Wall -Wextra -Werror` (GCC-style flags; there is no MSVC leg).
* camelCase for functions and variables, PascalCase for types, `kName` for constants; `#pragma once`; Doxygen `///` on every public declaration,
  saying what it is for and its contract, not restating the name.
* **Deterministic output** (docs/MOCK_SQUAD.md section 8): no `<random>` (engines, distributions, `std::shuffle`) outside `app/` and no `std::hash` in output paths: use `bench/rng.hpp` and a fixed hash; one generator
  draw per statement, never two in an argument list or operator chain. `tools/check_cxx_rules.cmake` (the quick-tier test `check_cxx_rules`) enforces this.
* `[[nodiscard]]` on functions whose result matters; `std::format` and `std::print`; `std::expected` for fallible operations in new code.
* Line length about 160; comments explain why, and cite the doc section a rule comes from.
* JavaScript, no dependencies, Node 20+, `node:test`; the module kind follows the directory: `src/*.js` are classic scripts that also export
  CommonJS (the page runs from `file://` and tests `require` them); `tools/*.mjs` are ES-module entry points; `tools/mirror/*.js` and `tools/format/*.js` (the format's content ids) are CommonJS,
  because tests `require` them. Do not mix kinds within a directory.

## Rules (the owner's standing rules)

This list is complete. Everything an agent or a person needs to work here is in this repository; nothing depends on notes kept outside it (an
assistant's local memory, a chat, a session card). When the owner gives a new standing instruction, add it here in the same change.

* **Nothing leaves the machine**: no frontier model, no uploading of code or data. Ask before downloads, installs or credentials.
* **Commit as you go** to `main` (fetch, rebase, build, test); **confirm with the owner before pushing**; never force-push or skip hooks; never
  stash, reset or switch branches in a checkout another session shares; commit only your own files. Commit as the owner's GitHub identity already
  set in the clone, with a `Co-Authored-By` trailer for the agent.
* **Clean up as you go** (standing permission): once work is merged, superseded or obsolete, delete its branches (local and remote), worktrees and
  stale folders (old run data under `out/`) without asking, after checking that it really is merged or superseded. Retire legacy documents once
  their content is integrated; git keeps them. "Never throw work away" protects unique, unmerged work only.
* **This is a public repository: never name or link a private repository** (the pre-push gate checks, `private_names.py`). **Closed-source
  codebases used as references are described by their shape and numbers only**: never their name, owner, internal tools, tracker keys, paths,
  titles or links, in files, commit messages or anything posted. Private proving grounds publish anonymised numbers only (R-CHG-9). The owner's
  clone carries a local-only guard for this (under `.git/`, never tracked); if a commit or edit is refused by it, rephrase, never bypass it.
* **Judge every design on three axes together** (2026-11-23): **data-driven** (behaviour in policies and formats a client may supply, not
  hard-coded), **extensible** (a seam with more than one implementation proved, even if the extra ones are proofs of concept run only as
  build or release gates) and **performance at the core** (scale is a design input; the budgets in SCALE.md and the review's section 5 apply to
  every new module).
* **Report faithfully**: regressions named, skipped steps said, numbers with their noise. Never throw work away.
* **CI**: smoke on every push and pull request; the full tier on `v*` tags, `ci:full`, dispatch and a change-gated weekly run. **Never dispatch the
  full tier after a routine push**: verify locally (`ctest --preset quick`, or `full`) and check only the smoke run after pushing. Full runs are for
  releases and version bumps; the one exception is a change that only another platform can prove (a Windows runner's behaviour): batch such
  changes and dispatch once. **Timing is measured locally only; deterministic metrics gate in every tier; the improve loop is Sub0Perf docs/PERF_LOOP.md** (one verdict
  line per step; the family's performance tooling, fetched for its tooling only).
* **Follow-up work** that belongs in its own session is written as a brief in [docs/sessions/](docs/sessions/README.md) (its workflow: write, pick
  up, on close move the evidence to its design doc and delete the brief in the same change), listed there and pointed at from the backlog. Not left in a chat or a session card, which other
  devices cannot open. Status lives only in RESUME and BACKLOG, never in a brief.
* **The name**: the project is **Sub0Canvas** in prose, docs, commit messages and URLs (`https://github.com/CraigHutchinson/Sub0Canvas.git`). Code and
  build identifiers stay lower-case: namespace `sub0canvas`, target `sub0canvas::sub0canvas`, headers `sub0canvas/*.hpp`, the CMake project name
  `sub0canvas`, `SUB0CANVAS_*` options, the npm package name. Do not rename those without the owner.
* **Changes the squad consumes** go here first, are tagged, then reach Sub0Squad through a pull request there ([docs/INTEGRATION.md](docs/INTEGRATION.md)).
* **Docs**: each piece of evidence has one home; a brief links to it rather than copying it. Retire a document once its content is integrated.
* **Work in your own full clone**, never a linked worktree and never a checkout another session holds.

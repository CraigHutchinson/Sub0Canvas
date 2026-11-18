# Integration with Sub0Squad: an iterative, parallel proving loop

Sub0Squad ([github.com/CraigHutchinson/Sub0Squad](https://github.com/CraigHutchinson/Sub0Squad)) is this library's first and most demanding consumer.
It is where a change meets real models and real agent loops. But it is **not** where a change is first proved: every change is proved here first, by
the ladder in [TESTING.md](TESTING.md), and only then integrated, one iteration at a time, through a **pull request** in Sub0Squad.

## How Sub0Squad consumes the library

* `cmake/dependencies.cmake` pins a tag: `CPMAddPackage(NAME Sub0Canvas GIT_REPOSITORY https://github.com/CraigHutchinson/Sub0Canvas.git GIT_TAG v0.2.x)`.
* `include/sub0squad/{outline,edits,canvas,doc_layers}.hpp` are forwarding headers (`using namespace sub0canvas`), so squad code is unchanged.
* What stays in Sub0Squad: the canvas loop (`src/canvas_agent.cpp`: `layeredVerdict`, READ ISSUE, the `SUB0SQUAD_ISSUE_LAYER` and
  `SUB0SQUAD_ISSUE_INLINE` switches, `layers/turn-N.json` per turn), the shim that passes those switches to jobs (`src/shim.cpp`), the gate's
  `failures.txt` (`src/harness_common.cpp`), and the tests `test_canvas_agent` and `test_blocks` (whose recorded replies in `tests/data/replies`
  exercise the edits code).
* For local work, Sub0Squad's `. scripts/dev-env.sh` exports `CPM_Sub0Canvas_SOURCE` (a user-level variable on the Windows dev machine) pointing at a sibling checkout of this repository, so an
  unreleased change can be built there. **That override masks a missing tag**: before pushing a pin, configure once with it unset.

## One iteration

1. **Here: build and prove.** Make the change on a branch or on `main` (the owner's single-maintainer flow), climb the rungs its kind requires
   (TESTING.md, the last table), and update the design doc it touches. Commit as you go.
2. **Here: release.** Bump `project(... VERSION)` in `CMakeLists.txt`, tag `v0.<minor>.<patch>`, and push `main` and the tag. The tag runs the full CI
   tier (Linux GCC and Clang, Windows MinGW): the release check. Never pin a tag that is not on GitHub.
3. **Sub0Squad: a pull request.** On a branch named for the iteration (`sub0canvas-v0.3.0-store`), bump the pin and add the squad side (an adapter, the
   loop's use of a new API, a switch to measure it), with tests. Open the PR; its smoke tier runs. Label it `ci:full` when the change could behave
   differently on Linux or Windows.
4. **Sub0Squad: prove it there.** Locally: `test_blocks`, `test_canvas_agent` and the full suite. When what a model sees changes: a tier 0 A/B with an
   off arm, 5 runs per arm or more (Sub0Squad `validation/tiers/run_t0.sh`, `gate_t0.py`; the slice 2 drivers are in Sub0Squad
   `out/runs/layers-slice2/scripts/`). Write the result into the PR and into Sub0Squad's `docs/LAYERED_DOCUMENT.md`, every regression named.
5. **Merge** when the squad's evidence supports it. A regression found at step 4 goes back to step 1 here, as a new iteration.

**Parallel work.** Iterations that touch different parts proceed in parallel: one PR per iteration, each pinned to its own tag. Only one iteration at a
time bumps the pin on Sub0Squad `main`; a later PR rebases onto the merged pin. Library work that the squad does not consume yet (the git and GitHub
adapters, scale work) needs no Sub0Squad PR at all until it does.

**The PRs are also test data (dogfooding).** The GitHub project view (BACKBONE.md G2) is first validated on real pull requests with review
comments: this repository's and Sub0Squad's integration PRs. Write integration PRs with that in mind: real review comments on real lines, kept, not
squashed away, and links between the PR, the tag and the doc section that records its evidence.

## Which iteration needs what

| change here | Sub0Squad PR? | evidence there |
|---|---|---|
| rendering a model sees | yes | tier 0 A/B, 5+ runs per arm, an off arm |
| identity rules or thresholds | yes | full suite; an A/B if issues move between blocks |
| index internals, no behaviour change | yes (pin bump only) | full suite; golden files byte-identical |
| format additions (B0) | yes (the producer writes the new fields) | full suite; the viewer opens a real attempt |
| store, events, subscriptions, claims (B1-B3) | yes, each behind an option, off by default | full suite with the option on; byte-identical verdicts (B2) |
| git or GitHub adapters (G1-G5), scale (S0-S4) | not until the squad consumes them | none |

## Coordination

* The tier 0 engine (a local 35B behind llama-server on `127.0.0.1:8092`) is shared between sessions. Never restart or retune it, and never start a
  second server on its port. Coordinate A/B runs with whoever owns it.
* Run long A/Bs from a dedicated git worktree of Sub0Squad, so other sessions' rebuilds cannot replace the program mid-run.
* Owner's standing rules apply in both repositories: nothing leaves the machine (no frontier model), ask before downloads, installs or credentials,
  commit as you go, confirm before pushing, never force-push or skip hooks.

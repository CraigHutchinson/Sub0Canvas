# Follow-up session: K0, the viewer's interactivity KPIs

Status: open (written 2026-11-18). Backlog: "K0: the viewer's interactivity KPIs" under *Next*. Design: [AUTHORING.md](../AUTHORING.md) section 3.2
(the budget table and baselines) and section 3.1 (live views); [SCALE.md](../SCALE.md) section 6.1 (the measurements that motivate it);
R-AUTH-15 in [REQUIREMENTS.md](../REQUIREMENTS.md).

## Goal

A scripted, repeatable benchmark that times every viewer interaction in AUTHORING.md's budget table at the reference scale, so the live-view
work (V1a), the file tree and content on demand, and the block-list redesign are steered by measurement. Do not optimise the viewer in this
session: measure, record and report against the budgets.

## What is known

* Baselines (2026-11-18, one run, a 1,590-file subtree with 20,075 blocks): about 1-1.3 s for expanding a folder, opening a file, selecting a
  block and switching turn; 0.77 s back to code; 0.12 s to a layer; 0.06 s per filter keystroke. Every click rebuilds every pane; 92% of the
  page is the global block list.
* The data sources exist: the synthetic mirror (`npm run mirror -- canvas --seed 1 --out out/mirror`, then `?bundle=out/mirror/mirror-seed-1.bundle.json`)
  and any local repository through `tools/serve.mjs`'s `/snapshot?repo=NAME&paths=...` (repositories registered in the clone's untracked
  `.git/sub0canvas/repos.json`). The page exposes `window.S0View` (`load`, `state`, `render`) for scripting.
* The chosen driver, from the analysis that was started: **headless Edge or Chrome over the Chrome DevTools Protocol, driven by Node's built-in
  `WebSocket`** (Node 22 or later; this machine has 24). No dependency, no browser download; `tools/screenshots.mjs` already finds the browser
  and starts `tools/serve.mjs` on a free port, and is the pattern to follow.
* Selectors the scenarios need: `.trow.dir` and `.trow.file` (the tree; a directory row's last child is its file count), `#nav .blockrow` (the
  block list), `.code .blockhead` (block headers in the code), `#turns .turn` (turns), the centre-on rows in `renderNav` (`src/view.js`), and
  `#nav input[type=search]` (the filter).
* A private repository is measured on the owner's machine only. Its results are recorded by scope size (files, blocks), never by its paths or
  name (AGENTS.md; the local guard checks).

## Steps

1. **The runner** (`tools/bench-ui.mjs`, ES module, no dependencies): start `tools/serve.mjs` on port 0; launch the browser with
   `--headless=new --remote-debugging-port=0 --user-data-dir=<temp> --window-size=1440,900`; read the DevTools URL from its stderr; open a target
   with the page and the source (`--bundle URL`, or `--repo NAME --paths a,b --history N`; default: the mirror, generated first); evaluate the
   scenarios with `Runtime.evaluate` (`awaitPromise`).
2. **The scenarios** (one function per row of the budget table, injected into the page): each performs its input on a real element and times
   from the input to the next painted frame (`requestAnimationFrame`, then a task). Alternate between two states so every repetition does work
   (expand then collapse; two files, one cached; blocks in different files; first and last turn; `a` then `ab` in the filter). First view is
   timed from navigation to the tree and the first file painted. "An event from another actor" is reported as not yet possible until V1a.
3. **Statistics and record**: 2 warm-up runs, then 20; report p50 and p95 per interaction against its budget, pass or fail; print a table and
   write a JSON record (interaction, p50, p95, budget, scope size, machine, CPU, browser and its version, Node, build, commit, power state) to
   `out/bench/` (ignored by git). Skip with `SKIP timing: on battery` when the machine is on battery (the family rule).
4. **ctest**: register it with label `timing`, and keep `timing` out of the `quick` and `full` presets (performance is never measured in CI);
   add a `timing` test preset that runs only it.
5. **Write the first measured numbers** into AUTHORING.md section 3.2 (a "measured" column beside the baselines), for the mirror and for the
   reference subtree (by size), and name the interactions furthest over budget: they order the V1a, tree, content and block-list work.

## Done means

* `node tools/bench-ui.mjs` (and `ctest --preset timing`) prints p50 and p95 for every interaction in AUTHORING.md section 3.2 against its
  budget, and records them with the machine.
* `ctest --preset quick` is unchanged and does not run it; CI does not run it.
* AUTHORING.md section 3.2 holds measured numbers for the mirror and the reference subtree, and the backlog's order follows them.

## Environment and constraints

Windows 11, Node 24, Microsoft Edge (or Chrome) installed; the build from `cmake --preset default` for `/snapshot`. The owner's standing rules
([AGENTS.md](../../AGENTS.md)): nothing leaves the machine; ask before downloads or installs (none are needed); closed-source references by
shape and numbers only; commit as you go, confirm before pushing.

# Follow-up session: MS4 and MS5, two-actor claims and pinned/tip workers in the mock squad

Status: open (planned 2026-11-24 by the planning agent; MS4 first). Backlog: [phase 1](../BACKLOG.md). Design: [MOCK_SQUAD.md](../MOCK_SQUAD.md) section 4,
[VIEWS.md](../VIEWS.md), [BACKBONE.md](../BACKBONE.md) section 3.4. Parent brief: [backbone-next-wave.md](backbone-next-wave.md).

## Ground truth

- The loop is `consumer/mock_squad/src/loop.cpp`: `step()` handles one reply; the loop stops the attempt once the gate passes; one store writer `{"mock","loop"}`.
- Scenarios are `ScriptedReply` only (`scenario.cpp`); an unknown section is an error. `seal()` writes `libraryCalls` into every `mock-trace.json`.
- The contract test (`tests/test_consumer_contract.cpp`) does `CHECK_GE(files.size(), 15)` and holds the `kRows` list.
- `Session::land` goes through `applyReply` with `baseRevision` = the pin and writes the author as `ns/writer`; loop replies write the bare actor name. Never mix the two on one file.
- A claim refusal reads `"<kind> <target> is held (<mode>) by ns/w until <t> (claim ns/w:<lamport>)"`.
- **The sixteen existing `fixtures/mock/*` directories must stay byte-identical.** Every new library call sits behind a new policy, so existing traces do not move.
  Check `git status fixtures/mock` shows only new directories before regenerating anything.

## Step 0 (first commit of MS4): control steps

- `scenario.hpp/.cpp`: `ScriptedReply` gains `std::string op` (empty means a reply) and `std::map<std::string,std::string> attrs`; sections `--- claim`, `--- release`,
  `--- expire`, `--- sync` (MS4) and `--- since`, `--- advance`, `--- refresh` (MS5). Attributes: `as=`, `at=<Instant>`, `block=<path>:<leaf>[@turn]`, `file=`, `ttl=`,
  `offline`, `form=`, `ack`, `atomic`. A step with an `op` has no body. Replies gain `at=N` (default 0).
- `loop.cpp`: `step()` dispatches on `op`. A control step spends no turn, writes no `reply-N.txt`, adds no layer turn; it records an `Outcome` (`claimed`, `claim-refused`,
  `released`, `expired`, `settled`, `since`, `advanced`, `refreshed`). The loop condition lets control steps run after the gate passes; replies still stop there.

## MS4: two-actor claims

- `loop.hpp`: `LoopPolicy::claims` (`off|on|enforce`), `Attempt::claimStates`. `loop.cpp`: one `Store` replica per actor (writer `{"mock",actor}`) plus `loop`, each with
  `registerClaimReducers(ClaimPolicy::standard())`; before a step an actor's replica merges the others unless the step is `offline`.
  - `claim` resolves `block=path:leaf[@N]` through `index_.blocks(path)` (or the id recorded in `out_.layers[N]`) and calls `takeClaim`; `release` calls `releaseClaim`;
    `expire` calls `expireDue`; `sync` merges every replica both ways and reports each claim whose status is `refused`.
  - Under `enforce`, after `applyReply` on the scratch copy and before commit: `diffLines(old,new)` per file, map changed old lines through `index_.at(path,line)`, run
    `checkClaim` at the reply's `at`; a conflict refuses with the `ClaimRefusal` message and the existing refusal invariant proves the canvas unchanged. Requires `scratch=yes`.
  - After each verdict's index update: `emitBlockCarries(*claimReplicas_["loop"], index_.events(), carried_)`.
  - Persist `claims/events.ndjson` and `claims/step-K.json` (`claimsJson(merged, at)`) only when claims are on.
- Contract test: `--- expect-claims` lines `live <n> @step`, `status <holder>:<k> <status>`, `holder <path:leaf> <ns/w> until <t>`; file count raised to 20; require
  `takeClaim`, `checkClaim`, `releaseClaim`, `expireDue`, `emitBlockCarries`, `claimsJson`, `Store::merge` to be called by some scenario; a negative check shows `checkClaims`
  fails on a wrong line; update the `kRows` deferral text that says "MS4".
- Scenarios (needles avoid block ids, R-ID-11):
  1. `b14-claim-contention`: `a` claims `square` at=1000 ttl=300; `b` claims it at=1100 and is refused `held (write) by mock/a until 1300`; `b`'s edit to `square` at=1100 is
     refused, files unchanged; `b`'s edit to `divide` lands; `a` edits `square` and passes.
  2. `b14-claim-expiry`: same take; `b`'s edit at=1299 is refused; at=1300 it lands (expiry exclusive); `--- expire at=1300` gives `expired:1`; expect `status a:1 expired`, `live 0`.
  3. `b14-claim-merge-order`: `a` and `b` both claim `divide` `offline` (lamport 1 each, derived by hand); `sync` gives `settled:mock/b:1 refused by mock/a:1`; `b`'s edit is then
     refused naming `mock/a`; the other sync order gives byte-identical `claims/step` files.
  4. `b8-claim-follows-merge`: `a` claims `twice`; `a`'s reply deletes `twice` and inlines its body into `quad` (token containment >= 0.6, so the index logs `merged`; check with an
     `expect-ids` line); `b` (`base=r1`) claims `block=src/calc.cpp:twice@0` and is refused naming `mock/a`; `a` releases; `b`'s claim on the old id is then taken and `claimsOn` covers the survivor.
- Requirement rows: **R-CONS-6** (contention: refused naming holder and expiry, files unchanged, expiry frees at `at+ttl` exclusive), **R-CONS-7** (offline claims settle by merge
  order identically in every sync order; a stale writer naming a merged block resolves to the survivor and is refused or covered after release). Add the scenarios to R-BB-6's
  evidence; update MOCK_SQUAD.md section 4 (MS4 built) and its status line.
- Freeze: `mock_squad fixtures/scenarios/squad fixtures/mock --update <the four names>` (only those directories regenerate); read every generated file by hand; commit scenarios
  and fixtures together.

## MS5: pinned and tip workers on sessions (after MS4)

- `scenario.cpp`: header `# views: a=pinned b=tip`. `loop.cpp`: with views set, require `scratch=no`; build a session side only then: separate store `sessStore_` (writer
  `{"mock","ws"}`, own `MemoryLog`, `registerViewReducers`), `LogViewStore`, `DataSessionPolicy{}`, `BlockCompactor`, `Workspace(canvas_, sessStore_, views, policy, compactor, &index_)`;
  `store_` stays untouched so `useStore` is byte-identical. One session per actor (`Identity{{"mock",actor},{}}`).
  - A pinned actor's reply goes `stage` then `land`; if any hunk applies, continue into the existing compile check, gate and verdict path; if all are refused, record `refused`.
  - `since` records the brief JSON; `advance` records applied, refused and anchor counts and the new pin.
  - `refresh form=X [ack]` calls `makeDelta`, and pushes a violation unless `applyHunks(oldText, net regions) == newText`, the steps replayed in order also equal it, and it equals
    `canvas_.text()` (R-PIN-6 at the consumer). Persist `views/<actor>-refresh-K-<form>.json` and `sessions/events.ndjson`.
- Contract test: count 22; require `Workspace::open`, `Session::stage`, `land`, `since`, `advance`, `refresh` and `makeDelta` to be called; across the four refreshes at one point:
  equal `to`, `files`, `events`; brief at most 520 bytes; after `ack` a further refresh says `"files":0`.
- Scenarios: `b8-pinned-workers-land` (two pinned workers at r1: `a` lands on `divide`; `b`'s disjoint edit to `square` lands rebased; `b`'s overlapping second edit is refused naming
  `mock/a` and the revision with its hunk kept; `since as=b` changes nothing; `advance as=b` gives `refused 1`) and `b14-tip-worker-four-forms` (`b` tip, `a` pinned; two edits by `a`;
  refresh in `brief`, `elided`, `compacted`, `full ack`; a last `compacted` refresh gives `"files":0`).
- Requirement rows: **R-CONS-8** (pinned workers through a session), **R-CONS-9** (tip worker, four forms agree and reach the canvas text; acknowledged refresh is empty); add the
  scenarios to R-PIN-2, R-PIN-3, R-PIN-6 evidence; update MOCK_SQUAD.md section 4 (an MS5 row) and VIEWS.md section 5's scenario list.

## Risks

Author strings differ between session lands and loop replies; the merge needs token containment >= 0.6 (check the `merged` event by hand); Lamport values in needles are derived by
hand (a take after a merge gets max+1); no JSON parsing in the consumer (substring needles only); gcc 14: never `CHECK_EQ` an element of a temporary; issue #4 (clang) may
flag the new fixtures, so CI is the check; no benchmark rows, so timing is unaffected.

## VIEWS.md section 8 limits

Quick (tests only): atomic land over two base revisions rolls back fully; `advance` with the index unfed reports an anchor in the session's own landed lines as moved; scope by path
prefix gives the same delta as the whole canvas; `isStale` with `notify=flag`; first refresh after `unpin` is the delta since the old pin (probably covered in `test_views_sessions`);
quotas as `SessionPolicy` data (pending bytes per actor). Need design: the canvas-path scan in `currentPosition`/`makeDelta` (a per-scope change index), `Canvas::textAt` copies, the
canvas copy on atomic land, the workspace feeding the `BlockIndex`, paging inside a form, the channel (R-PIN-8, with Fabric W4), presence and peer patches (R-PIN-13, deferred), `Pin.overrides`.

## Outcome (MS4)

Built 2026-11-24 in `Sub0Canvas-dev` (not pushed): Step 0 and MS4, commits `3de690f` (control-step parser and the loop's step order), `224c823` (claims policy in the loop), `26ddfb6` (four scenarios,
their frozen fixtures, the contract's claim checks), `907ae88` (R-CONS-6, R-CONS-7, R-BB-6 evidence, MOCK_SQUAD.md, the mock README). `ctest --preset quick` 21 of 21 and the full
`test_consumer_mock_squad` pass; `git status fixtures/mock` shows only the four new directories, so the sixteen earlier attempts are byte-identical.

Deviations from the plan, all following the code:

- A `release` names its target with `block=` or `file=` (as `claim` does) and releases the acting writer's live claim on it, resolved through merges; it does not name a claim id.
- Every step run records one outcome, so `# expect:` has one entry per step run (a control step has its own kind). A reply after the gate passed is skipped and counted unused; a control step still runs.
- `claims/step-K.json`: K is the position in the scenario's steps (not the turn); `expect-claims` lines take an optional trailing `@step` (default: the last step run) and add a `covers <holder>:<k> <path:leaf>` line beside `live`, `status`, `holder`. A step with no `at=` runs at 0, so a `sync` or `expire` that a `holder` or `live` line reads needs an `at=`.
- The step files and `status` lines are the loop replica's merged view, which merges every actor at each step: a take that loses on merge order reads `refused` from the first step after it, not only after the `sync`; what each actor knew before the sync shows only in the refusal of its next step.
- `sync` merges the replicas in the order of `# actors`, then the loop's: the contract re-runs a scenario that has a `sync` with the actors reversed (every step names its actor) and requires byte-identical `claims/` files, so there is no extra attribute.
- The block index logs a merge only when the retired body has at least 3 distinct tokens (punctuation is not one) and one live block holds 60 percent of them, so `b8-claim-follows-merge` uses a `twice` with `double y = x;` and a third function `square`; its first reply then fails the gate, which writes the layer file `expect-ids` reads.
- The loop also proves, per claims scenario, that the persisted `claims/events.ndjson` parses back and replays to the live claims (a violation otherwise). `LoopPolicy::claims` is an enum (`ClaimsMode`); `enforce` without `scratch=yes` is a scenario error.
- The MS5 steps (`since`, `advance`, `refresh`) parse, and the loop stops with "belongs to MS5" if one is run.
- The brief file itself was untracked in the clone; it is committed with this section.

Left: MS5 (pinned and tip workers on sessions), the VIEWS.md section 8 limits, the inbox half of B2 (not mocked), the Lamport values stay derived by hand in each scenario's `expect-claims` comment.

# The mock squad: Sub0Canvas tested as a project the squad consumes

Status: built (2026-11-25): MS0-MS5 and the store policy, 22 scenarios in `fixtures/scenarios/squad`, frozen in `fixtures/mock`. The evidence home for the consumer contract; section 4 says what each slice proves, section 6 holds what the baseline taught the library and section 7 what the Sub0Squad session is asked to take up.

The squad is the first and most demanding consumer ([INTEGRATION.md](INTEGRATION.md)), but real models cannot be re-run on demand, and most of
the squad's canvas loop is policy, not library calls (`src/canvas_agent.cpp` is about 1,100 lines, about 20 of them library calls). The **mock
squad** is an ideal, deterministic stand-in that lives here: scripted actors, a reference loop, a gate that reacts to the files, and the
squad's persisted attempt format. It emulates good and bad behaviour so a library or format change is proved as a consumer would meet it, before
a Sub0Squad pull request exists. Real goldens (`fixtures/golden/`) stay the truth about real output (R-U1).

## 1. What it is

A separate CMake project, `consumer/mock_squad/`, that fetches the library exactly as Sub0Squad's `cmake/dependencies.cmake` does
(`CPMAddPackage(NAME Sub0Canvas)`, with `CPM_Sub0Canvas_SOURCE` pointing at this checkout in the test) and links `sub0canvas::sub0canvas`. It sees only
public headers and compiles with the consumer's own `-Werror`. Today there is no `install(EXPORT)`, so CPM is the one consumption path proved.
Run by a top-level `ctest --build-and-test` entry (`test_consumer_mock_squad`); labelled `full`: measured on the owner's machine (MinGW GCC, Ninja) the first run takes about 32 s (it builds the library and simdjson a second time) and later runs about 1.3 s, so the label is the owner's to move to `smoke`.

| part | what it does |
|---|---|
| `scenario` | reads `fixtures/scenarios/squad/<name>.squad`: the sections of `fixtures/replies` (`--- base`, `--- reply`, `--- want`) plus header keys `# actors`, `# allowed`, `# policy`, `# expect` (outcome sequence), reply attributes mirroring the squad's `fake::Reply` (`finish=length|stop`, `pieces=`, `base=rN`), `--- reasoning`, `--- gate`, `--- expect-ids` |
| `gate` | a function of the current files, never a scripted verdict: test rules (`test <name> passes when <path> contains <text>`) give `GATE FAIL/PASS` lines and a `failures.txt`; compile rules give `path:line:col: error:` where the pattern really sits; `flap=<seed>` alternates on unchanged files |
| `loop` | the reference loop on the public API only, in the order the squad's loop uses it: stream guard (`detectRunaway`, `detectEditOverrun`), continuation (`cutOffInsideEdit`, `continuationPrompt`, `joinContinuation`, `completeCutFile`), reads, `parseCanvasReply` and `applyReply`, stage, commit, gate, `revertTo`, then the verdict through `BlockIndex`, `parseIssues`, `layerSummary`, `layerInline`, `layerFocus`. Every squad constant is a `LoopPolicy` field (later a `mock-loop@1` policy document: data-driven). Two actors share one `Canvas`, each holding the base revision it was shown |
| `record` | writes `canvas.json`, `canvas/<flat>.rN`, `layers/turn-N.json`, `reply-N.txt`, `gate.txt`, `agent.json` (a subset) in the production format, plus `mock-trace.json` (the outcome per turn), into `fixtures/mock/<name>/` |

It differs from `fixtures/replies` (one reply, one `applyReply`) by being a whole attempt: revisions, identities, issue keys and allowances carry
across turns and a gate reacts. It differs from `fixtures/golden` by regenerating deterministically (`--update`; a format change such as B0 is
regenerated and the diff reviewed by hand in the same commit). Scenarios use the words of [AUTHORING.md](AUTHORING.md) section 6 (`as`, `do`,
`expect`) so the two runners can merge once the store exists.

## 2. Behaviours emulated

Good: **G1** one-shot SYMBOL fix (R-CAN-6, R-ID-2). **G2** READ SYMBOL then READ ISSUE then edit (R-VIEW-6, R-REN-2/5). **G3** two files in one reply
(R-CAN-2, R-EDIT-1). **G4** rename, re-sign, split, merge and cross-file move over turns; the issue's key and `id` stay while its title changes
(R-ID-4/5/6/9, R-ISS-5, R-VWR-5). **G5** the fix away from the diagnostic, as in undeclared-member (R-ISS-3/4).

Bad (each cites the squad's T0 case or intervention it mimics): **B1** cut off inside an EDIT, the continuation repeats the overlap (R-TURN-1/2).
**B2** half-written FILE (R-TURN-3/7). **B3** the same lines repeated (`repeat` with `keep` and `detail`, R-TURN-4/6). **B4** a thinking loop caught on
reasoning only. **B5** restated draft, echoed view, comment run. **B6** SEARCH copying most of the file; a 150-line edit (R-TURN-5). **B7** undeclared-member
done badly three times: the same issue key each turn (R-ISS-5, R-CAN-5). **B8** stale base: rebased where lines do not overlap, refused naming the
other actor and revision where they do (R-CAN-4). **B9** FILE plus a refused EDIT: all-or-nothing leaves everything as it was; partial applies the FILE and
lists the EDIT in `skipped`; FILE and EDIT on one path refused (R-CAN-1, R-EDIT-7/8). **B10** SEARCH missing or ambiguous, lines out of range,
overlaps (R-EDIT-2/3, R-CAN-3). **B11** a write to a disallowed path (R-EDIT-5). **B12** malformed blocks (R-EDIT-6/8). **B13** no-op edit and
"I give up" prose. **B14** a duplicate or retried delivery and two actors on one block (idempotent; the second rebased or refused). **B15** (built, MS3) a flapping
test (same key, no new item). **B16** a huge reply and 200 failures (linear; inline output within budget; timing local only, R-SCALE-1). **B17** CRLF and
conflict markers in a SEARCH. **B18** delete a block that holds an issue, then write it again (orphan, then `revived`, R-ID-7/8).

Invariants checked on every turn of every scenario: a refusal leaves files and revisions byte-identical; a scenario run twice gives the same bytes; every
block anchor in a turn file resolves to a live block (R-VWR-7).

## 3. The consumer contract test

`consumer/mock_squad/tests/test_consumer_contract.cpp` asserts: (a) the project builds through CPM with public headers and the consumer's `-Werror`;
(b) every row of REQUIREMENTS.md "Sub0Squad's use, mapped" is exercised by at least one scenario, from an explicit list that fails when a row is
missing; (c) per scenario, the outcome sequence equals `# expect`, final files equal `--- want`, refusal texts contain the named strings, and
`expect-ids` hold (ids compared, never spelled, R-ID-11); (d) the persisted attempt is byte-identical to `fixtures/mock/<name>`; (e) the invariants
above; (f) the library's version (read from its own directory scope) equals `package.json`'s and is not older than the newest `v*` tag of the checkout (the `v0.3.0` tag said 0.2.1; the check failed on that and passes since the bump to 0.3.0). Behaviour comes from the
hand-written `expect` lines; regenerated files only prove the writer's bytes ([TESTING.md](TESTING.md): never compare against the code's own output).

## 4. What was built, by slice

| slice | what it proves |
|---|---|
| **MS0** | the consumer project, loop, gate and record writer; ten scenarios (G1, G2, G4, G6, B1, B3, B8, B9 twice, B12) frozen in the format of v0.3.0. Mutation proof: with `applyReply` made non-atomic, with a stale conflict applied silently and with no rename carry, the consumer contract goes red (b9 atomic, b8, g4) and green after the revert. The attempts are byte-stable across two runs. No library bug was found. The first run takes about 32 s, later runs about 1.3 s (label `full`; moving it to `smoke` is the owner's call) |
| **MS1 = B0** | `fixtures/mock` regenerated with `id`, `column`, ranges, `layerMeta`, `policies`, string revision ids, per-turn `events` with `eventsFrom`, `texts` by content id (15 scenarios: G3, G5, G7, B10, B11 added); `test/mock.test.js` opens every attempt in the viewer, traces an item by `id` through G4 (the title changes, so title matching would fail), checks per-turn and cumulative events agree and measures an 8-turn attempt against one turn (G7: 112,668 B by id, 2.5 times one turn at 45,828 B; 7.5 times with texts inline). R-U1-4 still needs one real capture |
| **MS2** | the producer API proposal: diff the mock loop against the squad's loop; generic turn policy (partial and prefill, rollback and kept file, continuation budget, the attempt record) is a candidate for the library, for example a pure `attemptRecord(...)` so naming and content ids live in one place. Answers section 6 |
| **MS3 = B1** | policy `useStore=yes`: each verdict's issues go through a `Store` over a `MemoryLog` (`putLayer`; the passing run offers an empty layer) and the verdict is built from what the store holds; it equals the direct verdict byte for byte (the contract re-runs every scenario with the policy flipped); `store/events.ndjson` and `store/snapshot.json` are persisted; replay and a checkpoint plus tail equal the live snapshot. B15 (gate rule `flap=<seed>`: fails on runs where run+seed is even) gives open, done, reopened with 6 events and 2 items; G4 gives one item and 5 events. `# expect-store` holds the hand-written truth. Off by default |
| **MS4 = B2, B3** | policy `claims=on` (control steps run; `claims/events.ndjson` and `claims/step-K.json` persisted) or `enforce` (a reply that changes a block another actor holds is refused on the scratch copy, before commit, with the library's refusal text). One `Store` replica per actor (writer `{"mock",actor}`) plus the loop's. Scenarios: `b14-claim-contention` (refused naming holder and expiry, files unchanged), `b14-claim-expiry` (frees at `at+ttl`, exclusive), `b14-claim-merge-order` (two offline takes, lamport 1 each, settled by `(lamport, writer)`; the contract re-runs it with the replicas merging in the other order and requires byte-identical claim files), `b8-claim-follows-merge` (a claim on a merged block covers the survivor; a stale id is refused naming the holder, then taken after the release). R-CONS-6, R-CONS-7 |
| **MS5** | header `# views: a=pinned b=tip` (needs `scratch=no`, not combined with claims) builds the session side: a separate store (writer `mock/ws`, own `MemoryLog`), `LogViewStore`, `DataSessionPolicy{}`, `BlockCompactor` and a `Workspace` over the canvas and the loop's index; one session and view per named actor. A pinned actor's reply goes `stage` then `land`; control steps `since`, `advance [atomic]` and `refresh form=... [ack]` check R-PIN-6 at the consumer. Scenarios: `b8-pinned-workers-land` (R-CONS-8: a disjoint edit lands rebased, an overlapping one is refused naming `mock/a` and the revision, `advance` keeps the refused hunk) and `b14-tip-worker-four-forms` (R-CONS-9: the four forms agree on `to`, `files` and `events` and reach the canvas text; an acknowledged refresh is empty). R-PIN-2, R-PIN-3, R-PIN-6 evidence |

Every new library call sits behind a new policy, so an earlier attempt's bytes never move when a slice is added: `git status fixtures/mock` shows only new directories.

### 4.1 How the control steps behave (MS4, MS5)

* A step with an `op` (`claim`, `release`, `expire`, `sync`, `since`, `advance`, `refresh`) has no body, spends no turn, writes no `reply-N.txt` and adds no layer turn; it records one outcome (`claimed`, `claim-refused`, `released`, `expired`, `settled`, `since`, `advanced`, `refreshed`). So `# expect:` has one entry per step run, and a control step still runs after the gate has passed (a reply after a pass is skipped and counted unused). A step with no `at=` runs at 0, so a `sync` or `expire` that a `holder` or `live` line reads needs an `at=`.
* A `release` names its target with `block=` or `file=` (as `claim` does) and releases the acting writer's live claim on it, resolved through merges. `claims/step-K.json`: K is the position in the scenario's steps; `expect-claims` lines take an optional trailing `@step` and add `covers <holder>:<k> <path:leaf>` beside `live`, `status`, `holder`.
* The step files and `status` lines are the loop replica's merged view, which merges every actor at each step: a take that loses on merge order reads `refused` from the first step after it; what each actor knew before the sync shows only in the refusal of its next step. `sync` merges in the order of `# actors`, then the loop's.
* The block index logs a merge only when the retired body has at least 3 distinct tokens (punctuation is not one) and one live block holds 60 percent of them, so `b8-claim-follows-merge` uses a body with `double y = x;` and a third function.
* The loop also proves, per claims scenario, that the persisted `claims/events.ndjson` parses back and replays to the live claims; `enforce` without `scratch=yes` is a scenario error.
* A pinned actor's refused hunk stays pending in the view after `land` (the library's rule), so its next reply stages on top of it until `advance` clears it. `b8-pinned-workers-land` has three gate tests so the gate does not pass before the overlapping edit and the `advance`. `b14-tip-worker-four-forms` uses two disjoint edits by `a` (a second overlapping edit through one pin would be refused against its own first). The fifth refresh, after the acknowledgement, has a later store head (the acknowledgement is itself logged), so the contract compares `to`, `files` and `events` from an acknowledged refresh up to the next one.
* Outcome kinds: `since` (the brief JSON, checked to change nothing: canvas files, the session store's event count, the view's pending summary), `advanced` (`applied N, refused M, anchors K, pin rB`; the verdict adds each refusal and the kept hunk), `refreshed` (`form=F files=N events=M [degraded] [ack]`). The loop builds the delta itself (`makeDelta` with no inbox, so `events` is 0 in these scenarios) and checks the answer's file count against it. Scenario errors: `since`/`advance` on a tip view, `refresh` on a pinned view, `base=rN` on a pinned actor's reply, `# views:` with `scratch=yes` or with claims, an `advance` that lands edits.

**Left**: a pinned actor's reads (`answerRead`) still show the tip, not the pin; an `advance` that lands edits through the loop; views combined with claims; fork, atomic land over two base revisions and `advance` with a moved held block (held anchors are never held in these scenarios, so `anchors` is always 0); the channel and presence; the inbox half of B2; the Lamport values stay derived by hand in each scenario's `expect-claims` comment; the scenarios of section 2 not built (B2, B4-B7, B10, B11, B13-B18 in part). Sub0Squad's copy of the loop is not touched.

## 5. Risks

* **Drift from the real squad.** Each scenario cites its T0 case or intervention; real goldens stay the truth; the shared scenario format lets the squad
  run the same files.
* **Squad policy copied here** (DRY across repositories): keep the loop minimal; every copied rule is an extraction candidate (MS2), never a second maintained
  copy.
* **Configure time** of the nested project: smoke only if about 30 s or less, else full.

Defaults taken, reversible by the owner: CPM is the one supported consumption path for now (`install(EXPORT)` is not built); the consumer project runs in the `full`
tier unless measured cheap enough.

## 6. Questions this answers for the Sub0Squad session

1. Which parts of the 1,100-line loop are generic and belong in the library, and what policy struct do they need?
2. Should the library own the attempt-record format, leaving the squad's `persistCanvas` thin?
3. `layeredJson` takes positional arguments (`int turn`, `map<string,int>`): should B0 bring a field-named `SnapshotSpec` (revision strings, policies,
   `eventsFrom`, texts)?
4. Should one scenario file be read by both this runner and the squad's `fake::Engine`, so it also runs in `test_canvas_agent`?
5. What does a second actor need: a stale-base inbox (changes to my blocks since rN), claims per block or file for the fork executor, and whether the
   model-facing refusal wording becomes a pinned text measured by an A/B there?
6. Should the gate's text formats that `parseIssues` relies on be specified here (a localise or issue-source policy) instead of implicitly in the squad?

### What the baseline taught (MS0, 2026-11-23)

The loop is about 370 lines (`consumer/mock_squad/src/loop.cpp`), 14 policy fields (`LoopPolicy`) and 25 library call sites; the ten scenarios run in about 0.1 s.

1. **Generic or the squad's own.** The order is generic: guard, join, parse, apply, compile check, commit, gate, revert, verdict. Candidates for the library, each
   copied from the squad into the mock and so a second copy: the guard's allowance rule (`aborts >= freeAborts && kind != "repeat"`, and `loopKept = soFar.substr(0, keep)`);
   `keptFileMadeWhole` (it parses a FILE header's path, a third place that does it after `parseCanvasReply` and the stream guard's scope check); the best-revision
   bookkeeping (`markBest`, `restoreBest`); the outcome vocabulary itself (read, cancelled, continued, refused, staged, failed, reverted, passed), which every consumer
   would otherwise invent. The policy struct they need is `LoopPolicy` as it stands. Not mirrored, so not yet a question the mock can answer: the prefill continuation,
   which is the squad's first choice when the context allows; `joinContinuation` is only its fallback and the path after a loop's rollback, so it sees less real traffic than R-TURN suggests.
2. **The attempt record.** The squad names a revision file by turning `/` into `_` (`src_calc.cpp.r1`; the design sketch said `%2F`): `src/a_b.cpp` and `src_a/b.cpp` give one name.
   A library-owned `attemptRecord` would fix that once. `persistCanvas` itself is ten lines; the record is thin only because the per-turn layer file and the revision log
   already come from the library (`layeredJson`, `logJson`).
3. **`layeredJson`.** The caller builds `int turn`, a `map<string,int>` of revisions and a `map<string,vector<LayerItem>>` on every verdict. And the baseline is not wholly "before
   B0": the issue items already carry `id` (the key), so MS1's diff shows `id` unchanged and the real additions are the rest (`column`, ranges, `layerMeta`, `policies`, string
   revision ids, `events`, `texts`). A field-named `SnapshotSpec` absorbed them (MS1).
4. **One scenario file for both runners.** A reply section is `fake::Reply` (content, finish, pieces), so a `.squad` file maps one to one onto `test_canvas_agent`'s
   engine; what the squad does not have is the gate (its harness is real) and `# expect`, which would become the squad's own assertions.
5. **A second actor.** `applyReply` cannot express a stale author: it applies every operation against the revision current when the call begins (`shown`). B8 therefore
   needs a second apply path in the loop (`applyStale`: `Canvas::apply` per file with `baseRevision`), which has none of `applyReply`'s allowed-path check, partial mode or `skipped`
   list. A `baseRevision` (or a per-path map) in `ApplyOptions` would give a stale reply the same semantics. The refusal itself already names the actor and the revision
   (`lines 5-5 were changed by a in revision 2 (lines 5-5): read the current text and redo this edit`); whether that wording is pinned and measured is still open. **Answered (MS1)**: `ApplyOptions::baseRevision` gives a stale reply the same semantics, and the loop's second apply path is gone.
6. **The gate's text formats.** `parseIssues` reads `FAIL n - name: message` and `path:line:col: error: message`, ignores lines holding ` | ` and appends indented lines to
   the item above; a test is localised only when its name is a function's last name or its message quotes a literal the function holds. The mock gate had to emit exactly
   that, so the formats are implicit in the library today. One consequence worth a decision: a compile diagnostic's key drops digits only, so two errors that differ only by a number
   (`expected 3 arguments`, `expected 4`) are one issue.

Smaller findings: the parser accepts an EDIT block that is never closed at the end of a reply (R-EDIT-6 refuses an empty block or one with no form, and a SEARCH that never closes),
so a consumer must decide a cut-off by the finish reason, not by a parse error. The stream guard judges after each streamed piece, so a loop's `detail` counts what had arrived (11 copies at 48
characters a piece, 10 being the limit): a message that quotes it is not stable across piece sizes. Partial application commits one revision per operation, so one reply of two operations
is two revisions in the log. No library bug was found. Not built, from section 2: B2, B4-B7, B10, B11, B13-B18 and G3, G5; the split and merge steps of G4 (a rename, a re-signing and a move
are there); `flap=<seed>` in the gate; the folded view the next message carries.

## 7. What to feed back to the Sub0Squad session (2026-11-24)

1. **The adoption is not closed**: the squad's adoption commits and the T0 comparison (`half-written`, `undeclared-member`, owner-run) are outstanding; one
   `test_canvas_agent` failure was seen once and never captured.
2. **B0 adoption**: move to `SnapshotSpec` (per-turn events, texts by id: 34% of today's bytes at 8 turns), use `%2F` revision-file naming (`_` maps
   `src/a_b.cpp` and `src_a/b.cpp` to one name), and capture one real attempt with compiler diagnostics (R-U1-4).
3. **Section 6's questions**: which loop parts become generic library code (the guard allowance, `keptFileMadeWhole`, best-revision restore, the outcome
   vocabulary), a library-owned `attemptRecord`, one `.squad` scenario file read by `fake::Engine` too, the gate's text formats, the compile-key digit collapse.
4. **The log module (Fabric)**: freeze `journal-1`'s tests as the behaviour to preserve; check its `seq` and `type` field names against the Canvas event
   fields; writer ids become composite `{namespace, writer}`; the portal's work registry is the merged reader's source list; the per-attempt segment
   option stays off by default.
5. **HTTP**: reconcile the squad's decision on Boost.Beast with the review's D10 (lift the portal's server and the engine client onto Platform `net`).
6. **Portal step 1**: Sub0Canvas will define a URL or view-spec scheme for deep links; the optional Node viewer reads the single projection definition (D11).
7. **Sub0Platform**: Canvas `app/` and `serve` become its second C++ consumer (proc, file, net).

## 8. Determinism across compilers (issue 4, 2026-11-24)

Two unspecified-order expressions made output depend on the compiler (gcc and the Windows toolchains evaluate call arguments right to left, Linux clang left to right); both are fixed so that every platform produces the same bytes:

* **The corpus generator** (`bench/corpus.cpp`): `std::format(..., r.below(4), ..., r.below(5), c.num())` and chained `operator+` with several generator draws. Every draw is now its own statement in the order the output always had, so no fixture or metric moved.
* **The loop's `failed` record** (`consumer/mock_squad/src/loop.cpp`): `layeredVerdict(..., focus)` and the `focus` argument were in one call, so gcc recorded an empty `focus` and clang the filled one. The verdict is now computed first (as the `staged` and `reverted` records already did), so a `failed` outcome carries its focus. The seven attempts with a `failed` outcome were regenerated; the diff is those `focus` values only (14 values in 7 files, checked mechanically). Sub0Squad's own copy of the loop should sequence the same way.

Rule for new code: never put two calls that draw from a generator, or that read and write the same variable, in one argument list or one operator chain.

# The mock squad: Sub0Canvas tested as a project the squad consumes

Status: design, in force for slice MS0 (2026-11-23). Evidence home for the consumer contract; the later Sub0Squad session reads section 6.

The squad is the first and most demanding consumer ([INTEGRATION.md](INTEGRATION.md)), but real models cannot be re-run on demand, and most of
the squad's canvas loop is policy, not library calls (`src/canvas_agent.cpp` is about 1,100 lines, about 20 of them library calls). The **mock
squad** is an ideal, deterministic stand-in that lives here: scripted actors, a reference loop, a gate that reacts to the files, and the
squad's persisted attempt format. It emulates good and bad behaviour so a library or format change is proved as a consumer would meet it, before
a Sub0Squad pull request exists. Real goldens (`fixtures/golden/`) stay the truth about real output (R-U1).

## 1. What it is

A separate CMake project, `consumer/mock_squad/`, that fetches the library exactly as Sub0Squad's `cmake/dependencies.cmake` does
(`CPMAddPackage(NAME Sub0Canvas)`, with `CPM_Sub0Canvas_SOURCE` pointing at this checkout in the test) and links `sub0canvas::sub0canvas`. It sees only
public headers and compiles with the consumer's own `-Werror`. Today there is no `install(EXPORT)`, so CPM is the one consumption path proved.
Run by a top-level `ctest --build-and-test` entry; labelled `full` unless its cost allows `smoke` (about 30 s).

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
"I give up" prose. **B14** a duplicate or retried delivery and two actors on one block (idempotent; the second rebased or refused). **B15** a flapping
test (same key, no new item). **B16** a huge reply and 200 failures (linear; inline output within budget; timing local only, R-SCALE-1). **B17** CRLF and
conflict markers in a SEARCH. **B18** delete a block that holds an issue, then write it again (orphan, then `revived`, R-ID-7/8).

Invariants checked on every turn of every scenario: a refusal leaves files and revisions byte-identical; a scenario run twice gives the same bytes; every
block anchor in a turn file resolves to a live block (R-VWR-7).

## 3. The consumer contract test

`consumer/mock_squad/tests/test_consumer_contract.cpp` asserts: (a) the project builds through CPM with public headers and the consumer's `-Werror`;
(b) every row of REQUIREMENTS.md "Sub0Squad's use, mapped" is exercised by at least one scenario, from an explicit list that fails when a row is
missing; (c) per scenario, the outcome sequence equals `# expect`, final files equal `--- want`, refusal texts contain the named strings, and
`expect-ids` hold (ids compared, never spelled, R-ID-11); (d) the persisted attempt is byte-identical to `fixtures/mock/<name>`; (e) the invariants
above; (f) `sub0canvas_VERSION` equals the release's version (the `v0.3.0` tag still says 0.2.1: fix at the next release). Behaviour comes from the
hand-written `expect` lines; regenerated files only prove the writer's bytes ([TESTING.md](TESTING.md): never compare against the code's own output).

## 4. Order of work

| slice | what | how the mock drives it |
|---|---|---|
| **MS0** | the consumer project, loop, gate, record writer; about 8 scenarios (G1, G2, G4, B1, B3, B8, B9, B12) frozen in today's format | the "before" picture for B0; brief [sessions/ms0-mock-squad-baseline.md](sessions/ms0-mock-squad-baseline.md) |
| **MS1 = B0** | regenerate `fixtures/mock` with `id`, `column`, ranges, `layerMeta`, `policies`, string revision ids, per-turn `events` with `eventsFrom`, `texts` by content id; `test/mock.test.js` opens every mock attempt in the viewer, traces an item by `id` through G4 (title changes, so title matching would fail), checks per-turn and cumulative events agree, measures an 8-turn attempt against one turn | B0 step 3 ([sessions/b0-format-catch-up.md](sessions/b0-format-catch-up.md)); R-U1-4 still needs one real capture |
| **MS2** | producer API proposal: diff the mock loop against the squad's loop; generic turn policy (partial and prefill, rollback and kept file, continuation budget, the attempt record) is a candidate for the library, for example a pure `attemptRecord(...)` so `%2F` naming and content ids live in one place, and a field-named `SnapshotSpec` for `layeredJson` | answers section 6 |
| **MS3 = B1** | the loop optionally writes events through a `Store` over a memory log; replay equals the live snapshot; B15 and G4 give the hand-checked open, done, reopened truth | before any tier-0 attempt |
| **MS4 = B2, B3** | two-actor scenarios (B8, B14) become inbox and claim tests: a refusal names the holder and expiry; a claim follows a merge | |

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

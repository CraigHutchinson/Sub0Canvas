# Scenarios

Whole attempts, written by hand, that a consumer lives through. Today only the mock squad's (`squad/`); scripted authoring sessions
([docs/AUTHORING.md](../../docs/AUTHORING.md) section 6) will join them.

## `squad/*.squad`: the mock squad's scenarios ([docs/MOCK_SQUAD.md](../../docs/MOCK_SQUAD.md))

One file is one attempt: a base, a gate that reacts to the files, the scripted model's replies, and what must follow. They are run by the reference loop in
`consumer/mock_squad/` and checked by `test_consumer_contract`. The expectations (`# expect`, `--- want`, `--- expect-ids`) are written by a person from the
requirement and the T0 case each cites; a regenerated attempt in `../mock/` proves only the writer's bytes ([docs/TESTING.md](../../docs/TESTING.md)).

```
# name: g1-symbol-fix          (the file's name)
# cites: ...                   (the T0 case or intervention it mimics, and the requirements it shows)
# actors: a b                  (who may send replies; the first is the default)
# allowed: a.cpp b.cpp         (files the actors may write; default: those of base and want)
# policy: partial=no scratch=no        (LoopPolicy overrides: partial, scratch, narrowScope, repairTurns, freeRejects, freeAborts, freeReads,
#                                       freeContinuations, issueMode, inlineIssues, maxEditLines)
# expect: failed | refused:the file has | passed
#                              (the outcome of each reply, in order: a kind, then `:` and a text that the outcome's detail or its verdict must contain.
#                               kinds: read, read-refused, cancelled, continued, empty, refused, staged, committed, failed, reverted, passed)
--- base <path>                (a file before; several allowed)
--- gate                       (test <name> passes when <path> contains|lacks <text>  /  compile <message> when <path> contains <text>)
--- reply [as=NAME] [base=rN] [finish=length|stop] [pieces=N]
                               (one reply of the scripted model; `base=rN`: an author holding revision N, applied with Canvas::apply; `finish=length`: cut off)
--- want <path>                (a file after; no other file may exist beside base and want)
--- expect-ids                 (id @N <path> <leaf> = <label> | gone @N <path> <leaf> | issue @N <n> = <label> | retitled <label> | anchor @N <n> <path> <leaf>;
                                @N is the turn file after N replies: `layers/turn-N.json`; ids and keys are compared, never spelled)
```

| scenario | shows | cites |
|---|---|---|
| `g1-symbol-fix` | one SYMBOL edit repairs the one failing function | tier 0 bugs-6 |
| `g2-read-symbol-then-issue` | READ SYMBOL, READ ISSUE (the issue in full with its code), then the edit | the squad's two free read replies |
| `g4-rename-resign-move` | a rename, a re-signing and a cross-file move over four replies while one compile error stays open: ids carry, the issue's key stays, its title changes | tier 0 undeclared-member, the demo story |
| `g6-worse-is-reverted` | a revision that fails more tests than the best is undone as a new revision | the squad's restoreBest |
| `b1-cutoff-overlap` | cut off inside an EDIT, the continuation repeats the lines before the cut | interventions rows 34, 35 |
| `b3-loop-rolled-back` | one line repeated inside a new file: stopped, kept, the worker moves on, the kept file is made whole | the 35B loop, 2026-11-18 |
| `b8-stale-base` | two actors: a stale edit on other lines is rebased; one on the same lines is refused naming the other actor and revision | the fork executor |
| `b9-file-and-bad-edit-atomic` | a FILE beside an EDIT that cannot apply, all or nothing, on the canvas itself: nothing changes | repair-new-file-and-edit |
| `b9-file-and-bad-edit-partial` | the same reply under the squad's own policy: the FILE lands, the EDIT is listed as skipped | the same |
| `b12-malformed-blocks` | an EDIT with no form, an empty SYMBOL block, prose with no edit: refused or told, then fixed | error-edit-without-form, error-prose-only |

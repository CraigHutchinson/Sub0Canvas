# Requirements: what consumers rely on, independent of how it is built

Status: living (2026-11-18). The catalogue the tests check. Use cases are in [USE_CASES.md](USE_CASES.md), the design that answers them in
[BACKBONE.md](BACKBONE.md) and [BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md), the testing method in [TESTING.md](TESTING.md).

## How to read and change this

* A requirement is **behaviour a consumer can observe**, stated without naming a type, a container or an algorithm. "A rename keeps the block's
  identity" is a requirement; "`BlockIndex` matches by token Jaccard >= 0.5" is a design choice that serves it, and may change.
* Each has an id that is never reused, the use cases it serves (U1-U11), a status (**built**: checked and true today; **design**: a target, with the
  slice that will build it) and its **check**: the test that fails when it stops being true. A test names the ids it checks in a comment.
* Tests of built requirements go through a **driver** where the architecture is expected to move (`lib/tests/doc_driver.hpp`): identities are
  compared, never parsed; blocks are found by name; history uses the format's carry names. When B1 puts a store in the path, the driver changes and
  the requirement tests do not.
* **R-REN** requirements are renderings a model reads. Their exact wording is the requirement, because a change of wording changes what the model
  does and is measured in Sub0Squad (TESTING.md, L4). They are pinned exactly on purpose; change them with an A/B, not to make a test pass.
* Changing a requirement is a design change: edit it here in the same commit as the test, and say why in the commit.

## U1: an agent's working document (built)

### The canvas (R-CAN)

| id | requirement | status | check |
|---|---|---|---|
| R-CAN-1 | A transaction applies entirely or not at all; a refused one changes nothing and keeps the revision. | built | `test_edits` (canvas), `test_canvas_model` |
| R-CAN-2 | Operations are numbered against the revision the author saw; several apply together; insertions after one line keep the order given. | built | `test_edits`, `test_views`, `test_canvas_model` |
| R-CAN-3 | Overlapping operations in one transaction are refused, saying so. | built | `test_edits`, `test_canvas_model` |
| R-CAN-4 | A stale transaction is rebased through later edits when it does not touch what they changed (every line it wrote lands, in order); otherwise it is refused naming the conflicting author and revision. | built | `test_edits`, `test_canvas_model` |
| R-CAN-5 | History is never rewritten: a revert is a new revision; every revision stays readable. | built | `test_edits`, `test_canvas_model` |
| R-CAN-6 | A function is replaced by name, down to a one-line function; a member sent from column 0 keeps its indentation; a name not yet present is added at the end of the file. | built | `test_edits`, `test_views` |
| R-CAN-7 | A whole-text replacement is a new revision; a path not yet on the canvas is added at revision 1. | built | `test_views` |
| R-CAN-8 | The log says who changed which span of which file into which revision, and each file's current revision. | built | `test_views` (`logJson`); format detail in Sub0Squad's portal |

### Edit replies (R-EDIT)

| id | requirement | status | check |
|---|---|---|---|
| R-EDIT-1 | Every reply form (`LINES`, `AFTER`, `SYMBOL`, `FILE`, `READ`, search/replace) means the same transaction as the operation it names. | built | `test_canvas_model` (LINES, AFTER via the reply text), `test_edits` |
| R-EDIT-2 | A SEARCH must match exactly once; otherwise the reply is refused naming the edit ("does not appear", "more than once"); other files are untouched. | built | `test_edits` |
| R-EDIT-3 | Line edits name lines of the draft shown; out of range is refused naming the file's length; one kind of edit per file per reply. | built | `test_edits` |
| R-EDIT-4 | Tolerance for what models really write: a copied number column, markers inside a LINES edit, indented markers, a merge conflict inside a SEARCH, a first line that lost its start, and indentation re-based to the file's. | built | `test_edits` |
| R-EDIT-5 | A path outside the allowed set is refused. | built | `test_edits` |
| R-EDIT-6 | Malformed replies (unclosed block, no edit block, an empty block) are refused naming what is wrong; a header written twice is read once. | built | `test_edits` |
| R-EDIT-7 | Every recorded reply shape seen on real runs gives its intended result, and the classic applier agrees where it applies; a refused reply leaves the canvas exactly as it was. | built | `test_replies` over `fixtures/replies` (30 cases, moved from Sub0Squad 2026-11-18 with `applyReply`) |
| R-EDIT-8 | A reply may be applied partially: every operation that can apply does, each against the revision the author was shown, and each part that cannot is named; a reply that failed to parse is refused whole. | built | `test_replies` |

### Turn control (R-TURN)

| id | requirement | status | check |
|---|---|---|---|
| R-TURN-1 | A reply cut off by the output limit and its continuation join into the reply that was meant: at a line boundary, with the overlap the model repeated removed once. | built | `test_turn_control` |
| R-TURN-2 | The continuation request quotes where the reply stopped and asks for the text that follows (mid-line or from the next line); a reply cut inside an edit block is recognised, so it is continued by appending. | built | `test_turn_control` |
| R-TURN-3 | A whole file cut off by the limit is completed from the previous revision (back to its last complete definition, then the previous functions after it), so no code on either side is lost. | built | `test_turn_control` |
| R-TURN-4 | A runaway reply is detected while it streams (a long SEARCH, a repeated cycle, a run of comments, a restated draft or echoed view), and a normal reply is left alone. | built | `test_turn_control` |
| R-TURN-5 | An edit overrun is detected: a SEARCH covering most of an existing file (a rewrite in disguise) or one edit past the size limit; a stub or new file is not judged. | built | `test_turn_control` |
| R-TURN-6 | A runaway that is a loop says what repeated (`Runaway::detail`, for example the same lines some times over), so a caller can write a message of its own; other runaways leave it empty. | built | `test_turn_control` |
| R-TURN-7 | A C-like source file cut off part way through a block is recognised (`endsInsideBlock`): braces still open at its end, ignoring braces in strings, characters and comments and counting an unfinished last line; a closed file and empty text are not. | built | `test_turn_control` |

### Views and reads (R-VIEW)

| id | requirement | status | check |
|---|---|---|---|
| R-VIEW-1 | A file that fits its budget is shown whole; otherwise function bodies fold to one line naming the folded range, and structure (includes, classes, namespaces, signatures) stays. | built | `test_edits` (folded views) |
| R-VIEW-2 | Lines named by a failure keep their region open while the budget allows; bodies of one or two lines are never folded. | built | `test_edits` |
| R-VIEW-3 | By default only functions worth folding are regions. | built | `test_views` |
| R-VIEW-4 | Addressing by name reaches every function, down to one line. | built | `test_views` |
| R-VIEW-5 | A section view opens the owner's function and folds its neighbours, naming each fold. | built | `test_views` |
| R-VIEW-6 | A read names lines or a symbol and gets numbered lines; a read of a path alone gets the outline, never the whole file; a read of what does not exist gets a short message. | built | `test_edits`, `test_views` |
| R-VIEW-7 | The language of a file is chosen from its path. | built (`.py` only; `isPythonPath` stays for callers that choose themselves) | `test_views`. Detection from file name, extension and `#!` is R-BLK-8 (`Languages::detect`, `test_outline`). |

### Skeletons (R-SKEL)

| id | requirement | status | check |
|---|---|---|---|
| R-SKEL-1 | `@@ signature` lines become functions whose bodies raise "not implemented: name", in Python, C++ and JavaScript; each is addressable by name. | built | `test_views` |
| R-SKEL-2 | C++ stubs compile cleanly under strict warnings: `<stdexcept>` added, named parameters used, prototypes for file-scope functions (none for methods). | built | `test_views` |
| R-SKEL-3 | A line that is not a signature, or a function declared twice, is refused with its line; a file with no `@@` is unchanged. | built | `test_views` |

### Block identity (R-ID)

| id | requirement | status | check |
|---|---|---|---|
| R-ID-1 | Every function-like block has an identity; a line resolves to the innermost block holding it; text outside every block resolves to none. | built | `test_doc_layers` |
| R-ID-2 | A SYMBOL replace keeps the identity; a block that moved keeps its own and resolves to its new lines. | built | `test_doc_layers` |
| R-ID-3 | A block's history records each change of its text with the turn it happened in; an untouched block records nothing. | built | `test_doc_layers` |
| R-ID-4 | A whole-file rewrite that reorders, re-signs and renames keeps every identity and says how (`resigned`, `renamed`). | built | `test_doc_layers` |
| R-ID-5 | A split keeps the original's identity; the new part gets its own. | built | `test_doc_layers` |
| R-ID-6 | A merge sends the merged identity to the survivor, and the survivor's history includes the merged block's. | built | `test_doc_layers` |
| R-ID-7 | A deleted block's items are orphans, and every rendering says the function no longer exists. | built | `test_doc_layers` |
| R-ID-8 | A block written again gets its identity back (`revived`). | built | `test_doc_layers` |
| R-ID-9 | A block moved to another file keeps its identity there. | built | `test_doc_layers` |
| R-ID-10 | Indentation-based languages get identities by the same rules. | built (Python) | `test_doc_layers` |
| R-ID-11 | Identities are opaque: consumers compare them and never derive meaning from their spelling. | built (as a test rule) | the driver; `test_golden_replay` maps ids one to one rather than comparing spellings |

### The issues layer (R-ISS)

| id | requirement | status | check |
|---|---|---|---|
| R-ISS-1 | A failing test lands on its suspect block (named like the test, or holding the literal under test, rarer tokens weighing more), marked suspect. | built | `test_doc_layers`, `test_golden_replay` |
| R-ISS-2 | A literal held by several blocks points at none of them: no pointer is better than a wrong one. | built | `test_doc_layers` |
| R-ISS-3 | A diagnostic lands exactly on the block holding its line; one message at several places is one item with an anchor per place; the first occurrence keeps its context. | built | `test_doc_layers` |
| R-ISS-4 | A diagnostic outside every block keeps its path and line. | built | `test_doc_layers` |
| R-ISS-5 | An item's key is the same across turns whatever its number. | built | `test_doc_layers`, `test_golden_replay` |
| R-ISS-6 | Python's syntax check, naming a line, lands like a diagnostic. | built | `test_doc_layers` |
| R-ISS-7 | A runner that reports no expression is localised from the test's own function in the test sources given; without them, no anchor. | built | `test_doc_layers` |
| R-ISS-8 | Carriage returns from a Windows runner, even inside a line, change nothing. | built | `test_doc_layers` |

### Renderings a model reads (R-REN: exact wording pinned on purpose)

| id | requirement | status | check |
|---|---|---|---|
| R-REN-1 | The summary: a count line naming the read keyword, then one line per item with its title and where it is ("likely in term() (lines 16-24)", "in 3 places: ..."). | built | `test_doc_layers` |
| R-REN-2 | An item in full: the producer's text, then its block's CURRENT numbered lines, or that it no longer exists, or its loose line. | built | `test_doc_layers` |
| R-REN-3 | The first few items in full within a character budget (the first always), a function shown once then named, the rest left to a read. | built | `test_doc_layers` |
| R-REN-4 | Focus lines for every item that resolves to a block, so folded views keep them open. | built | `test_doc_layers` |
| R-REN-5 | `READ: ISSUE n` (and `ISSUES a, b`) is read in order; a path containing "ISSUE" is not. | built | `test_doc_layers` |

### The interchange format (R-FMT)

| id | requirement | status | check |
|---|---|---|---|
| R-FMT-1 | A snapshot is valid JSON with the schema name, the turn, each file's text and revision, the blocks, their history and every layer's items and anchors. | built | `test_doc_layers`, `test/golden.test.js` |
| R-FMT-2 | The format grows only additively: an old reader ignores new fields; an old file means what it meant. | built (as a rule) | `test/model.test.js` (loose files, cumulative events), `test/mock.test.js` (the string-only shape) |
| R-FMT-3 | A snapshot writes each item's `id`, each anchor's `column` and range where known, a `layerMeta` entry for the issues, each file's `revisionId` and `textId`, and the `policies` that made it. | built | `test_doc_layers`, `test/mock.test.js` |
| R-FMT-4 | A snapshot's `events` are those since the previous one, `eventsFrom` says where they start in the log, and the cumulative log holds the same events in order. | built | `test_doc_layers`, `test/mock.test.js` |
| R-FMT-5 | Texts can travel by content id: a text is carried once per document, a snapshot alone carries what it needs, and the viewer reads them as inline texts; 8 turns cost a few times one. | built | `test_doc_layers`, `test/mock.test.js` (g7: 2.5x) |

### The use case end to end (R-U1)

| id | requirement | status | check |
|---|---|---|---|
| R-U1-1 | Replaying a real attempt gives the same blocks per file (names and lines) as the producer recorded. | built | `test_golden_replay` |
| R-U1-2 | Identities correspond one to one with the recorded ones across every turn of the attempt. | built | `test_golden_replay` |
| R-U1-3 | Each turn's identity history matches (carry and block). | built | `test_golden_replay` |
| R-U1-4 | The same verdict gives the same issues: keys in order, kinds, anchors on the same blocks with the same confidence. | built (test failures only) | `test_golden_replay`. **Gap**: no golden attempt with compiler diagnostics yet; capture one at B0. |

### The viewer (R-VWR, U7)

| id | requirement | status | check |
|---|---|---|---|
| R-VWR-1 | Folders of attempts, or loose turn files, load as documents; a malformed file is reported, not fatal. | built | `test/model.test.js`, `test/golden.test.js` |
| R-VWR-2 | Layers are whatever the files hold; every revision of every file is available. | built | `test/model.test.js` |
| R-VWR-3 | A block's identity history across turns: kept, renamed, re-signed, merged, retired, revived; orphans listed and reattached on revival. | built | `test/model.test.js` |
| R-VWR-4 | Code to layers and back: everything attached to a block, and one item fanned out to all its anchors in stepping order. | built | `test/model.test.js` |
| R-VWR-5 | An item is traced across turns by its id, not its title. | built | `test/model.test.js` |
| R-VWR-6 | Cumulative and per-turn events both read as per-turn. | built | `test/model.test.js` |
| R-VWR-7 | Real producer output validates, every item has a stable id, and every block anchor resolves to a live block. | built | `test/golden.test.js` |

## Sub0Squad's use, mapped (the coverage audit of 2026-11-18)

Every library call Sub0Squad makes (`grep` over its `src/`), the requirement it relies on, and where that is checked. A row whose check is only in
Sub0Squad is a gap here.

| Sub0Squad use | relies on | checked here |
|---|---|---|
| `canvas_agent.cpp`: the layered verdict (`parseIssues` with test sources, `layerSummary`, `layerInline`, `layerFocus`, `layeredJson` per turn) | R-ISS-1..8, R-REN-1..4, R-FMT-1, R-U1 | yes |
| `canvas_agent.cpp`: READ ISSUE answered by `layerDetail` | R-REN-2, R-REN-5 | yes |
| `canvas_agent.cpp`, `llm_agent.cpp`: reads (`parseReadRequests`, `answerRead`) | R-VIEW-6 | yes |
| `canvas_agent.cpp`, `blocks.cpp`, `fork.cpp`: `parseCanvasReply`, `applyEdits`, `replaceText`, `revertTo`, `textAt`, `logJson` | R-EDIT-1..6, R-CAN-1..8 | yes |
| `canvas_agent.cpp` (`applyCanvasReply`, now `applyReply` here): a parsed reply applied all or nothing across files | R-EDIT-7, R-EDIT-8, R-CAN-1 | yes (moved here 2026-11-18; a Sub0Squad PR adopts it) |
| `canvas_agent.cpp`, `llm_agent.cpp`: `joinContinuation`, `continuationText`, `continuationPrompt`, `detectRunaway`, `detectEditOverrun` (turn control, moved here) | R-TURN-1..7 | yes (moved here 2026-11-18; a Sub0Squad PR adopts it) |
| `fork.cpp`, `contract.cpp`, `test_mends`: `expandSkeleton`, `sectionView`, `foldRegions(…, all)` | R-SKEL-1..3, R-VIEW-4, R-VIEW-5 | yes (moved here from Sub0Squad's `test_blocks`) |
| `agent_card.cpp`, `fork.cpp`: `foldedView` | R-VIEW-1, R-VIEW-2 | yes |
| 19 calls of `isPythonPath` to choose the language | R-VIEW-7 | yes (and replaced by R-BLK-8 at D0) |
| the adaptive threshold, clipping, the `SUB0SQUAD_ISSUE_*` switches | squad policy, not library behaviour | stays in Sub0Squad |

## The consumer contract (R-CONS; MOCK_SQUAD.md section 3)

What a consumer such as Sub0Squad relies on, proved by the mock squad (`consumer/mock_squad/`), a separate project that fetches this library through CPM and
links only its public target.

| id | requirement | status | check |
|---|---|---|---|
| R-CONS-1 | The library builds as a CPM dependency with public headers alone, under the consumer's own `-Werror`, and says the release it is: the same version in `CMakeLists.txt` and `package.json`, never older than the newest tag. | built | `test_consumer_mock_squad` (`ctest --build-and-test`), check (a) and (f) of `test_consumer_contract` |
| R-CONS-2 | Whole attempts as the squad lives them (a one-shot fix, reads, a rename, a re-signing and a move over turns, a cut-off continuation, a loop rolled back, two actors, a FILE beside a bad EDIT, malformed blocks, a worse revision undone) end as the scenario's hand-written outcome sequence, final files, refusal texts and identity expectations say. | built | `test_consumer_contract` over `fixtures/scenarios/squad` |
| R-CONS-3 | A refused reply leaves every file and revision as it was, checked on the canvas itself; every block anchor of a turn file resolves to a live block; a scenario run twice gives the same bytes, equal to the stored attempt in `fixtures/mock`. | built | `test_consumer_contract`, checks (d) and (e) |
| R-CONS-4 | Every row of "Sub0Squad's use, mapped" is exercised by a scenario or deferred in writing with its reason; a new row fails the test until it is mapped. | built | `test_consumer_contract`, check (b) |

## U2-U11: requirements the groundwork must meet (design)

Stated now so that B0-B4, G1-G5, S0-S4, D0-D3, E1 and C1 are built to them, and so that nothing built for U1 forecloses them. Each becomes a test when
its slice is built.

### Block discovery (R-BLK; BLOCK_DISCOVERY.md)

| id | requirement | serves | slice | status | check |
|---|---|---|---|---|---|
| R-BLK-1 | Discovery is deterministic and total: any text, never a throw; syntax errors give a partial outline, marked so. | all | D0 | built (the heuristic providers) | `test_outline`: every prefix of every sample and 300 noise texts, never a throw, same outline twice; `partial` marked when a block never closes |
| R-BLK-2 | Linear in the size of the file. | U4 | D0, S0 | by construction (one pass per provider) | `Sub0Canvas_Bench` foldRegions rows (S0); no per-provider linearity test yet |
| R-BLK-3 | Nodes nest: a child within its parent, siblings disjoint, spans within the file. | all | D0 | built (flat outlines today) | `test_outline`: spans inside the file and the name line inside the span, over prefixes and noise; nesting of children waits for providers that emit them (D1) |
| R-BLK-4 | A node that carries an identity has a non-empty name; one that cannot be named never carries an identity. | U1, U3 | D0 | partly: names for generics, arrow functions, Go receivers and operators; an anonymous function still carries an identity | `test_outline` (R-BLK-4 cases); the identity policy that skips unnamed nodes is not done |
| R-BLK-5 | A brace, keyword or marker inside a string or comment never opens or closes a block. | all | D0 | built for block comments (`braces@2`); strings were already skipped | `test_outline` (R-BLK-5 cases) |
| R-BLK-6 | Decorators, attributes and doc comments are inside their block's span. | U1, U5 | D0 | built for Python decorators (`indent@2`); attributes and doc comments of C-like text are not yet | `test_outline` (R-BLK-6 case: span, name line, fold views unchanged, block index) |
| R-BLK-7 | Each supported language has a hand-checked conformance corpus, and a provider replaces another for a language only when it scores at least as well. | all | D1 | open | D1 |
| R-BLK-8 | The language is detected from the path, filename and first line, not from one suffix. | all | D0 | built: file name, extension, then the `#!` interpreter (no modelines); `.py` is the only language with a provider of its own | `test_outline` (R-BLK-8 cases) |
| R-BLK-9 | Data formats and documents have blocks named by their standard paths (JSON Pointer, element paths, heading paths). | U3, U9 | D2 | open | D2 |
| R-BLK-10 | A change of provider for a language keeps identities by the usual rules and reports, never silently retires, blocks the new provider does not see. | U3, U4 | D0, D3 | open | the `index.outliners` field and the reconciliation report are not written; provider names carry versions (`braces@3`, `indent@2`) ready for them |
| R-BLK-11 | Structure is derived: the same text, language and provider give the same outline and the same identities, whatever view or caller asks. | all | D0 | built | `test_outline`: foldRegions over the providers equals the regions of the outline for the path; a view never moves a block |
| R-BLK-12 | Folding is the caller's: a view chooses what is open, to what depth, within what budget; no view request changes the document, its identities or its history. Identity granularity is a per-document policy, recorded in the snapshot, changed only as a reported migration. | U1, U5, U7 | D0 | partly: the fold policy is `foldRegionsOf`, a view of its own; identity granularity is not yet a recorded per-document policy | `test_outline`; `index.identity` is not done |

### The backbone (R-BB; BACKBONE.md section 3)

| id | requirement | serves | slice |
|---|---|---|---|
| R-BB-1 | Every change is an event; replaying the log gives the same store, byte for byte in its snapshot. | U2-U6 | B1, built: `test_store` (replay, JSON-line messages and any arrival order give the live snapshot bytes), `Sub0Canvas_Bench_metrics` (1e5 events) |
| R-BB-2 | A re-derived layer reconciles by key: a new key is open, a vanished key is done by its source, and nothing is deleted. | U2, U3 | B1, built: `test_store` (a 5-turn layer, hand-written states and event counts; reopen, never duplicate) |
| R-BB-3 | An item's lifecycle is open, held, done, verified, dropped; orphaned is derived, never stored; a transition names its actor. | U2, U6 | B1, built: `test_store` (lifecycle, actor required, `orphaned` only through `isOrphaned`) |
| R-BB-4 | An inbox delivers what matched since the reader's cursor, summary first; the cursor moves only on acknowledgement. | U2, U3, U6 | B2 |
| R-BB-5 | Sub0Squad's verdict rebuilt as an inbox is byte-identical to today's. | U1, U2 | B2 |
| R-BB-6 | At most one write claim per target; a refusal names the holder and expiry; claims expire against a clock passed in; a block claim follows merges. | U2, U6 | B3 built: `test_claims` (two writers, a conflicting claim refused naming holder and expiry, expiry and release free it, renew, concurrent takes settled by merge order, replay equals live, a block claim follows a merge and a split and ends on a retirement, notes, unknown kinds survive); the claim metrics in `perf/metrics.json` |
| R-BB-7 | The core reads no clock, environment, network or file: time, logs and sources come in through interfaces. | all | all; B1 built: `test_store` (no clock supplied gives no time, identical calls give identical bytes, merged events not written back); the library has no I/O includes |
| R-BB-8 | Every mutation and every read is expressible as a serialisable message, so one process can own a store and others talk to it. | U5, U6 | B1 (A10), built at the event level: `test_store` (every mutation is an event that round-trips through `eventJson`/`parseEvent`); the stdio protocol is V1 |
| R-BB-9 | Unknown event kinds and fields survive a replay untouched. | all | B1 (A7), built: `test_store` (unknown kind, envelope member and payload field survive byte for byte) |
| R-BB-10 | Exports and snapshots take a query, so what is not asked for (a private note) never leaves by default. | U3, U6, U8 | B1 (A11), built: `test_store` (a `private/` item leaves only when its tag is asked for; scoped exports keep READ numbers) |

### Sources, history and anchors (R-SRC; BACKBONE.md sections 5 and 6)

| id | requirement | serves | slice |
|---|---|---|---|
| R-SRC-1 | Adapters never compute identities; they give source anchors (revision, path, range, quote), and one resolver maps every source's anchors by the same rules. | U3 | B4 |
| R-SRC-2 | An anchor that cannot be resolved is shown outdated, never silently moved. | U3, U6 | B4 |
| R-SRC-3 | A block's identity history follows it across a repository's commits, renames and moves, checked against hand-checked truth. | U3, U4 | G1: built (`test_revision_index`, `test/history.test.js`; truth in `fixtures/scenarios/g1-tinycalc`, an invented repository; real histories wait for S3) |
| R-SRC-4 | Two pending changes (PRs) touching one block are reported as a conflict before either merges. | U3 | G1, G2 |
| R-SRC-5 | Revision ids are opaque strings; a canvas's numbered revisions are one kind among others. | U1, U3 | B0 (A1): written (`revisionId`), read; G1: a `Canvas` as a second `DocumentSource` (test adapter), the same index over both |
| R-SRC-6 | Anchors may span a range (start and end line and column). | U3, U5, U8 | B0 (A3): written from compilers' ranges, read |
| R-SRC-7 | Nothing is written back to a source without the consumer's confirmation; a rejected proposal leaves no trace there. | U3, U6 | G5 |
| R-SRC-8 | Tests read captured data; no test uses the network. | all | all (G1: `fixtures/git`, captured plumbing output; the live-git test runs only local repositories it builds) |

### Scale (R-SCALE; SCALE.md)

| id | requirement | serves | slice |
|---|---|---|---|
| R-SCALE-1 | Cost is proportional to what is touched (paths viewed, anchored, subscribed to or changed), never to the repository. | U4 | S0-S4 |
| R-SCALE-2 | The budgets of SCALE.md section 6, once measured, hold at 10^5, 10^7 and 10^9 lines (locally, label `timing`). | U4 | S0, S1 |
| R-SCALE-3 | Identity allocation needs no global counter: shards and per-path replays allocate without coordinating. | U4 | S1, S4 (A2) |
| R-SCALE-4 | Sketch-based candidate search loses no identity that the exhaustive rule keeps, on the synthetic corpus. | U4 | S2 |
| R-SCALE-5 | Every path is classified before it is parsed; binary files get no blocks, and data and sidecar files are indexed only when the document's policy asks. | U4 | D0, S1 |
| R-SCALE-6 | Per-blob tables are shared by every clone and worktree of a repository on one machine; an extra clone costs only its own small state. | U4, U12 | S1 (A19) |
| R-SCALE-7 | A missing object (partial or shallow clone) makes an answer unknown, never an error; the view still renders. | U4, U3 | G1 (A20): built (`FileText::unknown`, `Replayed::unknown`, `historyCut`; a missing text, a missing tree, a shallow and a partial clone) |
| R-SCALE-8 | A trunk merge costs in proportion to the paths it changed and the open changes whose footprints it meets, not to the number of open changes. | U4, U12 | S4, P1 |
| R-SCALE-9 | Per-path history never walks commits that did not change the path (git's changed-path filters, or an equivalent index). | U4, U3 | G1 (A21): built (one `git log -- <scope>`; a replay reads and applies only the revisions it lists; exact metrics `history sparse scope`) |

### Change views (R-CHG; CHANGE_VIEWS.md, U12)

| id | requirement | serves | slice |
|---|---|---|---|
| R-CHG-1 | A change's footprint is the set of blocks its diff modifies, resolved at its merge base and carried to the viewed revision. | U12, U3 | P0 |
| R-CHG-2 | Overlap and conflict are relations between blocks, never inferred from shared files alone; shared files are reported only as a count. | U12 | P1 |
| R-CHG-3 | Every derived lineage link (carved from, supersedes, integrates, duplicates, depends on, ports) carries its evidence, a confidence and the policy version that made it; an author's statement overrides it, and both are kept. | U12 | P2 |
| R-CHG-4 | Each hunk of a spike is in exactly one of landed, in flight, not carved, obsolete or dropped, against the target branch. | U12 | P2 |
| R-CHG-5 | A rewritten carve-out still matches its spike hunk (patch id, or same block with similar text) at the recall P2 states. | U12 | P2 |
| R-CHG-6 | A topic view opens summary first within its budget, whatever the number of changes and files in the topic. | U12, U7 | P3 |
| R-CHG-7 | Changes need no forge: local branches, stacks and worktrees are changes from the git adapter alone. | U12 | P0 |
| R-CHG-8 | Nothing is created at a forge or tracker (a branch, a pull request, a closure) without the consumer's confirmation. | U12 | P5 |
| R-CHG-9 | A private proving ground publishes only anonymised numbers; no name, path, title or link of its source enters this repository. | all | all |

### Editors, collaboration and exports (R-ED, R-COL, R-EXP)

| id | requirement | serves | slice |
|---|---|---|---|
| R-ED-1 | An editor's margin shows the layers of what is open, and they follow a function moved by an edit. | U5 | E1 |
| R-ED-2 | Nested blocks and their kinds are available for an editor's outline. | U5 | D0 |
| R-COL-1 | Every comment survives the author's later edits on the right block; a reviewer's inbox lists exactly the unaddressed ones. | U6 | C1 |
| R-COL-2 | Replies to a comment are items linked to it (a thread), with no mechanism of their own. | U3, U6 | B1 (A5), built: `test_store` (a thread is `repliesTo` links and the reverse index) |
| R-EXP-1 | The issues layer exports as SARIF with every location and a fingerprint equal to the item's key. | U8 | later |
| R-EXP-2 | Numeric per-block data (coverage, churn, durations) rides on items without a mechanism per metric. | U8 | B1 (A4), built: `test_store` (item `data` kept verbatim, a change of it is a change of the item) |

### The authoring tool (R-AUTH; AUTHORING.md, U7)

| id | requirement | serves | slice |
|---|---|---|---|
| R-AUTH-1 | Every view is bounded by attention, not data: it takes a scope and a budget and returns a summary with counts, the first few in full, and cursors for the rest; nothing loads a whole log or history. | U7, U4, U2 | V0-V2 |
| R-AUTH-2 | Opening the tool shows what needs the reader (their inbox, changes since they last looked); history is a drill-down. | U7 | V2 |
| R-AUTH-3 | A comment is anchored to a block and to the selected text; after edits it stays on the right text or is shown outdated, never moved silently. | U7, U6 | V1, V3 |
| R-AUTH-4 | A thread is items linked to the comment; addressing (`done`) and accepting (`verified`) are state changes that name their actor and reason. | U7, U6 | V1 |
| R-AUTH-5 | An agent reads its inbox and replies through the same commands a person's page uses; no agent-specific mechanism. | U7, U2 | V2 |
| R-AUTH-6 | Saving from the page is a transaction against the revision shown: a concurrent edit is rebased or refused, never overwritten. | U7, U6 | V3 |
| R-AUTH-7 | Only allowlisted local commands run from the tool; their results become a checks layer. | U7 | V4 |
| R-AUTH-8 | A push happens only after an explicit confirmation each time, and through the repository's own gate. | U7 | V4 |
| R-AUTH-9 | The server listens on loopback only and refuses requests without the session's token. | U7 | V1 |
| R-AUTH-10 | A scripted scenario (actors, commands, expected views) replays deterministically against the same protocol and is the test of every authoring slice. | U7, U6 | V1-V5 |
| R-AUTH-11 | The viewer still opens a folder of snapshots read-only from `file://`. | U7, U1 | V0 |
| R-AUTH-12 | An update from another actor reaches an open page as events that change only what they name; the reader's scroll, selection, open folds, expanded tree and unsent input are never reset by one, and a selection changed underneath is marked, not replaced. | U7, U6, U2 | V1a |
| R-AUTH-13 | A click costs what is on screen, not what is loaded: lists render a window of rows, and no pane is rebuilt when nothing it shows changed. | U7, U4 | V1a |
| R-AUTH-14 | A text is sent to the page once, by content id; a later turn or view sends only the texts the page has not seen. | U7, U4 | V1a; the format half is built at B0 (R-FMT-5) |
| R-AUTH-15 | Every viewer interaction has a latency budget (AUTHORING.md section 3.2), measured at the reference scale by a scripted local benchmark and recorded with the machine; a change that breaks a budget says so. | U7, U4 | K0 |

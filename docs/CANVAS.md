# The canvas: a revisioned document edited by transactions, seen through folded views

Status: **built** (`lib/include/sub0canvas/{canvas,outline,edits}.hpp`, `test_edits`), imported from Sub0Squad on 2026-11-17, where it was designed and
proved on real models (Sub0Squad `docs/CANVAS.md`, which keeps the squad's executors, the fork coordinator and the run history). This document is
the library's half: the data model, the operations, the views, and the decisions behind them. The layers and block identities on top are in
[LAYERED_DOCUMENT.md](LAYERED_DOCUMENT.md); where it is all going is [BACKBONE.md](BACKBONE.md).

## The idea

A small model with a small window cannot be trusted to carry whole files through a conversation, to keep line numbers straight across several edits, or
to rewrite a file without breaking what it did not mean to touch. The canvas fixes each of those with one rule: **the document is the state, and an edit
is a transaction against a revision.**

| problem seen with agents | the canvas's answer |
|---|---|
| line numbers go stale when several edits apply | a transaction names its base revision; its numbers are rebased through the edits made since, or it is refused with the conflicting edit named (`Canvas::apply`) |
| the window fills with copies of files and earlier replies | the consumer rebuilds each turn from the current revision (a folded view); the reply is only a transaction |
| a model rewrites the whole file as one SEARCH block | `replaceSymbol` replaces one function by name; `replaceLines` and `insertAfter` for the rest; a whole-file replacement is a deliberate, separate operation (`replaceText`) |
| a half-valid edit leaves a broken file | all operations of a transaction apply or none; a refusal says which operation failed and why |
| an edit that went wrong cannot be undone | every revision is kept; `revertTo` restores one as a new revision; the log says who changed which lines |
| two workers in one file | the same rebase and conflict rule, then claims on sections ([BACKBONE.md](BACKBONE.md) 3.5) |

## The model (`canvas.hpp`)

| type | what it is |
|---|---|
| `Canvas` | files, each a list of revisions (revision 1 is the first text); `addFile`, `text`, `textAt`, `revision`, `apply`, `replaceText`, `revertTo`, `log`, `logJson` |
| `Transaction` | `path`, `baseRevision` (the revision the author was **shown**: set by the consumer, never copied by a model), `by`, and `ops` |
| `CanvasOp` | `replaceLines` (inclusive range, empty text deletes), `insertAfter` (0 = at the top), `replaceSymbol` (a function by name, signature to closing line) |
| `TxResult` | `ok`, the new `revision`, or an `error` naming the line, symbol or conflicting edit |
| `CanvasLogEntry` | who, which revision from which base, the span changed (in the numbering of the revision before), lines added or removed |
| `CanvasReply` / `parseCanvasReply` | a model's reply read as transactions (below) |

Rules:
1. **All or nothing.** Operations that overlap, a line out of range, an unknown symbol: the whole transaction is refused and the file is untouched.
2. **Rebase, never merge silently.** An edit made against revision r when the file is at r+k is rebased when its span does not overlap what changed
   since; otherwise it is refused with the overlapping edit named.
3. **History is never rewritten.** A revert is a new revision. `logJson()` (`{"files":{path:rev},"log":[{path,by,revision,base,from,to,delta}]}`)
   is what a viewer reads to say who changed what; the viewer also reads each revision's text from `canvas/<flat>.r<N>` files the consumer writes.

## The reply forms (`parseCanvasReply`)

The forms are the ones models already write, read as transactions, plus one:

```
=== EDIT: path LINES a-b ===  (or LINE a)      -> replaceLines
=== EDIT: path AFTER n ===                      -> insertAfter
=== EDIT: path SYMBOL name ===                  -> replaceSymbol: the whole function, signature to closing line
=== EDIT: path === with SEARCH/REPLACE          -> kept in searchEdits, applied by applyEdits (edits.hpp)
=== FILE: path ===                              -> wholeFiles: a deliberate whole-file replacement
=== READ: path LINES a-b | SYMBOL name ===      -> reads (outline.hpp)
=== READ: ISSUE n ===                           -> an issue in full (doc_layers.hpp, parseIssueReads)
```

Operations on one path form one transaction. A number column copied from the view (`  12| code`) is stripped only when **every** line of the block
has it (code that starts with `1| x` survives), and a markdown fence is removed. A block that cannot be read is named in `error`, and then nothing in
the reply is applied.

## Tolerant search/replace (`edits.hpp`)

`applyEdits` applies SEARCH/REPLACE blocks to a set of files, matching tolerantly (whitespace, a number column, a near-identical block). When the match
ignored indentation, the REPLACE text takes **the matched lines' indentation** (a tier 0 trace found five functions shifted right by a model that
indented its SEARCH text by 5 instead of 4). `replacementOfMarkedBlock` handles the marked-block form.

## Folded views and reads (`outline.hpp`)

A small window cannot hold a large file, and a model cannot patch what it cannot see. So the consumer shows the structure and lets the model ask for
the lines it needs, like an editor that folds braced regions:

* `foldRegions(text, python, all)`: function-like blocks by braces (C++, JavaScript) or indentation (Python). Namespaces and classes stay open so their
  members are blocks. `all = true` also returns one- and two-line functions, so a stub can be replaced by name. This is also the block discovery the
  layered document's `BlockIndex` uses.
* `foldedView(text, python, focusLines, budget)`: the file numbered (`  12| code`), whole when it fits, else each body collapsed to
  `... (41 lines folded: 7-47; name)`. Regions holding a focus line stay open, nearest first, within the budget. `layerFocus` (doc_layers) supplies
  focus lines from issues.
* `sectionView(text, python, openSymbols)`: everything folded but the named functions.
* `parseReadRequests` / `answerRead`: `=== READ: path LINES a-b ===` or `SYMBOL name`, at most 160 lines each; a path alone returns the outline, never
  the whole file. How many reads a model gets is the consumer's policy (Sub0Squad: two free, then each costs a turn).
* `expandSkeleton`: a declared skeleton (`@@ signature` lines) into stub sections, the start of a forked attempt.

Prior art taken: Aider's repository map (an outline within a token budget); SWE-agent's small window with explicit moves; Agentless localisation (file,
then function, then lines); editors' folding and LSP document symbols.

## Decisions, and why

* **Reply forms were kept, not replaced.** A transaction-only syntax would be a second language for a small model; every recorded reply form maps onto
  a transaction (Sub0Squad's `tests/data/replies`, exercised by its `test_blocks`).
* **The base revision is the revision shown,** set by the consumer, never written by a model.
* **Addressing by name needs every function,** not only the foldable ones (`all = true`).
* **Two insertions after one line keep their given order** (a stable sort; a model-based test found the unstable one).
* **Fold detection is heuristic.** Macros or odd formatting can show the wrong lines, which costs a read, never correctness: edits apply to the real
  text. A tree-sitter outline is the upgrade when a language or scale needs it (BACKBONE.md section 12).

## Not built

* Claims, notes and presence on sections (designed in Sub0Squad's CANVAS.md, now [BACKBONE.md](BACKBONE.md) 3.5 and the `note` event).
* A cross-file outline (the signatures of the files a task includes) and a `=== SEARCH: text ===` request.
* Persistence of a canvas beyond the consumer's own files (the consumer writes `canvas.json` and the revisions; BACKBONE.md's store would own it).

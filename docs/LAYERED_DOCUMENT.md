# The layered document: one canvas, many views, bound by stable block identities

Status: **built** in this library (slices 1 and 2, then the compact index of S1) (`lib/include/sub0canvas/doc_layers.hpp`, `lib/src/doc_layers.cpp`, `test_doc_layers`): the block index,
the issues layer, its summary, detail, inline detail and the `layered-doc-1` writer. The review and prose layers, and everything in
[BACKBONE.md](BACKBONE.md), are design. This document moved here from Sub0Squad's `docs/LAYERED_DOCUMENT.md` on 2026-11-18; Sub0Squad keeps only the
squad's integration and its tier 0 measurements (summarised at the end and linked).

How it fits with the rest: [CANVAS.md](CANVAS.md) is the revisioned document and its edits underneath; this document is the identity and the layers on
top; [BACKBONE.md](BACKBONE.md) is where both are going (state, events, subscriptions, sources, history, scale); [USE_CASES.md](USE_CASES.md) says who
it is for; [TESTING.md](TESTING.md) and [INTEGRATION.md](INTEGRATION.md) say how a change is proved.

## Why

Tier 0 (Sub0Squad `docs/BLOCK_PROVING_GROUNDS.md`, "Model tiers and the tier 0 gate") measured a worker that fixes **one defect per turn** even when the verdict lists
several: fix-two-bugs took two turns in two runs of three, with both failures shown in its first message. That cost is linear in the number of defects, and
it got worse with scale:

* the verdict is clipped (1,200 to 1,500 characters), and the harness's own excerpt keeps at most **six** failures, so with ten defects the worker is never
  told about four of them;
* a failing test names no line, so the folded view has no reason to keep the guilty function open, and the worker must first find where the defect is;
* everything about the document is a line number, and line numbers move with every edit.

The prompt can ask for every fix in one reply (Sub0Squad does). This design is the structural answer: the document is held as **layers** that are
indexed together. The worker sees a short summary of a layer first and asks for detail by number. Every layer item is bound to a **stable identity** of the
code it is about, not to a line.

## The model

| concept | meaning |
|---|---|
| **document** | the canvas: files at revisions ([CANVAS.md](CANVAS.md)). Unchanged by this design. |
| **block** | a semantic part of a file: a function or method, as `foldRegions(text, python, all=true)` finds them (namespaces and classes stay open so their members are blocks). Text outside every block is "loose" text. |
| **block id** | `b7`: assigned when the block first appears, carried through edits by reconciliation, never reused. Internal: the worker never sees ids, it names blocks by function name (`SYMBOL term`), and lines are resolved at render time. |
| **layer** | a named set of items about the document: `code` (the canvas itself), `issues` (compiler errors and failing tests), `review` (notes such as CONCERN blocks), `prose` (a description per function), and any other. Layers are open-ended: a layer is a name and a list of items. |
| **layer item** | one issue, note or description: `{layer, number, kind, title, detail, anchors[]}`. `number` is its place in the layer now (what `READ ISSUE 3` names). |
| **anchor** | where an item belongs: a block id (resolved to lines when rendered), else a path and line valid for the revision it was found in. Each anchor carries a confidence: `exact` (the producer named a line in it), `suspect` (inferred), `none`. |
| **1-N** | an item has one OR MANY anchors. The same diagnostic at twenty call sites is ONE issue with twenty anchors: it is reported once, read once, and the fix (a rename) applies at every anchor. |

## Block identity

### How an id survives edits

After any change to a file, the index reconciles the file's new blocks with its old ones (`BlockIndex::update`). The same reconciliation handles a SYMBOL
replace, a SEARCH/REPLACE snippet, line operations and a whole-file rewrite, because it looks only at the before and after texts and never at the
operation. A new block takes, in order:

1. the id of an unmatched old block of the same file with the **same signature and name**;
2. the id of the old block with the **same name**, when that name is unique on both sides (a changed signature: `resigned`);
3. the id of the old block with the **most similar body** (token Jaccard >= 0.5; a rename);
4. a **retired** id from any file with the same signature or a near-identical body (Jaccard >= 0.8): a block deleted and written again, or moved to another
   file (`revived`);
5. otherwise a new id (`added`).

An old block left unmatched is **retired**. If most of its body now lives in one live block (token containment >= 0.6), it is recorded as **merged** into
that block, and its items follow the survivor. Otherwise its items become orphans.

| edit | rule that carries the id | what happens to items |
|---|---|---|
| SYMBOL replace, body edit, a move within the file | 1 (signature) | unchanged; lines re-resolved |
| parameters changed | 2 (unique name) | unchanged |
| rename, same body | 3 (similarity) | unchanged |
| split (`f` becomes `f` + `helper`) | `f` keeps its id by 1 or 2; `helper` is added | stay on `f` |
| merge (`g` folded into `f`) | `g` retired, merged into `f` | `g`'s items resolve to `f` |
| delete | retired, no successor | orphaned (see below) |
| delete, then written again (FILE first, SYMBOL each function later) | 4 (revived) | come back |
| move to another file | 4, once both files are updated | come back |
| whole-file rewrite | 1 to 4 per block: signatures first, then names, then bodies | as above, block by block |

Only a kept block whose text changed is logged (`kept`, detail `edited`), so the history of an id is what happened to it, not every re-index.

### Recovery after a whole-file rewrite

A whole-file rewrite is not a special case. Signatures carry most blocks; names carry re-signed ones; body similarity carries renames. The tests cover a
rewrite that moves `factorial` to the top, changes `call`'s parameters and renames `term` to `multiplicative` in one step: every id survives.

The order is deliberate. A signature is exact and cheap. A unique name is next best, because a model rarely renames and re-signs at once. Similarity is last,
because two short functions can look alike. The thresholds are named constants in `lib/src/doc_layers.cpp`, and bodies of fewer than three tokens are never
compared.

### Orphans

An item whose block was retired without a successor is an **orphan**. It is never silently re-pointed. Its summary says "a function that no longer exists",
and its detail says the issue may be stale. For the issues layer this rarely lasts: issues are re-derived from each verdict, so an orphan lives one turn at
most. For a persistent layer (review, prose), an orphan is shown to whoever owns the layer: a review note on a deleted function is resolved or re-anchored by
a person or a workflow, never by a guess.

### Line ranges are the fallback

Loose text (includes, globals, a stray statement after a cut-off file) has no block. An item there is anchored by **path and line** in the revision it was
found in (`At src/calc.cpp:2 (outside any function)`). That is exact for issues, which are re-derived each turn. For a persistent layer, a line anchor should
also keep the **text of its line** (a quote selector, see "Prior art"), so it can be re-found after edits by search rather than by number. That is not built.

### Scope of an index

Today an index lives for one attempt, and each turn's state is written to `layers/turn-N.json`. Carrying ids across attempts (and across sessions) needs the
index persisted next to the canvas and reloaded with the draft it describes. The JSON has everything needed for that; the loader is not built.

## How a worker addresses an edit (and alternatives)

Today a worker sees `N| text` and edits by line, by SYMBOL or by SEARCH text. The canvas maps its line numbers through every edit since the revision it was
shown (a stale transaction is rebased or refused). The worker therefore sees line numbers while the architecture owns the mapping. That is a strong start, and
the block index generalises it: a layer item never stores a line. Alternatives, weighed:

| scheme | the model writes | pros | cons |
|---|---|---|---|
| **view-local line numbers, mapped** (today) | `LINES 41-43` against the revision it saw | what models know best; rebasing makes stale numbers safe | every edit shifts later lines; two edits in one reply must use the same view's numbers |
| **block-relative lines** | `term:3` (line 3 of `term`) | an edit in one function never moves another's addresses; matches folding | needs the block index (now built); still shifts within a block |
| **hierarchical / fractional line ids** | `41.1`, `41.2` for lines inserted after 41 (Dewey order, or CRDT positions such as Logoot/LSEQ) | an insertion never renumbers anything; ids are stable forever | unfamiliar to models; ids get long under churn; must be shown in the view |
| **content anchors** (SEARCH/REPLACE) | the text itself | no numbers at all; robust to shifts | the model must copy text exactly; ambiguous when text repeats; costly for large spans |
| **per-line hash ids** | `#a3f:` prefixes shown per line | stable while a line is unchanged; checkable | noisy view; a changed line gets a new id; tokens per line |
| **relative offsets ("RoPE-like")** | `+2 after <anchor>`, positions relative to a named block or anchor line | small numbers; shift-free outside the anchor's block | still needs an anchor scheme underneath; offsets inside a changing block move |
| **symbol-only** (SYMBOL edits) | `SYMBOL term` + the whole function | no numbers; one edit per function; ids map directly to blocks | rewrites a whole function for a one-character fix (tokens) |

Recommended next step: **block-relative lines** for READ and EDIT (`=== EDIT: path SYMBOL term LINES 3-4 ===`). The worker already sees functions as units
(folding, SYMBOL), the index makes the block a stable frame, and lines inside one function are few and move only when that function is edited. Hierarchical ids
and hash ids are the option for multi-worker canvases, where two workers editing one file must never invalidate each other's addresses.

## The summary and the query

### What the worker sees

A failing gate's verdict becomes the gate's count line plus the issue summary, one line per issue:

```
GATE FAIL: 3 of 16 tests failed
3 issues (`=== READ: ISSUE n ===` shows one in full with its code):
 1. test modulo: CHECK_NEAR failed: ev("10%3") is 3.33333 but 1 is 1 - likely in term() (lines 40-52)
 2. test factorial: CHECK_NEAR failed: ev("5!") is 24 but 120 is 120 - likely in factorial() (lines 57-62)
 3. error src/calc.cpp:125: 'terms' was not declared in this scope - in 3 places: expr(), call()
```

A compile or parse failure gets the same summary, then the compiler's own words (its source lines and carets are worth their characters). Every failure is
listed: the gate now writes `failures.txt` with every failing test unclipped, beside the clipped `gate.txt`, and the summary is built from it. The anchored
blocks are added to the view's focus, so a folded view keeps them open.

### The query

`=== READ: ISSUE 3 ===` (also `=== READ: ISSUES 1, 4 ===`) is answered like any read: no turn used, within the read allowance. The answer is the issue's full
text, then each anchor resolved **now**: the function's current numbered lines (up to three blocks, the rest named), the lines around a loose anchor, or the
fact that its block is gone:

```
=== ISSUE 1 (test) ===
FAIL 6 - modulo: CHECK_NEAR failed: ev("10%3") is 3.33333 but 1 is 1
Likely in src/calc.cpp, term(), now at lines 40-52:
  40|     double term()
  ...
```

The keyword is the layer's name made singular: `ISSUE` now, `NOTE` or `PROSE` later. One parser and one renderer serve every layer.

### Localisation (where a failing test points)

A failing test names no line. Its **suspect** block scores:

* 3 points when a word of the test's name is the block's name (`factorial` -> `factorial()`);
* 1/k points for each token of the expression under test held in the string or character literals of k blocks (`10%3` -> `'%'` lives only in `term()`).
  Rarer tokens weigh more.

The expression comes from the quoted text of the failure message (the C++ check helper prints `ev("10%3")`), and from the test's own function in the given
test sources, found by name. Python's unittest prints only `test_modulo: 30.0 != 1`, and the line it names is a shared assertion helper. Ties give up to three
suspects (an item with several anchors). The confidence is always `suspect`: it is a lead, not a verdict. A defect in a helper the suspect calls
(`fromRad()` behind `call()`) is pointed at the caller. Following calls is a later refinement.

### Prompt cost

Per issue, a summary line is at most 170 characters, typically 110 to 150: about the size of the raw failure line it replaces, plus the function and lines.
For tier 0's ten defects that is about 1.4k characters (about 400 tokens), against a clipped verdict of 1.2 to 1.5k that held only six. Detail costs nothing
until asked, then about the size of one function. The summary header is about 60 characters. The block index costs no prompt tokens: ids are never shown.

## Layers: how they are added, and who produces each

A layer is a producer that turns its source into `std::vector<LayerItem>`, anchored through the shared `BlockIndex`. Everything else is shared:
`layerSummary`, `layerDetail`, `layerInline`, `layerFocus`, the READ keyword, and the interchange JSON. Adding a layer takes three steps:

1. write the producer (text in, items out, anchors from `index.at(path, line)`, a name match or `suspects`);
2. give the loop a place to call it, and add its items to the `layers` map written per turn;
3. let `parseIssueReads` generalise to the layer's keyword (one line), and decide where its summary sits in the turn message.

| layer | producer | lives in | anchors | lifetime |
|---|---|---|---|---|
| code | the canvas | `lib/src/canvas.cpp` | the blocks themselves | the document |
| **issues** (built) | the gate's verdict (`failures.txt`, `gate.txt`) and the cheap check's output (precheck) | `parseIssues` (here), called from Sub0Squad's `src/canvas_agent.cpp` | exact (diagnostics), suspect (tests) | re-derived every verdict |
| review | the worker's CONCERN blocks (`takeConcerns`, Sub0Squad `src/stance.cpp`), later a reviewer persona | a producer beside `recordConcerns` | by function names mentioned in the concern, else the file | persistent until resolved |
| prose | the design stage: the architect's or planner's description per function (the skeleton's `@@ signature` lines, a contract paragraph) | Sub0Squad `src/architect.cpp` / `src/plan.cpp` | by function name, so prose exists before the code does: an anchor to a name not yet written resolves once the block appears | persistent; revised by design |
| (any) | a workflow over other layers (see below) | a workflow stage | derived | as its sources |

Prose sits **above** the code: a worker can be shown the prose summary of a file (one line per function) instead of its folded code, and drill into a
function's code from its prose (and from code to its prose). That view is what Sub0Canvas demonstrates first.

## Relations between layers, and collapsing duplicates

Items relate 1-N within a layer (one issue, many places) and across layers (a prose description anchors one block, a review note may anchor three). Two
workflows follow:

* **collapse**: issues with the same message (line numbers aside) are already one item with many anchors. A workflow can go further and merge items that are
  the same defect seen differently (a compile error and the test it breaks) into one report, keeping each source as an anchor;
* **batch fix**: an item with N anchors and an obvious mechanical fix (a rename, a regex replacement) can be applied at every anchor in one transaction,
  which the worker proposes once and the canvas applies N times. That is repeat-rename's tier 0 case. Not built.

## History and traceability

Each part is recorded:

* the canvas log: who changed which lines of which file at each revision (`canvas.json`, [CANVAS.md](CANVAS.md));
* the index's events: what happened to each id, stamped with the turn (`kept`/edited, `resigned`, `renamed`, `revived`, `added`, `retired`, `merged`);
  `blockHistory(index, id)` gives one section's identity history, including the ids merged into it;
* the layers per turn: `layers/turn-N.json` holds the files, blocks, events and every layer at that turn.

So "what happened to `term()`, and what is attached to it?" is answered by its id: its events, the canvas revisions whose spans overlap its lines, and every
item in every layer whose anchors resolve to it.

Runtime evidence adds a receiving test for this navigation. The same recorded execution should be discoverable from a failure or crash and from the source blocks observed in its stack. Opening a frame shows the source revision that ran. See [runtime evidence on canvas](sessions/canvas-runtime-evidence.md#navigate-in-both-directions).

## Formats

There are two audiences, and they get two formats:

* **the model** never reads JSON. It sees line-oriented text: the numbered view, the one-line summaries, `=== READ: ISSUE n ===`. That text is the
  domain-specific format, small and hard to get wrong, in the same family as the existing `=== EDIT ===` blocks. It is what token efficiency is measured on.
* **tools** (the viewer, a later loader, a CI report) read the interchange form, schema `layered-doc-1`, which is written by a serializer and never by a
  model. JSON is used because the program already has a writer and parser (simdjson), and a browser reads it natively:

```
{"schema":"layered-doc-1","turn":N,
 "files":{"<path>":{"revision":n,"text":"..."}},
 "blocks":[{"id","path","name","signature","first","last"}],
 "retired":[{"id","path","name","mergedInto"}],
 "events":[{"turn","carry","id","path","name","detail"}],
 "layers":{"issues":[{"id","number","kind","title","detail","anchors":[{"blockId","path","line","confidence"}]}]}}
```

`events` is the index's whole log up to this turn (cumulative; each event carries its turn, so a reader takes `turn == N` for one turn's changes).
`retired` is the set retired as of this turn. An item's `id` is stable across turns while its `number` is not: `issues/test/<name>` for a failing test,
`issues/<kind>/<hash of the message without numbers>` for a diagnostic. An anchor's `line` is in the revision given in `files` for that turn.

Considered: **YAML** (more readable, fewer tokens, but its indentation and implicit typing make it error-prone, and that matters only if a model writes it);
**BAML** (a typed schema-and-prompt language: worth it if a model is ever asked to *produce* layer items, such as a reviewer writing notes, because the parser
is lenient and typed); a **compact text DSL** or a token-oriented notation for tabular data (fewest tokens, the right choice for any layer data shown to a
model in bulk); **SARIF** for the issues layer (the standard static-analysis interchange: results with several locations, rules, and fingerprints for
matching results across revisions). Decision for now: JSON for tools, the line DSL for the model, and a SARIF export of the issues layer as a cheap later
step for CI viewers.

## Prior art

None of these was found by a fresh search for this document. They are known systems that solve parts of the same problem, listed so the design borrows
rather than reinvents:

* **SARIF**: results with multiple locations, related locations, and `partialFingerprints`, which identify the same finding across revisions (the issues
  layer's 1-N anchors and cross-revision identity).
* **Language Server Protocol**: diagnostics with ranges, related information and document symbols (the issue record and the block outline). Positions
  are line/character, so clients re-map them on every edit, which is the problem block ids avoid.
* **LSIF / SCIP / Kythe**: code-intelligence indexes with stable symbol monikers across files and versions (block ids, at a much larger scale).
* **Unison**: definitions are identified by a hash of their content, and names are metadata (the content-hash option for ids; a rename never changes
  identity).
* **GumTree and AST differencing**: matching of tree nodes across versions by top-down hash and bottom-up similarity, detecting moves, renames and
  updates (the reconciliation rules here are a lightweight, line-level version).
* **git blame -M/-C, diff move detection**: tracking lines and blocks moved within and across files.
* **IntelliJ smart pointers / Roslyn tracking spans**: references to code elements that survive edits, re-resolved on access (anchors resolved at render
  time).
* **W3C Web Annotation** (and Hypothesis): anchors with a text-quote selector (exact text plus prefix and suffix) and a position selector, re-anchored
  fuzzily after edits (the line-anchor fallback for persistent layers).
* **Gerrit / GitHub review comment porting**: comments carried to later patch sets, marked "outdated" when their lines change (orphans, honestly shown).
* **Literate programming (WEB, noweb, org-babel) and notebooks**: prose and code in one document, prose first (the prose layer and the prose-centred view).
* **CRDT sequence identifiers (Logoot, LSEQ, RGA)**: position ids that never shift on insertion (the hierarchical-id option for multi-worker editing).

## Measured in Sub0Squad (the evidence stays there)

The layer was measured where it was born, on Sub0Squad's tier 0 calculator cases with a local 35B model. The tables, traces and run directories are in
Sub0Squad [docs/LAYERED_DOCUMENT.md](https://github.com/CraigHutchinson/Sub0Squad/blob/main/docs/LAYERED_DOCUMENT.md) ("Slice 1 on tier 0", "Slice 2 on
tier 0"). What they mean for this library:

* **Slice 1** (summary only, 3 runs per arm): it helped where the gate's excerpt clips (bugs-10: 2.31 to 3.75 defects per turn) and cost where the old
  text already fit (fix-many 5.0 to 3.0). One quality regression traced to the edit applier's indentation (fixed in `edits`: a REPLACE takes the
  matched lines' indentation) and to the localiser pointing on a shared operator (fixed in `parseIssues`: an operator points at a block only when one
  block holds it). A wrong pointer costs more than no pointer.
* **Slice 2** (5 runs per arm, 80 runs): always showing the layer with the first 3 issues inline regressed three of four cases (+32% prompt tokens).
  Showing it only where the gate clipped (more than 6 failures) regressed nothing, and bugs-10 rose from 3.12 to 4.17. The spread between identically
  treated arms reached 67% on one case, so the gain is suggestive. The worker never used READ ISSUE (0 in 80 runs).
* For producers and consumers of layers: **render a layer where it adds information the raw text lacks, not by default**; keep pointers honest
  (`suspect`, or nothing); measure with an off arm and enough runs to see the noise.

## Out of scope here (for now)

* persisting the index across attempts and sessions (designed in BACKBONE.md: the store, the log, checkpoints);
* line anchors with text-quote selectors (BACKBONE.md 3.2 and 5.1);
* block-relative or hierarchical edit addressing (above, "How a worker addresses an edit");
* following calls in localisation (a defect in a callee of the suspect);
* the batch-fix transaction for 1-N items, and workflows that merge items;
* nested blocks: `foldRegions` returns outermost function-like regions only;
* languages beyond brace-delimited and Python, and their measured faults: [BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md) (a provider per language, prior art,
  who decides structure and folding).

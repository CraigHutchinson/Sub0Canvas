# Sub0Canvas

A viewer for the **layered document** that [Sub0Squad](https://github.com/CraigHutchinson/Sub0Squad)'s canvas worker produces: one canvas seen through any number of layers
(code, gate issues, review notes, prose per function, decisions, or anything else). Each layer binds to **stable block ids**, never to line
numbers. Lines are resolved only when the view renders.

It is a static web app with no build step and no dependencies. Open `index.html`, then open an attempt directory, drop its files on the page, or load
the demo.

![One issue anchored at eight call sites, stepping through the anchors, with a batch fix preview](docs/screenshots/one-to-many.png)

## What you can do with it

| | |
|---|---|
| **Centre on any layer** | The left rail lists `code` plus every key of the turn file's `layers` object. The set is not hard-coded, so a new layer appears without a code change. |
| **Prose → code** | Every item has a *Show code* callout attached to its card. The callout shows the block the item is anchored to, resolved to that turn's lines. |
| **Code → layers** | Select a block (in the code, the block list or a callout). The inspector lists every item in every layer anchored to it, including items that still name a block merged into it. |
| **A block's history** | For each turn: what happened to the id (added, kept, renamed, resigned, merged, retired, revived), the canvas revisions that touched its file, whether its text changed, and which layers had items on it. |
| **1-N items** | One item can have many anchors across blocks and files. The card shows the fan-out (`1→8`). The callout steps through the anchors (◀ ▶ or `[` `]`) and groups them by file. |
| **Batch fixes** | An item with a `fix` (a regex) previews the change at every anchored line. The viewer only previews it. The producer applies it. |
| **Duplicates → one report** | Items in a layer that look like the same finding (same kind, similar title, a block in common) are flagged. *Preview unified report* merges them into one item with the union of their anchors. |
| **Trace an item** | Follow one item through the turns: when it appeared, how its anchors moved, and when it went away (fixed). |

Every view is a URL hash (`#turn=3&centre=prose&block=b2&item=prose%23prose-isdigit`), so a view can be linked or reloaded. `,` and `.` step through turns.

### Prose → code

![A prose item that spans three functions; its callout shows the code at anchor 1 of 3](docs/screenshots/prose-to-code.png)

### Code → layers, and the history of a block

`parseNumber` (b2) at turn 3. The block was renamed from `number` at turn 2, then absorbed `isDigit` (b1) in the turn 3 rewrite, so the isDigit
prose now lands here.

![The code view with per-block layer badges and the inspector showing b2's attachments and history](docs/screenshots/code-to-layers.png)

![printResult (b6) retired at turn 3, its prose orphaned, and revived at turn 4](docs/screenshots/block-history.png)

### Orphans and duplicates

![At turn 3 the printResult prose is an orphan: its block retired without a successor](docs/screenshots/merge-orphan.png)

![Two review notes about the same line, merged into one unified report](docs/screenshots/duplicates.png)

## Input: `layered-doc-1`

The viewer reads what Sub0Squad's canvas writes in each attempt directory (`<home>/state/workflows/w-*/items/<item>/attempt-N/`):

```
layers/turn-N.json      one snapshot per turn
canvas.json             the revision log  {"files":{path:rev},"log":[{path,by,revision,base,from,to,delta}]}
canvas/<flat>.r<N>      every revision's text ('/' in the path flattened to '_')
```

```jsonc
{"schema":"layered-doc-1","turn":3,
 "files":{"src/parser.cpp":{"revision":5,"text":"..."}},
 "blocks":[{"id":"b2","path":"src/parser.cpp","name":"Parser::parseNumber","signature":"int Parser::parseNumber()","first":42,"last":47}],
 "retired":[{"id":"b1","path":"src/parser.cpp","name":"isDigit","mergedInto":"b2"}],          // "" = its items are orphans
 "events":[{"carry":"merged","id":"b1","path":"src/parser.cpp","name":"isDigit","detail":"b2"}], // since the previous turn
 "layers":{"issues":[{"number":1,"kind":"warning","title":"...","detail":"...",
                      "anchors":[{"blockId":"b4","path":"src/parser.cpp","line":18,"confidence":"exact"}]}]}}
```

You can open a whole workflow folder. Each directory that holds `layers/turn-N.json` becomes its own document, and you pick one from the drop-down.
Loose turn files (dropped without their folders) also load. Malformed files are listed at the top of the page instead of stopping the load.

### Optional fields the viewer understands

These fields are not yet in the producer. The demo uses them, and the viewer works without them:

| Field | Where | Purpose |
|---|---|---|
| `id` | layer item | A stable item identity across turns. It enables *Trace across turns* and merge bookkeeping. Without it, items are matched by kind and title. |
| `column` | anchor | Keeps several sites on one line apart (three `peek()` calls in one `while`). |
| `fix` | item, or an anchor to override the item's | `{"kind":"regex","pattern","replacement","flags","summary"}`: a proposed fix to apply at every anchor. |
| `mergedFrom` | item | The item keys a unified report was built from. |

## Development

```bash
npm test             # node:test, no dependencies (Node 20+)
npm run fixtures     # regenerate fixtures/demo and fixtures/demo.js from tools/gen-fixtures.mjs
npm run serve        # http://127.0.0.1:8765 (only needed where a browser refuses scripts from file://)
node tools/screenshots.mjs   # regenerate docs/screenshots with headless Edge or Chrome
```

| Path | What it is |
|---|---|
| `src/model.js` | Pure model: loading, validation, a per-turn index (live and retired blocks, anchors keyed by the block they *resolve* to through merges), and the queries (`attachedTo`, `fanOut`, `blockHistory`, `itemTrace`, `duplicateGroups`, `unify`, `previewFix`). It is a classic script and a CommonJS module, so the page and the tests load the same file. |
| `src/view.js` | The page: plain DOM, re-rendered from one state object kept in the URL hash. |
| `tools/gen-fixtures.mjs` | The demo story in four turns, with block lines computed from the source text: a SYMBOL edit, a batch regex fix, a rename, a whole-file rewrite with a signature change, a merge, an orphan, a revival, a 1-N issue (8 anchors, 2 files), duplicate review notes, and prose and decision layers. |
| `test/` | Model tests against the demo, plus a check that the committed fixtures match the generator. |

The page uses classic scripts, not ES modules, so it runs when opened straight from disk. The demo is bundled into `fixtures/demo.js` for the same reason.

## The C++ library (`lib/`)

The canvas tooling that writes `layered-doc-1` lives here too, as a C++23 static library, `sub0canvas::sub0canvas`. It works on its own and does not depend on Sub0Squad. Sub0Squad is its first consumer.

| Header | What it is |
|---|---|
| `sub0canvas/outline.hpp` | Folded views of source files (`foldRegions`, `foldedView`, `sectionView`), READ requests and their answers, and skeleton expansion. |
| `sub0canvas/edits.hpp` | Search/replace and line edits applied to a set of files (`applyEdits`), with tolerant matching. |
| `sub0canvas/canvas.hpp` | The revisioned canvas: files at revisions, transactions (LINES, AFTER, SYMBOL), conflict detection, the revision log, and the reply parser. |
| `sub0canvas/doc_layers.hpp` | Stable block ids (`BlockIndex`: reconciliation through edits, rewrites, renames, splits, merges and moves), layers and items with 1-N anchors, the issues layer, summaries, the first issues inline (`layerInline`) and READ ISSUE detail, and the `layered-doc-1` writer. |

```bash
cmake -S . -B build/mingw -G Ninja -DCMAKE_BUILD_TYPE=Release   # fetches simdjson through CPM (set CPM_SOURCE_CACHE to share a cache)
cmake --build build/mingw -j6
ctest --test-dir build/mingw
```

To consume the library from another CMake project, pin it to a tag:

```cmake
CPMAddPackage(NAME sub0canvas GIT_REPOSITORY https://github.com/CraigHutchinson/sub0canvas.git GIT_TAG v0.2.1)
target_link_libraries(my_target PRIVATE sub0canvas::sub0canvas)
```

`ctest --preset quick` runs the library's tests and, when node is installed, the viewer's (`test_viewer`); `ctest --preset full` adds any test
labelled `full` (none yet).

**CI** follows the family strategy (CraigHutchinson/.github `docs/ci-strategy.md`): the smoke tier (`ci.yml`, one Linux job) on every push to `main`
and every pull request, documentation-only changes skipped; the full tier (`ci-full.yml`: Linux GCC and Clang, Windows MinGW-w64) on `v*` and
`milestone/*` tags, by dispatch, on a pull request labelled `ci:full`, and weekly only if `main` changed. Locally: activate the pre-push gate once
per clone with `git config core.hooksPath .githooks` (it checks private names, builds and runs the quick tier), prove it with
`python ../CraigHutchinson.github/tools/ci/hook_check.py --repo .`, and run `.ci/local.json` with that repository's `tools/ci/local_ci.py`.

When the consumer already provides `simdjson::simdjson`, that target is reused. For local development, set `CPM_sub0canvas_SOURCE` to a checkout.

The library came from Sub0Squad. `tools/import-sub0squad.sh <repo> <ref>` re-imports it from a git ref there, and `lib/IMPORTED_FROM` records which one. That script is for the cut-over only: after it, changes are made here first.

## Where it is going

[docs/BACKBONE.md](docs/BACKBONE.md) is the design (not yet built) for sub0canvas as the backbone of agentic work: items with state, tags and links,
an event log, subscriptions delivered as inboxes, claims, views, source adapters (local git, GitHub) and a block index replayed over a project's history,
with ordered slices and their checks.

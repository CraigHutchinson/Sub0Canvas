# Sub0Canvas agent summary

Sub0Canvas is a pure C++23 library and a static viewer for revisioned, layered documents. A consumer supplies files and evidence, drives work, and publishes the resulting document. It owns execution, I/O, model calls, tool permissions, and restart policy. Start with the [repository guide](https://github.com/CraigHutchinson/Sub0Canvas), then [Canvas transactions](https://github.com/CraigHutchinson/Sub0Canvas/blob/main/docs/CANVAS.md) and the [layered document format](https://github.com/CraigHutchinson/Sub0Canvas/blob/main/docs/LAYERED_DOCUMENT.md).

## Build and consume

- Add `Sub0Canvas` through CPM at a release tag, from the repository root. The static target is `sub0canvas::sub0canvas`, public headers live under `lib/include/sub0canvas/`, and symbols use `namespace sub0canvas`. The library reuses an existing `simdjson::simdjson` target.
- `SUB0CANVAS_BUILD_TESTING`, `SUB0CANVAS_BUILD_BENCHMARKS` and `SUB0CANVAS_BUILD_IO` default off when embedding the library. Enable I/O and link `sub0canvas::io` for the native Git source. `CPM_Sub0Canvas_SOURCE` selects a local checkout and can override the release pin. The project's Windows receiving toolchain is MinGW. {{source:CMakeLists.txt|add_library(sub0canvas::sub0canvas ALIAS sub0canvas)}}
- `consumer/mock_squad/` models a downstream receiver. `lib/tests/test_canvas_model.cpp`, `lib/tests/test_doc_layers.cpp`, and `test/golden.test.js` cover revision behavior, issue layers, and recorded snapshots. See the [proving ladder](https://github.com/CraigHutchinson/Sub0Canvas/blob/main/docs/TESTING.md).

## APIs by task

- **Hold and edit a file.** `Canvas::addFile` seeds revision 1 and resets that path's history if called again. `replaceText` records a whole-file revision and canonicalizes the trailing newline for an existing file. `apply(Transaction)` applies line operations as a unit and rebases non-overlapping stale edits. `textAt` and `revertTo` expose earlier text and an explicit new revision. {{source:lib/include/sub0canvas/canvas.hpp|class Canvas}}
- **Read less source.** `foldedView` uses a character budget and diagnostic focus lines to choose folded bodies. `answerRead` returns numbered ranges or a symbol view with line limits. Enforce the actual request budget in the caller. {{source:lib/include/sub0canvas/outline.hpp|std::string answerRead(}}
- **Accept text-based worker replies.** `parseCanvasReply` and `applyReply` handle Canvas's `FILE`, `EDIT`, and `READ` syntax with a caller-supplied allowlist. `applyEdits` also permits relaxed matches. A caller exposing typed tool arguments can validate them directly and use the revision operations. {{source:lib/include/sub0canvas/canvas.hpp|struct CanvasReply}}
- **Attach diagnostics.** Set the turn on `BlockIndex` and update it with each changed file's text. `parseIssues` recognizes the library's diagnostic and test verdict forms, deduplicates repeated diagnostics, and anchors them where possible. Keep the raw verdict and surface unmatched errors. {{source:lib/include/sub0canvas/doc_layers.hpp|std::vector<LayerItem> parseIssues(}}
- **Export for inspection.** `layeredJson(SnapshotSpec, BlockIndex)` emits `layered-doc-1`. Supply files, per-file revisions, and layers from the same state. `Canvas::logJson()` exposes edit spans and revision metadata. Durable state and file history recovery are consumer responsibilities. {{source:lib/include/sub0canvas/doc_layers.hpp|struct SnapshotSpec}}

## Viewer and running sessions

- Place snapshots at `layers/turn-N.json` under a run directory, with optional `canvas.json` edit metadata. Load that directory in the static viewer. Its `?bundle=` input accepts a JSON array of `{ "path": "...", "text": "..." }` entries with those paths. Use one ordered snapshot sequence per run. {{source:src/model.js|function loadEntries(}}
- `store.hpp`, `event.hpp`, and `inbox.hpp` provide state projection from events and standing queries. `session.hpp` is the workspace/view/overlay API. A document session holds views and pending edits. The agent loop and child inference sessions belong to the consumer. {{source:lib/include/sub0canvas/session.hpp|Workspace(Canvas& canvas, Store& store,}}
- `turn_control.hpp` provides pure text rules for runaways and continuation. Choose the retry policy and any use of streaming in the caller. Inspect recorded reply tests before changing these rules. {{source:lib/include/sub0canvas/turn_control.hpp|struct RunawayLimits}}

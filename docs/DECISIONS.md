# Decisions

Status: the record of what is decided and why (2026-11-25). Each row is a decision, its one-line reason, and the seam that keeps it revisable; options and their pros and cons appear only where an arm is still open (section 9). A decision
changes by a new row here with its date, never by an edit that hides the old one. The reasoning that produced them is in git history (tags v0.3.0 and v0.4.0 hold the documents as they were); the design each decision serves is in
[BACKBONE.md](BACKBONE.md) (amendments A1-A22 in section 14.2, the register of open arms in 14.3), [SCALE.md](SCALE.md), [VIEWS.md](VIEWS.md) and [CHANGE_VIEWS.md](CHANGE_VIEWS.md).

**The yardstick** (owner, 2026-11-23, also in AGENTS.md): every decision is judged on three axes together: **data-driven** (behaviour in policies and formats a client may supply, not hard-coded), **extensible** (a seam with more than one
implementation proved, even if the extra ones are proofs of concept run only as build or release gates) and **performance at the core** (scale is a design input; the budgets of SCALE.md apply to every new module). Beside them: one truth
(one implementation of each rule, one home for each fact), offline and local first (nothing leaves the machine unless a person sends it), reproducible (the same inputs give the same outputs on any machine), reuse before invention.

## 1. Release and format

| decision | reason | seam that keeps it revisable |
|---|---|---|
| v0.3.0 (moved proving grounds) and v0.4.0 (B0 format, store, inbox, claims, history, resolver, query layer, views and sessions, mock squad to MS5) are tagged; the next tag is breaking for Sub0Squad ([INTEGRATION.md](INTEGRATION.md)) | unreleased work and Sub0Squad's copies piled up; a tag runs the full tier | the tag is the release check; the pin is Sub0Squad's PR |
| One format revision: per-turn `events` with `eventsFrom`, revision ids as opaque strings, texts by content id, item `id`, anchor column and range, `layerMeta`, `policies` (O5, A1, A3, A13) | the viewer, Sub0Squad and the live server need one shape; per-turn events keep a bundle at 2.5 times one turn, not eight | `layered-doc-1` is additive; a field that changes meaning gets a new schema name |
| The C++ standard is C++23 (O8) | the family is C++23; `std::expected` and `std::print` are used; lowering it buys reuse no consumer needs | the toolchain floor in [TESTING.md](TESTING.md) |
| `std::expected` replaces `std::optional` only where the caller loses a reason, one public function per change, each tagged for [INTEGRATION.md](INTEGRATION.md) | a signature change is breaking; `applyEdits` was the first | the owner picks the next function |
| Fixture regeneration (ids, `index.outliners`, a format change) is one deliberate batch per change: a hand-reviewed diff of every changed line, a mechanical "identical modulo what changed" check, the changed deterministic metrics accepted through the metrics tooling with a reason, nothing regenerated silently | the golden replay and the mock attempts are the check on the library; a regenerated fixture proves only the writer | the check script is one-off and not committed |

## 2. Where the data lives (O2, O3)

The data is not one thing; each kind has its own home:

| kind of data | home | reason |
|---|---|---|
| the document | the working tree and its git history (`DocumentSource` "local/worktree", the git adapter) | already in git |
| shared annotations (comments, threads, states, claims) | events in git under `refs/sub0canvas/<document>/<writer>`, one append-only ref per writer, merged by Lamport clock with the writer as tie-break; **private items go to `refs/sub0canvas-local/...`, never pushed** | the Gerrit NoteDb, git-bug and Radicle pattern: offline, replicated, content-addressed, no noise in branches or diffs, no write conflicts. Pushing to a public remote makes annotations public, so nothing is pushed until the owner asks |
| derived caches | per user, keyed by object id, shared by every clone and worktree of a repository (A19); only what differs stays under the git common directory | a work-dominated monorepo is several clones of hundreds of gigabytes; deleting a cache costs time, never data |
| per-reader state (inbox cursors, folds, the server token) | the user's state directory (`dirs.state("sub0canvas")`), keyed by repository | private and small |
| exports | on demand, scoped by a query (A11): `sub0canvas export review --query ...`; a view, never the truth | deliberate |
| configuration | committed under `.sub0canvas/` (identity policy, command allowlist, policy files) like `.editorconfig` | part of the project, reviewed like code; annotations never go there |

Consequences adopted: one writer per **segment** (per ref), the store reads many; every event carries its writer and a Lamport clock (A15); the log is never rewritten per writer; the log is a module of its own (A18); the `Log` interface gains a
`GitRefLog` in the tool, not the core. **The seam is proved by more than one backend** (owner amendment 2026-11-23): the directory store (committed files and local-only are one implementation with two roots) and git refs are real; **git notes (C) and SQLite (E)
are proofs of concept**, each just enough to pass the one store contract suite (append, `since(cursor)`, atomic batch), run only as optional build and release gates, off by default, linked by no consumer, promoted to a real backend only if one proves good on
the budgets of section 5.

## 3. Identity and writers

| decision | reason | seam |
|---|---|---|
| Block ids are **deterministic birth ids** (O4, A16): a hash of the revision a block first appeared in, its path and signature there, and an ordinal among identical births, carried thereafter by the usual rules | no counter and no coordination, shard-safe, and every clone replaying the same history derives the same id, so an annotation synced through git resolves on another device; a body edit or a rename never changes it (content-addressed ids would orphan every layer on each edit; random ids differ per clone) | tests never spell an id (R-ID-11) |
| A record's writer is a composite `{namespace, writer}` (project, source home, source-local id) (D6) | two homes both mint `w-000001`; browser-facing keys are opaque | the envelope |
| The event envelope is frozen with no `seq` and no `type` (A22) | Sub0Squad's `journal-1` line is `{seq, ts, type, d}` and Canvas's is `{kind, writer, lamport, subject, time, payload}`: no shared field name, so a reader tells them apart by shape and a merged stream never carries two meanings of `seq` | `EventSource::since(position)` carries the position |
| Ordering is data (D4): the order rule is a header field of the stream (`seq`, `lamport` with writer tie-break, `aligned-time`), one streaming merge for all | three projects order three ways | the stream header |
| Spikes are marked by the topic policy; `lineage@1` only reads the mark (O13) | one owner for the definition | `lineage@1` |
| Scale profile B and change views are requirements (O12): R-SCALE-5 to 9, A19-A21, U12, R-CHG | measured on a private monorepo, numbers only | SCALE.md, CHANGE_VIEWS.md |

## 4. Sources and the network

* **GitHub (O7)**: read-only through the user's `gh`, in a separate target that Sub0Squad never links, so "nothing leaves the machine" holds there; tests read captured files only (R-SRC-8); write-back only through `WriteSink` behind a person's confirmation. git-bug's
  bridges are the prior art for mirroring GitHub items into the git refs of section 2 so they can be read offline. Seam: `ItemSource`, `WriteSink`.
* **tree-sitter (O9)**: an optional target, grammars pinned at exact CPM commits, each grammar's download approved on its own, adopted per language only where the conformance corpus (D1) shows it at least as good as the provider it replaces; the core never depends
  on it. Seam: `OutlineProvider` ([BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md)).
* **The store in Sub0Squad (O10, D5)**: physical per writer (a segment per attempt, in the per-project format, behind an option off by default), logical per project by a read-time merge across homes, never a move of data; Sub0Squad's work registry is the merge's source list.

## 5. Transport, portal and Sub0Fabric

| decision | reason | seam |
|---|---|---|
| **Protocol first** (D7, O6): one versioned query and event protocol (`query-1`, [QUERY.md](QUERY.md)) with golden contract tests, independent of transport; the transport only calls `QueryServer::handle` | the server question was mis-framed as Node against C++ | the transport seam |
| **Native HTTP is the local default** (owner 2026-11-23); HTTP/1.1 the required baseline, HTTP/2 the target where tooling supports it; **stdio JSON lines** the second transport; the Node bridge an optional delivery process, not the default | Platform's `net` has landed (`SocketListener`, `SocketStream`) and Sub0Squad's portal already serves HTTP on it; the viewer avoids the six-connection limit with one cursor-addressed event stream per page, so HTTP/2 is an optimisation, not a prerequisite | one `IHttpFront` / `IHttpClient` seam in Fabric `transport` |
| HTTP implementation A first: Sub0Squad's hand-rolled HTTP/1.1 server and client lifted onto Platform `net` (loopback, per-run token, `Origin` and `Host` checks against DNS rebinding, path-escape check, response caps) plus server-sent events, no TLS; implementation B is Boost.Beast (already built in Sub0LlmEngine behind the same interfaces), adopted if hostile-input exposure or HTTP/2 matters; one contract suite both pass (agreed with the owner 2026-11-24) | three hand-rolled HTTP surfaces collapse into one `http` module (D10), homed in Fabric `transport`; Platform stays mechanism-only | the seam; [Sub0LlmEngine issue 1](https://github.com/CraigHutchinson/Sub0LlmEngine/issues/1) tracks its adoption |
| Projections are policy (D11): the remote viewer's field allowlist, opt-in project list and opaque ids are **one** projection definition read by the native portal and any Node viewer; `layeredJson` and the query layer share one writer | no second hand-written projection | `projection.hpp` |
| Portal integration (D8): within at the data layer, integrated at the shell, beside as step one, in that order of delivery: deep links from a portal row to its canvas view, then an integrated shell, then work items, campaigns and executions as Canvas items with links through Fabric's merged reader | Sub0Squad's portal must extend the canvas views, not become a second app; the viewer's `centre` is already any layer key | view definitions are data |
| **Sub0Fabric is built now** (O11, owner 2026-11-23, D1-D3), as its own repository: F0 lifts Sub0Squad's journal (segment, torn-tail recovery, `journal-1` codec, tests), behaviour-preserving; Canvas's events are the second user; modules `segment`, `record`, `order`, `cursor`, `store`, `transport`, `projection`; no dependency on Canvas, Squad or Log. The Canvas core never links Fabric; `app/` wires a Fabric store to the core's `Log`; Fabric proves the Canvas envelope as a second record shape in its own tests | the extraction trigger (a second consumer of the same mechanics) was already met by `journal-1`, the designed event log and Sub0Log's segment merge; the risk (designing for consumers that have not used it) is met by lifting what already runs | `Log` / `EventSource` in `event.hpp` |
| Sub0Log is not extended: its contract is lossy by design (it counts drops; a domain log must never drop). It gets a **read-only codec adapter** in Fabric, so the portal shows diagnostics beside work; Log itself is unchanged | a second proved codec for the record seam | the codec seam |

**Fabric's budgets** (binding on every module, each a benchmark from F0): append O(1), one writer per segment, no lock, durability a policy (per record, per batch, per interval; default batch); `since(cursor)` O(records returned) through a sparse offset index, never a
rescan; merge streaming k-way with memory bounded in the number of segments; parse with simdjson on-demand and zero-copy views; the git-ref backend writes one commit per batch, never per event; live fan-out through one in-memory ring shared by every subscriber;
projection computed once per refresh and shared across clients; indexes and caches per user keyed by object id.

## 6. Views

Decided in [VIEWS.md](VIEWS.md) section 6: sessions and server-side containment of many users are the foundation (section 7); presence and peer patches are planned, not built. The extension points stay open and are named there with their requirement rows
(R-PIN-8, R-PIN-13): `ViewStore`, `ScratchLayer`, `SessionPolicy`, `PresenceProvider`, `ConflictView`, `Compactor`, and `Session::openChannel` (which answers `not-supported` until the channel exists).

## 7. The Sub0 libraries

| library | decision |
|---|---|
| **Sub0Platform** | adopt in the tools (`app/`, `serve`, the CLI: files, locks, processes, per-user directories, `net`), **not in the core**, which stays pure. Sub0Canvas is its second C++ consumer (its test helper `check.hpp` already comes from it) |
| **Sub0Log** | diagnostics of `serve` and the CLI; its segment and merged-reader design is the model for the event log, and Fabric's seed |
| **Sub0Pub** | optional, if `serve` grows several in-process listeners; durable subscriptions stay inboxes with cursors (BACKBONE.md 3.4) |
| **Sub0Pipeline** | the tools only, at V4's check runs and S3's parallel indexing |
| **Sub0MemPage**, **Sub0TieredCache** | at S2 and S4 only if S0's numbers show the index outgrowing memory (mapped, paged index segments and logs; frozen per-blob block tables) |
| **Sub0Perf** | the performance tooling (benchmarks, the gate, the metrics check), in its own repository, fetched through CPM for its tooling only at a pinned commit |
| **Sub0LlmModelRegistry** | the pattern (immutable versioned folders with a manifest) for the policy registry (BACKBONE.md 14.4), not the code |

## 8. Moved from Sub0Squad, and what stays

Moved (done): `applyCanvasReply` became `applyReply` (atomic by construction: the canvas is worked on as a copy and committed only on success; before, whole files were written before a later edit could fail); turn control (`turn_control.hpp`: joining cut-off
replies, runaway and overrun detection, the continuation prompt); the recorded-reply corpus (30 cases, `fixtures/replies`, `test_replies`, which also checks the canvas itself is unchanged after a refusal); turn control's tests; block 4, the skeleton block and the
canvas model check (`test_views`, `test_canvas_model`). **Stays in Sub0Squad**: the fork harness and throughput model, the runaway policy's use in the streaming loop, `applySectionSignature` and `compileFindings`, the platform tests, and `run-v11-test-app-reply.txt`
(its platform test reads it). What Sub0Squad does is a pull request after a tag here ([INTEGRATION.md](INTEGRATION.md)).

## 9. Still open

| question | arms | when it is decided |
|---|---|---|
| HTTP/2 route (W4) | browsers speak HTTP/2 only over TLS, and Platform's `net` has no TLS or HTTP/2 framing: a local certificate plus either a vetted library (nghttp2; each dependency approved on its own) or own framing; on plain loopback the viewer runs HTTP/1.1 | at Fabric W4, by K0's budgets measured on both versions |
| Which store proof of concept (git notes or SQLite) is promoted, if either | by the budgets of section 5 | after both pass the contract suite |
| The remaining `std::optional` to `std::expected` conversions (`parseTargetKind`, `parseState`, `parseDeltaForm`, `holderOf`, `conflictSince`, `textAtPin`, `DocumentSource::changes`/`resolve`, `RevisionIndex::carry`) | each has one failure cause today and callers that already hold the reason; converting breaks a signature | the owner, one function per change |
| Whether the mock-squad consumer test moves from `full` to `smoke` | about 32 s the first run, 1.3 s after | the owner |
| Block-list shape in the viewer, event-log window, policy registry | the register in BACKBONE.md 14.3 | by K0's numbers and the first policy that needs a second arm |

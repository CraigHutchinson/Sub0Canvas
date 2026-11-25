# The authoring tool: the viewer becomes where people and agents work on a document

Status: **design** (2026-11-18); built so far: `sub0canvas snapshot` and `tools/serve.mjs` (V0 in part), the pure query layer ([QUERY.md](QUERY.md), V1a-q), views and sessions ([VIEWS.md](VIEWS.md)) and the interactivity benchmark ([K0.md](K0.md)); the server process, the page's windowing and everything from V1 on are not. It answers the owner's direction of 2026-11-18 ([DIRECTION.md](DIRECTION.md)
section 4): the interactive viewer is the **first use case to invest in**, extended into a Markdown, code and GitHub-like authoring tool in which a
person can test, validate, push and respond on a document that later becomes multi-user, with scripted scenarios as its tests; and **Sub0Canvas's own
design work is its first document**. Use case U7 ([USE_CASES.md](USE_CASES.md)); requirements R-AUTH ([REQUIREMENTS.md](REQUIREMENTS.md)); the
mechanisms it uses are [BACKBONE.md](BACKBONE.md) section 3, with the amendments of section 14.

## 1. The loop it must make work first

The acceptance test of the whole track is one real round on this repository:

1. The owner opens Sub0Canvas's docs in the viewer, selects a sentence in BACKBONE.md section 11, and writes a comment: an item in the `review` layer,
   anchored to that section's block and to the selected text, state `open`, actor `person/<owner>`.
2. An agent (a session working on this repository, or a Sub0Squad worker later) asks for its **inbox**: open review items in `docs/`. It gets a summary
   line per comment, then each in full with the text it is anchored to.
3. The agent acts: edits the document (a transaction against the revision it read), replies in the comment's thread (an item linked `repliesTo`),
   and marks the comment `done` with a reason.
4. The owner's view shows the comment addressed, **still anchored to the right section after the edit**, with the reply and the diff. The owner
   marks it `verified`, or reopens it with a further reply.
5. Checks run (the local test presets), their results land in a `checks` layer, and the owner commits and, after an explicit confirmation, pushes.

Nothing in that loop is specific to design docs: the same loop is code review, a backlog triage, or a squad worker's verdict (B2).

## 2. Concise views over very large data

The underlying data grows without limit: every event, every revision, a repository's whole history. The person and the agent must still see a clean,
short view. The rule (R-AUTH-1): **a view is bounded by attention, not by the data.**

* **Every view has a scope and a budget.** A request carries a query (paths, layers, states, actor) and limits (items, characters). The answer is a
  summary with counts, the first few in full, and cursors for the rest: the model's verdict rendering (R-REN) generalised. A person's view and an
  agent's prompt are the same function with different budgets.
* **"What needs me" is the default.** Opening the tool shows the reader's inbox (open items addressed to them, changes since they last looked), not
  the document's whole state. GitHub's "new since your last review" and the worker's verdict are both this.
* **History is a drill-down, never the default.** A block shows "changed 3 times since you last looked"; its full history, the log and older
  revisions are fetched by range when asked. The viewer never loads a whole log: it reads a snapshot and the events after it (BACKBONE.md 3.3, 4).
* **Large is the server's problem, not the page's.** The page holds what is on screen. Lists are paged; long files render folded (the same folded view
  a model gets, CANVAS.md), with the anchored sections open.
* This is the same rule as U4's ("cost proportional to what is touched", SCALE.md section 1), applied to attention: what is shown is proportional
  to what concerns the reader.

## 3. Architecture

```
  browser (index.html, src/*.js)  ──HTTP + server-sent events──▶  tools/serve.mjs (bridge) ──JSON lines over stdio──▶  sub0canvas serve
  agent / script / CLI  ─────────────────────────── sub0canvas <command> (via the running server, or directly) ──────────┘
                                                                                                           owns: the store, its log (A17),
                                                                                                           the worktree as document source
```

* **One process owns the document** (BACKBONE.md section 4, rule 1): `sub0canvas serve`, a new executable in this repository linking the library.
  It speaks **JSON lines over stdio**, LSP-like: each request is a command (an event to append, R-BB-8) or a query (an inbox, a view, a range of
  history). The library stays pure; the executable does the I/O (files, the clock, running checks).
* **The bridge** (`tools/serve.mjs`, Node, no dependencies, grown from today's static and git-backed server) serves the page, forwards requests to the
  process, and pushes updates to the page as server-sent events. It binds `127.0.0.1` only and requires a per-session token in every request, so
  another local web page cannot drive it.
* **Why not a JavaScript store**: the store's rules (identity, reconciliation by key, claims) must have one implementation, or the viewer and the
  squad would disagree. The C++ library is it; the viewer stays a renderer of views. The same `serve` process is E1's editor server later.
* **The viewer keeps working read-only from `file://`** (today's mode: open a folder of snapshots). Authoring needs the server.
* **Agents use the same protocol**: `sub0canvas inbox`, `sub0canvas reply`, `sub0canvas state` (a CLI over the same commands) talk to the running
  process, or open the log themselves when none runs (a lock file keeps one writer). No agent-specific mechanism.

### 3.1 Live views: scoped queries in, events out, patches on the page

Measured on a real 1,590-file subtree ([SCALE.md](SCALE.md) section 6.1), the whole-bundle model breaks before the data does: every click
re-renders 65,000 nodes (2 s), and new data means reloading everything. With people and agents writing at once, updates arrive all the time,
and each must reach an open page without disturbing the reader (the owner, 2026-11-18). So the page never holds "the document"; it holds a
**view**, and the server keeps it current.

* **Queries are scoped, answers are pages.** The page asks for what it shows: the file tree of a directory (names and counts, no texts), one
  file at one revision, the blocks of one file, the first page of a layer with counts, an item in full. Each answer carries the revision or log
  cursor it was taken at. Nothing asks for "every file" or "every block" (R-AUTH-1, R-SCALE-1).
* **Updates are events since a cursor.** The server pushes the log's events after the page's cursor (server-sent events through the bridge; the
  same `since(cursor)` a CLI agent calls). An event names what it touches (an item key, a block id, a path), so the page knows which of its
  views are stale without asking.
* **The page patches, it does not rebuild.** Each pane renders from its own slice of state, keyed by item, block and path; an event re-renders
  only the panes and rows whose keys it names. The reader's scroll, selection, open folds, expanded tree and unsent text are never reset by an
  update; if the selected block or item changes underneath, it is marked ("changed by agent/worker-2, 3 s ago"), not replaced (R-AUTH-12).
* **Long lists are windows.** A list renders the rows on screen plus a margin, and the rest on scroll; the block list is scoped to the open
  file or a filter, never all blocks of the document (R-AUTH-13).
* **Content is sent once.** Texts travel by content id (a blob hash); a turn or a view refers to the id, and the page asks only for ids it has
  not seen. A snapshot of eight commits costs the changed files, not eight copies of every file (R-AUTH-14).
* **The bundle stays as the offline form.** A folder or bundle still opens from `file://` (R-AUTH-11): it is the same events and contents,
  replayed locally, so one renderer serves both.

**Built (V1a-q, 2026-11-24)**: the query half, as a pure library layer: `QueryServer::handle(json) -> json` over a `QueryDocument`, schema `query-1`, one home
[QUERY.md](QUERY.md) (operations `info`, `tree`, `file`, `revisions`, `blocks`, `layers`, `layer`, `events`; bounds and cursors; the policy as data; unknown members by policy).
It writes through the same projection as the bundle, which a test proves on the mock attempts. The `serve` process, its transports and the page's windowing are not built.

This is the same rule as U4's and section 2's, applied to time: what is sent and redrawn is proportional to what changed.

### 3.2 Interactivity budgets (the viewer's KPIs)

Every interaction has a latency budget, measured, not assumed (the owner, 2026-11-18). The budgets hold at the **reference scale**: a 1,590-file
subtree with 20,000 blocks (SCALE.md section 6.1) and the synthetic mirror (MIRROR.md), and the numbers are recorded with the machine and build,
like any benchmark (local only, label `timing`). Time is from the input event to the next painted frame; a budget is the 95th percentile of 20
repetitions.

| interaction | budget | baseline (2026-11-18, 1,590 files, one run) | what the design changes (section 3.1) | measured p95 (K0, 2026-11-24): mirror / 1,590 files |
|---|---|---|---|---|
| expand or collapse a folder (1,588 files below it) | 50 ms | 1.25-1.35 s | the tree renders only open folders' rows, windowed; a toggle patches its own rows; children may be listed on demand (and streamed) instead of up front | 20 ms / **446 ms** |
| open a file from the tree | 100 ms (text cached), 300 ms (fetched) | 1.1 s | the code pane alone re-renders; the text is fetched on demand by content id, and the next likely files (neighbours, anchored files) prefetched | 19 ms / **530 ms** (cached: the bundle holds every text) |
| select a block (list, code, inspector) | 50 ms | 1.1-1.3 s | patches the selection, the inspector and one scroll; nothing else | 21 ms / **492 ms** (list); 21 ms / **579 ms** (code) |
| switch turn | 200 ms | 1.0 s | applies the turn's delta: changed files and events only | 20 ms / **460 ms** |
| centre on a layer / back to code | 100 ms | 0.12 s / 0.77 s | windowed cards; the code pane kept, not rebuilt | 21 ms / **530 ms** (layer); 20 ms / **410 ms** (code) |
| one keystroke in a filter | 30 ms | 0.06 s | windowed results; the search runs where the data is (the server at scale) | 20 ms / **223 ms** (`a` matches every block; p50 107 ms) |
| first view of a served subtree | 1 s to the tree, 2 s to the first file | 1.6 s load and render, after a 2-4 s snapshot | the tree and the first file first, the rest on demand | 88 ms / 700 ms (bundle fetched, no snapshot; tree and first file paint together) |
| an event from another actor while reading | 50 ms, and nothing the reader holds moves | not possible (reload only) | events since a cursor, patched by key (R-AUTH-12) | not measurable yet |

**Measured (K0, [K0.md](K0.md), 2026-11-24, mains power, headless Edge 159, 20 repetitions after 2 warm-up).** On the mirror (49 files, 235 blocks) every interaction is
inside its budget, at the idle frame (16.5 ms: vsync and the task hop are the floor under every number). On the 1,590-file subtree (20,034 blocks, 60,896 DOM nodes) **eight of ten
interactions breach by 2.3x to 11.6x**, all of them the same cost (a full re-render, 380-460 ms at p50): selecting a block in the code (11.6x), in the list (9.8x), expanding a folder
(8.9x), a filter keystroke that matches everything (7.4x, 223 ms), opening a file and centring on a layer (5.3x each), back to code (4.1x), switching turn (2.3x). The first view
(bundle fetched, no snapshot) is inside both budgets at 700 ms. Machine and build are in the record (`perf/k0/<host>.json`); read the numbers to about 15%.

**The block list needs a different design, not only a faster one.** Today it is one global list of every block in the document (20,075 rows,
92% of the page), unscoped and rebuilt on every click. Candidate shapes, to be weighed when it is designed (BACKBONE.md 14.3's register): blocks
of the open file as an outline inside the tree (where editors put them); blocks of the open folder; a search over all blocks answered by the
server, showing a page of matches; and the identity history views (retired, merged, orphaned) as their own query rather than rows mixed into
the list. Whatever is chosen, it is scoped and windowed.

A future compound view can scope code and comments to one block, with bounded excerpts above a fold and exact numbered source on expansion.
Keep separately linked evidence available through a scoped query. The [prose verification brief](sessions/canvas-prose-verification.md#code-and-comments-in-a-bounded-view)
describes the first small-context consumer.

## 4. The document and its layers

| part | in the authoring tool |
|---|---|
| document source | the working tree (`DocumentSource` "local/worktree"): a revision per save through the tool, and per change found on disk (by content hash); git history joins through G1 |
| blocks | Markdown sections by heading, fenced code by its own language, code by function (BLOCK_DISCOVERY.md: the Markdown provider is pulled forward) |
| `review` | comments: anchored to a block plus the selected text (a quote with prefix and suffix, BACKBONE.md 3.2) and its range; threads are items linked `repliesTo` (amendment A5); state `open`, `done` (addressed), `verified` (accepted), `dropped` |
| suggestions | a review item with a `fix` of kind `replaceText`: accepting it applies a canvas transaction against the revision it was made on, rebased or refused by R-CAN-4 |
| `checks` | the result of each allowlisted local command (`ctest --preset quick`, `npm test`): an item per failing test, anchored to the test's block; state follows the latest run |
| `issues`, `plan` | as today, and `docs/BACKLOG.md` as items (BACKBONE.md section 8) |
| actors | `person/<name>`, `agent/<name>`; every event names one. One person now; more later with no new mechanism (claims give presence, BACKBONE.md 3.5) |

**Where the review log lives**: through the store's `Log` interface, so the answer changes a backend, not the tool. Recommended
([DECISIONS.md](DECISIONS.md) section 3, A17; for the owner to confirm, [PLAN.md](PLAN.md) O2): one append-only segment per writer, shared
through git refs of its own (`refs/sub0canvas/...`, synced by fetch and push, never in branches or the working tree), private items under a
namespace that is never pushed. V1 writes that segment format locally; V2 adds the git-ref backend. The tool's configuration (identity policy,
command allowlist) is committed under `.sub0canvas/`, like any project configuration.

## 5. Testing, validating and pushing from the tool

* **Edit**: a Markdown or code pane; saving is a transaction against the revision shown, so a concurrent agent edit is rebased or refused, never
  overwritten (R-CAN-4).
* **Test and validate**: a button runs a command from an allowlist in the repository (`.sub0canvas/commands.json`: name, argv, working directory;
  no free-form shell). Output is parsed by the same issue parser (R-ISS) into the `checks` layer.
* **Commit and push**: the tool shows the diff since the last commit and the checks' state. Commit is local. **Push asks for explicit confirmation
  every time** (AGENTS.md: confirm before pushing; never force-push or skip hooks), and runs the repository's pre-push gate like any push.
* **Respond on GitHub** (G5): later, through `WriteSink`, always behind confirmation. Nothing leaves the machine before that slice, by construction.

## 6. Scripted scenarios: the tests, and the way to multi-user

A scenario is a file of steps, each an actor and a command, with assertions on views, replayed headlessly against the same protocol the page uses:

```jsonc
{ "scenario": "comment-survives-edit", "document": "fixtures/scenarios/comment-survives-edit/doc",
  "steps": [
    { "as": "person/owner", "do": "comment", "at": { "path": "docs/X.md", "quote": "five open questions" }, "text": "Answer question 2 here." },
    { "as": "agent/a",      "expect": { "inbox": { "count": 1, "first": { "layer": "review", "state": "open" } } } },
    { "as": "agent/a",      "do": "edit", "path": "docs/X.md", "replace": { "quote": "five open questions", "with": "six open questions" } },
    { "as": "agent/a",      "do": "reply", "to": "$1", "text": "Added question 6." }, { "as": "agent/a", "do": "state", "key": "$1", "to": "done" },
    { "as": "person/owner", "expect": { "item": { "key": "$1", "state": "done", "anchor": { "section": "Open questions", "status": "outdated-quote-reanchored" } } } } ] }
```

Scenarios are the L2 rung of TESTING.md for this track: deterministic (a passed clock, seeded ids), byte-compared views, hand-checked truth. A scenario
with two people and an agent is how C1 (multi-user review) is tested long before two real people use it.

## 7. Slices

Each slice is used (by the owner, on this repository) before the next is built.

| slice | what is built | check |
|---|---|---|
| **V0** a worktree snapshot | the `sub0canvas` executable with `snapshot <dir> [paths]` writing `layered-doc-1`; a minimal Markdown provider (headings, fences); the viewer renders Markdown (headings, lists, code) and code side by side | `sub0canvas snapshot . docs` opens in the viewer; every heading of every doc is a block; golden snapshot of `docs/` checked byte for byte |
| **V1a** live views (**V1a-q, the pure query layer, is built: [QUERY.md](QUERY.md)**; the server transport and the page remain) | section 3.1 without a store yet: `serve` answers scoped queries (tree, file at revision, blocks of a file, layer page) from the snapshot executable, texts by content id; the page renders per pane with keyed patches and windowed lists; turns as deltas (changed files, per-turn events) | on the 1,590-file subtree of SCALE.md 6.1: a click under 50 ms, first view under 1 s, and a scripted stream of 100 events applied while a reader is scrolled and has a block selected leaves scroll and selection unchanged (a scenario, R-AUTH-10) |
| **V1** comments | `serve` with a minimal store (B1's core: `item.put`, `item.state`, `item.link`) and the log as its own module (A18), events with writer and Lamport clock (A15), written locally in the per-writer segment format; the bridge; comment, reply, resolve from the viewer | scenario: a comment and its thread survive a restart (replayed from the log) and render identically |
| **V2** the agent's inbox | B2's `inbox` and `acknowledge` over the review layer; the CLI (`inbox`, `reply`, `state`); views with budgets (R-AUTH-1); the git-ref log backend (A17), so a review on one device reaches an agent on another | **the round of section 1 on this repository**, done for real, and recorded as a scenario |
| **V3** edits and suggestions | editing in the page as transactions; suggestions with `fix`; quote re-anchoring (B4's resolver, first part) | scenario: a comment's anchor follows its text through an agent's rewrite of the section, or is marked outdated, never moved silently |
| **V4** checks and push | the command allowlist, the `checks` layer, commit, push behind confirmation | scenario with a failing then passing test; a push refused without confirmation |
| **V5** a second actor | presence from claims (B3), two sessions on one document, scenarios with two people and an agent | C1's check, as a scenario |

V0-V2 pull forward the parts of B1, B2, B4, D2 and E1 they need, and nothing else. They are proved on the owner's own use before Sub0Squad depends on
them (INTEGRATION.md: library work the squad does not consume needs no Sub0Squad PR).

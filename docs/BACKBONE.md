# The sub0canvas backbone: layers as the place where agents, people and sources meet

Status: **design, for the owner to read.** Nothing here is implemented. Written 2026-11-17 from the owner's direction ([DIRECTION.md](DIRECTION.md), originally Sub0Squad's `docs/LAYERED_DOCUMENT.md`; the sections
"Direction: sub0canvas as the squad's backbone" and "Sources"), the slice 1 and slice 2 code in `lib/`, the viewer in `src/`, and the viewer's schema
requests in [the README](../README.md#optional-fields-the-viewer-understands).

## 1. What this document decides

sub0canvas today is a library (`outline`, `edits`, `canvas`, `doc_layers`) and a viewer. It holds one document, a turn at a time, with one layer that
anyone produces (the gate's issues). The owner's direction is that it becomes the **backbone** of agentic work: the place where a planner, a scheduler, a
worker, a reviewer, an owner and outside sources (git, GitHub) all read and write, each in its own layer, anchored to the same stable block identities.

This document fixes:

* the **mechanisms** (section 3), few and general: items with state and tags, links, an event log, subscriptions delivered as inboxes, claims, views;
* the **store** that holds them and the rules that keep it trustworthy (section 4);
* the **adapter interfaces** through which sources feed it, and the **revision-aware index** that resolves their anchors (sections 5 and 6);
* the **format** (`layered-doc-1`, extended additively) and the new event log file (section 7);
* how **Sub0Squad's existing pieces** become layers and adapters, and which stay where they are (section 8);
* the **consumers** outside Sub0Squad (section 9);
* **ordered slices**, each with a check that can fail (section 10), and the open questions that need the owner (section 11);
* how it all scales to a codebase of **billions of lines** (section 12).

It does not decide how use cases are built on the mechanisms (a scheduler's policy, a reviewer's prompt). Those are built on the backbone, not into it.

## 2. The model as it stands

| concept | today (v0.2.x) | where |
|---|---|---|
| document | a set of files, each with a linear revision history | `canvas.hpp` (`Canvas`) |
| block | a function-like region with a stable id, carried through edits by signature, name, then body similarity; retired, merged or revived | `doc_layers.hpp` (`BlockIndex`) |
| layer | a named `std::vector<LayerItem>` | `doc_layers.hpp` |
| item | kind, title, detail, a `key` stable across turns, 1-N anchors | `LayerItem` |
| anchor | a block id (exact or suspect) or a path and line of one revision | `Anchor` |
| rendering | one summary line per item, an item in full, the first few in full, focus lines for folded views | `layerSummary`, `layerDetail`, `layerInline`, `layerFocus` |
| interchange | one snapshot per turn, `layered-doc-1` | `layeredJson` |

Three limits shape everything below:

1. **Items are re-derived, not kept.** The issues layer is rebuilt from every verdict. Nothing has a lifecycle, so nothing can be "held" or "done".
2. **One line of revisions.** `BlockIndex` follows one sequence of texts. It cannot resolve an anchor made on another branch or at an older commit.
3. **No log of item changes.** The index logs block events, but nothing logs "item written", "tag added", "claim taken". A subscriber has nothing to read.

## 3. The mechanisms

Seven mechanisms, each small, each usable without the others. The names in `code` are the proposed C++ types (namespace `sub0canvas`).

### 3.1 Items with a source, state and tags

`LayerItem` grows (every new field optional in the format):

| field | type | meaning |
|---|---|---|
| `key` | string | already exists. Becomes the item's **identity**, namespaced by source: `squad/issues/test/modulo`, `github/pr/123/comment/456`, `plan/parser/term`. Two items with one key are the same item at two times. |
| `source` | string | the adapter that produced it (a name from the document's `sources` map). |
| `state` | enum | `open`, `held`, `done`, `verified`, `dropped`. `orphaned` is **derived**, never stored: an item whose every block anchor resolves to nothing. |
| `tags` | set of strings | free-form, lower-case, `[a-z0-9-/]`. Conventions are use cases, not mechanism (`ready-for-implementation`, `owner/parser`). |
| `links` | vector of `Link` | typed relations to other items (3.2). |
| `fix` | optional `Fix` | the viewer's request: a proposed mechanical change (`regex` now; `replaceLines`, `symbol` later) applied at every anchor. |
| `revision` | the document revision the item was made against | lets a later reader see the item in its own context. |

The **lifecycle** is deliberately small. Transitions are events (3.3); any transition is allowed, because the policy (who may mark `verified`) belongs
to the consumer. The core enforces only two things: a transition names who made it, and `held` requires a live claim (3.5).

```
open --claim--> held --> done --gate/review--> verified
  ^               |        |
  +---release-----+        +--reopen--> open          any --> dropped
```

Re-derived layers (the gate's issues) keep working: the producer writes the full set each time, and the store **reconciles by key**. A key that is new
is `open`; a key that disappears is set `done` by the source (the failure went away), and a consumer may later set it `verified`. So "trace this issue
until it was fixed", the viewer's request, falls out of the key and the event log instead of title matching.

### 3.2 Anchors and links

Anchors (to code) and links (to other items) are kept apart, because they resolve differently.

```cpp
struct Anchor {                    // to code
    enum class Confidence { exact, suspect, none };
    std::string blockId;           // resolved by the index (today)
    std::string path; int line = 0, column = 0;   // column: the viewer's request (several sites on one line)
    std::string revision;          // the revision path/line belong to (the viewer's request for loose anchors)
    std::string quote;             // the anchored text (W3C text-quote selector), for re-anchoring when lines move
    std::string name;              // a block not yet written: "Parser::term" resolves once a block of that name appears (the prose layer)
    Confidence confidence = Confidence::none;
};

struct Link {                      // to another item
    enum class Kind { duplicates, causes, fixes, implements, discusses, blocks, partOf };
    std::string key;               // the other item's key, possibly in another layer or source
    Kind kind;
};
```

* A **source anchor** (section 5) is an `Anchor` with `revision`, `path`, a line range and a `quote`, and no `blockId`. The core fills `blockId`.
* A **name anchor** is how prose and plans exist before code: "the planner described `term()`" resolves when `term()` is written. Until then the item
  is unanchored, and shown as waiting, not as an orphan.
* `mergedFrom` (the viewer's request) is a set of `duplicates` links on the surviving item, plus the merged items set `dropped`. Nothing is deleted.
* Links are directed and stored on the item that states them. The store keeps a reverse index, so "what implements this plan item?" is a lookup.

### 3.3 The event log

One append-only log per document: every change to the backbone is an event, and every view, inbox and replay is derived from the log.

```cpp
struct Event {
    std::uint64_t seq;             // dense, monotonic within the log: the cursor
    std::string kind;              // see the table
    std::string source;            // which adapter or actor
    std::string actor;             // who: "squad/worker/app#2", "person/craig", "github/user/x"
    std::string subject;           // an item key, a block id, a path, a claim id
    std::string time;              // ISO 8601, for display only: order is seq
    std::string data;              // JSON object, kind-specific (the new state, the tag, the old name)
};
```

| kind | subject | data |
|---|---|---|
| `revision` | path | `{revision, parents, by, message}`: a document revision (the canvas log, a commit) |
| `block` | block id | `{carry, path, name, detail}`: today's `BlockEvent` |
| `item.put` | item key | the item's fields that changed (`layer`, `kind`, `title`, `anchors`...) |
| `item.state` | item key | `{from, to, reason}` |
| `item.tag` / `item.untag` | item key | `{tag}` |
| `item.link` / `item.unlink` | item key | `{kind, key}` |
| `claim.take` / `claim.release` / `claim.expire` | claim id | `{target, mode, holder, expires}` |
| `note` | item key or block id | `{text, kind}`: CANVAS.md's notes (`heads-up`, `question`, `contract-change`, `done`) |
| `source.cursor` | source name | `{cursor}`: how far an adapter has read |

Rules:

* **The log is the truth for the backbone; snapshots are views.** `layers/turn-N.json` stays as the snapshot a viewer reads without replaying, and is
  written from the store. A store is rebuilt by replaying the log.
* **Per-turn or cumulative?** The two documents disagree today: Sub0Squad's LAYERED_DOCUMENT.md says the turn file's `events` is cumulative, while this
  viewer assumes per turn (and strips a repeated prefix). Decision proposed: the **log is cumulative** (it is the log); a turn file's `events` holds
  **only the events since the previous snapshot**, with `eventsFrom` giving the first `seq`. A reader that wants all of it reads the log.
* The log does not replace Sub0Squad's journal (section 8): the journal records the squad's *decisions* about jobs; the backbone log records changes to
  *layers*. Where one causes the other, the event's `data` carries the other's id, so the two can be joined, never merged.

### 3.4 Subscriptions, delivered as inboxes

A subscription is a **standing query** with a **cursor**. Its inbox is "what matched since the cursor", rendered exactly as a worker's issues are
today: a summary first, the first few in full, the rest on request.

```cpp
struct Query {                     // every field optional; an empty field matches all
    std::set<std::string> layers, kinds, tags, states, sources;
    std::set<std::string> blocks;  // block ids: "anything anchored here"; the index's merges are followed
    std::set<std::string> paths;   // path prefixes: src/canvas* as an owner's area
    std::set<std::string> events;  // event kinds: "block" with carry retired, "item.state" to done
    std::string text;              // a substring of the title or detail
};

struct Subscription { std::string id, owner; Query query; std::uint64_t cursor = 0; };

/// What matched since the subscription's cursor: items (by key, latest state) and the events that made them match.
struct Inbox { std::vector<LayerItem> items; std::vector<Event> events; std::uint64_t upTo = 0; };

[[nodiscard]] Inbox inbox(const Store& store, const Subscription& sub);
void acknowledge(Subscription& sub, std::uint64_t upTo);   // the cursor moves only when the reader says so
```

* The **worker's verdict becomes one subscription**: layer `issues`, source `squad/gate`, area = the files of the item. Slice 2's adaptive switch is a
  rendering decision on its inbox. This is the first check that the mechanism is general (B2 in section 10): the squad's verdict text, rebuilt through
  `inbox` and `layerSummary`/`layerInline`, must be byte-identical to today's.
* `READ ISSUE n` generalises to any layer by its keyword (`READ PLAN n`, `READ REVIEW n`); `parseIssueReads` already takes the keyword pattern.
* Delivery is pull. A consumer asks for its inbox when it is ready (a worker at its next turn, a scheduler on a tick, a person on opening the view).
  Push (webhooks, a file watcher) only feeds the log; it never calls a subscriber. That keeps the core free of threads and callbacks.

### 3.5 Claims

A claim is a **time-limited lease** on an item or a block, as Sub0Squad's CANVAS.md designed it and never built.

```cpp
struct Claim {
    enum class Mode { write, observe };
    std::string id, target;        // target: an item key or a block id
    std::string holder;            // the actor
    Mode mode;
    std::string expires;           // absolute time; the store expires it when asked at a later time
};

[[nodiscard]] std::expected<Claim, std::string> claim(Store&, std::string target, std::string holder, Claim::Mode, std::chrono::seconds ttl, Clock now);
```

* At most one `write` claim per target; any number of `observe` claims. A refused claim names the holder and when it expires.
* Claims expire; nothing waits on one without a deadline. Expiry is evaluated against a clock passed in (`Clock now`), so the core stays deterministic
  and testable; the consumer's daemon decides when to look.
* A block claim covers what it **resolves to**: after a merge, the survivor; after a retire, nothing (the claim is released with reason `retired`).
* Taking a write claim on an item sets it `held`; releasing it returns it to `open` unless it moved to `done`.

### 3.6 Views

A view is a pure function from the store to text or JSON: **centre** (a layer, or code), **scope** (a query), **depth** (summary, first few, full).
The viewer's layer-centred UI and the worker's prompt are both views. Proposed C++ surface:

```cpp
struct ViewSpec { std::string centre; Query scope; std::size_t inlineCount = 3, maxChars = 3000; };
[[nodiscard]] std::string renderText(const Store&, const ViewSpec&);    // the model's line-oriented text
[[nodiscard]] std::string renderJson(const Store&, const ViewSpec&);    // layered-doc-1 for tools
```

`layerSummary`, `layerDetail`, `layerInline` and `layerFocus` remain the building blocks; views compose them.

### 3.7 What is deliberately not a mechanism

* **Workflow logic.** "When tagged `ready-for-implementation`, start a job" is a consumer's subscription plus its own action. The core has no rules engine.
* **Permissions.** The core records actors; whether an actor may verify, post or approve is the consumer's policy (and, for writes back to a source,
  always a confirmation, section 5).
* **Merging of concurrent text edits.** The canvas's revisions reject stale edits today (an edit names its base). Real-time co-editing (a CRDT) is out of
  scope; section 9.2 explains how a collaborative editor still fits.

## 4. The store

```cpp
class Store {
public:
    explicit Store(Log log);                                    // replays the log
    void append(Event e);                                       // the only way to change anything
    [[nodiscard]] const BlockIndex& index() const;
    [[nodiscard]] std::vector<const LayerItem*> items(const Query&) const;
    [[nodiscard]] const LayerItem* item(const std::string& key) const;
    [[nodiscard]] std::vector<const Claim*> claims(const std::string& target) const;
    [[nodiscard]] std::uint64_t head() const;
    // conveniences that build and append events:
    void putLayer(const std::string& layer, const std::string& source, std::vector<LayerItem> items);   // reconcile a re-derived layer by key
    void setState(const std::string& key, ItemState, std::string actor, std::string reason);
    void tag(const std::string& key, std::string tag, std::string actor);
};

class Log {                                                     // the persistence seam
public:
    virtual ~Log() = default;
    virtual void append(const Event&) = 0;
    [[nodiscard]] virtual std::vector<Event> since(std::uint64_t seq) const = 0;
};
// MemoryLog (tests, the squad's per-attempt use) and FileLog (NDJSON, canvas-events-1, torn final line discarded on open, as Sub0Squad's journal does).
```

Rules:

1. **Single writer per document.** One process owns a store's log. Other processes read the file, or ask the owner. That is how Sub0Squad's daemon
   already owns its journal, and it avoids file locking across platforms.
2. **The core stays pure.** No sockets, no threads, no clock reads, no environment. Time and I/O come in through `Log`, `Clock` and the adapters.
3. **Deterministic replay.** The same log gives the same store, so a view can be reproduced offline, and a test can be a log file.
4. **Bounded growth.** Snapshots (`store.snapshot()` written beside the log at a `seq`) let a reader skip the prefix; the log is never rewritten.

## 5. Sources: the adapter interfaces

Everything that feeds the store is an adapter. An adapter declares its capabilities; the core implements none of them for a particular source.

```cpp
struct RevisionInfo { std::string id; std::vector<std::string> parents; std::string author, time, message; };

class DocumentSource {             // capability: document
public:
    virtual ~DocumentSource() = default;
    [[nodiscard]] virtual std::vector<RevisionInfo> revisions(const std::string& from, const std::string& to) const = 0;   // to's ancestors after from
    [[nodiscard]] virtual std::map<std::string, std::string> files(const std::string& revision) const = 0;
    [[nodiscard]] virtual std::vector<std::string> changedPaths(const std::string& a, const std::string& b) const = 0;     // cheap replay
};

struct Fetch { std::vector<LayerItem> items; std::vector<Event> events; std::string cursor; };

class ItemSource {                 // capability: items (and events)
public:
    virtual ~ItemSource() = default;
    [[nodiscard]] virtual std::string name() const = 0;          // "squad/gate", "github"
    [[nodiscard]] virtual Fetch fetch(const std::string& cursor) = 0;   // items with SOURCE anchors only; never block ids
};

class WriteSink {                  // capability: write (optional)
public:
    virtual ~WriteSink() = default;
    struct Proposal { std::string action, target, body; };      // "comment", "github/pr/123", "..."
    [[nodiscard]] virtual std::expected<std::string, std::string> apply(const Proposal&) = 0;   // called only after the consumer's confirmation
};
```

The five rules from LAYERED_DOCUMENT.md stand, restated as interface contracts:

1. **Adapters never compute block ids.** `fetch` returns source anchors (`revision`, `path`, line range, `quote`). The store's resolver maps them.
2. **The index is revision-aware** (section 6).
3. **Keys are namespaced by source.** Cross-source duplicates are linked (`duplicates`) by a collapse step that a consumer runs, never merged silently.
4. **Cursors make fetches incremental and views reproducible.** `fetch` returns an opaque cursor; the store logs it (`source.cursor`); a cache keyed by
   source and cursor lets a view be replayed offline.
5. **Credentials and writes stay at the edge.** An adapter owns authentication. The store, the log, the snapshots and the viewer never hold credentials.
   A `WriteSink` is called only by the consumer after a person or a policy approved the proposal; the core has no path to call it.

**Network.** The git adapter is local. The GitHub adapter reads over the network with the user's own `gh` login, read-only until G5, and is a separate
target (`sub0canvas::github`) so that the core library and Sub0Squad never link it by accident. Sub0Squad's rule that nothing leaves the machine is
kept: Sub0Squad does not use the GitHub adapter, and the adapter never sends document text anywhere (it only reads).

### 5.1 The resolver

```cpp
/// Map a source anchor made at `anchor.revision` onto the store's viewed revision: replay the index from that revision (section 6), take the block holding
/// the line there, follow its id to now. If the line moved, the quote re-anchors it (exact match nearest the old line, then a fuzzy match scoring prefix
/// and suffix); if nothing matches, the anchor is outdated: kept, shown as such, never moved silently.
[[nodiscard]] Anchor resolve(const Anchor& sourceAnchor, const RevisionIndex& history, const std::string& viewedRevision);
```

One resolver serves every source, so a GitHub review comment, a gate failure and a planner's note are anchored by the same rules and fail the same way.

## 6. The revision-aware index

Today `BlockIndex` is fed texts in order. The extension keeps that class unchanged and adds a layer above it:

```cpp
class RevisionIndex {
public:
    explicit RevisionIndex(const DocumentSource& doc, std::size_t checkpointEvery = 50);
    /// The block index as of `revision`: replayed from the nearest checkpoint on its first-parent line, then cached.
    [[nodiscard]] const BlockIndex& at(const std::string& revision);
    /// The identity history of a block across revisions: every event of its id and of ids merged into it, each with the revision it happened at.
    [[nodiscard]] std::vector<std::pair<std::string, BlockEvent>> history(const std::string& revision, const std::string& blockId);
    /// The id at `to` of the block that was `id` at `from` (from may be on a branch: replayed from the merge base).
    [[nodiscard]] std::optional<std::string> carry(const std::string& from, const std::string& id, const std::string& to);
};
```

* **Replay along first parents**, with `BlockEvent.turn` generalised to a revision id. A merge commit is replayed as the text after the merge (its
  first-parent diff), which is what `git blame --first-parent` does for lines.
* **Branches** (a PR) replay from the merge base. Identity across the two lines is by the same rules (signature, name, similarity), so "PR 123 changes
  `term()`" and "main renamed `term()` to `parseTerm()`" meet on one id, and two PRs touching one block are a **conflict seen before either merges**.
* **Checkpoints** (a serialised `BlockIndex` every N revisions, `json()` plus a reader) bound a replay. Only changed paths are re-indexed
  (`changedPaths`), so a 200-commit replay of a 50-file repository updates a few files per commit.
* **Ids are local to a replay root.** Two replays from different roots give different ids for the same function. Interchange files therefore name the
  root (`index.root`), and ids are only compared within one root. A durable id across roots would need a content hash (Unison's approach); not proposed
  now, because a body edit would then change identity.

## 7. The format

`layered-doc-1` stays, extended **additively**: an old reader ignores what it does not know; an old file has none of the new fields and means what it
meant. A new schema name is used only if a field changes meaning, and none does.

| field | on | meaning | requested by |
|---|---|---|---|
| `id` | item | the item's key (namespaced) | viewer |
| `source`, `state`, `tags`, `links`, `revision` | item | section 3.1 | backbone |
| `fix` | item, anchor | a proposed mechanical change | viewer |
| `mergedFrom` | item | the keys merged into it (also written as `duplicates` links) | viewer |
| `column`, `revision`, `quote`, `name` | anchor | section 3.2 | viewer, sources |
| `kind`, `parent` | block | nested blocks when `foldRegions` reports them | viewer |
| `layerMeta` | top level | `{layer: {label, description, producer}}` | viewer |
| `sources` | top level | `{name: {kind, capabilities, cursor}}` | sources |
| `revisions` | top level | `[{id, parents, author, time, message}]` for history views | sources |
| `claims` | top level | the live claims at the snapshot | backbone |
| `eventsFrom` | top level | the first `seq` in `events` (section 3.3) | backbone |
| `index.root` | top level | the revision a replay started from (section 6) | sources |

Two smaller fixes the viewer asked for are adopted: turn files are written with `\n` line endings (they already are), and revision text files escape
`/` as `%2F` instead of `_` (an old name with `_` is still read).

**The event log file**, `events.ndjson`, schema `canvas-events-1`: one `Event` per line, as section 3.3, first line
`{"schema":"canvas-events-1","document":"..."}`. A torn final line is discarded on open.

**SARIF export** of the issues layer (results with locations; `partialFingerprints` = the item key) remains a cheap later step for CI viewers.

## 8. How Sub0Squad's pieces map

Sub0Squad is the first consumer. The rule: **what describes code, or work anchored to code, becomes a layer; what decides about jobs stays in the squad.**

| Sub0Squad today | becomes | stays in Sub0Squad |
|---|---|---|
| the canvas (`Canvas`, revisions per transaction) | the `DocumentSource` "squad/canvas" (in-process, one revision per transaction) | the canvas loop, its prompts |
| the gate verdict, `failures.txt`, the precheck | the `ItemSource` "squad/gate" producing the `issues` layer; the worker's verdict is its inbox (3.4) | the harnesses and the gate itself |
| CONCERN blocks (`takeConcerns`, `stance.cpp`) | items in a `review` layer, anchored by the functions they name | the stance experiment |
| the planner's plan (`plan-2`), the architect's skeleton (`@@ signature` lines) | items in a `plan` layer (one per node) and a `prose` layer (one per function, name anchors) | the planner and its checks |
| workflow nodes and their status (`daemon_workflow.cpp`) | the `orchestration` layer: one item per node, its state mirroring the node's (open, held while a job runs, done when accepted, verified by the gate) | the daemon, the ladder, the policy seam |
| the journal (`journal-1`) | read by an adapter as events, never written by the backbone | **the journal stays the truth for job state** |
| mends (`registry/mends.json`) | a mend applied is an event on the issue it mended (`item.state` with `reason: mend <name>`) | the mend table and actions |
| CANVAS.md claims and notes (designed, not built) | backbone claims (3.5) and `note` events | the fork coordinator's policy |
| the overseer's escalation classes | a subscription over failing items, and its verdict a tag (`escalation/guidance`) | the overseer's rules |
| docs/BACKLOG.md | a markdown `ItemSource` (one item per bullet, tags from its section) | the backlog file itself |
| the ledger, the trace | not layers: resource accounting and transcripts | both |

**Migration in Sub0Squad**, in order: (1) the canvas loop builds its verdict through a `Store` with a `MemoryLog` per attempt (B2 below), no behaviour
change; (2) the store's log is written to the attempt directory beside `layers/`; (3) the orchestration layer is written by the daemon, read by the portal
and the viewer; (4) only then, a scheduler subscription replaces any direct call. Each step is behind an option, off by default, as every new capability
in Sub0Squad is.

### 8.1 The extension to `layered-doc-1` for Sub0Squad

The producer (`canvas_agent.cpp`, through `layeredJson`) adds, in B0: item `id` (already computed as `key`), anchor `column` (compilers report it), a
`layerMeta` entry for `issues`, `eventsFrom`, and per-turn `events`. Later slices add `state`, `source` and `tags` once the store exists.

## 9. Consumers outside Sub0Squad

Each consumer is a check on the design: if it needs a mechanism that is not in section 3, the design is wrong or incomplete.

### 9.1 A text editor over layers

An editor (VS Code, or a small native editor) shows layers in the margin and lets the user centre on prose. Needs: the document source is the editor's
buffers (a revision per save, or per edit batch); views render to decorations; a block id under the cursor gives "what is attached here". **Fits** as an
LSP-like process: the editor sends `didChange`, the process owns the store, and diagnostics, code lenses ("3 issues, 1 PR") and hovers are views.
Mechanisms used: document source, index, items, views. Nothing new.

### 9.2 A collaborative editor with review, comments and explainers

Several people (and agents) edit one document, leave comments, request and give reviews, and write explainers (prose anchored to code). Needs:
* comments and explainers are items in `review` and `prose` layers, anchored to blocks: they survive edits and move with the code;
* a review is a set of items with state (`open` → `done` when addressed → `verified` by the reviewer), and a request for review is a tag plus a
  subscription for the reviewer;
* presence is derived from claims (`observe` while viewing, `write` while editing a block), as CANVAS.md designed;
* concurrent text editing: **not** solved by the backbone. Either edits are serialised through the canvas (an edit names its base and a stale one is
  refused and rebased, which suits block-sized edits by agents), or the editor runs its own CRDT and feeds the backbone one revision per settled change.
  The backbone needs only revisions, so either works.
Mechanisms used: items, state, tags, links (`discusses`, `fixes`), claims, subscriptions. Nothing new.

### 9.3 The GitHub project view

Point the canvas at a repository and see the project around the code: block history across commits, open PRs as pending overlays and conflicts, review
comments in the margin and outdated when the code moved, issues and project items anchored by permalinks and names, check runs and SARIF beside the local
gate. The mapping in LAYERED_DOCUMENT.md ("How GitHub maps onto the model") stands; this design supplies what it relies on: source anchors and the
resolver (5.1), the revision-aware index (6), keys by source (3.1), state and tags for project fields, the event log for webhooks, and `WriteSink` for G5.

### 9.4 Sub0Squad

Section 8.

## 10. Slices

Ordered so that each slice is used by a consumer before the next is built. Each check can fail, and says how it is measured.

| slice | what is built | check |
|---|---|---|
| **B0** format catch-up | the viewer's requested fields that need no store: item `id`, anchor `column`, `layerMeta`, `eventsFrom` and per-turn `events`, `%2F` revision names; a shared golden set (`fixtures/golden/`) written by the C++ tests and read by the viewer's tests | the C++ test writes the golden turn files byte-for-byte; `npm test` validates them; the viewer's trace-across-turns works on a real Sub0Squad attempt without title matching |
| **B1** store and log | `Event`, `Log` (`MemoryLog`, `FileLog`), `Store` with `putLayer` (reconcile by key), state, tags, links; snapshot written from the store | a replay of a recorded log gives the same snapshot as the live store; a re-derived issues layer over 5 turns yields one item per key with states open → done, matching hand-checked truth on a tier 0 attempt |
| **B2** inbox | `Query`, `Subscription`, `inbox`, `acknowledge`; Sub0Squad's verdict rebuilt as an inbox | Sub0Squad's `test_canvas_agent` and `test_blocks` pass unchanged, and the verdict text is byte-identical with the store in the path (an A/B with no expected difference: 0 changed bytes over the recorded replies) |
| **B3** claims and notes | `Claim`, expiry by a passed clock, block claims that follow merges, `note` events | unit tests for every rule in 3.5; Sub0Squad's fork executor (two workers, one file) takes write claims per section, and a seeded conflict is refused with the holder named |
| **B4** source anchors and the resolver | `Anchor.revision/quote/name`, text-quote re-anchoring, name anchors | a corpus of 50 anchors made at revision r and resolved at r+k (k up to 20, with moves, renames, deletions) from Sub0Squad's own canvas logs: each resolves to the right block or is marked outdated, against hand-checked truth; zero silent moves |
| **G1** git document adapter and `RevisionIndex` | `DocumentSource` over a local repository (reading `git` plumbing output), checkpoints, `history`, `carry` | the identity history of five known functions across Sub0Squad's last 200 commits, renames and the move to sub0canvas included, against hand-checked truth; a replay of 200 commits under 30 s |
| **G2** GitHub items, read-only | `sub0canvas::github`: PRs as a `pulls` layer, review comments as source anchors | each review comment on this repository's last 20 PRs lands on the right block at main's head or is outdated, against hand-checked truth |
| **G3** issues, discussions, project items | items with tags and state from project fields | the state mechanism is driven by a real project board: a status changed on GitHub shows in the view after one fetch |
| **G4** events and subscriptions over GitHub | the GitHub event feed into the log; an owner's subscription | "failing checks or new review comments on blocks under `lib/src/doc_*`" delivers an inbox summary with exactly the expected items |
| **G5** write back | `WriteSink` for comments, behind the consumer's confirmation | an agent proposes a reply anchored to a block; it is posted only on approval, and a rejected proposal leaves no trace at the source |
| **E1** editor | an LSP-like process serving decorations and hovers from views | a VS Code session shows the issues and prose of a Sub0Squad attempt in the margin, and they follow a function moved by an edit |
| **C1** collaborative review | review requests as tags plus subscriptions, presence from claims | two people and one agent review one file: every comment survives the author's subsequent edits on the right block, and the reviewer's inbox lists exactly the unaddressed ones |

| **S0** scale harness | a deterministic synthetic corpus generator (files, blocks, commits with edits, renames, moves, merges; generated on the fly, never written whole) and benchmarks of today's index (`bench_index`, label `timing`, local only) | the budgets of section 12 are measured and recorded for 10^5, 10^7 and 10^9 lines; today's limits (memory per block, retired scan) are numbers, not guesses |
| **S1** compact, lazy index | blocks keyed by blob hash and reused; no full texts held; fixed-size sketches instead of token sets; per-path laziness | S0 at 10^9 lines (streamed) within the memory budget; `test_doc_layers` unchanged; Sub0Squad's tier 0 cases unchanged (identity events byte-identical on the golden fixtures) |
| **S2** sketch index and per-path history | MinHash/LSH candidates for revive and merge; per-path checkpoints; `RevisionIndex` per path | revive and merge recall against the exhaustive scan on S0's corpus (no lost ids at 10^7 blocks); a path's history over 10^5 commits under the budget |
| **S3** large real repositories | G1's checks run against large public repositories cloned locally (the Linux kernel, Chromium; the owner approves each download) | the G1 identity-history check on five hand-checked functions per repository; the budgets hold on real code |
| **S4** sharded store and log | the store and log partitioned by path prefix; queries and inboxes touch only their shards | an inbox over one area of a 10^9-line synthetic project reads only that area's shards (measured), and replays in bounded time |

B0 is small and unblocks the viewer now. B1 and B2 are the core of the backbone and are proved on Sub0Squad, which has the most tests. G1 is the main
strengthening of the design (identity across a project's history) and can start after B1, in parallel with B2 to B4. **S0 runs first, in parallel
with B0**: its numbers decide the data structures B1 (the store) and G1 (the revision index) are built on, and S1 changes `BlockIndex`'s internals
before more code depends on them. Every slice climbs the proving ladder of [TESTING.md](TESTING.md) (unit, golden, use case, scale) before it is
integrated into Sub0Squad through a pull request there ([INTEGRATION.md](INTEGRATION.md)).

## 11. Open questions for the owner

1. **Where the store lives in Sub0Squad.** Per attempt (in the canvas loop, simplest, B2) or per workflow in the daemon (needed for the orchestration
   layer and for subscriptions that span workers). Proposed: per attempt first, the daemon in step 3 of the migration.
2. **Per-turn `events`.** Section 3.3 proposes per-turn events in snapshots and a cumulative log. That changes what Sub0Squad writes today (cumulative).
   The viewer already copes with both. Confirm.
3. **Ids across replay roots** (section 6). Proposed: local to a root, named in the file. A content-addressed id is the alternative, at the cost of
   identity changing on every body edit.
4. **The GitHub adapter and the network rule.** It reads from github.com with the user's `gh` login. It is a separate target that Sub0Squad never links.
   Confirm that this is acceptable for sub0canvas, or keep G2 to G5 on a local mirror only (`gh` export to files, then read offline).
5. **The C++ standard in the core.** `std::expected` (C++23) is used in the sketches, as Sub0Squad already builds C++23. Confirm sub0canvas keeps C++23
   rather than lowering it for wider reuse.

## 12. Scale: a codebase of billions of lines

The backbone must hold for a monorepo of billions of lines (use case U4 in [USE_CASES.md](USE_CASES.md): about 2 billion lines, 9 million files and
35 million commits is the published size of the largest known one). The rule that follows: **cost is proportional to what is touched, never to the
repository.** What is touched is the paths a view shows, an anchor names, a subscription's area covers, or a commit changes.

**What changes in the core** (today's code, measured against that rule):

| part | today | at scale |
|---|---|---|
| block discovery | `foldRegions` heuristic per file | unchanged per file (O(n)); a tree-sitter backend behind the same `FoldRegion` result for languages the heuristic gets wrong; chosen per path by extension |
| block storage | `Block` holds `std::set<std::string>` of tokens and of literals; `BlockIndex::texts_` holds every indexed file's full text | a compact block: id, path id (interned), span, signature hash, name (interned), and a fixed-size **MinHash sketch** of its tokens (64 x 32 bits) instead of the sets; texts fetched from the `DocumentSource` when needed, never held |
| reuse | a path is re-scanned when its text changes | blocks are computed **once per blob** (the git object id, or a hash of the text) and cached by it, so a file identical across 10^4 revisions and branches is scanned once |
| reconciliation within a file | signature, then unique name, then Jaccard over token sets | the same order; similarity estimated from sketches (MinHash's estimate of Jaccard); exact comparison only for the few candidates the sketch keeps |
| revive and merge across files | every retired block is scanned (`for (... : retired_)`) | an **LSH index** over retired sketches (bands of the sketch as bucket keys): candidates in O(1) per block, then the exact rule on those few; retired blocks expire from the index after a horizon (a revival after 10^4 commits is a new block) |
| history | one `BlockIndex` follows one line of texts; `RevisionIndex` (section 6) replays a tree | **per-path replay**: a path's own revisions (`git log --follow`-style, with git's rename and copy detection to hand cross-file moves to the LSH), checkpoints per path every N revisions; a whole-tree replay is never needed to resolve one anchor |
| store and log | not built (B1) | **sharded** by path prefix (a directory subtree per shard, split when large); each shard its own log segment and snapshot; a query names its area, and only those shards are read; cross-shard items (a PR touching many areas) are stored once and indexed in each shard they anchor into |
| output | `layeredJson` writes every file, block and event | scoped writers: a view or snapshot takes a query (paths, layers) and writes only what it covers; the full form remains for U1 |

**Budgets** (first targets; S0 replaces them with measurements): at least 1 million lines per second per core for cold indexing; an incremental
update proportional to the changed files of a commit; a warm anchor resolution under 10 ms; a compact block under about 200 bytes; resident memory
proportional to the paths touched. Performance is measured locally, never in CI (the family rule): benchmarks carry the ctest label `timing` and
skip on battery.

**What does not change:** the identity rules (section 2 and LAYERED_DOCUMENT.md), the item and anchor model, the event kinds, the adapter interfaces
(a `DocumentSource` already returns files per revision and changed paths), and the format (scoped snapshots are still `layered-doc-1`).

**Risks.** Sketch-estimated similarity can miss or invent a match near the thresholds: S2 measures recall and precision against the exhaustive rule
on the synthetic corpus, and the exact rule always confirms a candidate. Heuristic folding on unusual code yields wrong blocks: tree-sitter for the
languages that matter, chosen by measurement on S3's repositories. Interning and caching add state that must survive restarts: they are caches,
rebuilt from the document source, never the truth.

## 13. Prior art this design borrows from

See LAYERED_DOCUMENT.md's "Prior art". The additions specific to the backbone: **event sourcing** (the log as truth, views as projections, replay for
reproducibility); **Linear and GitHub Projects** (a small state lifecycle with free-form labels, status as a field); **LSP** (one process owns the
model, editors are views); **Backstage / Grafana data sources** (capability-declared adapters, credentials at the edge); **W3C Web Annotation** (the
text-quote selector of source anchors); **GitHub and Gerrit comment porting** (outdated, never silently moved).

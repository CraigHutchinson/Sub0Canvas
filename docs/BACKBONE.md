# The Sub0Canvas backbone: layers as the place where agents, people and sources meet

Status: **design, for the owner to read.** Built so far: slices 1 and 2 (`outline`, `edits`, `canvas`, `doc_layers`, the viewer) and amendment A12
(`applyReply` and turn control); everything else here is design. Section 14 (2026-11-18) checks it against the later use cases, lists the
amendments that found, and keeps the register of open decisions; the authoring tool ([AUTHORING.md](AUTHORING.md)) is now the first use case to invest
in, and block discovery has its own design ([BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md)). Written 2026-11-17 from the owner's direction ([DIRECTION.md](DIRECTION.md), originally Sub0Squad's `docs/LAYERED_DOCUMENT.md`; the sections
"Direction: Sub0Canvas as the squad's backbone" and "Sources"), the slice 1 and slice 2 code in `lib/`, the viewer in `src/`, and the viewer's schema
requests in [the README](../README.md#optional-fields-the-viewer-understands).

## 1. What this document decides

Sub0Canvas today is a library (`outline`, `edits`, `canvas`, `doc_layers`) and a viewer. It holds one document, a turn at a time, with one layer that
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
* how it all scales, in outline (section 12; the design is [SCALE.md](SCALE.md));
* pull requests, spikes and their dependencies as change views, in outline ([CHANGE_VIEWS.md](CHANGE_VIEWS.md)).

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
| **G1** git document adapter and `RevisionIndex` | `DocumentSource` over a local repository (reading `git` plumbing output), checkpoints, `history`, `carry` | the identity history of five known functions across Sub0Squad's last 200 commits, renames and the move to Sub0Canvas included, against hand-checked truth; a replay of 200 commits under 30 s |
| **G2** GitHub items, read-only | `sub0canvas::github`: PRs as a `pulls` layer, review comments as source anchors | each review comment on this repository's last 20 PRs lands on the right block at main's head or is outdated, against hand-checked truth |
| **G3** issues, discussions, project items | items with tags and state from project fields | the state mechanism is driven by a real project board: a status changed on GitHub shows in the view after one fetch |
| **G4** events and subscriptions over GitHub | the GitHub event feed into the log; an owner's subscription | "failing checks or new review comments on blocks under `lib/src/doc_*`" delivers an inbox summary with exactly the expected items |
| **G5** write back | `WriteSink` for comments, behind the consumer's confirmation | an agent proposes a reply anchored to a block; it is posted only on approval, and a rejected proposal leaves no trace at the source |
| **E1** editor | an LSP-like process serving decorations and hovers from views | a VS Code session shows the issues and prose of a Sub0Squad attempt in the margin, and they follow a function moved by an edit |
| **C1** collaborative review | review requests as tags plus subscriptions, presence from claims | two people and one agent review one file: every comment survives the author's subsequent edits on the right block, and the reviewer's inbox lists exactly the unaddressed ones |

| **S0** scale harness | a deterministic synthetic corpus generator (files, blocks, commits with edits, renames, moves, merges; generated on the fly, never written whole) and benchmarks of today's index (`bench_index`, label `timing`, local only) | the budgets of SCALE.md section 6 are measured and recorded for 10^5, 10^7 and 10^9 lines; today's limits (memory per block, retired scan) are numbers, not guesses |
| **S1** compact, lazy index | blocks keyed by blob hash and reused; no full texts held; fixed-size sketches instead of token sets; per-path laziness | S0 at 10^9 lines (streamed) within the memory budget; `test_doc_layers` unchanged; Sub0Squad's tier 0 cases unchanged (identity events byte-identical on the golden fixtures) |
| **S2** sketch index and per-path history | MinHash/LSH candidates for revive and merge; per-path checkpoints; `RevisionIndex` per path | revive and merge recall against the exhaustive scan on S0's corpus (no lost ids at 10^7 blocks); a path's history over 10^5 commits under the budget |
| **S3** large real repositories | G1's checks run against large public repositories cloned locally (the Linux kernel, Chromium; the owner approves each download) | the G1 identity-history check on five hand-checked functions per repository; the budgets hold on real code |
| **S4** sharded store and log | the store and log partitioned by path prefix; queries and inboxes touch only their shards | an inbox over one area of a 10^9-line synthetic project reads only that area's shards (measured), and replays in bounded time |

B0 is small and unblocks the viewer now. B1 and B2 are the core of the backbone and are proved on Sub0Squad, which has the most tests. G1 is the main
strengthening of the design (identity across a project's history) and can start after B1, in parallel with B2 to B4. **S0 runs first, in parallel
with B0**: its numbers decide the data structures B1 (the store) and G1 (the revision index) are built on, and S1 changes `BlockIndex`'s internals
before more code depends on them. Every slice climbs the proving ladder of [TESTING.md](TESTING.md) (unit, golden, use case, scale) before it is
integrated into Sub0Squad through a pull request there ([INTEGRATION.md](INTEGRATION.md)).

**Two further tracks (2026-11-18).** **D0-D3**, block discovery ([BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md) section 5): D0 (the provider seam, today's
heuristics behind it, byte-identical) lands before S1 and G1 replay more files through `foldRegions`. **V0-V5**, the authoring tool
([AUTHORING.md](AUTHORING.md) section 7), the first use case to invest in: V0-V2 pull forward the parts of B1, B2, B4, D2 and E1 they need (a store
with a file log, inboxes, quote re-anchoring, a Markdown provider, a process that owns the store), proved on the owner's own design work before
Sub0Squad depends on them. The order across all tracks, in phases with their gates, is [PLAN.md](PLAN.md) section 4. **P0-P5**, change views
([CHANGE_VIEWS.md](CHANGE_VIEWS.md) section 8): P0 after G1 and B4, P1 with S2's sketches, P3 in the authoring tool after V2, P4 widening G2.

## 11. Open questions for the owner

The eleven questions asked here are worked through in [DECISIONS.md](DECISIONS.md), and their recommended answers are defaults in the register
(14.3) or amendments (14.2). What the owner is asked to confirm, gathered with SCALE.md's and CHANGE_VIEWS.md's, is one sheet:
[PLAN.md](PLAN.md) section 3. This table keeps the original numbering so earlier references resolve.

| # | question | recommendation | now lives in | owner sheet |
|---|---|---|---|---|
| 1 | where the store lives in Sub0Squad | per attempt first, in the per-project format | register (14.3); DECISIONS.md 5 | O10 |
| 2 | per-turn `events` in snapshots | per turn with `eventsFrom`; the log is cumulative | register; B0 | O5 |
| 3 | ids across replay roots | deterministic birth ids | A16 (refines A2); DECISIONS.md 4 | O4 |
| 4 | the GitHub adapter and the network rule | read-only through `gh`, a separate target; captured files in tests | DECISIONS.md 5 | O7 |
| 5 | the C++ standard | C++23 | DECISIONS.md 5 | O8 |
| 6 | revision ids as strings | yes, at B0; numbers still read | A1 | O5 |
| 7 | the block id scheme | as question 3 | A16 | O4 |
| 8 | tree-sitter | an optional target; each grammar approved; adopted by measurement | register; DECISIONS.md 6 | O9 |
| 9 | applying a parsed reply in the library | **done** (2026-11-18) | A12 | O1 (the release) |
| 10 | where the review log lives | storage layered by kind; git refs per writer | A17, A19; register | O2 |
| 11 | the authoring server's stack | C++ `serve` over stdio, a Node bridge | register; DECISIONS.md 7 | O6 |

## 12. Scale

Moved to [SCALE.md](SCALE.md) (2026-11-18), the one home for scale: two target profiles (the largest published monorepo, lines-dominated; a private product monorepo measured locally, dominated by non-code files, history width and change volume), the requirements they set, the design (compact blocks keyed by blob, sketches and LSH, per-path replay, sharded store and indexes), the budgets and the risks. The rule it holds the backbone to: **cost is proportional to what is touched, never to the repository.** What does not change at scale: the identity rules, the item and anchor model, the event kinds, the adapter interfaces and the format.

## 13. Prior art this design borrows from

See LAYERED_DOCUMENT.md's "Prior art". The additions specific to the backbone: **event sourcing** (the log as truth, views as projections, replay for
reproducibility); **Linear and GitHub Projects** (a small state lifecycle with free-form labels, status as a field); **LSP** (one process owns the
model, editors are views); **Backstage / Grafana data sources** (capability-declared adapters, credentials at the edge); **W3C Web Annotation** (the
text-quote selector of source anchors); **GitHub and Gerrit comment porting** (outdated, never silently moved). For block discovery: **Notepad++
(Scintilla, Lexilla, user-defined languages), KSyntaxHighlighting, tree-sitter, LSP, Universal Ctags, GitHub Linguist** (BLOCK_DISCOVERY.md section
3). For policies: **Sub0Squad's processes as data** (14.4).

## 14. The later use cases against the groundwork (2026-11-18)

Written after the owner asked that the groundwork be checked against the later use cases, so that it is solid and extensible for them and for others
not yet named ([DIRECTION.md](DIRECTION.md) sections 3 and 4). It walks each use case over the mechanisms of section 3 (14.1), lists the amendments
that walk found, which are cheap now and expensive once B1 and G1 are built (14.2), and keeps a register of the decisions that are deliberately left
open for real use to resolve (14.3). Requirements are in [REQUIREMENTS.md](REQUIREMENTS.md), block discovery in [BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md),
the authoring tool (now the first use case to invest in) in [AUTHORING.md](AUTHORING.md).

### 14.1 The walk

A use case passes when it needs no mechanism beyond section 3. Where it needed a change, the change is an amendment (14.2), not a new mechanism.

| use case | mechanisms it uses | what the walk found | amendment |
|---|---|---|---|
| U7 the authoring tool (first) | items, state, links, inbox, views, the canvas, a store owned by one process | threads of replies; anchoring to selected text and ranges; views bounded by attention; a process the page and agents both talk to; Markdown blocks | A3, A5, A8, A10, A14 |
| U3 G2 pull requests and review comments | source anchors, the resolver, the revision index, keys by source | multi-line comments need ranges; comment replies are threads; PR heads are commit ids, not numbers | A1, A3, A5 |
| U3 G3 issues and project items | items, state, tags | a project field with values beyond the lifecycle (priority, iteration) | A4 (data), tags by convention |
| U3 G4 events and subscriptions | the log, inboxes | webhook payloads evolve; unknown kinds must survive | A7 |
| U3 G5 write back | `WriteSink`, confirmation | a private note must never be posted by accident | A11 |
| U3 forks, several repositories | stores, keys | a reference from one repository's item to another's | A9 |
| U4 S3 large real repositories | the index, per-path replay | a language the heuristic gets wrong is wrong at every commit | A8 |
| U4 S4 the sharded store | the store, the log, identities | a global id counter cannot be shared across shards and per-path replays | A2 |
| U5 E1 an editor | views, the index, a document source per buffer | nested blocks and kinds for the outline; ranges for decorations; one process serving the editor | A3, A8, A10 |
| U6 C1 collaborative review | items, state, tags, claims, inboxes | threads; authorship and time of an item; presence | A5, A6 |
| U8 quality signals (CI checks, SARIF, coverage, profiles) | items, anchors, layers | numeric per-block data with no mechanism per metric; SARIF regions are ranges | A3, A4 |
| U9 a prose-first reader of a codebase | prose layer, name anchors, views | documents are Markdown: headings must be blocks | A8 |
| U10 a multi-repository or dependency view | stores, links | items that point across documents | A9 |
| U11 an agent's long-lived memory of a codebase | `note` events, the store, the resolver | notes must outlive sessions and follow code across history | none new: B1's log plus B4's resolver |
| U12 change views ([CHANGE_VIEWS.md](CHANGE_VIEWS.md)) | items, links, anchors, views, inboxes | changes and topics are items; lineage is links (new conventions under A5); footprints are anchors; hunk fingerprints are item data (A4); a profile-B repository needs shared caches, partial history and git's own indexes | A19, A20, A21 |

The [prose verification brief](sessions/canvas-prose-verification.md) combines U8 checks and U9 prose on shared, revision-aware anchors.
Its receiving slice will establish how a change registers a new check and how historical evidence stays attached to the code, prose
and tests it actually checked. This is a backlog item for the claims and resolver work.

### 14.2 Amendments

Each is folded into the section it names when its slice is built; until then this list is the decision.

| # | amendment | changes | slice |
|---|---|---|---|
| A1 | **Revision ids are opaque strings** document-wide. A canvas's per-file numbers are one source's scheme; a commit id is another. Readers accept a number or a string. | `Anchor.revision`, `BlockEvent.turn` → a revision id; `revisions` in the format | B0 (format), G1 |
| A2 | **Identities are allocated without a global counter**: opaque, never parsed. Tests already compare rather than spell them (R-ID-11). **Refined by A16** (2026-11-18): deterministic birth ids meet this by construction, so the earlier per-root naming is dropped. | `BlockIndex` id allocation; ties to open question 3 | S1, S4 |
| A3 | **Anchors may be ranges**: `endLine`, `endColumn` beside `line`, `column`. | `Anchor`, the format | B0 |
| A4 | **Items carry open data**: `data` (a JSON object, kind-specific: coverage, durations, a project field) and `detailFormat` (`text` or `markdown`). A metric is a layer of items with data, never a mechanism. | `LayerItem`, the format | B1 |
| A5 | **Link kinds are open strings** with registered conventions (`duplicates`, `causes`, `fixes`, `implements`, `discusses`, `blocks`, `partOf`, and `repliesTo` for threads, `supersedes`, `tests`, `documents`). The core treats none specially except where a rule says so. | `Link::Kind` → string | B1 |
| A6 | **Authorship and time of an item are derived from the log** (its first and latest `item.put`), exposed by the store, never written by producers as fields. One truth. | the store's API | B1 |
| A7 | **The log evolves additively**: unknown event kinds and fields are kept and skipped by replay; the log's header records producer versions. | `Event`, `FileLog` | B1 |
| A8 | **Block discovery behind a provider seam**, with nested blocks, kinds and non-code formats; structure derived, identity granularity per document, folding per caller ([BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md) 4.7). | `outline.hpp`, `BlockIndex`, the canvas's SYMBOL | D0 |
| A9 | **One store per document; references across documents are links with document-qualified keys** (`<document>:<key>`), never anchors into another store. A workspace view composes stores. | keys, views | G2, later |
| A10 | **The store's surface is a message protocol**: every mutation is an event and every read a query or view spec, both serialisable, so one process owns a store and pages, editors, agents and scenarios talk to it alike (AUTHORING.md section 3). | the store's API, a `serve` executable | V1 |
| A11 | **Exports take a query**: snapshots, SARIF and write-back include only what the query selects, so a private item (by tag convention, `private/<actor>`) never leaves by default. Visibility is policy; the query is the mechanism. | writers, `WriteSink` callers | B1, G5 |
| A12 | **Applying a parsed reply moves into the library**: the all-or-nothing application of a `CanvasReply` across files (Sub0Squad's `applyCanvasReply`) is library behaviour that every consumer (the squad, the authoring tool, an editor) needs. With it, the 30 recorded-reply cases become this library's own test (R-EDIT-7). **Done 2026-11-18** (`applyReply`, atomic by construction; turn control moved too; [DECISIONS.md](DECISIONS.md) section 2). | `canvas.hpp`; a Sub0Squad PR to call it | next minor |
| A13 | **Policies are named and recorded** (14.3): every output records the policy versions that produced it, so two runs can be compared and a change of policy is visible in the data. | the format (`policies`), views | B0 onward |
| A14 | **Views take budgets**: a scope, item and character limits, and cursors; the summary-first rendering of R-REN generalised to every view (R-AUTH-1). | `ViewSpec` (3.6) | V2 |
| A15 | **Events carry their writer and a Lamport clock**: the store reads many writers' segments and merges them deterministically (Lamport clock, then writer); section 4's "single writer per document" becomes "single writer per segment" ([DECISIONS.md](DECISIONS.md) section 3). | `Event`, the store, section 4 | B1 / V1 |
| A16 | **Block ids are deterministic birth ids**: a hash of the revision, path and signature a block was born with, plus an ordinal among identical births, so every replay of the same history derives the same ids with no counter ([DECISIONS.md](DECISIONS.md) section 4). | `BlockIndex` id allocation; with A2 | S1, or B0 if cheap |
| A17 | **Storage is layered by kind**: git refs for shared annotations (one per writer, private items under a never-pushed namespace), the git common directory for caches, the user's state directory for per-reader state, exports on demand ([DECISIONS.md](DECISIONS.md) section 3). | the `Log` interface (`GitRefLog` in the tools), the cache layer | V1 |
| A18 | **The log is a module of its own**: its own format spec and no dependency on the rest of the library, so it can be lifted out into a shared orchestration layer (Sub0Fabric) when a second consumer needs it ([DECISIONS.md](DECISIONS.md) section 9). | the log's module boundary | B1 / V1 |
| A19 | **Caches are per user and keyed by object id**, shared by every clone and worktree of a repository (the repository identified by its root commit); per-clone state holds only what differs. Extends [DECISIONS.md](DECISIONS.md) section 3's cache layer, which was per clone. | the cache layer, `DocumentSource` | S1 |
| A20 | **Document sources may lack objects**: `files` and `revisions` return an unknown marker for a missing blob or a cut history, and callers render unknown rather than fail. | `DocumentSource` | G1 |
| A21 | **The git adapter uses git's own indexes**: commit-graph with changed-path Bloom filters for per-path history, one long-lived `cat-file --batch` for objects, patch ids for hunks; it never walks the whole tree or every ref per request. | the git adapter | G1, P0 |

### 14.3 Open decisions, kept open by design

The owner's rule: what is not clear-cut is kept as an **A/B decision** that real use resolves, and the design is shaped so that keeping it open is
cheap. The pattern for each:

1. **The mechanism is settled; the policy is a named, versioned parameter** with a stated default (`fold@1`, `identity-granularity@1`).
2. **Every output records the policies that made it** (A13), so data is never ambiguous about which arm produced it.
3. **It is resolved by measurement on a real use case**, with an off or baseline arm and enough runs to see the noise (Sub0Squad's practice), or, for
   deterministic policies, by replaying the same recorded history under both arms offline (the golden replay and scenarios make that cheap).
4. **The resolution is written here** with its evidence, and the losing arm is kept only if a use case still wants it.

| decision | arms | default now | resolved by | where it is kept open |
|---|---|---|---|---|
| fold policy: which bodies a view folds | worth folding (bodies of three lines or more) / by depth / by budget only / focus first | worth folding, focus open | Sub0Squad A/B on what the worker sees; the owner's use of the authoring tool (V2) | the view's fold policy (`ViewSpec`) |
| identity granularity per language | functions only / functions and methods / classes too; YAML depth; Markdown heading depth | functions and methods; Markdown headings to `###` | golden replay and G1 history under each arm: fewer orphans and fewer false merges win | the document's identity policy |
| block discovery per language | heuristic / declarative / tree-sitter / LSP | heuristic; tree-sitter as an optional target, adopted per language where measured to win (DECISIONS.md section 6) | the conformance corpus (D1) and S3's repositories | the provider registry (BLOCK_DISCOVERY.md 4.2) |
| reconciliation thresholds | Jaccard 0.5 and containment 0.6 / other values / sketch-estimated | today's | S0's corpus truth and G1's hand-checked history | an identity-rules policy |
| issue localisation | name, then literals / plus the call graph / none | name, then literals | Sub0Squad tier 0: a wrong pointer costs more than none | the issues producer's policy |
| verdict rendering | off / adaptive (threshold 6) / always; inline count | adaptive 6, inline 3 | measured in Sub0Squad (slice 2); continuing | Sub0Squad's switches, recorded in `agent.json` |
| events in a snapshot | per turn / cumulative | per turn, with `eventsFrom` (open question 2; DECISIONS.md section 5) | the viewer copes with both; settle at B0 | `eventsFrom` in the format |
| identity scheme | counter per root / random / content-addressed / deterministic birth ids | deterministic birth ids proposed (DECISIONS.md section 4) | G1 and S4: does identity survive what users do, and agree across clones | A2, A16 |
| where the store lives in Sub0Squad | per attempt / per workflow / per project | per attempt first, in the per-project format (DECISIONS.md section 5) | B2 in the squad | the squad's option |
| where the review log lives | working-tree files / git refs of its own / git notes / local only / a database / the forge | git refs per writer, caches in the git directory, state per user (DECISIONS.md section 3) | the owner's use (V1) | the `Log` interface (A17) |
| a shared orchestration layer (Sub0Fabric) | build now / build here behind a liftable boundary / extend Sub0Log | here, liftable (A18) | the second consumer of the log mechanics (likely Sub0Squad's journal) | DECISIONS.md section 9 |
| the authoring server's stack | C++ `serve` over stdio with a Node bridge / a C++ HTTP server / Node only | C++ over stdio with a bridge, on Sub0Platform's C++ layer (AUTHORING.md section 3; DECISIONS.md section 7) | V1: build cost and one truth for the store's rules | the protocol (A10), which every arm speaks |
| hunk equivalence (change views) | stable patch id only / patch id or same block with sketch similarity / block only | patch id or same block with similarity at least 0.8 | P2's precision and recall on synthetic and reference truth | the lineage policy (`lineage@1`, [CHANGE_VIEWS.md](CHANGE_VIEWS.md) section 2) |
| what marks a spike (change views) | title marker / label / long-held draft / stated only | title marker or label, set by the topic policy | the owner's use; the policy is data | the topic policy (CHANGE_VIEWS.md section 2) |
| topic grouping (change views) | tracker key in title or branch / labels / milestone / by hand | tracker key, with hand grouping overriding | P4 on the reference topic | the topic policy (CHANGE_VIEWS.md section 2) |
| the viewer's block list | an outline of the open file inside the tree / blocks of the open folder / a server-side search with paged matches; identity history (retired, merged, orphaned) as its own view in every arm | the open file's outline in the tree, with search | K0's numbers and the owner's use in V1a | the page's view spec ([AUTHORING.md](AUTHORING.md) section 3.2) |
| areas in the topic map (change views) | directories by depth / ownership files / clusters of co-changed blocks | directories folded to the budget | P3, by what the owner reads fastest | the topic map's view spec (A14) |

The reasoning behind the rows is in 14.5, [DECISIONS.md](DECISIONS.md) and [CHANGE_VIEWS.md](CHANGE_VIEWS.md) section 9; the rows themselves live only here.

This register is the place to add a decision when a design choice turns out not to be clear-cut. A decision leaves the register with its evidence.

### 14.4 Policies as data, supplied by the client

Sub0Squad already made this choice for its workflows: **processes as data** (`registry/processes/standard@1.json`, versioned `name@N`, a built-in
constant equal to the default file, a few parameterised executors in code; "extension is files and jobs, no plugin ABI", Sub0Squad
`docs/PROCESS_DESIGN.md`). The owner's direction is that Sub0Canvas's policies follow it: **iterable** (a new version is a new file, compared against
the old by A/B) and **client-provided** where an integration needs its own (Sub0Squad supplies its fold policy for its worker's prompt; the authoring
tool supplies another for a person's screen).

* **A policy is a versioned JSON object** with a name (`fold@1`, `identity-granularity@1`, `outline-cpp@1`, `reconcile@1`, `localise@1`,
  `render-verdict@1`), validated on load, with a refusal that names the field. The library ships each default **twice, identical**: as a constant in
  code (so the pure core needs no file) and as `registry/policies/<name>@<n>.json` (so it can be read, diffed and copied). A test proves the two are
  equal.
* **Code holds the executors, data holds the choices.** The fold executor takes a fold policy; the reconciler takes thresholds and the rule order; a
  declarative outline provider takes a language definition (BLOCK_DISCOVERY.md 4.3). No policy contains code, and there is no plugin ABI: a
  capability that needs code (a tree-sitter grammar) is compiled in and selected by data.
* **Who may supply which policy** follows from 4.7 of BLOCK_DISCOVERY.md: presentation policies (fold, render, budgets) are the caller's on every
  request; document policies (identity granularity, outline providers, reconciliation) are fixed per document, so a client supplies them when it
  creates the document, and changing them later is a reported migration. A client never changes a document policy per request.
* **Recorded in every output** (A13): a snapshot's `policies` names each policy and version that produced it, so two arms are told apart by data.
* **Kept open until relevant** (the owner's words): today only the defaults exist, as constants. The registry file, the loader and client supply are
  built when the first use case needs a second arm: the fold policy at V2 or in Sub0Squad's next A/B, whichever comes first. Until then the seam is the
  parameter struct, which costs nothing.

### 14.5 Pros and cons of the open decisions

Grounded comparisons to guide each decision; the evidence that will settle each is in 14.3's "resolved by". Each recommendation is a default, not a
closing of the question.

**Who decides folding** (BLOCK_DISCOVERY.md 4.7)

| option | for | against |
|---|---|---|
| the caller decides everything, structure included | maximum flexibility | identities would depend on who asked; layers made by one caller would not land for another; R-BLK-11 cannot hold |
| the library decides everything, folding included | one behaviour, simplest | a model's prompt and a person's screen need different folds; the squad's A/Bs on folding become impossible |
| **structure derived, granularity per document, folding per caller** (recommended) | identities are one per document; every reader folds as it needs; each part testable alone | three concepts to explain; granularity changes need a migration path (BLOCK_DISCOVERY.md 4.6) |

**Policies: built in, data, or client-provided** (14.4)

| option | for | against |
|---|---|---|
| constants in code only | simplest; nothing to load or validate | every experiment is a rebuild; a client cannot bring its own; arms are not visible in the data |
| **data files with built-in defaults, client supply when needed** (recommended) | Sub0Squad's proven pattern; A/B by swapping a file; recorded in outputs; no code in data | a loader and validation to maintain; versioning discipline (a changed file is a new version) |
| plugins (code supplied by the client) | anything is possible | an ABI to keep stable; untestable combinations; the core is no longer pure; Sub0Squad rejected it for the same reasons |

**Block discovery backend** (BLOCK_DISCOVERY.md)

| option | for | against |
|---|---|---|
| keep the heuristic, fix its faults | no dependency; fast; already identity-tested on real runs | every language is hand work; names and nesting stay approximate; misses data formats |
| declarative definitions (UDL-like) | the long tail without code; users add languages | regexes cannot name everything (generics, receivers); a rule engine to maintain |
| **tree-sitter where measured to win, heuristic and declarative behind it** (recommended) | real trees, names, nesting, error recovery, incremental; upstream queries; the editors' consensus | a dependency (owner approval); generated C per grammar, build size; spans move between grammar versions (handled by 4.6) |
| LSP outlines | the most accurate symbols where a server runs | a process and I/O per language; never in the core; only for consumers that already run servers (E1) |

**The authoring server's stack** (AUTHORING.md section 3)

| option | for | against |
|---|---|---|
| Node only, a JavaScript store | quickest to start; the viewer is already JavaScript | two implementations of the store's rules (identity, reconciliation, claims): the viewer and the squad would drift apart |
| a C++ HTTP server | one language, one truth | an HTTP and socket layer to write and test on Windows and Linux, with no dependency allowed without approval |
| **C++ `serve` over stdio with a Node bridge** (recommended) | one truth for the rules (the library); stdio is trivial and portable; the same process serves an editor (LSP-like, E1); the bridge is about as small as today's `serve.mjs` | two processes to start; a protocol to version (A10, which every option needs anyway) |

**Where the review log lives** (AUTHORING.md section 4)

| option | for | against |
|---|---|---|
| committed in the repository (`.sub0canvas/`) | shared across the owner's devices; history in git; review is part of the project's record | public in a public repository; commits for every comment unless batched; merge conflicts on the log (append-only, so mostly mechanical) |
| local only (ignored by git) | private; no commit noise | lost with the clone; not shared across devices; an agent on another machine cannot see the inbox |
| local, exported on demand (a query, A11) | private by default; shareable deliberately | one more step; two places to look |

The owner decides (open question 10); the server takes the location from its configuration, so all three stay possible. [DECISIONS.md](DECISIONS.md)
section 3 widens the options (git refs, git notes, a database, the forge) and recommends git refs per writer (A17).

**Events in a snapshot** (open question 2)

| option | for | against |
|---|---|---|
| **per turn, with `eventsFrom`** (recommended) | snapshots stay small (R-AUTH-1); the log is the cumulative record | Sub0Squad writes cumulative today, so B0 changes the producer |
| cumulative | a snapshot is self-contained | grows with every turn; duplicates the log; the viewer strips it anyway |

**Identity scheme** (open question 3, A2)

| option | for | against |
|---|---|---|
| local to a replay root, opaque (the earlier proposal) | identity survives body edits and renames (the point of block ids); no coordination within a root | ids differ between roots, so interchange names the root; shards need allocation without a counter (A2) |
| content-addressed (Unison-like) | the same everywhere, no coordination | every body edit is a new identity, which defeats layers that should follow the code; renames are new ids unless names are excluded |
| hierarchical position ids (CRDT-like) | stable under concurrent insertion | identity is about position, not about the function; a move breaks it |
| **deterministic birth ids** (recommended, A16) | the same on every clone that replays the same history, with no counter or coordination; survives body edits and renames | opaque hashes; live ids are deterministic only given the same edit sequence |

The full comparison, with counters and random ids, is [DECISIONS.md](DECISIONS.md) section 4.

# Decisions: the open questions, worked through

Status: **proposed, for the owner to confirm** (2026-11-18), except where marked **done**. Written for the owner's direction of 2026-11-18
([DIRECTION.md](DIRECTION.md) section 5): prefer a forward-looking design, the most capable solution first time where viable; brainstorm options and
weigh them for each question; move the proving grounds and the code they need from Sub0Squad (go-ahead given); for where data lives, consider existing
tooling such as `.git`, the Sub0 glue libraries and other prior art; keep a shared orchestration layer ("Sub0Fabric") open if the review finds that
shape. It answers [BACKBONE.md](BACKBONE.md) section 11 (questions 1-11); its recommendations are the defaults in section 14.3's register of open decisions, and its amendments are rows of 14.2.

Prior art here is from knowledge, not a fresh search; the facts about the Sub0 libraries are from their READMEs in local checkouts (2026-11-18).

## 0. In one table

| # | question | recommendation | status |
|---|---|---|---|
| 9 | apply a parsed reply in the library | moved, atomic by construction; turn control and the 30 recorded-reply cases moved with it | **done** (section 2) |
| 10 | where the data lives | by kind, in layers: git for truth and shared annotations (own refs, the Gerrit NoteDb / git-bug pattern), `.git/` for per-clone caches, the user's state directory for per-reader state; a working-tree export only on demand | proposed (section 3) |
| 3, 7 | the block identity scheme | **deterministic birth ids**: a hash of where and when a block was born, so every clone replaying the same history derives the same ids with no counter and no coordination | proposed (section 4) |
| 1 | where the store lives for Sub0Squad | per project (the repository), with each attempt a writer of its own segment; per-attempt use first, behind an option, already in the final format | proposed (section 5) |
| 2 | events in a snapshot | per turn, with `eventsFrom`; the log is cumulative | proposed (section 5) |
| 6 | revision ids | opaque strings in the format from B0; numbers still read | proposed (section 5) |
| 4 | the GitHub adapter and the network | a separate, read-only target with captured fixtures in tests; write-back only through confirmation; Sub0Squad never links it | proposed (section 5) |
| 5 | the C++ standard | stay on C++23, as the Sub0 family does | proposed (section 5) |
| 8 | tree-sitter | approve as an optional target; grammars pinned, adopted per language by measurement | proposed (section 6) |
| 11 | the authoring server's stack | **revised 2026-11-23** ([review](reviews/2026-11-23-fabric-portal-store.md) D7, owner): one transport-independent protocol; **native HTTP the local default** (HTTP/2 where tooling supports it, HTTP/1.1 the baseline) on Platform's `net`; stdio JSON lines as the second transport; the Node bridge optional | revised (section 7) |
| - | the Sub0 glue libraries | adopt Sub0Platform in the tools (not the core), Sub0Log for diagnostics, Sub0Pipeline for check runs and parallel indexing, Sub0MemPage and Sub0TieredCache at scale, Sub0Math's performance tooling for benchmarks | proposed (section 8) |
| - | Sub0Fabric | **decided 2026-11-23 (owner): build it now** as its own repository; the trigger below was already met. Scope, modules and first consumers: [review](reviews/2026-11-23-fabric-portal-store.md) D1-D4 | **decided** (section 9) |

## 1. How the options were weighed

Every question was scored against the same forward-looking criteria, taken from the use cases ([USE_CASES.md](USE_CASES.md)) and requirements
([REQUIREMENTS.md](REQUIREMENTS.md)):

* **Capability at the far end**: does it still work for U3 (a repository's history and its PRs), U4 (billions of lines), U6 (many people and agents)
  and U7 (the authoring tool) without being replaced?
* **One truth**: one implementation of each rule, one home for each fact.
* **Offline and local first**: works with no network and no server; nothing leaves the machine unless a person sends it (AGENTS.md).
* **Reproducible**: the same inputs give the same outputs on any machine (tests, A/Bs, two clones agreeing).
* **Reuse before invention**: existing tools and the Sub0 libraries first.
* **Cost now**: build effort, dependencies, the risk of getting it wrong.

## 2. Moving the proving grounds and their code (question 9): done

| moved from Sub0Squad | to | notes |
|---|---|---|
| `applyCanvasReply` (`src/canvas_agent.cpp`) | `applyReply` and `applyReplyText` in `canvas.hpp` | **improved**: all or nothing is now atomic by construction (the canvas is worked on as a copy and committed only on success); before, whole files were written before a later edit could fail, and every caller had to apply to a scratch copy. `std::expected` per the code style. |
| turn control (`turn_control.hpp`, `.cpp`: joining cut-off replies, runaway and overrun detection, the continuation prompt) | `sub0canvas/turn_control.hpp` | pure text over the reply format, unchanged but for the namespace; every model-driven consumer of the reply format needs it |
| the recorded-reply corpus (`tests/data/replies`, 30 cases) | `fixtures/replies/`, run by `test_replies` | the runner restated from the case format; it now also checks the canvas itself is unchanged after a refusal (not a scratch copy) |
| turn control's tests (`test_blocks` block 1, rows 34-36) | `test_turn_control` | every check as it was |
| earlier today: block 4, the skeleton block, the canvas model check | `test_views`, `test_canvas_model` | |

Checked: `ctest --preset quick` passes (8 suites); a tampered copy of two cases fails `test_replies` with each case named.

**What stays in Sub0Squad** (its own, not the library's): the fork harness and the throughput model (block 6), the runaway policy's use in the
streaming loop, `applySectionSignature` and `compileFindings` (harness assembly), the platform tests. **What Sub0Squad must do** (a PR there, after a
tag here): replace its `applyCanvasReply` and `turn_control` with forwarding headers to these, drop the copied tests, and keep
`run-v11-test-app-reply.txt` (its platform test reads it).

## 3. Where the data lives (question 10)

The data is not one thing. Each kind has different needs, so the answer is a layering, each layer using existing tooling:

| kind of data | examples | needs |
|---|---|---|
| the document | files and their history | already in git |
| shared annotations | review comments, threads, states, decisions, plan items, claims | durable, shared across devices and people, mergeable without a server, attributable, append-only |
| derived caches | block index checkpoints, sketches, snapshots for the viewer | rebuildable, per clone, large, never the truth |
| per-reader state | inbox cursors, folds and view preferences, the server token | private, per user, small |
| exports | a review rendered for a PR, SARIF, a snapshot to share | deliberate, scoped by a query (A11) |
| configuration | the identity policy, the command allowlist, policy files a client supplies | part of the project: reviewed, versioned, shared |

### 3.1 Options for the shared annotations (the hard part)

| option | how | for | against |
|---|---|---|---|
| **A. files committed in the working tree** (`.sub0canvas/events.ndjson`) | ordinary commits | simplest; visible and reviewable in PRs; everything is in git | every comment is a commit on the branch, or batched; merge conflicts on one file when two devices append; review data mixed into code history and diffs; follows branches, so a comment made on a branch disappears when switching |
| **B. git refs of their own** (`refs/sub0canvas/...`), the **Gerrit NoteDb / git-bug / Radicle** pattern | events are written as git objects (a commit per batch, its tree holding the event segment) on refs outside branches; synced by `git push` / `fetch` with a refspec; merged by union | in git, so offline, replicated, content-addressed and signed like code; **no noise** in branches, diffs or the working tree; independent of the checked-out branch; proven at scale (Gerrit keeps all review metadata this way); a writer per ref means no write conflicts | invisible to people who only use GitHub's web pages; needs a refspec to sync (a one-time `git config`); refs pushed to a public remote are public (so private notes go to a local-only namespace); git plumbing to write (the library stays pure: the tool does the I/O) |
| **C. git notes** (`refs/notes/sub0canvas`), the **git-appraise** pattern | notes attached to commits | built into git; `cat_sort_uniq` merge for line-based data | anchored to commits, not to blocks or documents; one notes tree per ref merges awkwardly at volume; a weaker fit than B for an event log |
| **D. local only** (`.git/sub0canvas/`) | files inside the git directory | private; no sync to configure | lost with the clone; not shared across devices or with an agent elsewhere |
| **E. a database file** (SQLite, as Fossil keeps tickets and wiki) | one file | queries, transactions | a binary file in git merges badly; outside git it is option D; a second storage engine to depend on |
| **F. a server or GitHub itself** | the forge holds it | shared with everyone on the web | needs a network and an account; the opposite of local first; GitHub stores review comments server-side, which is what G2 reads, not where our own truth should live |

### 3.2 Recommendation

1. **The document**: the working tree and its git history (as now; `DocumentSource` "local/worktree" and G1's git adapter).
2. **Shared annotations: option B**, events in git under `refs/sub0canvas/<document>/<writer>`, one ref per writer (a person's device, an agent),
   each an append-only chain of commits whose trees hold event segments in the log format (`canvas-events-1`). Readers merge all writers' refs,
   ordered by a Lamport clock with the writer as tie-break. This is the shape Sub0Log already uses for processes (each writes its own segment; readers
   merge), lifted onto git for replication, which is the strongest sign for Sub0Fabric (section 9). Sync is `git fetch` and `git push` with a refspec
   the tool configures on first use (with the owner's confirmation); **private items go to `refs/sub0canvas-local/...`, which is never pushed**.
3. **Derived caches: option D**, under the git common directory (`git rev-parse --git-common-dir`, so all worktrees of a clone share them):
   `.git/sub0canvas/cache/`. Rebuildable from the document and the log; deleting them costs time, never data. **Amended (A19, SCALE.md)**: per-blob
   tables keyed by object id move to a per-user cache shared by every clone of the repository, because a work-dominated monorepo is held as several
   clones of hundreds of gigabytes each; only per-clone state stays under the git directory.
4. **Per-reader state**: the user's state directory, via Sub0Platform's `dirs.state("sub0canvas")`, keyed by repository (inbox cursors, folds, the
   server's session token).
5. **Configuration: committed** under `.sub0canvas/` (`config.json`, `commands.json`, policy files), like `.editorconfig`: it is part of the
   project, changes rarely, and is reviewed like code. Annotations never go there.
6. **Exports: option A only as an export**: `sub0canvas export review --query ...` writes a readable Markdown or JSON file a person may commit or attach
   to a PR. It is a view, never the truth.

**What this changes in the design.** BACKBONE.md section 4's rule "single writer per document" becomes **single writer per segment** (per ref); the
store reads many segments. Every event carries its writer and a Lamport clock (amendment A15). Rule 4 ("the log is never rewritten") holds per writer.
The `Log` interface gains a `GitRefLog` implementation in the tool (not the core), beside `MemoryLog` and `FileLog`.

**Pushing annotations to this public repository** makes them public. The owner decides per namespace, and the default is to push nothing until asked.

## 4. Block identity (questions 3 and 7)

Identity must be stable through edits (the whole point), unique without a global counter (shards, per-path replay: A2), and, newly important with
section 3, **the same on every clone**: a comment anchored to a block on the owner's laptop must resolve on another device that replayed the same
history.

| option | for | against |
|---|---|---|
| a counter per index (today: `b7`) | short, readable | depends on processing order: two clones, or a sharded and an unsharded replay, give different ids; needs coordination across shards |
| a counter per replay root, named in the file (the earlier proposal) | stable within a root | still order-dependent; ids from two roots cannot be compared, so a comment made against one root is lost against another |
| random ids at birth (128-bit, UUIDv7-like) | no coordination; unique | two clones replaying the same history get different ids, so annotations synced through git (section 3) would not resolve on the other clone |
| content-addressed (Unison: a hash of the body) | the same everywhere | a body edit is a new identity: layers would fall off the code on every edit, the opposite of the requirement |
| **deterministic birth ids** (recommended) | an id is a hash of **where and when the block was born**: the revision it first appeared in, its path there, its signature there, and an ordinal among identical births; carried thereafter by the usual rules. Every replay of the same history derives the same ids (no counter, no coordination, shard-safe), yet a body edit or a rename never changes it | ids are opaque hashes (shown shortened, with the block's name beside them); a block born in a working-tree edit gets its birth revision from the canvas or the worktree revision (A1), so live ids are deterministic only given the same edit sequence, which is true for the log replayed |

With A1 (revision ids are strings) this is cheap: the birth revision is already known when a block is added. Tests are already independent of the
spelling (R-ID-11; the golden replay maps ids one to one), so switching costs no test changes. **Amendment A16** records it.

## 5. The remaining questions

**Question 1, where the store lives for Sub0Squad.**

| option | for | against |
|---|---|---|
| per attempt, in the canvas loop | simplest; B2's byte-identical check is local | issues, notes and claims die with the attempt; nothing spans workers |
| per workflow, in the daemon | spans workers; the orchestration layer can live there | one more process owning state; still forgets across workflows |
| **per project (the repository), each attempt a writer of its own segment** (recommended) | the same store a person sees in the authoring tool: a squad's issues and notes are reviewable by the owner and survive; a later attempt can read what an earlier one learnt (U11); sharding and merging come from section 3 for free | the most to build; must stay behind an option until proved |

Start per attempt (B2, behind an option, off by default, as every new capability in Sub0Squad), but write the per-project format from day one, so the
step to per project is configuration, not migration.

**Question 2, events in a snapshot**: per turn with `eventsFrom` (BACKBONE.md 14.5 has the comparison). Settle at B0.

**Question 6, revision ids as strings**: yes, at B0. A canvas revision is written `"r3"` or `"src/calc.cpp@3"`, a commit as its id, a worktree state
as its content hash; readers accept numbers in old files.

**Question 4, the GitHub adapter and the network.**

| option | for | against |
|---|---|---|
| live reads with the user's `gh` login, read-only, in a separate target (recommended) | real data for U3; `gh` handles authentication; Sub0Squad never links it, so its "nothing leaves the machine" holds | network access from a library target (only when a person runs it) |
| an offline mirror only (`gh api` exported to files, then read) | no network in the library at all | a manual step every time; stale data |
| no GitHub support | nothing to secure | U3 is lost |

Tests read captured files only (R-SRC-8); write-back (G5) only through `WriteSink` behind a person's confirmation. **git-bug's bridges** (GitHub, GitLab,
Jira imported into git-stored issues) are prior art for importing items into the section 3 log, so GitHub data can also be mirrored into git refs
and read offline.

**Question 5, the C++ standard**: stay on C++23. The family is C++23 (Sub0Pub, Sub0Pipeline, Sub0Log, Sub0Platform's C++ layer); `std::expected` and
`std::print` are used; lowering it buys reuse no current consumer needs.

## 6. tree-sitter (question 8)

| option | for | against |
|---|---|---|
| never | no dependency | every language is hand work forever; names, nesting and error recovery stay approximate (BLOCK_DISCOVERY.md section 2) |
| **an optional target, grammars pinned, adopted per language by measurement** (recommended) | real syntax trees, error recovery, incremental parsing (E1, scale), upstream `tags.scm` / `outline.scm` queries; the core never depends on it; the conformance corpus (D1) decides each language | a download per grammar (the owner approves each); generated C, larger builds; spans move between grammar versions (handled by BLOCK_DISCOVERY.md 4.6) |
| tree-sitter in the core, for every language | one path | the core is no longer dependency-free; every consumer pays the build |

The runtime is a small C library with no dependencies (MIT). Grammars come through CPM at exact pins, so a build is reproducible, and the cache is
shared (`CPM_SOURCE_CACHE`).

## 7. The authoring server's stack (question 11)

> **Revised 2026-11-23.** The recommendation below assumed Platform's `net` module had not landed; it has (`SocketListener`, `SocketStream`), and
> Sub0Squad's portal already serves HTTP on it. The owner decided native HTTP is the local default, with HTTP/2 where tooling supports it and
> HTTP/1.1 as the baseline, stdio as the second transport and the Node bridge optional. The reasoning, the TLS constraint on HTTP/2 and the shared
> `http` module are in [reviews/2026-11-23-fabric-portal-store.md](reviews/2026-11-23-fabric-portal-store.md) D7 and D10. The text below is kept as
> the original analysis.

BACKBONE.md 14.5 compares a JavaScript store, a C++ HTTP server, and a C++ process over stdio with a bridge. The survey adds one fact: **Sub0Platform's
C++ layer** (processes, files and atomic writes, locks, local IPC, sockets, per-user directories; today Sub0Squad's layer 0, to be moved to Sub0Platform)
is exactly what the `serve` executable and the CLI need, so the tools do not reinvent it.

Recommendation: `sub0canvas serve`, C++, JSON lines over stdio (LSP-like), built on Sub0Platform's C++ layer for files, locks, processes (running
allowlisted checks) and later local IPC; the core library stays pure and does not depend on Sub0Platform. A small Node bridge serves the page and
turns HTTP and server-sent events into stdio messages, because a browser needs HTTP. When Sub0Platform's `net` module lands, the bridge can be
replaced by the process serving HTTP itself; the protocol (A10) does not change.

## 8. The Sub0 glue libraries: what to adopt, iterate or leave

| library | what it offers (from its README) | overlap with Sub0Canvas | recommendation |
|---|---|---|---|
| **Sub0Platform** (MIT) | the OS layer: paths and WSL, per-user directories (config roaming, cache and state local), processes, atomic files, cross-process locks (a crashed owner's lock set aside); later sockets, local IPC, mapping. Python now; C++ moving from Sub0Squad | the `serve` process, the CLI, `GitRefLog`, running checks, per-reader state (section 3), the single-writer lock | **adopt in the tools, not the core**; Sub0Canvas becomes the second C++ consumer, which is a reason to finish the C++ move (Sub0Platform issue 1). Iterate: a `git` plumbing helper may belong there if a second project wants it |
| **Sub0Log** (MIT) | a structured diagnostic stream: typed records, call-site descriptors, crash-safe memory-mapped segments, one segment per process merged at read time, `sub0log-cat` to text or JSON lines | diagnostics of `serve` and the CLI; **its segment and merged-reader design is the shape section 3 needs for the event log** | adopt for diagnostics; use its design as the model for the log; the shared part is Sub0Fabric's seed (section 9) |
| **Sub0Pub** (MIT) | typed publish-subscribe, direct wiring or a bounded broker, synchronous, no heap in the core | in-process change notification inside `serve` (the store notifies views, views notify the bridge) | optional; adopt if `serve` grows several in-process listeners. Durable subscriptions stay inboxes with cursors (BACKBONE.md 3.4), which Sub0Pub does not replace |
| **Sub0Pipeline** (MIT) | a DAG job scheduler: dependencies, executors, cancellation, timeouts, deferred completion, remote sub-graphs over a transport, observer events | V4's check runs (configure, build, test as a DAG with cancellation); S1-S3's parallel indexing of many files; remote sub-graphs for distributed indexing at U4 | adopt in the tools at V4 and S3; not in the core |
| **Sub0MemPage** (MIT) | paging orchestration over memory-mapped regions | index segments and logs too large for memory (S2, S4): sketches and checkpoints mapped and paged by use | adopt at S2/S4 if S0's numbers show the index outgrowing memory; part of Sub0Fabric's shape (owner, 2026-11-18) |
| **Sub0TieredCache** (MIT) | a bounded cache for huge frozen sparse-lookup tables, sharded over immutable sources, with a versioned remote chunk mirror | frozen per-blob block tables and sketch indexes at scale (blocks computed once per blob, BACKBONE.md section 12) | adopt at S2/S4 for frozen tables; evaluate against S0's numbers |
| **Sub0Math** (MIT) | the shared math vocabulary; **hosts the family performance tooling** | no math overlap; its performance tooling fits L3 benchmarks (S0) | use its performance tooling for `bench_index` if it fits; nothing else |
| **Sub0LlmModelRegistry** | immutable versioned folders with a manifest, aliases, a generated index | the pattern for the policy registry (14.4) and checkpoint layout | borrow the pattern, not the code |
| Sub0Squad's `journal-1` and job store | NDJSON with a torn final line discarded; job state | the same problem as the event log | converge on one log format (section 9) |
| CraigHutchinson/.github CI tools | CI standard, local gates, private-name guard | already used | keep |

## 9. Sub0Fabric: a shared orchestration layer, kept open

> **Decided 2026-11-23.** The owner chose to build Sub0Fabric now. The extraction trigger below was already met (Sub0Squad's `journal-1`, this
> project's designed event log and Sub0Log's segment merge each exist or are specified), so the "kept open" recommendation is superseded. The
> review ([reviews/2026-11-23-fabric-portal-store.md](reviews/2026-11-23-fabric-portal-store.md) D1-D4) sets the scope: lift Squad's journal first,
> Canvas's events second, Sub0Log through a read-only adapter. The table and options below stay as the analysis.

The owner's concept (2026-11-18): extend Sub0Log's architecture into **Sub0Fabric**, a shared orchestration layer; Sub0MemPage may fit it too. The
review did find this shape. Three projects solve parts of one problem:

| part | Sub0Log | Sub0Canvas (designed) | Sub0Squad |
|---|---|---|---|
| append-only records with typed schemas | records with call-site descriptors | events with versioned kinds (A7) | `journal-1` entries |
| one writer per segment, merged at read time | per process | per writer ref (section 3) | one journal per daemon |
| crash safety | memory-mapped segments survive a hard kill | a torn final line discarded | a torn final line discarded |
| cursors and subscriptions | a live tailer | inboxes with cursors (3.4) | the daemon's polling |
| transport and replication | files | git refs (section 3) | files; Sub0Pipeline's remote sub-graphs for jobs |
| paging large data | | logs and indexes at U4 | |

**The shape of Sub0Fabric, if it is extracted**: segments per writer (Sub0Log's design), records with versioned schemas, merged readers ordered by
Lamport clock and writer, durable cursors (inboxes) and live notification (Sub0Pub), replication backends (files, git refs), paging of large segments
(Sub0MemPage), transports for remote readers (Sub0Pipeline's `ITransport`, Sub0Platform's IPC).

| option | for | against |
|---|---|---|
| build it as Sub0Fabric now | one design from the start; three consumers benefit | designed for consumers that have not used it; a new repository and its upkeep before the first use proves the shape |
| **build it in Sub0Canvas behind a liftable boundary, extract on the second consumer** (recommended) | proved first on a real use (V1-V2); the boundary (a `log` module with its own format spec and no dependency on the rest of the library) makes extraction mechanical; the trigger is concrete | some rework at extraction (namespaces, packaging) |
| extend Sub0Log itself | the segment code exists and is tested | Sub0Log's contract is diagnostics (bounded, lossy under pressure by design, no producer formatting); a domain log must never drop a record, so the contracts differ |

**The trigger to extract**: the second consumer that needs the same mechanics, most likely Sub0Squad moving its journal onto the log format, or
Sub0Log wanting git replication. It stays in BACKBONE.md 14.3's register until then.

## 10. Amendments this adds

A15-A18 (writers and Lamport clocks, deterministic birth ids, storage layered by kind, the log as a module of its own) are rows of the one
amendment register, [BACKBONE.md](BACKBONE.md#142-amendments) section 14.2.

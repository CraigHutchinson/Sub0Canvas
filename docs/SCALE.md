# Scale: very large repositories and the work around them

Status: **design, for the owner to read** (2026-11-18). This is the one home for scale: the target profiles, the requirements they set, the design
that answers them, and the budgets. It consolidates what was in USE_CASES.md (U4), BACKBONE.md section 12 and the scale rows of REQUIREMENTS.md,
which now point here. Nothing here is built. S0 ([sessions/s0-scale-harness.md](sessions/s0-scale-harness.md)) replaces the guesses with
measurements.

## 1. The rule

**Cost is proportional to what is touched, never to the repository** (R-SCALE-1). What is touched is the paths a view shows, an anchor names, a
subscription's area covers, a commit changes, or a change's diff modifies. Every structure below exists to keep that rule at the sizes of
section 2.

## 2. Two target profiles

Scale has more than one shape. A repository can be large in lines, or large in files, history and concurrent work. The design is checked against
both shapes.

| measure | **A. the largest published monorepo** (lines-dominated) | **B. a private product monorepo** (work-dominated; measured locally 2026-11-18) |
|---|---|---|
| tracked files | about 9 million | 440,000 |
| what the files are | source, mostly | a third are metadata sidecars beside assets (147,000); 62,000 images; 72,000 C#, 33,000 C/C++ headers, 9,000 C++ sources; 26,000 text, 11,000 Markdown; about 25,000 serialised scene and asset files |
| lines of source | about 2 billion | about 30 million lines of code, plus about 10 million of text and Markdown |
| commits | about 35 million | about 920,000 in trunk's ancestry, over nineteen years |
| git directory | (server-side) | 430 GB on the developer's disk; 107 million objects in 190 packs (170 GiB) |
| branches | | 12,400 remote branches; release branches with ports between them |
| change volume | | about 1,700 pull requests opened and 1,500 merged per week (about 300 merges per working day); 2,550 open at once, 55% of them drafts |
| how developers hold it | a virtual file system | several full clones per developer, and up to tens of worktrees per clone (18 on the measured one), one per line of work |

Profile A is the published size of the largest known monorepo (Google, 2015). Profile B was measured on a working checkout of a private
repository and is recorded here **without its name** (AGENTS.md: a public repository never names a private one). Public stand-ins for S3 are large
open repositories (the Linux kernel, Chromium) cloned locally, each download approved by the owner.

**What each profile stresses.** Profile A stresses the index: lines to scan, blocks to hold, a history too long to replay. Profile B stresses
everything else. Most files are not code. The git directory alone is larger than most disks' free space. History is wide (thousands of branches)
as well as long. Hundreds of changes merge each day while thousands are open, and every developer keeps several clones. A design that meets A
alone would still fail B, by indexing sidecar files as code, by keeping one cache per clone, or by recomputing every open change's footprint on
each trunk merge.

## 3. Requirements

From profile A (stated in U4 since 2026-11-17):

| requirement | why | today's code |
|---|---|---|
| **Lazy, per-path indexing.** A path is indexed when first needed, never the whole tree. | 10^7 files cannot be read for one view | `BlockIndex` indexes what it is fed: fine, the consumer decides |
| **Content-addressed reuse.** Blocks of a file are computed once per blob (its git object id) and shared across revisions, branches and clones. | most files are identical across most revisions | not built: `update` re-scans text, keyed by path |
| **No full texts held.** The index keeps hashes, spans and sketches, and fetches text from the document source on demand. | 10^9 lines of text do not fit in memory | `BlockIndex::texts_` keeps every path's text; `Block` keeps two `std::set<std::string>` |
| **Sub-linear candidate search.** Revive, merge and hunk matching use a sketch index (MinHash with locality-sensitive hashing), not a scan. | a scan over every retired block is quadratic across history | `retired_` is scanned in full on every unmatched block |
| **Per-path history.** Replay follows one path's revisions with rename detection and checkpoints, never every commit of the tree. | replaying 10^7 commits for one anchor is impossible | `RevisionIndex` is designed per tree; section 4 makes it per path |
| **Sharded store and log.** Items, events and indexes are partitioned by path prefix; queries read only the shards of their area. | one log of a monorepo's events is too large to replay | the store is not built (B1): decide before building |
| **Bounded output.** Snapshots and views are scoped by a query, never "everything". | `layeredJson` writes every file and block | fine for U1; scoped writers for U3, U4, U12 |
| **Linear, correct parsing.** O(n) per file in every language. | the brace and indent heuristic is fast but language-blind | `foldRegions`; providers and tree-sitter ([BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md)) |

Added from profile B (2026-11-18):

| requirement | why | design |
|---|---|---|
| **Kinds before blocks.** Every path is classified cheaply (by path and name, then the first bytes) before any parse: code, document, data, sidecar, binary. Binary files get no blocks; sidecars and serialised assets are data formats, indexed only when a document policy asks. | two thirds of profile B's files are not code; parsing them as code makes wrong blocks and wastes the budget | the provider registry's detection (BLOCK_DISCOVERY.md 4.2) runs first and is total; `kinds@1` is a document policy |
| **One cache per user, shared across clones.** Per-blob tables (blocks, sketches) are keyed by object id and shared by every clone and worktree of the same repository on the machine; per-clone state is small. | several 430 GB clones per developer: per-clone caches multiply cost by the number of clones | amendment A19 |
| **History may be incomplete.** Partial clones (missing blobs), shallow clones (missing history) and objects fetched on demand are normal. An anchor or replay that needs a missing object reports it as unknown and asks the source; it never fails the view. | profile B's history is too large to require in full; developers trim it | amendment A20 |
| **Wide history.** Replays go along one branch's first parents, per path; release branches are replayed separately from their branch points; ports are links (CHANGE_VIEWS.md), not merges. | 12,400 branches; release lines live for years | `RevisionIndex` per (branch, path) |
| **Incremental under merge load.** A trunk merge updates what it touched: its changed paths' blocks, the open changes whose footprints meet them (drift), the subscriptions whose areas cover them. Cost is proportional to the merge, not to the open changes. | about 300 merges a working day against 2,550 open changes | the block-to-open-changes index (section 5) |
| **Footprints once per head.** A change's footprint and hunk fingerprints are computed once per (base, head) pair and cached by it; a push recomputes only that change. | 1,700 new changes a week, each pushed many times | content-addressed like blobs |
| **Git as the engine, not the bottleneck.** Per-path history uses git's commit-graph with changed-path Bloom filters; objects are read through one long-lived `git cat-file --batch` per repository; nothing walks the whole tree or all refs per request. | `git log -- path` without Bloom filters reads every commit; a process per object is too slow at 10^8 objects | the git adapter (G1); amendment A21 |

## 4. The design

| part | today | at scale |
|---|---|---|
| classification | by suffix inside `foldRegions` | a total classifier first (section 3), its result cached per path and policy version |
| block discovery | `foldRegions` heuristic per file | a provider per language behind one outline result, measured by a conformance corpus ([BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md)); O(n) per file whichever provider |
| block storage | `Block` holds `std::set<std::string>` of tokens and of literals; `BlockIndex::texts_` holds every indexed file's text | a compact block: id, path id (interned), span, signature hash, name (interned), a fixed-size **MinHash sketch** of its tokens (64 x 32 bits); texts fetched from the `DocumentSource` when needed, never held |
| reuse | a path is re-scanned when its text changes | blocks computed **once per blob** and cached by object id in the per-user cache (A19), so a file identical across 10^4 revisions, branches and clones is scanned once |
| reconciliation within a file | signature, then unique name, then Jaccard over token sets | the same order; similarity estimated from sketches; exact comparison only for the few candidates the sketch keeps |
| revive, merge and hunk matching across files | every retired block is scanned | an **LSH index** over sketches (bands of the sketch as bucket keys): candidates in O(1) per block, then the exact rule on those few; retired blocks expire from the index after a horizon (a revival after 10^4 commits is a new block) |
| history | one `BlockIndex` follows one line of texts; `RevisionIndex` (BACKBONE.md section 6) replays a tree | **per-path replay** along one branch's first parents, using git's rename and copy detection to hand cross-file moves to the LSH; checkpoints per path every N revisions; a whole-tree replay is never needed to resolve one anchor |
| store and log | not built (B1) | **sharded** by path prefix (a directory subtree per shard, split when large); each shard has its own log segment, snapshot and indexes; a query names its area, and only those shards are read; an item anchored into many areas (a change touching many directories) is stored once and indexed in each shard it anchors into |
| open changes | not built | a **block-to-open-changes index** per shard (section 5) |
| output | `layeredJson` writes every file, block and event | scoped writers: a view or snapshot takes a query and writes only what it covers; the full form remains for U1 |

## 5. Indexes that keep queries proportional

| index | key, value | serves | updated by |
|---|---|---|---|
| per-blob blocks | object id, compact blocks | every view, replay and resolution | a blob seen for the first time |
| sketch LSH | sketch band, block ids | revive, merge, hunk matching (CHANGE_VIEWS.md section 2) | blocks retired, hunks fingerprinted |
| per-path checkpoints | (branch, path, revision), `BlockIndex` state for that path | anchor resolution, history | replay, every N revisions |
| block to open changes | block id, the open changes whose footprints contain it | overlap, conflict, drift, pending overlays | a change pushed (its footprint), merged or closed (removed), a trunk merge (drift) |
| block to items | block id, item keys anchored to it | "what is attached here", subscriptions by area | `item.put` with anchors |
| reverse links | item key, items linking to it | lineage, threads, "what implements this" | `item.link` |

Each index is a **cache**: rebuildable from the document source and the log, never the truth (BACKBONE.md section 4). Each lives in the cache
layer of [DECISIONS.md](DECISIONS.md) section 3, sharded like the store.

## 6. Budgets

First targets; S0 replaces them with measurements, and R-SCALE-2 holds them at 10^5, 10^7 and 10^9 lines.

| operation | budget | gated by |
|---|---|---|
| cold indexing of a code file | at least 1 million lines per second per core | timing: `index cold build 1e5 lines`; every tier: blocks found equal the generator's truth |
| classification of a path | under 10 us, without reading the file for most paths | not yet benchmarked (the classifier is S2's) |
| a compact block | under about 200 bytes | every tier: metric `index cold 1e5: live heap bytes per block`, an xfail until S1; its ceiling (total live heap bytes) holds meanwhile |
| a warm anchor resolution | under 10 ms | timing: `index warm resolve and at, x1000` |
| an incremental commit or merge | proportional to the paths it changed and the open changes it meets | timing: `index incremental update, 1e5 lines, per commit`; every tier: events, live blocks and allocations over a fixed 100-commit stream |
| a change's footprint | proportional to its diff; computed once per (base, head) | not yet benchmarked (P0) |
| opening a topic of 25 changes (warm) | under 1 s to the summary | not yet benchmarked (P1) |
| first view of a 440,000-file worktree (cold) | reads only the files the view shows, plus the classifier's cache | not yet benchmarked (S2) |
| resident memory | proportional to the paths touched, not the repository | every tier: metric `index cold 1e5: live heap bytes` (a ceiling) |
| disk per extra clone of the same repository | the per-clone state only: shared per-blob tables are not duplicated | not yet benchmarked (B1) |
| the interchange file of a turn (`layeredJson`) | proportional to what changed in the turn, not to the document | every tier: bytes written per turn against cumulative (below); timing: `layeredJson` rows |

Timing is measured locally only (the family rule) and judged against a per-host baseline by the family's sub0perf tooling; the deterministic metrics
(`perf/metrics.json`, the `Sub0Canvas_Bench_metrics` test) run in every tier. How: [TESTING.md](TESTING.md), "Performance".

### 6.0 Measured on the synthetic corpus (2026-11-23, S0 step 3 in part)

`Sub0Canvas_Bench` on the seeded profile A corpus ([bench/corpus.hpp](../bench/corpus.hpp), seed 1), library only, one machine (baseline `croglegion`: Windows 11,
MinGW-w64 GCC 16.2, Release, mains power, load 5-6%, 5 interleaved rounds, noise 0.2% to 4%). Not yet measured: 10^7 and 10^9 lines (the stream exists; the benchmark does not use it),
profile B, a real repository. Medians:

| what | size | result | against the budget |
|---|---|---|---|
| cold index build | 100,145 lines, 262 files, 4,800 blocks | 59 ms: 1.7 million lines per second | meets the 1 M lines per second guess |
| incremental update | a commit of a stream (1-4 edits, 2.6 files) | 1.46 ms per commit (0.56 ms per file touched) | about 2.5 times the cold cost per file: reconciliation and revival, not just parsing |
| warm resolve and `at` | 1,000 ids of the 1e5 index | 3.5 us each | far under 10 ms |
| `foldRegions` | 190 C++ files, 3,597 regions; 72 Python files, 1,203 regions | 6.7 ms and 1.5 ms: 11.7 and 5.1 times a hand-written scan of the same text | the brace heuristic is not the dominant cost of the cold build |
| revive scan | a file of 22 novel blocks against 486 and 4,800 retired blocks | 7.7 ms and 73.6 ms (the copy of the index is subtracted): linear, 0.7 us per (new block, retired block) | extrapolated, at 10^6 retired blocks one novel block costs about 0.7 s: the scan must go before history is indexed (S1) |
| `applyReply` | 1, 10, 50 single-line operations over five files (the canvas copy included) | 0.18, 0.50 and 0.65 ms | not a scale concern |
| `layeredJson` | 58 files, 988 blocks, 2e4 lines | 2.8 ms for one turn; eight turns 17.8 ms per-turn against 22.7 ms cumulative | |
| heap | the cold 1e5 index | 12.5 MB live: 2,613 bytes per block, 2,122 without the held texts; 85 allocations per block | **13 times the 200-byte target**: S1's compact layout is needed (the xfail) |

Bytes written for eight turns of the 2e4-line corpus (22 files changed over the turns): cumulative events with inline texts, which Sub0Squad writes today, 6.23 MB; per-turn
events with inline texts 5.49 MB; cumulative events with texts by content id 2.87 MB; per-turn events with texts by id 2.13 MB, **34% of today's**. Texts by id carry most of
the saving; per-turn events matter more as the event log grows. All are exact metrics, so a change to the format shows as a number that moved.

### 6.1 Measured: whole-bundle snapshots of profile B (2026-11-18)

The first measurement on real code: `sub0canvas snapshot` (app/) over subtrees of profile B's repository, on the owner's machine (Windows 11,
MinGW GCC 13, Release, mains power, one run each, so read the timings to about 20%). Scopes are named by size only. "Turns" is the number of
commits replayed; every turn is written in full (`layeredJson` repeats every file's text and the cumulative event log).

| scope (code files) | turns | snapshot time | snapshot peak memory | bundle | blocks | viewer model: parse + load (Node) | viewer heap |
|---|---|---|---|---|---|---|---|
| 5 | 1 / 8 | 1.1 s / 5.1 s | 6 / 8 MB | 0.1 / 0.7 MB | 80 | 3 / 8 ms | 5 / 7 MB |
| 230 | 1 / 8 | 1.3 s / 5.8 s | 37 / 97 MB | 5.5 / 43 MB | 3,526 | 41 / 351 ms | 35 / 235 MB |
| 1,590 | 1 / 8 | 2.0 s / 6.4 s | 136 / 524 MB | 27 / 213 MB | 20,280 | 195 ms / 1.6 s | 161 MB / 1.1 GB |
| 4,369 | 1 / 8 | 3.3 s / 12 s | 346 MB / 1.3 GB | 66 / 517 MB | | | |
| 19,162 | 1 | 10 s | 1.3 GB | 244 MB | | | |

The first run took 229 s for the 19,162-file scope: the commits layer looked each event's block up with `BlockIndex::resolve`, which scans every
live block, so the base turn was quadratic in blocks. The tool now builds one id map per turn (the numbers above are after the fix); `resolve`
itself stays linear until S1's compact index keys blocks by id.

In the browser, served from git by `tools/serve.mjs` (1 turn):

| scope | fetch (snapshot made on request) | first render | every later click (a full re-render) | DOM nodes | of which the block list |
|---|---|---|---|---|---|
| 230 files | 1.8 s | 0.37 s | 0.28-0.34 s | 11,444 | |
| 1,590 files | 4.0 s | 2.4 s | **1.9-2.1 s** | 65,397 | 60,238 (20,075 rows) |

**Re-measured by K0 (2026-11-24, [K0.md](K0.md); mains power, headless Edge 159, p95 of 20 after 2 warm-up).** A generated subtree of the same size (1,590 files, 20,034
blocks, 3 turns, 60,896 DOM nodes; shorter files than real code, so not the same page, and a different machine state) costs **0.38-0.46 s p50 per click** (p95 0.41-0.58 s), about
a fifth of the 1.9-2.1 s above, and a first view of 0.70 s (bundle fetched; no snapshot). Every click is still the same full re-render, so the shape of the finding holds and the
budgets of AUTHORING.md section 3.2 are breached by 2.3x to 11.6x on this scope (a filter keystroke matching every block: 223 ms against 30). The mirror (49 files) is inside every
budget, at the idle frame. Slow interactions are recorded, not fixed here: V1a fixes them (K0.md, "What V1a must fix").

**What the numbers say.**

1. **Making a snapshot is not the bottleneck.** Seconds even for 19,000 files, and git's own work (history for a pathspec, one `cat-file` batch
   per turn) is a small part of it. Its memory (1.3 GB) is the whole bundle assembled in one string beside the library's `BlockIndex`, which holds
   every text and token set: deltas (point 2) and S1's compact index (section 4) remove both.
2. **Whole bundles grow as files times turns.** Every turn repeats every file and the whole event log, so 8 turns of 1,590 files is 213 MB to
   move, parse and hold, of which a few megabytes are new. A turn should carry only what changed: per-turn events (`eventsFrom`, BACKBONE.md
   section 3.3, already proposed for B0) and files by content id, each text sent once.
3. **The page, not the data, breaks first.** At 20,000 blocks every click costs about 2 s, because each render rebuilds every pane, and 92% of
   the page is a block list nobody reads in full. Cost must follow what is on screen (R-AUTH-1), and a change must touch only what it changes.
4. **Live use needs patches, not reloads.** Today the only way to see new data is to reload the bundle, which resets the reader's place. With
   several people and agents writing, updates arrive continuously; each must reach the open page as a small event that updates only the parts
   it concerns, leaving the reader's scroll, selection, folds and unsent input alone (R-AUTH-12). This is a requirement of the multi-user and
   agentic use cases, not an optimisation.

The design that follows is [AUTHORING.md](AUTHORING.md) section 3.1; the slice is V1a (section 7 there).

### 6.2 Measured: the revision index (G1, 2026-11-24)

`RevisionIndex` (per-scope first-parent replay, checkpoints every 50 revisions) on the synthetic stream of S0's corpus: 200 commits over 2e4 lines (58 files, 507 changed paths), no git, one machine (baseline `croglegion`, Windows 11,
MinGW-w64 GCC 16.2, Release, interleaved rounds, noise 0.4% to 1.3%; the timing rows are new and not yet in the baseline file). Deterministic facts gate in every tier (`perf/metrics.json`, `history ...` keys).

| what | result | against the budget |
|---|---|---|
| a 200-commit replay including the baseline (a cold build of the 58 files) | 551 ms: 2.75 ms per commit | the brief's 30 s: met by 50 times |
| the same, cost per commit | **one `BlockIndex::update` per path changed**: 507 updates for 507 changed paths (493 texts read, the rest deletions) | R-SCALE-1: exact metric |
| a scope of five files over the same 200 commits | 66 commits replayed (those that touched it), 70 updates and 70 texts read, 12 ms: 0.18 ms per commit | R-SCALE-9: no revision outside the scope is replayed or read |
| a question about revision 125, with a checkpoint at 100 | 26 commits replayed (not 126), 69 ms, most of it copying the checkpoint's index | checkpoints bound a replay |

On real local repositories through git (`sub0canvas history`, Windows, one run each, so read to about 20%; the repositories are described by shape only): a small project (71 first-parent commits, a directory scope that 11 of
them touched) took 0.73 s over 9 git processes; a larger C++ project (291 first-parent commits, its source directory as the scope, 142 revisions changed it, 775 updates, 745 texts read in 3 batches) took 4.9 s over 47 git
processes, of which 40 were whole-diff queries asked because a file was added in the scope (the rename chase). About 30 revisions a second end to end: on Windows the process count, not the replay, is the cost
(about 0.1 s per git process), and the next gain is fewer processes (a long-lived `cat-file --batch`, a stopping revision, batching the added-file diffs), not a faster index. Not measured: memory (the index holds every
text and a copy per checkpoint until S1), a partial clone's cost, a repository of 10^5 commits (S3).

## 7. Proving at scale

| rung | data | what it proves |
|---|---|---|
| S0 | a deterministic synthetic corpus, streamed: profile A's line counts **and** profile B's mix (non-code files, sidecars, binaries, wide branches, change sets with known lineage) | budgets and identity truth at 10^5, 10^7, 10^9 lines |
| S3 | large public repositories cloned locally | the budgets and G1's identity checks on real code |
| S3-private | profile B's repository, on the owner's machine | the budgets on a work-dominated monorepo, the per-user shared cache, partial clones, merge load. **Only numbers are published** (CHANGE_VIEWS.md section 7) |

## 8. Risks

* **Sketch similarity near the thresholds** can miss or invent a match. S2 measures recall and precision against the exhaustive rule on S0's
  corpus, and the exact rule always confirms a candidate.
* **Heuristic folding on unusual code** gives wrong blocks at every commit. tree-sitter for the languages that matter, chosen by measurement on
  S3's repositories.
* **Caches shared across clones** must never mix repositories. Keys include the repository's identity (its root commit), and a cache entry is
  keyed by object id, so a wrong entry cannot be served for different content.
* **Interned and cached state** must survive restarts: it is a cache, rebuilt from the document source, never the truth.
* **A private profile can leak through its numbers** if they are specific enough to identify the source. Publish rounded counts and ratios, never
  paths, names or anything a search would match.

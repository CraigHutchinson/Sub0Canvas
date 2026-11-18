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

| operation | budget |
|---|---|
| cold indexing of a code file | at least 1 million lines per second per core |
| classification of a path | under 10 us, without reading the file for most paths |
| a compact block | under about 200 bytes |
| a warm anchor resolution | under 10 ms |
| an incremental commit or merge | proportional to the paths it changed and the open changes it meets |
| a change's footprint | proportional to its diff; computed once per (base, head) |
| opening a topic of 25 changes (warm) | under 1 s to the summary |
| first view of a 440,000-file worktree (cold) | reads only the files the view shows, plus the classifier's cache |
| resident memory | proportional to the paths touched, not the repository |
| disk per extra clone of the same repository | the per-clone state only: shared per-blob tables are not duplicated |

Performance is measured locally, never in CI (the family rule): benchmarks carry the ctest label `timing` and skip on battery.

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

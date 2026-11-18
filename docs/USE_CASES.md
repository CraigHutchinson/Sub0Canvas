# Use cases: who sub0canvas is for, and what each one demands

Each use case is a consumer the design must serve **without a mechanism of its own** (if one needs a new mechanism, the design is incomplete, see
[BACKBONE.md](BACKBONE.md) section 9). Each has a proving ground: something that can be run and that fails when the design is wrong
([TESTING.md](TESTING.md)).

| # | use case | scale | built today | proving ground |
|---|---|---|---|---|
| U1 | **An agent's working document** (Sub0Squad's canvas worker) | one task: a few files, hundreds of lines, tens of turns | yes (canvas, folding, block ids, issues layer) | unit tests; Sub0Squad's `test_blocks`, `test_canvas_agent` and tier 0 A/B ([INTEGRATION.md](INTEGRATION.md)) |
| U2 | **The squad's backbone**: planner, scheduler, workers, reviewers and owners meeting in layers | one workflow: tens of files, many agents, days | design (BACKBONE.md B1-B3) | the same Sub0Squad checks, plus a recorded workflow replayed through the store |
| U3 | **A GitHub project view**: history, PRs, review comments, issues, checks around the code | one repository, its whole history and open PRs | design (BACKBONE.md G1-G5) | this repository's and Sub0Squad's own history and PRs, hand-checked truth |
| U4 | **Very large codebases**: monorepos up to **billions of lines** | 10^9-10^10 lines, 10^7 files, 10^7 commits | no | a synthetic corpus at scale, then large public repositories cloned locally (section U4 below) |
| U5 | **A text editor over layers** | the files open, the repository behind them | no | an LSP-like process serving one editor (BACKBONE.md E1) |
| U6 | **A collaborative editor with review, comments and explainers** | a team on one document set | no | two people and an agent reviewing one file (BACKBONE.md C1) |
| U7 | **A viewer** of any of the above | whatever the producer wrote | yes (`index.html`, `src/`) | `npm test`, the demo and golden fixtures |

## U1. An agent's working document (built)

A small model (8k to 16k tokens per slot) edits a few files over tens of turns. It needs: the current text in a view that fits (folding), edits that
cannot go stale or half-apply (transactions), a verdict it can act on in one turn (the issues layer, rendered only where it adds information), and
identities that survive its rewrites (block ids). Everything in [CANVAS.md](CANVAS.md) and [LAYERED_DOCUMENT.md](LAYERED_DOCUMENT.md) serves this,
and it is measured in Sub0Squad (defects per turn, collateral lines, tokens).

## U2. The squad's backbone (design)

The canvas becomes the place where Sub0Squad's roles meet: the planner writes a plan layer and tags items `ready-for-implementation`, a scheduler's
subscription picks them up, a worker claims a block, the gate's issues land on it, a reviewer's notes follow it through edits, and an owner's inbox
hears about failures in their area. BACKBONE.md section 8 maps today's squad pieces onto layers and adapters, and which stay in the squad.

## U3. A GitHub project view (design)

Point the canvas at a repository and see the project around the code: each block's history across commits, open PRs as pending overlays (two PRs on
one block are a conflict before either merges), review comments in the margin and marked outdated when the code moves on, issues and project items
anchored by permalinks and names, CI checks and SARIF beside the local gate. **Dogfooding:** the first data set is this repository's own PRs and
Sub0Squad's backbone-integration PRs ([INTEGRATION.md](INTEGRATION.md)), so every integration iteration also produces test data for G2.

## U4. Very large codebases (design target)

**The target.** The design must hold for a monorepo of billions of lines: Google described its monorepo in 2015 as about 2 billion lines in about
9 million source files with about 35 million commits; single large public projects (the Linux kernel, Chromium) are tens of millions of lines each.
At that scale nothing may be proportional to the repository: only to **what is touched** (the paths a view, an anchor, a subscription or a commit
concerns).

**What it demands of the design** (BACKBONE.md section 12 holds the design; these are the requirements it answers):

| requirement | why | today's code |
|---|---|---|
| **Lazy, per-path indexing.** A path is indexed when first needed, never the whole tree. | 10^7 files cannot be read for one view | `BlockIndex` indexes what it is fed: fine, the consumer decides |
| **Content-addressed reuse.** Blocks of a file are computed once per blob (its git hash) and shared across revisions and branches. | most files are identical across most revisions | not built: `update` re-scans text, keyed by path |
| **No full texts held.** The index keeps hashes, spans and compact sketches, and fetches text from the document source on demand. | 10^9 lines of text do not fit in memory | `BlockIndex::texts_` keeps every path's text; `Block` keeps two `std::set<std::string>` per block |
| **Sub-linear candidate search.** Revive and merge look for similar blocks among retired ones with a sketch index (MinHash / locality-sensitive hashing), not a scan. | a scan over every retired block is quadratic across history | `retired_` is scanned in full on every unmatched block |
| **Per-path history.** Replay follows one path's revisions (and git's rename detection), with checkpoints, not every commit of the tree. | replaying 10^7 commits for one anchor is impossible | `RevisionIndex` is designed per tree; section 12 makes it per path |
| **Sharded store and log.** Items and events are partitioned (by path prefix), and queries touch only the shards in their area. | one log of a monorepo's events is too large to replay | the store is not built yet (B1): decide before building |
| **Bounded output.** Snapshots and views are scoped (a query, a set of paths), never "everything". | `layeredJson` writes every file and block | fine for U1; scoped writer needed for U3/U4 |
| **Parsing that scales and is right.** O(n) per file, many languages. | the brace/indent heuristic is fast but language-blind | `foldRegions` heuristic; tree-sitter is the upgrade |

**Budgets to measure against** (first guesses, to be replaced by S0's measurements): indexing at least 1 million lines per second per core on a cold
file; an incremental commit update proportional to the files it changed; a warm anchor resolution under 10 ms; a compact block under about 200 bytes;
memory proportional to the paths touched, not the repository.

## U5. A text editor over layers (design)

An editor shows layers in the margin and lets the user centre on prose. The process owns the store; the editor sends changes and asks for views
(decorations, code lenses such as "3 issues, 1 PR", hovers). Needs nothing beyond documents, the index, items and views.

## U6. A collaborative editor with review, comments and explainers (design)

Comments and explainers are items in `review` and `prose` layers anchored to blocks, so they move with the code; a review is items with state; a
review request is a tag plus a subscription; presence is claims. Concurrent typing is not the backbone's job: either edits are serialised as
transactions (agents, block-sized edits) or the editor runs its own CRDT and feeds settled revisions.

## U7. A viewer (built)

The static viewer reads `layered-doc-1` turn files and the canvas log, centres on any layer, drills between prose and code, shows a block's identity
history and steps through 1-N anchors. It understands optional fields the producer does not write yet (README, "Optional fields"); BACKBONE.md B0
closes that gap.

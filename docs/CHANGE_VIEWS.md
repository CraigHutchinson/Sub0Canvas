# Change views: pull requests, spikes and their dependencies as layers

Status: **design, for the owner to read** (2026-11-18). Nothing here is built. Use case U12 ([USE_CASES.md](USE_CASES.md)); requirements R-CHG
([REQUIREMENTS.md](REQUIREMENTS.md)); scale in [SCALE.md](SCALE.md). It extends the GitHub project view (U3, [BACKBONE.md](BACKBONE.md) section 9.3),
which treats a pull request as one item with a pending revision line, to **sets of changes that serve one goal**: an exploratory spike, the focused
changes carved out of it, the stacks they form, what supersedes what, and what still has to land.

## 1. The problem, from a reference topic

The design is checked against a **reference topic** held privately by the owner: one bug fix in a large private monorepo, worked over seven weeks
as 25 pull requests. Only its shape is recorded here (measured 2026-11-18); its names, paths and links stay out of this repository.

| shape | count | what a reader needs to see |
|---|---|---|
| exploratory spikes marked never-to-merge (one umbrella of 65 files and 25 commits; a reproducer-only spike of 1 file; a spike filed under a second tracker topic) | 4 | which of the spike's hunks have landed, are in flight, are not yet carved out, or were dropped |
| focused changes carved out of the umbrella spike (1 to 46 files each) | 17 | where each came from; which area it covers; what it depends on |
| merged (the unrelated refactor below included) | 13 | what is done, on which branch (one merged into a feature branch, not trunk) |
| still open (4 of them spikes or the integration change) | 8 | what is left, and in what order |
| closed without merging: 3 carve-outs superseded by later ones, 1 reproducer spike | 4 | what replaced them; nothing of theirs is lost without trace |
| an integration change collecting the remaining fixes (17 files, 15 shared with one open carve-out) | 1 | which carve-outs it duplicates; when it can be retired |
| a stack three deep (each based on the previous change's branch) | 3 | the merge order; what a change below needs from the one above |
| test-only changes (a performance test, a test-setup fix) | 2 | the evidence a topic carries, not only its code |
| a large unrelated refactor sharing 4 files with the spike | 1 | that sharing files is not sharing work |
| the topic as a whole | 274 distinct files; 23 files touched by four or more changes | the hotspots; where the changes of the topic meet |

The topic also has a lineage outside the forge: it began as another contributor's exploratory pull request with an investigation write-up, and the
tracker links it to a port ticket for a release branch. A change view must hold all of it: tracker topic, spikes, carve-outs, stacks, ports.

**What a forge shows today**: a flat list of 25 pull requests and, per pull request, a file list. Two facts make that list hard to read, and the
design answers both. File overlap is noise: 21 of the other 24 changes share files with the umbrella spike, and the unrelated refactor shares more
files with it (4) than three real carve-outs do (1 or 2). Lineage is implicit: carved-from, supersedes, integrates and depends-on are in
the author's head and in titles, never in data.

## 2. The model: no new mechanism

A change view needs nothing beyond [BACKBONE.md](BACKBONE.md) section 3. It is items, links, anchors and views, with three derived facts.

| concept | in the backbone | notes |
|---|---|---|
| **change**: a pull request, a pushed branch, a local branch or stack, a patch | an item in a `changes` layer (kind `change`), with a pending revision line from its base (BACKBONE.md section 6, branches) | a change does not need a forge: local branches and worktrees are changes too |
| **topic**: the goal several changes serve | an item in a `topics` layer; changes link to it `partOf` | found by a **topic policy** (data, BACKBONE.md 14.4): a tracker key in a title or branch, a label, a forge milestone, or the author's grouping by hand |
| **footprint**: what a change touches | the change's anchors: one per block its diff modifies, resolved at the merge base and carried to the viewed revision (B4's resolver) | blocks, not files: the answer to the noise of section 1 |
| **hunk**: a unit of a change's diff | a sub-item (`partOf` the change) anchored to one block, with a **hunk fingerprint** in `data` (A4) | the fingerprint is git's stable patch id over the hunk with whitespace normalised, plus the block's MinHash sketch of the added text, so a rewritten carve-out still matches its spike hunk ([SCALE.md](SCALE.md) section 4) |
| **spike**: a change that is never merged | a change with the tag `spike`, set by the topic policy (a title marker, a label, a draft flag held long) | tags, not a kind: a spike becomes a normal change when its author says so |
| lineage | typed links (A5: open strings), below | derived or stated; a derived link carries its evidence and a confidence |

**Lineage links** (conventions registered under A5; the core treats none specially):

| link | from, to | derived from | confidence |
|---|---|---|---|
| `stackedOn` | a change, the change whose head is its base | the base branch is another change's head branch | exact |
| `carvedFrom` | a focused change, a spike | its hunks match a subset of the spike's hunks (fingerprints), in the same blocks | scored: share of the change's hunks found in the spike |
| `supersedes` | a later change, a closed one | the closed change's hunks are covered by the later change's, by fingerprint or by block | scored |
| `integrates` | an aggregating change, the changes it contains | it holds hunks of open changes whose fingerprints match | scored |
| `duplicates` | two open changes | they modify the same blocks with matching fingerprints | scored |
| `dependsOn` | a change, a change it needs | **semantic**: it references a name (a block, a function, a type) that the other change introduces. This is a name anchor (BACKBONE.md 3.2) resolved against the other change's head instead of trunk | exact when the name resolves to a block born in the other change; otherwise suspect |
| `ports` | a change on a release branch, its trunk original | matching fingerprints across branches; the tracker's port link | scored, or exact from the tracker |
| `tests` | a test-only change, the changes or blocks it exercises | its anchors (tests named after blocks), or stated by the author | stated, or suspect |
| `discusses` | a write-up, a tracker issue, the topic | the tracker link, a document linked from the description | exact |

Derived links are proposals of a **lineage policy** (`lineage@1`): thresholds, which links to derive, how spike coverage is scored (what counts as a spike is the topic policy's `spike` tag, which `lineage@1` only reads). Each derived link
records the policy version that made it (A13). An author's statement overrides a derivation, and both are kept in the log.

## 3. The derived facts

**Landing coverage of a spike.** Each hunk of a spike is in one of five states, from the fingerprints of every change of the topic and of the
target branch:

| state | meaning |
|---|---|
| landed | an equivalent hunk is on the target branch (through a merged carve-out, or a merge under another topic) |
| in flight | an equivalent hunk is in an open change |
| not carved | no other change holds it, and it still applies to the target branch's block |
| obsolete | its block was rewritten or retired on the target branch since the spike's base, so the hunk no longer applies as written |
| dropped | only a closed change held it |

The spike's summary is then one line, for example "41 hunks: 22 landed, 9 in flight, 6 not carved, 3 obsolete, 1 dropped", with drill-down per area. This
counts down a spike's remaining work, and it is the question an author asks of a seven-week topic each morning.

**Overlap and conflict at block level.** An inverted index from block to open changes (per shard, [SCALE.md](SCALE.md) section 5) answers which
changes touch a block in one lookup per footprint block. Two open changes that modify one block are an **overlap**. They are a **conflict** when
their hunks there differ (by fingerprint), and **duplicates** when the hunks match. File-level sharing is shown only as a count, never as a
relation.

**Readiness and merge order.** A change is ready to merge when everything it `stackedOn` or `dependsOn` has merged. The topic's dependency
graph gives a merge order, and a cycle is reported as a finding. Drift is counted per change: the blocks of its footprint that the target
branch has modified since its base. This estimates the rebase cost before the rebase starts.

## 4. The views

Every view follows the authoring tool's rule: **bounded by attention, summary first, drill-down on request** (R-AUTH-1, [AUTHORING.md](AUTHORING.md)
section 2). A topic of 25 changes and 274 files opens as one screen.

| view | centre | shows | drill-down |
|---|---|---|---|
| **topic summary** | a topic | the counts (changes by state, spikes, hotspots), the spike coverage line, what needs the reader: changes ready to merge, conflicts, drift, review comments unanswered | any count opens its list |
| **topic map** | a topic | changes as lanes, areas as columns (directories, folded to the depth the budget allows, then blocks); a cell marks a change touching that area; stacks drawn as nested lanes; spikes as background layers that can be toggled | a cell opens the area's blocks across the topic's changes |
| **lineage graph** | a topic | changes as nodes, lineage links as edges, merged ones faded; the merge order | a node opens its change |
| **change** | a change | its footprint by area, its hunks with their state against the spike, what it depends on and what depends on it, its checks and review threads (U3, U8) | a hunk opens its block |
| **block across changes** | a block | every pending variant of the block side by side (the trunk version, each open change's version, the spike's version), each labelled by change and state; the block's history below (G1) | a variant opens its change |
| **spike coverage** | a spike | its hunks grouped by area and coloured by the five states; "not carved" grouped into proposed carve-outs (section 6) | a hunk opens the block across changes |
| **pending overlay** | code at the target branch's head | the code with selected changes overlaid as layers (the viewer's layer-centred UI, with each change one layer): a margin mark per block naming the changes that modify it | a mark opens the block across changes |

A change view is a view over the same store as every other: a topic's review comments, a block's issues, a spike's design note and a carve-out's
checks are one query away, not a separate tool.

## 5. Sources

| source | capability | gives | notes |
|---|---|---|---|
| local git (G1's adapter) | document, items | branches, worktrees and local stacks as changes; their diffs and merge bases; patch ids | works with no forge and no network; the first source built |
| a forge (G2's adapter, generalised) | items, events, write | pull requests with base, head, state, draft, labels, title; review threads; check runs | the forge's **host is configuration**: a public forge and a company's own forge instance are the same adapter with different hosts and credentials (BACKBONE.md section 5, rule 5) |
| a tracker | items | topics (an issue and its key), their links (implements, ports, duplicates), status | a separate adapter per tracker; the topic policy maps its keys into titles and branches |

Private data stays private: interchange files and caches of a private repository are private (DIRECTION.md section 2, rule 5), they live under
the git directory or the user's state directory ([DECISIONS.md](DECISIONS.md) section 3), and nothing from them is written into a public
repository. A change view of a private monorepo runs entirely on the owner's machine.

## 6. Agents in a change view

The mechanisms of the backbone give agents a place in this work with nothing new:

* **A topic subscription** (BACKBONE.md 3.4) on an author's topic delivers what changed since they last looked. A carve-out merged, so these spike
  hunks are now landed. A trunk commit modified a block in an open change's footprint, which is drift. A review comment arrived on a change. Two
  open changes now conflict on a block.
* **Carve-out proposals.** An agent reads a spike's "not carved" hunks and proposes a carve-out: a plan item (`plan` layer) grouping hunks by area
  and by `dependsOn`, ordered so that each proposed change builds and tests alone. The author accepts, edits or rejects it. A proposal creates no
  branch and opens no pull request without confirmation (R-SRC-7).
* **Retirement checks.** When a spike's coverage reaches "nothing not carved", or an integration change's hunks are all landed, an inbox item says
  it can be closed. The agent proposes the closure; it does not close.
* **Port tracking.** For a topic with port tickets, the view shows which landed hunks have a `ports` twin on each release branch, so a port is a
  checklist rather than a memory exercise.

## 7. Proving grounds

| ground | public | what it proves |
|---|---|---|
| synthetic change sets from S0's corpus generator: a spike, carve-outs (some rewritten), supersessions, a stack, an integration change, a duplicate, an unrelated large change sharing files, a port onto a branch, with the true lineage and hunk states recorded | yes | every derivation of sections 2 and 3 against known truth, at small and at large scale |
| **the synthetic mirror** ([MIRROR.md](MIRROR.md), built): a seeded, invented project with the reference topic's anonymised shape, its forge and tracker state captured as files, and the truth | yes | P0-P2 and P4 on realistic shape with known truth, small enough to run in `npm test` and `ctest` |
| this repository and Sub0Squad: their branches and pull requests | yes | the local git and forge adapters on real, small data (G2) |
| **the owner's reference topic** (section 1), captured to a local folder outside every public repository | **no** | lineage, coverage and overlap against the owner's hand-checked truth on real work at scale. **Only numbers are published** (counts, precision and recall, timings); no names, titles, paths or links |

The private ground follows a rule that holds for every private proving ground: the capture, the truth and the run stay on the owner's machine, and
what reaches this repository is an anonymised measurement that cannot identify its source. The local reference guard (AGENTS.md) backs the rule
mechanically.

## 8. Slices

| slice | what is built | check | needs |
|---|---|---|---|
| **P0** changes from local git | `changes` layer items from local branches and worktrees; footprints at block level; hunk fingerprints; `stackedOn` | on S0's synthetic change sets: every footprint and stack exact; fingerprints match rewritten carve-outs at the stated recall | G1, B4 |
| **P1** overlap and conflict | the block-to-open-changes index; overlap, conflict, duplicates; drift | synthetic truth exact; on the reference topic, block-level relations against the owner's judgement, with the file-level baseline's false relations counted | P0, S2 (sketches) |
| **P2** lineage | `carvedFrom`, `supersedes`, `integrates`, `dependsOn`, `ports`, `tests` under `lineage@1`; spike coverage's five states | synthetic truth; the reference topic's lineage against the owner's hand-checked truth: precision and recall per link kind published as numbers | P1 |
| **P3** the views | topic summary, map, lineage graph, change, block across changes, coverage, overlay in the viewer, within budgets | scripted scenarios (AUTHORING.md section 6); the reference topic opens in one screen with every count right | P2, V2 |
| **P4** forge and tracker sources | the forge adapter for changes, with a configurable host; a tracker adapter for topics and port links; captured fixtures in tests | the reference topic assembled from its forge and tracker by the topic policy alone, matching the hand-made grouping | G2 |
| **P5** agents | topic subscriptions; carve-out proposals; retirement and port checks | a recorded topic replayed: the inbox at each step lists exactly the expected items; a proposed carve-out builds alone on a synthetic set | P3, V2 |

P0 to P2 are library work proved without a forge. P3 lands in the authoring tool. P4 widens the sources, and P5 puts agents on top.

## 9. Open decisions

Four decisions here are kept open by design: hunk equivalence, what marks a spike, topic grouping, and areas in the topic map. Their arms, defaults
and the evidence that settles each are rows of the one register, [BACKBONE.md](BACKBONE.md#143-open-decisions-kept-open-by-design) section 14.3;
the reasoning is in sections 2 (the topic and lineage policies), 3 and 4 above.

## 10. Gaps the mirror exposes

The synthetic mirror ([MIRROR.md](MIRROR.md)) contains edge cases on purpose. Those this design does not yet cover are open design work, each
checkable on the fixture once designed (`truth.edgeCases`, `today: gap`):

| gap | what the design must add |
|---|---|
| revert and reland | a `reverted` state: a holder that merged and was reverted no longer lands a hunk; a reland is a `relands` link |
| a force-pushed rebase under a stack | a change's history is a sequence of heads, not a line; a change based on a superseded head is a **stale stack**, reported |
| an evil merge | content in a merge commit from neither parent is attributed to the merge itself, and shown, not lost |
| an assets-only change | a footprint includes files without blocks (binaries, sidecars), so R-CHG-1 widens from blocks to blocks and files |
| a block inside `#if` | whether the guard is part of the block's span (BLOCK_DISCOVERY.md) |
| CRLF, a BOM, tabs, non-ASCII | anchor line and column units (bytes, code points, display columns) stated once for every source |
| a case-only rename | path identity on case-insensitive file systems |
| wholesale regeneration of a generated file | identity when every block changes at once (generated files may be classified out of identity entirely: `kinds@1`) |


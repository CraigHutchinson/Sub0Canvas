# The plan: from here to the authoring tool, history and scale

Status: **proposed** (2026-11-18), for the owner to confirm. It consolidates the day's parallel work (the design of the later use cases, the
proving grounds moved from Sub0Squad, [DECISIONS.md](DECISIONS.md), [SCALE.md](SCALE.md) with its measured profile, [CHANGE_VIEWS.md](CHANGE_VIEWS.md),
the synthetic mirror, the `sub0canvas snapshot` app and the viewer's interactivity budgets) into one order of work. It holds the **shape**: phases,
gates, dependencies and why. [BACKLOG.md](BACKLOG.md) is the ordered list of items that follows it, and the slices themselves stay in their design
documents ([BACKBONE.md](BACKBONE.md) section 10, [AUTHORING.md](AUTHORING.md) section 7, [BLOCK_DISCOVERY.md](BLOCK_DISCOVERY.md) section 5,
[CHANGE_VIEWS.md](CHANGE_VIEWS.md) section 8, [SCALE.md](SCALE.md) section 7).

## 1. Where it stands, in one paragraph

Released: v0.2.1, which Sub0Squad pins (canvas, folded views, tolerant edits, block identity, the issues layer, the viewer). Built since, not
released: the requirements catalogue with tests through a driver, the golden replay and the canvas model check, `applyReply` (atomic) and turn
control with the 30 recorded-reply cases, the synthetic mirror, and `sub0canvas snapshot` (a local git repository's subtree into a viewer bundle,
served from git). Measured: snapshots are cheap; whole bundles grow as files times turns; **the page breaks first** (about 2 s per click at 20,000
blocks, SCALE.md 6.1). Designed: the backbone (state, events, inboxes, claims, adapters, history), the authoring tool, language-aware block
discovery, scale with two profiles, change views, and the decisions on storage, identity and the Sub0 libraries. Nothing of the design has been
confirmed by the owner yet; section 3 gathers what needs an answer.

## 2. The review: what disagreed, and how it now reads

Each of these was found by reading the day's documents against each other; each is fixed in the documents named, in the same commit as this plan.

| found | resolution | where |
|---|---|---|
| The authoring server's diagram owned an `events.ndjson` in the worktree and called the review log's location an open question, while DECISIONS.md section 3 recommends git refs per writer | the server owns the store and its log through the `Log` interface; V1 writes the per-writer segment format locally, V2 adds sync through git refs | AUTHORING.md sections 3, 4 and 7 |
| A2 (ids unique per replay root) and A16 (deterministic birth ids) both stood as amendments | A16 refines A2: no global counter (A2's point) by construction; A2's per-root naming is dropped | BACKBONE.md 14.2 |
| Section 11's questions 3 and 7 still proposed the per-root scheme; the register consolidation asked whether section 11 should fold into the register | section 11 becomes a status table: each question, its recommendation and where it now lives (register, amendment, done) | BACKBONE.md section 11 |
| "What counts as a spike" was given to both the topic policy and `lineage@1` | the topic policy sets the `spike` tag (an input); `lineage@1` only reads it, for spike coverage | CHANGE_VIEWS.md section 2 |
| B0 (format) and V1a (live views) both change `layered-doc-1`: per-turn events, revision strings, texts by content id | **one format revision**: B0 writes all of them, additively, so Sub0Squad's output, the bundle form and the live server's answers share one shape | B0's brief; this plan, phase 1 |
| The file tree on demand, file content on demand and the block list redesign were three backlog items beside V1a, which needs all three | they are V1a's steps; the block list's shape is a register row, weighed with K0's numbers | BACKLOG.md; BACKBONE.md 14.3 |
| Committed configuration (`.sub0canvas/config.json`, `commands.json`) was not placed in DECISIONS.md's layering | configuration is part of the project, committed like `.editorconfig`; annotations are not | DECISIONS.md section 3 |
| V0's brief described building the executable, which now exists (`app/`) | V0's brief is narrowed to what is left: Markdown blocks and rendering, the worktree's uncommitted state, merge titles | sessions/v0-worktree-snapshot.md |
| RESUME said section 11 held five questions and repeated "not yet read" three times | one "waiting on the owner" pointer to section 3 here | RESUME.md |
| Unreleased work is piling up (the moved proving grounds, the app, the tests) and Sub0Squad still carries copies | a release is the first step of phase 1 (it needs the owner's go-ahead to push a tag) | section 4, phase 1 |

## 3. What the owner is asked to confirm

One sheet, so the answers can be given in one sitting. Each line has a recommendation; "yes" adopts it as the default in the register (BACKBONE.md
14.3), which keeps every arm open to measurement. The reasoning is in the linked section.

| # | decision | recommendation | reasoning | blocks |
|---|---|---|---|---|
| O1 | Release v0.3.0 (push the tag; the full CI tier runs) | yes, at the start of phase 1 | unreleased work and Sub0Squad's copies | the Sub0Squad PR |
| O2 | Where data lives | layered by kind: shared annotations in git refs per writer (private ones never pushed), caches per user keyed by object id, per-reader state per user, exports on demand, configuration committed | DECISIONS.md 3; A17, A19 | V1, V2 |
| O3 | Events carry a writer and a Lamport clock; one writer per segment; the log a module of its own | yes | DECISIONS.md 3, 9; A15, A18 | B1, V1 |
| O4 | Block ids | deterministic birth ids | DECISIONS.md 4; A16 | S1 (or B0 if cheap) |
| O5 | Events in a snapshot; revision ids | per turn with `eventsFrom`; opaque strings, numbers still read | DECISIONS.md 5; A1 | B0 |
| O6 | The authoring server | C++ `serve` over stdio, a small Node bridge (today's `serve.mjs` grown) | DECISIONS.md 7; AUTHORING.md 3 | V1a |
| O7 | GitHub | read-only through the user's `gh`, a separate target Sub0Squad never links; tests on captured files; write-back only on confirmation | DECISIONS.md 5 | G2-G5, P4 |
| O8 | The C++ standard | stay on C++23 | DECISIONS.md 5 | - |
| O9 | tree-sitter | an optional target in principle; each grammar's download approved on its own, adopted per language by measurement | DECISIONS.md 6 | D3 |
| O10 | The store in Sub0Squad | per attempt first (behind an option), in the per-project format | DECISIONS.md 5 | B2 in the squad |
| O11 | Sub0Fabric | kept open: built here behind a liftable boundary, extracted when a second consumer needs it | DECISIONS.md 9 | - |
| O12 | The second scale profile and change views as designed | confirm R-SCALE-5 to 9, A19-A21, U12 and R-CHG as requirements | SCALE.md; CHANGE_VIEWS.md | S0's profile B mix; P0 |
| O13 | Spikes | the topic policy marks them; `lineage@1` reads the mark | CHANGE_VIEWS.md 2 | P2 |

**Not blocking**: phase 1 below needs none of O2-O13. It is groundwork every arm needs, so it starts now and the answers shape phase 2.

## 4. The phases

```
 phase 1  groundwork (parallel, now)        phase 2  the authoring tool             phase 3  history and scale            phase 4  widening
 ─────────────────────────────────────      ───────────────────────────────         ───────────────────────────           ──────────────────
 release v0.3.0 ─▶ Sub0Squad adopts          V0 (finish) ─▶ V1a live views ─▶        G1 git adapter (after D0)             V3-V5
 D0 block discovery seam ───────────────────▶ V0 Markdown ┐    V1 comments ─▶        S1 compact index (after S0) ─▶ S2     G2-G5, P3-P5
 S0 scale harness ──────────────────────────────────────────────────────────────────▶ S1                                  D2, D3, E1, C1
 B0 one format revision ────────────────────▶ V1a deltas  │    V2 inbox + git sync   P0 ─▶ P1 ─▶ P2                         S3, S4
 K0 interactivity benchmark ────────────────▶ V1a         │    = acceptance round    B3, B4
 change-view gaps (design only)                           │
```

### Phase 1: groundwork, in parallel, now

Each item is independent of the others and of the owner's answers. Each has a brief or will have one.

| item | why now | done means |
|---|---|---|
| **Release v0.3.0** (O1), then **the Sub0Squad PR** adopting `applyReply`, turn control and the moved checks, dropping its copies | the squad stops carrying duplicate code; the next format change rides on a clean pin | tag green on the full tier; Sub0Squad's suite green on the new pin; its copies gone |
| **D0** the block discovery seam ([brief](sessions/d0-block-discovery-seam.md)) | everything later replays files through block discovery: S1, G1, V0's Markdown | byte-identical on every test and the golden replay; the measured faults fixed, each with a test |
| **S0** the scale harness ([brief](sessions/s0-scale-harness.md)), profile A's sizes and profile B's mix | S1's data structures and A16's cost are chosen by numbers | the budgets of SCALE.md 6 measured at 10^5 and 10^7 lines, streamed to 10^9 |
| **B0** one format revision ([brief](sessions/b0-format-catch-up.md)): item `id`, `column`, ranges (A3), `layerMeta`, policies (A13), revision ids as strings (A1), per-turn events with `eventsFrom`, texts by content id | the viewer, Sub0Squad and V1a's server all need the same shape; doing it once avoids two migrations | golden files byte-compared on both sides; a real Sub0Squad attempt traced by `id`; an 8-turn bundle of the 1,590-file subtree a few times the size of one turn, not eight |
| **K0** the interactivity benchmark ([brief](sessions/k0-ui-kpis.md)) | V1a's work is steered by numbers, not impressions | every interaction in AUTHORING.md 3.2 timed, 95th percentile of 20, recorded with the machine |
| **The change-view gaps** (CHANGE_VIEWS.md section 10), design only | P0 must not be built on a model that cannot say "reverted" or "stale stack" | each gap has a design line and a `truth.edgeCases` check named on the mirror |

### Phase 2: the authoring tool, proved on this repository

The first use case to invest in (DIRECTION.md section 4). Sequential, because each slice is used before the next is built.

1. **V0, finished**: Markdown blocks through D0's provider seam, Markdown rendered in the viewer, the worktree's uncommitted state, merge commits
   titled by the change they merge. Check: `sub0canvas snapshot . docs` opens with every heading a block.
2. **V1a, live views** (AUTHORING.md 3.1): the `serve` process over stdio answering scoped queries; the bridge; texts by content id (B0); the file
   tree and file content on demand; the block list in its chosen shape (register row, K0's numbers); per-pane keyed patches and windowed lists.
   Check: the budgets of AUTHORING.md 3.2 on the 1,590-file subtree, and 100 scripted events applied under a reader without moving them.
3. **V1, comments**: the store's core (B1: `item.put`, `item.state`, `item.link`), the log as its own module (A18) with writers and Lamport clocks
   (A15), written locally in the per-writer segment format; comment, reply and resolve from the page. Check: a thread survives a restart.
4. **V2, the agent's inbox and sync**: B2's inbox and `acknowledge` over the review layer, the CLI, views with budgets (A14); the git-ref log backend
   (A17) so a review made on one device reaches an agent on another. **Acceptance: a real review round on this repository's docs** (the owner
   comments, the agent actions the inbox), recorded as a scenario.

The tools use **Sub0Platform's C++ layer** for files, locks and processes once it has moved out of Sub0Squad (DECISIONS.md section 8); until then
`app/` uses the standard library and a small process helper, kept behind one header so the switch is local.

### Phase 3: history and scale

Library work, proved without a forge, in this order of dependency:

* **G1**, the git adapter and per-path history (after D0; A20, A21): five functions' identity history against hand-checked truth.
* **S1**, the compact, lazy index (after S0's numbers): blocks keyed by id and by blob, birth ids (A16), no texts held.
* **S2**, sketches and per-path checkpoints; **P0** changes from local git (after G1 and B4); **P1**, **P2** overlap and lineage (with S2).
* **B3** claims and notes, **B4** source anchors and the resolver (V3 pulls B4's quote re-anchoring forward).

### Phase 4: widening

V3-V5 (edits, checks and push, a second actor); G2-G5 and P3-P5 (GitHub, change views in the tool, agents on topics); D2 data formats and D3
tree-sitter (by O9 and measurement); E1 the editor over the same `serve`; C1 collaborative review; S3 and S4 at real and synthetic scale; M1 the
live forge sandbox (parked). The order inside phase 4 is set when phase 2's acceptance round is done, by what the owner's use showed.

## 5. Across repositories

| repository | what | when |
|---|---|---|
| Sub0Squad | adopt `applyReply`, turn control and the moved checks; drop its copies; later B0's fields and the per-attempt store (O10) | after v0.3.0; after B0 |
| Sub0Squad | measure the issues layer where it acts (off against adaptive on clipped cases) | any time; no library change |
| Sub0Platform | finish moving the C++ layer out of Sub0Squad; Sub0Canvas's tools become its second consumer | before or during V1a |
| Sub0Log, Sub0MemPage, Sub0Pub, Sub0Pipeline, Sub0TieredCache | adopted where DECISIONS.md section 8 places them: diagnostics in `serve`; paging and frozen tables at S2/S4; check runs at V4 | when their slice comes |

## 6. Risks to the plan

* **The owner's answers change phase 2's shape.** Mitigated: phase 1 needs none of them, and the server reads the log through one interface, so the
  storage answer changes a backend, not the tool.
* **Format churn for Sub0Squad.** Mitigated by one format revision (B0), additive, with readers that accept the old shape.
* **D0 moves identities.** Its brief requires byte-identical behaviour first and an explained, recorded change for every fix that moves an id on the
  golden replay.
* **The page's performance work grows without end.** K0's budgets bound it: V1a is done when the numbers hold, not when the code feels fast.
* **Private references leak through numbers.** Only rounded counts and ratios are published (SCALE.md section 8); the pre-push guard stays on.

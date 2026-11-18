# Use cases: who Sub0Canvas is for, and what each one demands

Status: living (2026-11-18). U1 and the viewer part of U7 are built; the rest is design.

Each use case is a consumer the design must serve **without a mechanism of its own** (if one needs a new mechanism, the design is incomplete, see
[BACKBONE.md](BACKBONE.md) section 9; section 14 walks U3-U11 against it). Each has a proving ground: something that can be run and that fails when
the design is wrong ([TESTING.md](TESTING.md)). What each one requires, stated so that it outlives any implementation, is in
[REQUIREMENTS.md](REQUIREMENTS.md).

**Investment order (owner, 2026-11-18, [DIRECTION.md](DIRECTION.md) section 4):** U1 stays served; **U7, the authoring tool, is the first use case to
invest in**, proved on this project's own design work; U2 and U6 grow out of it (the store, inboxes, then more actors); U3 and U4 follow their slices.

| # | use case | scale | built today | proving ground |
|---|---|---|---|---|
| U1 | **An agent's working document** (Sub0Squad's canvas worker) | one task: a few files, hundreds of lines, tens of turns | yes (canvas, folding, block ids, issues layer) | unit tests; Sub0Squad's `test_blocks`, `test_canvas_agent` and tier 0 A/B ([INTEGRATION.md](INTEGRATION.md)) |
| U2 | **The squad's backbone**: planner, scheduler, workers, reviewers and owners meeting in layers | one workflow: tens of files, many agents, days | design (BACKBONE.md B1-B3) | the same Sub0Squad checks, plus a recorded workflow replayed through the store |
| U3 | **A GitHub project view**: history, PRs, review comments, issues, checks around the code | one repository, its whole history and open PRs | design (BACKBONE.md G1-G5) | this repository's and Sub0Squad's own history and PRs, hand-checked truth |
| U4 | **Very large codebases**: monorepos up to **billions of lines**, and monorepos large in files, history and concurrent work | 10^9-10^10 lines, 10^7 files, 10^7 commits; or 10^5-10^6 files, 10^4 branches, 10^3 open changes, 10^2 merges a day | no | a synthetic corpus at scale, then large public repositories cloned locally (section U4 below) |
| U5 | **A text editor over layers** | the files open, the repository behind them | no | an LSP-like process serving one editor (BACKBONE.md E1) |
| U6 | **A collaborative editor with review, comments and explainers** | a team on one document set | no | two people and an agent reviewing one file (BACKBONE.md C1) |
| U7 | **The viewer, becoming an authoring tool**: Markdown, code and a GitHub-like review surface where a person and agents comment, reply, edit, test, validate and push | one project's docs and code first (this repository), then any document above | viewer yes (read-only); authoring design ([AUTHORING.md](AUTHORING.md), V0-V5) | `npm test`, golden fixtures; scripted scenarios (AUTHORING.md section 6); the owner's real review round on this repository |
| U8 | **Quality signals around the code**: CI checks, SARIF, coverage, profiles, churn as layers | one repository and its CI | no | captured CI and SARIF output, hand-checked anchors |
| U9 | **A prose-first reader of a codebase**: the docs and explanations at the centre, the code a drill-down | one repository | no | Markdown blocks (BLOCK_DISCOVERY.md 4.4), name anchors resolving as code is written |
| U10 | **Several repositories or dependencies together** | a workspace of documents | no | links with document-qualified keys (BACKBONE.md A9) |
| U11 | **An agent's long-lived memory of a codebase**: notes that outlive sessions and follow the code | one repository over months | no | notes resolved across G1's history, hand-checked |
| U12 | **Change views**: pull requests, spikes and local branches as layers; the changes serving one goal as a topic, with lineage (stacks, carve-outs, supersessions, dependencies, ports), block-level overlap and conflicts, a spike's landing coverage, drill-down to every variant of a block | one topic of tens of changes, within a repository of thousands of open ones | no | synthetic change sets with known lineage (S0); this repository's and Sub0Squad's branches; the owner's private reference topic, numbers only ([CHANGE_VIEWS.md](CHANGE_VIEWS.md) section 7) |

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

Two shapes of scale: a monorepo of billions of lines (the largest published one: about 2 billion lines, 9 million files, 35 million commits), and a monorepo that is large in everything but lines: hundreds of thousands of files, most of them not code, a git directory of hundreds of gigabytes, thousands of branches, hundreds of merges a day against thousands of open changes, several clones per developer (measured on a private repository, 2026-11-18). The requirements, design and budgets for both are in [SCALE.md](SCALE.md).

## U5. A text editor over layers (design)

An editor shows layers in the margin and lets the user centre on prose. The process owns the store; the editor sends changes and asks for views
(decorations, code lenses such as "3 issues, 1 PR", hovers). Needs nothing beyond documents, the index, items and views.

## U6. A collaborative editor with review, comments and explainers (design)

Comments and explainers are items in `review` and `prose` layers anchored to blocks, so they move with the code; a review is items with state; a
review request is a tag plus a subscription; presence is claims. Concurrent typing is not the backbone's job: either edits are serialised as
transactions (agents, block-sized edits) or the editor runs its own CRDT and feeds settled revisions.

## U7. The viewer, becoming an authoring tool (viewer built; authoring designed; the first investment)

Built: the static viewer reads `layered-doc-1` turn files and the canvas log, centres on any layer, drills between prose and code, shows a block's
identity history and steps through 1-N anchors. It understands optional fields the producer does not write yet (README, "Optional fields"); B0 closes
that gap.

Next ([AUTHORING.md](AUTHORING.md)): it becomes where work happens. The owner comments in a `review` layer on this repository's own design documents;
an agent reads its inbox, edits, replies and marks the comment addressed; the owner verifies; checks run into a `checks` layer; a push happens after an
explicit confirmation. The underlying data (the log, the history, every revision) grows without limit, but each view stays concise: bounded by the
reader's attention, "what needs me" first, history on demand (R-AUTH-1, R-AUTH-2). One person and agents first; more people later, tested first by
scripted scenarios.

## U8-U11. Further consumers (design targets)

Named so the groundwork is checked against them (BACKBONE.md section 14.1). Each needs no new mechanism: quality signals are layers of items with data
(A4) and ranges (A3); a prose-first reader needs Markdown blocks (A8) and name anchors; several repositories need document-qualified links (A9); an
agent's memory is `note` events in the log, resolved across history by B4 and G1.

## U12. Change views (design)

A topic of work in a large repository is rarely one pull request. It is an exploratory spike, focused changes carved out of it, stacks, supersessions, an integration change and ports to release branches, spread over weeks. The forge shows a flat list. U12 shows the set: what each change came from and depends on, where changes meet at block level (not file level, which is noise), what of a spike has landed, and every pending variant of one block side by side. It needs no new mechanism: changes and topics are items, lineage is links, footprints are anchors ([CHANGE_VIEWS.md](CHANGE_VIEWS.md)).

# The owner's direction (2026-11-17)

Status: **source material, kept verbatim.** These are the two sections of Sub0Squad's `docs/LAYERED_DOCUMENT.md` in which the owner set the direction
for this project, moved here on 2026-11-18. [BACKBONE.md](BACKBONE.md) is the design written from them; where the two differ, BACKBONE.md is the newer
decision and this file is the intent it answers. Wording such as "this repository" and "the squad" refers to Sub0Squad, where it was written.

## 1. Direction: sub0canvas as the squad's backbone (owner, 2026-11-17)

The full design (mechanisms, the store, adapter interfaces, the revision-aware index, the format extension, how the squad's pieces map, consumers, slices
with checks, open questions) is in sub0canvas: [BACKBONE.md](BACKBONE.md). This
section and the next are the owner's direction it was written from.

**Scope.** The canvas tooling (the canvas core, folding, the block index and layers, their build and their tests) becomes the **sub0canvas** project: a
tool for agentic workflows that works on its own, outside Sub0Squad. Sub0Squad is its first and most integrated consumer. Sub0Squad keeps what is
squad-specific: the attempt loop, the gates and harnesses, the prompts. The code in this repository is the starting point, moved, not rewritten. Other
consumers keep the design honest: a text editor over layers; a collaborative multi-user document editor with review, comments and explainers; a prose-first
reader of a codebase.

**The idea.** The canvas is not only the document a worker edits. It is where the squad's agents meet. Each role reads and writes layers and reacts to
what others write:

* a **planner** writes the plan or prose layer (a description per function, a backlog item per piece of work) and **tags** items, for example
  `ready-for-implementation`;
* a **scheduler** watches for that tag, as an inbox: a tagged item, with the context its anchors give it, becomes a job delegated to a worker, and the item
  records who holds it (the claims of [CANVAS.md](CANVAS.md));
* a backlog written inside a project (docs/BACKLOG.md, issues in a file) is just another layer. A job that reads it can flag items for delegation the same
  way, so **orchestration is a layer** too: what is planned, held, done and verified, anchored to the parts of the document it concerns;
* an **owner** of an area (a person or an agent) watches that area: a failing test anchored to "their" code is an event they want to hear about, the way a
  worker sees its issues;
* **monitors** are freeform: by area (blocks, paths), by kind (failing tests, concerns), by layer, by tag, by event (a block merged away, an item left
  orphaned).

**The mechanisms, kept few and general** (how they are used is left open; use cases are built on them, not into them):

| mechanism | what it is | built on |
|---|---|---|
| layers and items | a named list of items with kind, title, detail, anchors, a stable `key` | slice 1 |
| anchors, 1-N | to block ids, loose lines, other items (item-to-item links), and names not yet written | slice 1, plus item links |
| tags and state | free-form tags and a small lifecycle per item (open, held, done, verified, orphaned) | new |
| events | an append-only log of every change: item written, tag added, block retired, claim taken | the index's events plus the canvas log |
| subscriptions | a standing query over layers, anchors, tags and events, delivered as an inbox (a summary first, detail on request, as `READ ISSUE n`) | the summary and READ machinery |
| claims | a time-limited hold on an item or a block, with an expiry ([CANVAS.md](CANVAS.md)) | design |
| views | any layer at the centre, with drill-down to what it anchors (sub0canvas's viewer) | sub0canvas |
| sources | adapters that feed documents, items and events (the squad's canvas and gate, local git, GitHub), anchored in their own terms and resolved by the core ("Sources" below) | new |

**Path.** Integrate today: move the slice-1 library and the canvas core into sub0canvas, consume it from Sub0Squad, and keep the canvas loop working
unchanged. Then iterate towards the backbone: tags and state, subscriptions as inboxes, item links, claims. Planning and designing the backbone is a major
item of work of its own, for a later session. It must start from what the squad already does (the planner's plan, the workflow's items and decisions, the
journal, concerns, mends), and show which of them become layers and which stay where they are.

## 2. Sources: an adapter abstraction, and the GitHub project view (owner, 2026-11-17)

**The use case.** Point the canvas at a GitHub repository and see the project around the code, not only the code:

* the files at a commit (the code layer), and the **change history** of each block across commits: who changed `term()`, in which commit and PR, and
  what it was called before;
* the **open pull requests** as a pending layer: each PR's diff is anchored to the blocks it touches, so a block shows "2 open PRs change this", and two
  PRs that touch one block are a conflict seen before either merges;
* **comments inside PRs** (review comments on lines, review summaries) anchored to the code they discuss. From the code you see the discussion, and from a
  comment you jump to that code as it is now, after later commits. A comment whose block was rewritten or deleted is shown as outdated or orphaned, never
  silently moved;
* **issues and discussions** that reference code (permalinks such as `blob/<sha>/src/calc.cpp#L41-L50`, or a function named in the text) become items
  anchored to those blocks: "this function has 3 open issues and was last discussed in #12";
* **project board items** (GitHub Projects) with their status and fields, which map directly onto the backbone's tags and state;
* **CI check runs and code-scanning results** (SARIF), which go into the issues layer beside the local gate's verdict and collapse with it when they are the
  same failure.

This is the backbone's use case seen from outside the squad: the same layers, anchors, tags, events and subscriptions, fed by a source the squad does not
own. Doing it well needs the design to stop assuming that the document is the Sub0Squad canvas.

**The abstraction.** Everything that feeds the canvas is an **adapter**. An adapter declares which of these capabilities it has. The core implements none
of them for a particular source.

| capability | what the adapter provides | examples |
|---|---|---|
| **document** | the revisions (an id, parents, author, time, message), the files at a revision, and optionally the diff between two revisions | the Sub0Squad canvas (a revision per transaction), a local git repository, GitHub's API at a ref, a plain directory, an editor's buffers |
| **items** | items for a layer, each with a namespaced key, kind, title, detail, state and tags, and **source anchors** (below), fetched since a cursor | the gate verdict, CONCERN blocks, prose from the design stage, PR review comments, issues, project items, check runs |
| **events** | a change feed since a cursor: polling with ETags, or webhooks/push, delivered into the event log that subscriptions read | GitHub's events and webhooks, the canvas log, a file watcher |
| **write** (optional) | an action back at the source: post or resolve a comment, set a project status, open a PR, apply a canvas transaction | GitHub's API, the canvas |

Five rules keep adapters small and the core trustworthy:

1. **Adapters never compute block ids.** An adapter anchors in its source's own terms: a **source anchor** is `{revision, path, line range, quote}`, where
   the quote is the anchored text (as in W3C Web Annotation's text-quote selector). The core resolves it to a block id by mapping that revision forward to
   the revision being viewed. One resolver serves every source, and a GitHub comment, a gate failure and a design note are anchored by the same rules.
2. **The index becomes revision-aware.** Today `BlockIndex` follows one line of revisions, a turn at a time. To resolve an anchor made at an older commit,
   or on a PR branch, the core replays the index along the revision graph: from the anchor's revision to the viewed one, or from the merge base for a branch.
   It keeps checkpoints so a replay is bounded. Each replay emits the usual events (`kept`, `renamed`, `merged`, `retired`...), so **change history at block
   level** is the index run over git history: what git blame -M/-C does for lines, done for functions. This is the main strengthening of the design: block
   identity across the history of a project, not only across one worker's turns.
3. **Keys are namespaced by source.** Examples are `github/pr/123/comment/456`, `github/issue/12`, `squad/issues/test/modulo`. Duplicates across sources
   (a CI failure and the local gate's failure of the same test) are merged by the collapse workflow into one item that keeps both sources as provenance.
   They are never merged silently.
4. **Cursors and snapshots make views reproducible.** An adapter returns an opaque cursor with each fetch. The core caches what it fetched, keyed by source
   and revision or cursor, so a view can be replayed offline and two people looking at "PR 123 at its head" see the same thing. Large histories are read
   incrementally: since the merge base, or the last N commits, with index checkpoints.
5. **Credentials and writes stay at the edge.** The adapter owns authentication (for GitHub, the user's `gh` login or a token from the environment). The
   core, the interchange files and the viewer never hold credentials. Interchange files of a private repository are private data. Every write is
   outward-facing and goes through the consumer's confirmation: an agent that wants to post a comment proposes it, and a person or a policy approves it.

**What changes in the design.**

* The Sub0Squad canvas becomes one document adapter among several, and the gate verdict, concerns and prose become item adapters. This is the boundary that
  lets sub0canvas stand alone: the library has no dependency on the squad, and the squad plugs in as adapters.
* `layered-doc-1` gains:
  * a `sources` map (name, adapter kind, capabilities, cursor);
  * a `source` on every item and event;
  * `revision` and an optional `quote` on anchors;
  * `state` and `tags` on items;
  * a `revisions` list with parents, for history views.

  Existing readers keep working: every addition is optional.
* The viewer gains a project view:
  * history along a block's identity;
  * PRs as pending overlays, with conflicts between PRs;
  * comment threads in the margin of the code they discuss;
  * an outdated or orphaned marker when the code has moved on.

**How GitHub maps onto the model.**

| GitHub | canvas |
|---|---|
| commit, branch, merge base | revision, and a line of revisions; the index replays along it |
| file at a commit | document file at a revision |
| PR | an item in a `pulls` layer, anchored to every block its diff touches (1-N), state open/draft/merged/closed, plus a pending revision line (its branch) |
| PR review comment, suggestion | an item in a `review` layer: a source anchor at the PR's commit, a thread as detail, resolved/outdated as state; a suggestion is a proposed `fix` |
| issue, discussion | an item in an `issues/github` layer, anchored by permalinks and named functions, otherwise unanchored (an orphan, shown as such) |
| project item and its fields | tags and state on the item it tracks |
| check run, code-scanning alert (SARIF) | an item in the issues layer; SARIF's own fingerprints become the key |
| webhook or event | an event in the log; subscriptions and inboxes read it |

**Slices**, each with a check:

| slice | what is built | check |
|---|---|---|
| G1 | a local git document adapter, and the revision-aware index replayed over history | the identity history of a known function across a real repository's last 200 commits, renames and moves included, against hand-checked truth |
| G2 | GitHub items, read-only: open PRs as a `pulls` layer, and PR review comments as source anchors resolved to blocks | each comment on this repository's recent PRs lands on the right block at main's head, or is marked outdated, against hand-checked truth |
| G3 | issues, discussions and project items, as items with tags and state | the backbone's tag and state mechanism is driven by a real project board |
| G4 | events and subscriptions over GitHub | an owner's subscription ("failing checks or new review comments on blocks under `src/canvas*`") delivers an inbox summary |
| G5 | write back through the consumer's confirmation | an agent proposes a review reply anchored to a block; it is posted only on approval |

**Prior art for this part.**
* **GitHub and Gerrit** already port review comments to new revisions and mark them outdated, but per line and per PR. The block index carries them across
  PRs and history.
* **GitLens** (blame, line history and PRs in the editor), **CodeStream** (discussions anchored to code across branches) and **Sourcegraph** (code
  intelligence across history; batch changes as 1-N edits) are the closest products.
* Plugin architectures such as **Backstage** and **Grafana data sources** are the model for adapters: capability-declared, credentials at the edge, a core
  that knows none of them.

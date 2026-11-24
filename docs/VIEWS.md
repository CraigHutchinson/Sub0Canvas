# Pinned and unpinned views: a tip workflow and a pinned workflow

Status: **design, 2026-11-24.** Nothing here is built. This is the one home for the idea; the mechanisms it rests on are described where they are built
and are linked, not copied.

The owner's idea (2026-11-24, verbatim): "a canvas view/observer could be pinned or unpinned. A pinned view is at a base revision for consistent editing
without external async change events, while unpinned is a view on latest and refreshing a view contains the delta of changes in a brief, elided,
compacted, full form depending as option. So it's possible to have a tip workflow or a pinned workflow. Having a channel to get live events published
on the unpinned view could also be valid consideration for example the app/Web live viewport viewer."

What exists already: views as pure functions of the store ([BACKBONE.md](BACKBONE.md) 3.6, A14), subscriptions with cursors ([BACKBONE.md](BACKBONE.md) 3.4,
`inbox.hpp`), stale-base rebase in `applyReply` (`ApplyOptions::baseRevision`, `canvas.hpp`), the anchor resolver ([BACKBONE.md](BACKBONE.md) 5.1, `resolver.hpp`),
live views over SSE ([AUTHORING.md](AUTHORING.md) 3.1), the query layer's `at` positions, cursors and `events` op ([QUERY.md](QUERY.md), landing with V1a-q),
and the backlogged actor views, scratch revisions and viewpoint clones ([sessions/canvas-source-addressing.md](sessions/canvas-source-addressing.md)).
This document adds one concept that ties them together: **the pin state of a view**.

## 1. The model

A **View** is an observer of one document. It holds:

| field | what it is | built on |
|---|---|---|
| `pin` | `pinned` (at a base position) or `unpinned` (follows the tip) | new |
| `position` | the `at` tuple it was last computed at: `generation` (canvas revisions; per-file revisions for a canvas), `indexEvents`, `storeHead` (lamport), `storeEvents`. Pinned: fixed until re-pinned. Unpinned: where the last refresh left it | QUERY.md section 1, inbox positions (lamport) |
| `scope` | a query: paths, blocks, layers, states, actor; the same `SubscriptionQuery` an inbox uses | `inbox.hpp`, BACKBONE.md 3.4 |
| `detail` | the default delta form for a refresh (section 3) and the rendering budget (items, characters, folded or whole) | A14, R-AUTH-1, `foldedView` |
| `owner`, `id`, `parent` | the actor holding it, its identifier, and the view it was forked or re-pinned from (lineage) | actor views (source-addressing brief) |

A view is **data**, recorded like a subscription (events folded into store state, so replay gives the same views), never a live object in the core. The
library stays pure: "pinned" means "computed at a fixed position", and "unpinned" means "the caller asks again with its last position". Push is the
edge's job (section 4).

## 2. The pinned workflow

**Reading.** Every read through a pinned view (folded view, READ SYMBOL, layer page, inbox) is answered at the pinned position: files at their pinned
revisions, the store folded to the pinned lamport. Two reads in one turn always agree, whatever other actors do meanwhile. No change event is ever
delivered to a pinned view; nothing it holds moves.

**Editing.** An edit carries the view's position as its base: `applyReply` with `ApplyOptions::baseRevision` (and later a per-path revision set). The
canvas rebases it over what changed since where lines do not overlap, and refuses it, naming the other actor and revision, where they do (MOCK_SQUAD.md B8,
R-CAN-4). Store writes (item puts, claims) are checked against current state at commit time, as `checkClaim` does; a refusal names the holder and expiry.
Per the source-addressing brief, an address resolves inside the view's editable ranges and pending changes first; a match elsewhere never widens it.
Accepted edits do not move the pin: the view now has **pending work** over its base (a scratch revision), visible to its owner only until it lands.

**Learning what moved, undisturbed.** A pinned view may ask, when *it* chooses, `since`: a brief summary (section 3, form `brief`) of the changes between
its pin and the tip within its scope, e.g. "3 files changed in scope, 1 overlaps your pending edit (`parse.cpp:parseExpr`, by worker-2 at r14)". It is
never pushed and never changes the view. This is the stale-base inbox MOCK_SQUAD.md section 4 question 5 asks for.

**Re-pin operations.**

* **advance**: move the pin to the tip (or to a named position). Pending work is rebased; the result is a **rebase report**: what landed, what was
  refused and why, plus the refresh delta (section 3) for the view's scope. Anchors the view holds (issues, notes, visible ranges) are re-anchored by the
  resolver: `editable` ones stay editable; `moved` ones are shown as probable and lose edit authority until re-read; `orphaned` ones are listed.
* **fork**: clone the view at its pinned position into a new view with its own id, actor or session, scratch history and newly issued addresses,
  recording parent, position and a manifest fingerprint (the viewpoint clone in the source-addressing brief and the backlog's "revision-pinned viewpoint
  clones"). Addresses issued to one branch are refused in the other.
* **unpin**: become a tip view; the first refresh is the delta since the old pin.

## 3. The unpinned (tip) workflow and refresh forms

An unpinned view is answered at the tip. **Refresh** returns the delta since the view's position, then moves the position to the answer's `at`
(acknowledge, as `acknowledge` moves an inbox cursor: only when the reader says so). The form is an option, per refresh or as the view's default:

| form | contents | for | typical cost |
|---|---|---|---|
| `brief` | counts and keys only: files, blocks and items changed, by kind and actor; the new position | a model's turn header, a status line, `since` on a pinned view | tens of tokens, independent of edit size |
| `elided` | each changed region with a little context, unchanged stretches elided as `... (n lines)`, the same fold markers as `foldedView` | a reviewer skimming; a worker deciding what to READ | proportional to changed lines, bounded by the budget |
| `compacted` | net change per block (or per item): many edits to one block merged to its before and after (or a single diff); intermediate states and reverted edits dropped; item events folded to latest state, as an inbox's `items` are | the default for agents: the delta that matters, once | proportional to blocks touched, not events |
| `full` | every event since the position in merge order, and every intermediate edit | audit, replay, a viewer patching its own state | proportional to events |

The forms nest: `brief` is the header of `elided`, `compacted` and `full`, so a reader can always stop early. Each obeys the view's budget (A14): when it
does not fit, it degrades towards `brief` and returns a cursor for the rest, never a truncated diff without saying so. This is the token-efficiency rule
(R-AUTH-1, "a view is bounded by attention, not by the data", AUTHORING.md section 2) applied to time: a refresh costs what changed in scope, not the
document. `brief` and `compacted` are an inbox (`{items, events, upTo}`) rendered two ways; `elided` is a folded view of the changed blocks; `full` is the
query layer's `events` op filtered by scope.

## 4. The live channel

An unpinned view may open a **channel**: the query layer's `events` op as a stream (SSE through the bridge for a page, JSON lines over stdio for an agent;
AUTHORING.md section 3). The channel's position is the view's cursor, so it is **replayable**: a reconnect resumes from its last position, and a position
older than the events held answers `gone` and the client falls back to a refresh. **Back-pressure**: the server never queues without bound; a slow
reader's pending events are **coalesced** into one `compacted` (or `brief`) delta per key, and the stream says it did so. The web viewport viewer is the
first user: each event names the item key, block id or path it touches, so the page patches only those rows, and nothing the reader holds moves
(AUTHORING.md 3.1, R-AUTH-12; the K0 row "an event from another actor while reading", [K0.md](K0.md)). **Pinned views get no channel**; asking for one is a
refusal naming the pin.

## 5. How it composes

* **B1 store events**: positions are lamport values; a pinned store read is replay up to the pin (or a checkpoint plus the events to it).
* **B2 inboxes**: one subscription per view; its cursor is the view's position. Per-writer cursors (owed to V1) matter more here, since a tip view must
  not miss a late event below its cursor.
* **B3 claims**: claims are always checked at the tip, even from a pinned view; a pinned view may hold claims, which is what makes editing at a base safe.
* **B4 resolver**: re-pin and unpin re-anchor everything the view holds; authority follows the resolution (`editable` or not).
* **Sub0Squad's loop**: a **worker is a pinned view per turn**: pinned at turn start, edits rebased on submit, `since` brief at the next turn's start,
  advance between turns; a retry is a fork of that turn's view. A **reviewer or portal** is an unpinned view, with a channel when it is a page.

**Proposed requirements** (not added to REQUIREMENTS.md; `R-VIEW-` is already taken by folded views, so `R-PIN-`):

| id | requirement |
|---|---|
| R-PIN-1 | Every read through a pinned view returns the same bytes until the view is re-pinned, whatever is written meanwhile. |
| R-PIN-2 | An edit through a pinned view is rebased over non-overlapping changes since its base, or refused naming the other actor and revision. |
| R-PIN-3 | `since` on a pinned view returns a brief within its scope and changes nothing; a pinned view never receives pushed events. |
| R-PIN-4 | Advance returns a rebase report, and re-anchors held anchors with authority following the resolution. |
| R-PIN-5 | A fork records parent, position and fingerprint; addresses issued to one branch are refused in the other; a mismatched checkpoint is reported. |
| R-PIN-6 | A refresh in any form, applied to the view's old state, gives the view's new state (compacted and full agree on the end state). |
| R-PIN-7 | A refresh's cost is proportional to the changes in scope; `brief` is bounded independently of edit size. |
| R-PIN-8 | A channel resumes from its position without loss or duplicate, coalesces under back-pressure and says so, and answers `gone` past what is held. |
| R-PIN-9 | Views and their positions replay from the log to the same state. |

**Mock-squad scenarios to prove it**: two workers pinned at r10 in one file, disjoint blocks, both land (B8 extended); overlapping edits, the second
refused with actor and revision, both scratch histories kept (B14); a pinned worker's `since` brief names the overlap without moving anything; advance
with a moved block re-anchors the issue as `moved` and refuses an edit until re-read; fork a stuck turn, advance the original, edit both, integrate one
(the viewpoint-clone receiving case); a tip reviewer refreshes in all four forms over the same history and every form reaches the same end state; a channel
dropped mid-stream resumes; a slow channel under a 200-event burst is coalesced.

**Metrics and budgets** (deterministic counters in `perf/metrics.json`; timing local only): `brief` tokens per refresh (budget: under 100 regardless of
edit size); `compacted` tokens relative to `full` on the S0 edit stream; events visited per refresh equal to events in scope (as `InboxReader`); a pinned
read at an old position under 10 ms with checkpoints (SCALE.md budgets); advance with rebase plus re-anchoring of 2,000 anchors inside the resolver's
existing 10 ms budget; channel event to painted patch inside K0's 50 ms row.

## 6. Open questions for the owner

1. Is the canvas pin a single revision for every file (today's `baseRevision`) or a per-path revision set, which a fork's manifest needs anyway?
2. Should a pinned view see its own pending edits in reads (scratch overlay), or only its base until advance? The brief implies the overlay.
3. Does `compacted` merge by block identity only, or also fold item events across layers into one delta?
4. Is `since` allowed to be pushed as a single "you are stale" flag (no content), or strictly pull, as written here?
5. Do views persist across process restarts (events in the log, like subscriptions) or live in per-reader state (A17's user state directory)?
6. Does advance stop on the first refusal or land what it can (as `applyReply`'s partial mode does) and report the rest?

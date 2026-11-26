# Adapters out of the box: git, forge and tracker without a side pipeline

Design only. Backlog: item 16 (the scale extraction) and the adoption goal in [DIRECTION.md](DIRECTION.md). Builds on [BACKBONE.md](BACKBONE.md) section 5
(`DocumentSource`, `ItemSource`, rules 1-5), [CHANGE_VIEWS.md](CHANGE_VIEWS.md) section 5 and [sessions/scale-extraction.md](sessions/scale-extraction.md).

## Question
Can the data the extraction needs (commits and their hunks per block, pull requests with reviews and comments, tracker tickets with stack traces) come straight
from a git, forge and tracker adapter, so that a new user gets the layers by pointing `sub0canvas` at a checkout, with no separate scripts?

## Answer
Yes for git and the forge with no configuration beyond what the machine already has; yes for a tracker with one line of configuration. The seams exist in
BACKBONE section 5; `ItemSource` is the unbuilt one. Measured on this repository's own forge (read-only, one call each):

| need | source | one call gives | cost |
|---|---|---|---|
| commits per block, hunk ranges | local git (built: `snapshot`, hunk spans) | one `log` and one diff per turn for the scope | local |
| pull request of a commit | the subject of a merge commit (`Merge pull request #N`) or a squash (`(#N)`) | the number, with no network | local |
| pull request of a commit that names none | forge `commits/<sha>/pulls` or a GraphQL alias per commit | the numbers, many commits per request | 1 point per request of the 5,000 an hour |
| PR commits, files, title, body, state | GraphQL `pullRequest` (commits, files, reviews, comments in one request, paged) | everything about up to 100 PRs per request when aliased | about 1 point each |
| review threads anchored to code | GraphQL `reviewThreads`: `path`, `line`, `originalLine`, `isOutdated`, and per comment `originalCommit` and `diffHunk` | the anchor the resolver needs: revision, path, line | with the PR |
| commits inside a PR branch (the ones first-parent folds away) | `refs/pull/N/head` fetched by git, or the PR's commit list | the branch's commits | one ref fetch |
| tickets and their stack traces | tracker REST by key list (`key in (...)`, up to about 100 keys, fields: description, comments, links, fix versions) | text that holds the trace | one request per batch |

## Design

### 1. Tiers: each works alone, each adds layers
| tier | needs | adds |
|---|---|---|
| 0 local git | a checkout | commits, hunks, merge-subject PR numbers, ticket keys found by pattern in subjects, branch names and trailers (a key is an item with no body) |
| 1 forge | a forge login the machine already has (the `gh` CLI, or a token in the environment) | PR title, body, state, reviews, review threads, check runs, PR-branch commits |
| 2 tracker | a base URL and a credential, found in the environment or one config line | ticket text, links, versions, stack frames and message templates, anchored to blocks |
The viewer and every query degrade the same way: a layer that has no source is absent, not an error (BACKBONE A20 applied to items).

### 2. Detection, not setup
* The forge host and repository come from `remote.origin.url`; the same adapter serves a public forge and a company instance (CHANGE_VIEWS section 5).
* Authentication is the user's existing login. The adapter shells out to `gh api` when `gh` is logged in to that host (so no token is read, stored or logged
  by Sub0Canvas), else to a token named by an environment variable over HTTPS. Rule 5 holds: credentials stay at the edge.
* The tracker is found from the keys in the history: the first key's project prefix plus a configured base URL. One line in `.git/sub0canvas/config` (or an
  environment variable) names the base URL and where the credential comes from. Nothing is guessed about a host and nothing is sent until that line exists.
* Default is read-only and the first remote call prints what it will ask (host, counts) and waits for a yes, or for `--yes`. A tier never runs in the
  background.

### 3. Shape in the code
* `lib` stays pure (BACKBONE rule 2): it gains only parsers, as for git: PR and review responses to `LayerItem` and source anchors; ticket text to frames
  and templates; merge and squash subjects to numbers; each tested on captured, invented fixtures.
* `app/` (or separate targets `sub0canvas::forge` and `sub0canvas::tracker`, so Sub0Squad never links a network adapter by accident) holds the runners:
  a small `Fetch` seam (`get(request) -> text`) with two implementations, `gh` and `curl`, and the `ItemSource` implementations over it.
* `sub0canvas snapshot` gains `--layers pr,tracker` (default: every tier that is detected and approved). The result is the same bundle the viewer already
  reads, so no second format and no second tool.

### 4. Batching and cost (the monorepo lesson applied)
* Resolve locally first: merge and squash subjects cover most commits; only the rest go to the forge.
* One GraphQL request carries many aliased lookups; one tracker request carries a key list. Never one request per commit, PR or ticket.
* A cache keyed by (source, id, updatedAt) under the git directory (BACKBONE A19: per user, shared by clones) makes a rerun cost only what changed;
  conditional requests where the service supports them; the rate-limit headroom is read from the response and the run pauses, never fails, when it is low.
* A cursor per source (`source.cursor`, BACKBONE rule 4) makes a later fetch incremental and a view replayable offline from the cache.

### 5. Anchoring (the part that is not plumbing)
* A review thread gives revision (`originalCommit`), path and line: a `SourceAnchor` the resolver already maps to a block at that revision and carries to
  the viewed one. `isOutdated` becomes the anchor's outdated state, kept and shown.
* A ticket anchors three ways (scale-extraction item 6): a stack frame (path, symbol, line) at the revision its version names; a quoted symbol or message
  found in a block; the commits and PRs that carry its key. The claim a ticket makes about the code stays apart from the checked outcome.
* A forge or tracker item is stored with its source namespace; duplicates across sources are linked, never merged (BACKBONE rule 3).

### 6. What this changes for the scale work
* The gold pipeline's census and hunk walks stay as measurement tools. Its forge and tracker stages (G3/G4 in [scale-dataset-plan.md](sessions/scale-dataset-plan.md))
  are replaced by the adapters: the extraction becomes `sub0canvas snapshot` with layers on, and the pipeline only measures the bundle it produces.
* The anonymiser and the synthetic replica work on the layered document, after the adapters, so they need no knowledge of any source; an adapter's raw
  output is never stored outside `out/`.
* Out of the box for a new project: tier 0 needs nothing, tier 1 needs a login most developers already hold, tier 2 needs one line. That is the adoption
  path; there is no import step.

## Risks and open questions
| item | note |
|---|---|
| Company forge instances may lack a GraphQL feature or lag the public one | probe the schema once per host and fall back to REST; both are fixture-tested |
| Tracker text is free-form | frames and templates are best-effort; an unparsed ticket stays a text item and is counted, so the gap is visible |
| Rate limits and slow tracker queries on a large history | batching, the cache, and a run budget (requests and minutes) that stops with a partial, labelled result |
| Review threads on files that moved after the review | the resolver's carry and quote re-anchoring; the outdated state when neither holds |
| Squash-merged PRs hide their branch commits | the PR's commit list and `refs/pull/N/head` recover them; first-parent turns stay the unit |
| Credentials | never read by the library; the `gh` path reads none at all; a token path reads an environment variable at the edge and never logs it |

## Slices
1. Merge and squash subject parser, ticket-key detector (pure, in `lib`, tier 0) with fixtures.
2. `Fetch` seam with the `gh` and `curl` runners, the cache, and the approval prompt.
3. Forge `ItemSource`: PRs, reviews, threads to items and source anchors; `snapshot --layers pr`.
4. Tracker `ItemSource` behind one config line: ticket text, frame and template parsers, three anchor kinds.
5. Replace the gold forge and tracker stages with the adapters; the viewer shows both layers on a block.

# The authoring tool: the viewer becomes where people and agents work on a document

Status: **design, for the owner to read** (2026-11-18). Nothing here is built. It answers the owner's direction of 2026-11-18 ([DIRECTION.md](DIRECTION.md)
section 4): the interactive viewer is the **first use case to invest in**, extended into a Markdown, code and GitHub-like authoring tool in which a
person can test, validate, push and respond on a document that later becomes multi-user, with scripted scenarios as its tests; and **Sub0Canvas's own
design work is its first document**. Use case U7 ([USE_CASES.md](USE_CASES.md)); requirements R-AUTH ([REQUIREMENTS.md](REQUIREMENTS.md)); the
mechanisms it uses are [BACKBONE.md](BACKBONE.md) section 3, with the amendments of section 14.

## 1. The loop it must make work first

The acceptance test of the whole track is one real round on this repository:

1. The owner opens Sub0Canvas's docs in the viewer, selects a sentence in BACKBONE.md section 11, and writes a comment: an item in the `review` layer,
   anchored to that section's block and to the selected text, state `open`, actor `person/<owner>`.
2. An agent (a session working on this repository, or a Sub0Squad worker later) asks for its **inbox**: open review items in `docs/`. It gets a summary
   line per comment, then each in full with the text it is anchored to.
3. The agent acts: edits the document (a transaction against the revision it read), replies in the comment's thread (an item linked `repliesTo`),
   and marks the comment `done` with a reason.
4. The owner's view shows the comment addressed, **still anchored to the right section after the edit**, with the reply and the diff. The owner
   marks it `verified`, or reopens it with a further reply.
5. Checks run (the local test presets), their results land in a `checks` layer, and the owner commits and, after an explicit confirmation, pushes.

Nothing in that loop is specific to design docs: the same loop is code review, a backlog triage, or a squad worker's verdict (B2).

## 2. Concise views over very large data

The underlying data grows without limit: every event, every revision, a repository's whole history. The person and the agent must still see a clean,
short view. The rule (R-AUTH-1): **a view is bounded by attention, not by the data.**

* **Every view has a scope and a budget.** A request carries a query (paths, layers, states, actor) and limits (items, characters). The answer is a
  summary with counts, the first few in full, and cursors for the rest: the model's verdict rendering (R-REN) generalised. A person's view and an
  agent's prompt are the same function with different budgets.
* **"What needs me" is the default.** Opening the tool shows the reader's inbox (open items addressed to them, changes since they last looked), not
  the document's whole state. GitHub's "new since your last review" and the worker's verdict are both this.
* **History is a drill-down, never the default.** A block shows "changed 3 times since you last looked"; its full history, the log and older
  revisions are fetched by range when asked. The viewer never loads a whole log: it reads a snapshot and the events after it (BACKBONE.md 3.3, 4).
* **Large is the server's problem, not the page's.** The page holds what is on screen. Lists are paged; long files render folded (the same folded view
  a model gets, CANVAS.md), with the anchored sections open.
* This is the same rule as U4's ("cost proportional to what is touched", BACKBONE.md section 12), applied to attention: what is shown is proportional
  to what concerns the reader.

## 3. Architecture

```
  browser (index.html, src/*.js)  ──HTTP + server-sent events──▶  tools/canvas-serve.mjs  ──JSON lines over stdio──▶  sub0canvas serve
  agent / script / CLI  ─────────────────────────── sub0canvas <command> (via the running server, or directly) ──────────┘
                                                                                                           owns: the store, events.ndjson,
                                                                                                           the worktree as document source
```

* **One process owns the document** (BACKBONE.md section 4, rule 1): `sub0canvas serve`, a new executable in this repository linking the library.
  It speaks **JSON lines over stdio**, LSP-like: each request is a command (an event to append, R-BB-8) or a query (an inbox, a view, a range of
  history). The library stays pure; the executable does the I/O (files, the clock, running checks).
* **The bridge** (`tools/canvas-serve.mjs`, Node, no dependencies, growing from today's `tools/serve.mjs`) serves the page, forwards requests to the
  process, and pushes updates to the page as server-sent events. It binds `127.0.0.1` only and requires a per-session token in every request, so
  another local web page cannot drive it.
* **Why not a JavaScript store**: the store's rules (identity, reconciliation by key, claims) must have one implementation, or the viewer and the
  squad would disagree. The C++ library is it; the viewer stays a renderer of views. The same `serve` process is E1's editor server later.
* **The viewer keeps working read-only from `file://`** (today's mode: open a folder of snapshots). Authoring needs the server.
* **Agents use the same protocol**: `sub0canvas inbox`, `sub0canvas reply`, `sub0canvas state` (a CLI over the same commands) talk to the running
  process, or open the log themselves when none runs (a lock file keeps one writer). No agent-specific mechanism.

## 4. The document and its layers

| part | in the authoring tool |
|---|---|
| document source | the working tree (`DocumentSource` "local/worktree"): a revision per save through the tool, and per change found on disk (by content hash); git history joins through G1 |
| blocks | Markdown sections by heading, fenced code by its own language, code by function (BLOCK_DISCOVERY.md: the Markdown provider is pulled forward) |
| `review` | comments: anchored to a block plus the selected text (a quote with prefix and suffix, BACKBONE.md 3.2) and its range; threads are items linked `repliesTo` (amendment A5); state `open`, `done` (addressed), `verified` (accepted), `dropped` |
| suggestions | a review item with a `fix` of kind `replaceText`: accepting it applies a canvas transaction against the revision it was made on, rebased or refused by R-CAN-4 |
| `checks` | the result of each allowlisted local command (`ctest --preset quick`, `npm test`): an item per failing test, anchored to the test's block; state follows the latest run |
| `issues`, `plan` | as today, and `docs/BACKLOG.md` as items (BACKBONE.md section 8) |
| actors | `person/<name>`, `agent/<name>`; every event names one. One person now; more later with no new mechanism (claims give presence, BACKBONE.md 3.5) |

**Where the review log lives** is an open question for the owner (BACKBONE.md section 11, question 10): committed under `.sub0canvas/` (shared through
git across devices, history kept, visible in this public repository) or local only (private, lost with the clone).

## 5. Testing, validating and pushing from the tool

* **Edit**: a Markdown or code pane; saving is a transaction against the revision shown, so a concurrent agent edit is rebased or refused, never
  overwritten (R-CAN-4).
* **Test and validate**: a button runs a command from an allowlist in the repository (`.sub0canvas/commands.json`: name, argv, working directory;
  no free-form shell). Output is parsed by the same issue parser (R-ISS) into the `checks` layer.
* **Commit and push**: the tool shows the diff since the last commit and the checks' state. Commit is local. **Push asks for explicit confirmation
  every time** (AGENTS.md: confirm before pushing; never force-push or skip hooks), and runs the repository's pre-push gate like any push.
* **Respond on GitHub** (G5): later, through `WriteSink`, always behind confirmation. Nothing leaves the machine before that slice, by construction.

## 6. Scripted scenarios: the tests, and the way to multi-user

A scenario is a file of steps, each an actor and a command, with assertions on views, replayed headlessly against the same protocol the page uses:

```jsonc
{ "scenario": "comment-survives-edit", "document": "fixtures/scenarios/comment-survives-edit/doc",
  "steps": [
    { "as": "person/owner", "do": "comment", "at": { "path": "docs/X.md", "quote": "five open questions" }, "text": "Answer question 2 here." },
    { "as": "agent/a",      "expect": { "inbox": { "count": 1, "first": { "layer": "review", "state": "open" } } } },
    { "as": "agent/a",      "do": "edit", "path": "docs/X.md", "replace": { "quote": "five open questions", "with": "six open questions" } },
    { "as": "agent/a",      "do": "reply", "to": "$1", "text": "Added question 6." }, { "as": "agent/a", "do": "state", "key": "$1", "to": "done" },
    { "as": "person/owner", "expect": { "item": { "key": "$1", "state": "done", "anchor": { "section": "Open questions", "status": "outdated-quote-reanchored" } } } } ] }
```

Scenarios are the L2 rung of TESTING.md for this track: deterministic (a passed clock, seeded ids), byte-compared views, hand-checked truth. A scenario
with two people and an agent is how C1 (multi-user review) is tested long before two real people use it.

## 7. Slices

Each slice is used (by the owner, on this repository) before the next is built.

| slice | what is built | check |
|---|---|---|
| **V0** a worktree snapshot | the `sub0canvas` executable with `snapshot <dir> [paths]` writing `layered-doc-1`; a minimal Markdown provider (headings, fences); the viewer renders Markdown (headings, lists, code) and code side by side | `sub0canvas snapshot . docs` opens in the viewer; every heading of every doc is a block; golden snapshot of `docs/` checked byte for byte |
| **V1** comments | `serve` with a minimal store and `FileLog` (B1's core: `item.put`, `item.state`, `item.link`); the bridge; comment, reply, resolve from the viewer | scenario: a comment and its thread survive a restart (replayed from the log) and render identically |
| **V2** the agent's inbox | B2's `inbox` and `acknowledge` over the review layer; the CLI (`inbox`, `reply`, `state`); views with budgets (R-AUTH-1) | **the round of section 1 on this repository**, done for real, and recorded as a scenario |
| **V3** edits and suggestions | editing in the page as transactions; suggestions with `fix`; quote re-anchoring (B4's resolver, first part) | scenario: a comment's anchor follows its text through an agent's rewrite of the section, or is marked outdated, never moved silently |
| **V4** checks and push | the command allowlist, the `checks` layer, commit, push behind confirmation | scenario with a failing then passing test; a push refused without confirmation |
| **V5** a second actor | presence from claims (B3), two sessions on one document, scenarios with two people and an agent | C1's check, as a scenario |

V0-V2 pull forward the parts of B1, B2, B4, D2 and E1 they need, and nothing else. They are proved on the owner's own use before Sub0Squad depends on
them (INTEGRATION.md: library work the squad does not consume needs no Sub0Squad PR).

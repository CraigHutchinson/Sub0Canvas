# Backlog

What is next, in order. Each item names its slice in [BACKBONE.md](BACKBONE.md) (section 10 has the check that proves it) and, when it has one, its
brief in [sessions/](sessions/README.md). Items in *Now* can run in parallel.

## Now

* **The owner reads BACKBONE.md** and answers its open questions (section 11). The answers decide B1's shape (where the store lives, per-turn
  events) and G2's scope (the network rule). Nothing below B1 should be built before that.
* **S0: the scale harness.** A deterministic synthetic corpus and benchmarks of today's index, so the data structures B1 and G1 build on are chosen by
  measurement. Brief: [sessions/s0-scale-harness.md](sessions/s0-scale-harness.md).
* **B0: format catch-up.** The producer writes the fields the viewer already understands; a golden set is checked on both sides. Brief:
  [sessions/b0-format-catch-up.md](sessions/b0-format-catch-up.md).
* **G1: the git document adapter and per-path history.** Block identity across a repository's history, checked on this repository's and Sub0Squad's
  history against hand-checked truth. Brief: [sessions/g1-git-history.md](sessions/g1-git-history.md).

## Next

* **S1: the compact, lazy index** (after S0's numbers): blob-keyed reuse, no held texts, sketches instead of token sets.
* **B1: the store and the event log**, then **B2: inboxes** (Sub0Squad's verdict rebuilt as an inbox, byte-identical), then **B3: claims and notes**.
* **S2: the sketch index and per-path checkpoints** (with G1's per-path replay).
* **B4: source anchors and the resolver** (text-quote re-anchoring, name anchors).
* **Sub0Squad: measure the layer where it acts**: off against adaptive on bugs-10 and other clipped cases, 10 runs per arm; inline detail against the
  summary alone (`SUB0SQUAD_ISSUE_INLINE=0` against 3). A Sub0Squad PR with no library change ([INTEGRATION.md](INTEGRATION.md)).

## Later

* **G2-G5**: GitHub items, issues and project items, events and subscriptions, write-back (captured data in tests; live reads only by hand, with the
  owner's go-ahead).
* **S3**: large public repositories cloned locally (each download approved by the owner). **S4**: the sharded store.
* **E1**: an editor process serving layers. **C1**: collaborative review.
* A SARIF export of the issues layer; tree-sitter block discovery for the languages S3 shows the heuristic gets wrong.

## Tidy-ups

* `lib/tests/check.hpp`: avoid `getenv`'s deprecation under clang on Windows (`_dupenv_s` there, or read once through a small helper).

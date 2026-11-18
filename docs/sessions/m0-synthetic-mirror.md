# Follow-up session: M0, a synthetic mirror project as a validation fixture

Status: **done** (2026-11-18); see Outcome. Written 2026-11-18. Backlog: "M0: the synthetic mirror" under *Now*. Design: [CHANGE_VIEWS.md](../CHANGE_VIEWS.md) section 7,
[SCALE.md](../SCALE.md) section 2 (profile B) and section 7; [TESTING.md](../TESTING.md) rungs L2 and L3.

## Goal

Build a **generator** that creates, from a seed, a small git project with the *state* of a work-dominated monorepo and a topic of changes. It has
no relation to any real codebase: an invented product, invented paths, invented history. Its output is the public stand-in for the owner's
private reference topic, so P0-P2 and G1 can be tested on realistic shape with known truth. Do not build change views or adapters in this session;
build the fixture they will be checked against.

## What is known

* The shape to mirror, anonymised (CHANGE_VIEWS.md section 1; SCALE.md section 2): one goal served by about 25 changes over weeks; an umbrella
  spike that never merges (about a quarter of the topic's files), a reproducer-only spike, a spike filed under a second topic; focused carve-outs
  of 1 to 46 files, some rewritten relative to the spike; 3 carve-outs closed and superseded; an integration change duplicating an open carve-out
  almost entirely; a stack three deep; a carve-out merged into a feature branch rather than trunk; a performance test and a test-setup fix as
  test-only changes; an unrelated large refactor that shares files with the spike; a tracker issue with a port ticket to a release branch;
  hotspots (about 1 in 12 of the topic's files touched by four or more changes).
* The repository around it, scaled down: most files not code (a third are sidecar metadata beside assets, many small binaries), code in two
  languages plus headers, Markdown docs, serialised data files; several long-lived release branches; a trunk with many unrelated merges between
  the topic's changes, some touching the topic's blocks (drift).
* Nothing of the private reference may appear: no names, paths, titles, products or tracker keys. The local guard checks the vocabulary; the
  author checks that the invented product is unrelated (for example a desktop photo catalogue, a static-site generator, or an embedded data
  logger: pick one, say why it fits the shape).

## Steps

1. **The generator** (Node 20, no dependencies, `tools/mirror/`, seeded and deterministic; or C++ beside S0's corpus if the two should share code:
   decide and say why). It writes a git repository through `git` plumbing or `fast-import` into a target folder: a trunk of about 300 commits,
   2 release branches with a port, about 300 files (a third sidecars, small binary assets, C++ and Python, Markdown, JSON or YAML data), and the
   topic's changes as branches with merge bases where the shape says (stacks based on each other's heads).
2. **The forge and tracker state**, captured as files beside the repository in the shape the adapters will read (P4): pull requests (number,
   title with an invented tracker key, base, head, state, draft, labels, created, merged or closed), review threads on a few blocks, check runs,
   a tracker issue with links (implements, port). Stored as JSON, schema named and versioned (`mirror-state-1`).
3. **The truth**, written by the generator because it made every choice: per change its footprint (paths and the blocks it modifies), its lineage
   links, and per spike hunk its state (landed, in flight, not carved, obsolete, dropped) at the final trunk. Schema `mirror-truth-1`.
4. **The fixture**: generate at a small, fixed seed into `fixtures/mirror/` the **state and truth only** (JSON, small: tens of kilobytes) and a
   `git bundle` of the repository if it stays under about 1 MB; otherwise tests regenerate the repository from the seed. A test
   (`test/mirror.test.js`) proves regeneration is byte-identical (same seed, same bundle hash and same JSON), so the fixture never drifts from
   its generator.
5. **Validate the shape**: a script prints the topic's statistics (changes by kind and state, file overlap pairs, hotspots, stack depth) next to
   the targets above, so a reader sees the mirror matches the anonymised shape.

## Done means

* `node tools/mirror/generate.mjs --seed 1 --out <dir>` creates the repository, `mirror-state-1` and `mirror-truth-1` deterministically.
* `fixtures/mirror/` holds the small captured state and truth (and the bundle if small); `npm test` includes the regeneration check.
* The shape statistics match the targets within stated tolerances; TESTING.md lists the mirror as a proving ground for P0-P2 and G1.
* The guard passes on the tree and on every commit.

## Open for the owner

* **A separate public repository** holding the generated project with real pull requests on a public forge would let P4's forge adapter run
  against live data (G2's dogfooding). Creating it is outward-facing and the owner's call. Until then the captured forge state of step 2 is enough
  for every test (R-SRC-8: tests read captured data).

## Environment and constraints

Windows 11, Node 20+, git 2.4x or later; no dependencies. The owner's standing rules ([AGENTS.md](../../AGENTS.md)): nothing leaves the machine,
ask before downloads, installs or creating repositories, commit as you go, confirm before pushing; closed-source references by shape and numbers
only.

## Outcome

Built in `tools/mirror/` (Node, no dependencies), documented in [MIRROR.md](../MIRROR.md), tested by `test/mirror.test.js` (8 tests: determinism,
the fixture, seven more seeds, the viewer loading the canvas output, the source, the truth's consistency, the forge plan, git rebuilding the same
commit ids). Fixture: `fixtures/mirror/seed-1/` (state, truth, shape; about 235 KB, larger than the brief's guess because the truth carries every
footprint and hunk state). Beyond the brief, at the owner's direction: an **edge-case register** of real-world situations the canvas may not handle
yet (`truth.edgeCases`, MIRROR.md), whose gaps are CHANGE_VIEWS.md section 10. Modes: shape, source (JSON or stdio), local, canvas, github (a plan;
`--execute` written, not run live), fixture. **Parked by the owner**: a live sandbox on a local forge (Gitea or Forgejo), a forge-CLI stand-in,
git-native review (MIRROR.md, "Parked").


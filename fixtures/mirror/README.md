# The synthetic mirror's fixture

Output of `tools/mirror/` for seed 1 ([docs/MIRROR.md](../../docs/MIRROR.md)): an invented project, Fernhall, with a work-dominated repository and
a topic of 31 changes. Generated: never edit by hand. Refresh with `npm run fixtures:mirror`; `test/mirror.test.js` fails when these files differ
from what the generator writes.

| file | schema | holds |
|---|---|---|
| `seed-1/state.json` | `mirror-state-1` | what a forge and a tracker hold: commits (real git ids), branches, pull requests, review threads, check runs, tracker items |
| `seed-1/truth.json` | `mirror-truth-1` | what change views must derive: per change its footprint, files, lineages and flags; lineage links; each spike hunk's state; the edge-case register (`edgeCases`, each `handled`, `designed` or `gap`) and what is not modelled |
| `seed-1/shape.json` | | the statistics: file kinds, commits, changes by kind and state, hotspots, spike coverage |

The repository itself is not committed: `npm run mirror -- local --out <dir>` rebuilds it, with the commit ids in `state.json`.

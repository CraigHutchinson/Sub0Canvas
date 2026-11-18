# Golden fixtures: real producer output, frozen

Each directory is one attempt directory captured from Sub0Squad's slice 2 A/B on tier 0 (2026-11-18; Sub0Squad `docs/LAYERED_DOCUMENT.md`,
"Slice 2 on tier 0"). They are what the producer really writes, so tests here check the library and the viewer against reality, not against the
hand-made demo. Captured files: `layers/turn-N.json` (`layered-doc-1`), `canvas.json` and `canvas/<flat>.r<N>` (the revision log and texts),
`gate.txt` (the gate's verdict at the end), `agent.json` (the run's stats and switches), and `reply-N.txt` (the model's replies, in order).

| directory | from (Sub0Squad run) | build | what it shows |
|---|---|---|---|
| `adaptive6-bugs-10-run-1` | `t0-adaptive6/bugs-10/run-1/attempt-1` | 0.0.0-180-g15baab1 | 10 failing tests, the layer shown (the gate's text clips): issues over 3 turns, 10 then 2 then 1 |
| `on-bugs-6-run-2` | `t0-on/bugs-6/run-2/attempt-1` | 0.0.0-178-ga03fb86 | the layer always on (mode 1) with 6 failures: a run that changed 14 lines where 6 were needed |
| `adaptive6-fix-many-run-1` | `t0-adaptive6/fix-many/run-1/attempt-1` | 0.0.0-180-g15baab1 | 5 short failures passed through as the gate's text (adaptive mode), fixed in one turn; the layers are still written |

Checked by `test/golden.test.js` (every turn validates, items carry stable ids, anchors resolve to live blocks) and by `test_golden_replay` (the
library, fed each turn's texts and the gate's lines, gives the same blocks, identities one to one, the same history and the same issues). Re-capture from a real run when the
producer's output changes on purpose, and review the diff as part of that change. The calculator sources inside are Sub0Squad's tier 0 cases.

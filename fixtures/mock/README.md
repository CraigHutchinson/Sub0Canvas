# Mock squad attempts

One directory per scenario of [../scenarios/squad](../scenarios/README.md): the attempt the reference loop persists, in today's Sub0Squad format
(`canvas.json`, `canvas/<flat>.rN`, `layers/turn-N.json`, `reply-N.txt`, `gate.txt`, `failures.txt` while tests fail, `agent.json` as a subset) plus
`mock-trace.json` (the outcome and the next message for each reply, the focus text, the library functions called). Generated, never edited by hand:

```
mock_squad <fixtures/scenarios/squad> <fixtures/mock> --update      # the binary built by consumer/mock_squad
```

`test_consumer_contract` runs every scenario and requires these bytes to be exactly what it writes, in both directions (no missing and no stale file).
Review the diff by hand in the commit that regenerates them. MS1 regenerated them in the B0 format (item `id`, `column` and ranges, `layerMeta`, `revisionId`, `textId`, `policies`, per-turn `events` with `eventsFrom`; `events.ndjson` is the cumulative log; `g7` sends texts by content id).

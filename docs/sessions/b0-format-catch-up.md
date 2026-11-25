# Follow-up session: B0, format catch-up (what is left)

Backlog: item 2. Design: [BACKBONE.md](../BACKBONE.md) sections 7 and 8.1, slice B0. This brief is deleted when the cleanup makes the library write and read one `layered-doc-1` shape; its remaining work is the release, the Sub0Squad PR and the real-attempt re-capture.

## Goal

The producer (`layeredJson`, called by Sub0Squad's canvas loop) writes the fields the viewer understands, and a golden set of real turn files is checked on both sides. The library half is built (item `id`, anchor column and range, `layerMeta`, per-file `revisionId` and `textId`, `policies`, per-turn events with `eventsFrom`, texts by content id, `SnapshotSpec`); the mock squad's fixtures are the byte-for-byte writer check.

## Left

1. Release: the full CI tier green on the tag (v0.5.0 after the cleanup).
2. A Sub0Squad pull request: bump the pin, move to `SnapshotSpec` and the `%2F` revision file names, its full suite green; open one real attempt's `layers/` in the viewer and check that *Trace across turns* follows an issue by `id` (no title matching).
3. Re-capture `fixtures/golden/` from a real attempt with compiler diagnostics at several places (R-U1-4), through the new writer.

## Done means

A real Sub0Squad attempt opens in the viewer with items traced by `id`; the new fields are written and tested on both sides; an 8-turn snapshot of the 1,590-file subtree is a few times the size of one turn, not eight (met by the mock's G7 at 2.5 times); README's "Optional fields" table says which fields the producer writes.

## Environment and constraints

[AGENTS.md](../../AGENTS.md) and [INTEGRATION.md](../INTEGRATION.md): the Sub0Squad side is a pull request; the pin needs the tag pushed first.

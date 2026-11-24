# The query protocol: `query-1`

Status: **built** (2026-11-24, slice V1a-q): `lib/include/sub0canvas/query.hpp`, `lib/src/query.cpp`, `lib/tests/test_query.cpp`, `bench/bench_query.cpp`. This is the one
home of the request and response schema. Design: [AUTHORING.md](AUTHORING.md) section 3.1 (live views), decision D7 (protocol first, independent of transport) and D11
(one projection definition) of [reviews/2026-11-23-fabric-portal-store.md](reviews/2026-11-23-fabric-portal-store.md); requirements R-AUTH-1, R-AUTH-13, R-AUTH-14,
R-AUTH-16, R-AUTH-17 ([REQUIREMENTS.md](REQUIREMENTS.md)).

The query layer is **pure**: a JSON request in, a JSON response out, over a `QueryDocument` (files at revisions, borrowing a `BlockIndex` and a `Store`). No transport and
no I/O: the stdio and HTTP transports (D7) and the page's windowing (V1a-page) call `QueryServer::handle(request) -> response`.

## 1. Envelope

Request: one JSON object. `op` is required; `schema` (if present) must be `"query-1"`; `id` (a string or a whole number) is echoed.

Success: `{"schema":"query-1","ok":true,"op":"tree","id":..,"at":{..}, ...payload..., "unknownFields":[..]}`.

Error: `{"schema":"query-1","ok":false,"op":"tree","id":..,"error":{"code":"..","field":"..","message":".."}}` (`op` and `id` only when the request got that far).
Codes: `bad-request`, `unsupported-schema`, `unknown-op`, `unknown-field` (policy `reject`), `not-found`, `not-a-folder`, `bad-cursor`, `not-available` (the document has no
index or store), `gone` (events no longer held), `too-large` (request over `maxRequestBytes`). Never an exception or an empty reply.

`at` is the position the answer was computed at: `generation` (document revisions recorded), `indexEvents` (block-identity events, when an index is attached),
`storeHead` (lamport of the last store event) and `storeEvents` (events folded). A client compares `at` between answers to know what it holds is current; the event stream
(`events`, and later SSE) says what changed.

## 2. Operations

Every list takes `limit` (default and maximum per operation are policy, section 5; a larger limit is clamped and the response says `"clamped":true`) and `cursor` (opaque:
use the `next` of the previous page; `next` is `null` on the last page). Every list response carries `entries` and `next`.

| op | request | answer |
|---|---|---|
| `info` | | `ops`, `policy`, `files`, `index`/`store` present, `layerCount` |
| `tree` | `path` (folder, `""` the root) | `path`, `folders`, `files` (direct), `filesBelow`; `entries`: folders first then files, by name. Folder `{name,kind:"folder",folders,files,filesBelow}`; file `{name,kind:"file",revision,revisionId,textId,bytes,lines}`. No text, no block. Cursor `d:<name>` or `f:<name>` (the last name sent: a page costs its entries, not its offset) |
| `file` | `path` [+ `revision`], or `textId`; `offset`, `maxBytes`, `have[]` | `path`/`revision`/`revisionId`/`textId`/`lines` (by path), `bytes`, then `offset`, `length`, `text`, `next` (a byte offset, never inside a character). `have` lists content ids the client holds: a text it holds is answered `"have":true` and not sent (R-AUTH-14). Invalid UTF-8 is `"binary":true`, not text. A text over `maxTextBytes` is read in chunks |
| `revisions` | `path` | `total`; `entries` `{revision,revisionId,textId,bytes,lines}` ascending; cursor the last revision number |
| `blocks` | `path` (required: one file) | `total`; `entries` the file's blocks `{id,path,name,signature,first,last}` in file order; cursor an offset |
| `layers` | | `total`; `entries` `{name,items[,label,description,producer]}` |
| `layer` | `layer`, `query{kinds,states,tags,sources,text}`, `count` | `total` (the layer's size), `matched` (only when `count` is true and the layer is within `maxScan`), `scanned`; `entries` the bundle's item `{id,number,kind,title,detail,anchors}` plus the store's `source`, `state`, `tags`. `number` is the item's place in its whole layer (what `READ ISSUE n` names), whatever the filter. Private tags (`hiddenTagPrefixes`) hide an item unless `query.tags` names the tag exactly (R-BB-10) |
| `events` | `source` (`index` default, or `store`), `from` | `from`, `end`, `entries`, `next` (**always a position**: a reader that is caught up keeps it and asks again). `index`: the bundle's `events`; `store`: each event as `eventJson` writes it. A position before the events held (a store restored from a checkpoint) is `gone` |

**Cursors are places, not snapshots.** The `layer` cursor is the number of the last item examined, `blocks` an offset, `tree` the last name, `revisions` the last number:
items appended between two pages neither repeat nor skip one, and a state change cannot move a page. The set a client pages through is what existed when each page was cut;
`at` tells it whether anything moved.

## 3. Bounds (R-AUTH-1)

A response is bounded three ways, all policy: a page of at most `limit` entries; a page that stops adding entries once the response reaches `maxResponseBytes` (the first
entry is always added, so one entry over the cap, such as an item with a huge `detail`, goes alone: the bound is the cap plus one entry); and for `layer`, at most `maxScan`
items **examined**, so a filter that matches nothing cannot walk a million items to say so (the page ends early with a cursor and `scanned`). Work is proportional to the
answer: `QueryStats::visited` counts the nodes a request touched (path steps and entries for `tree`; the file's blocks for `blocks`; items examined for `layer`; events
returned for `events`) and is a metric (section 9). Asking for one folder never visits another.

## 4. Texts by content id (R-AUTH-14)

`textId` is the git blob id of the text (`contentId`): two paths with the same content share one id, and an id never changes meaning. A client keeps what it has seen, sends
`have` with a `file` request, and is sent only texts it lacks. `tree` and `revisions` carry `textId`, so the client knows what it holds before asking.

## 5. The policy (data)

`QueryPolicy`, from JSON (`parseQueryPolicy`) and reported by `info`:

```json
{"tree":{"default":500,"max":2000}, "blocks":{"default":200,"max":2000}, "layers":{"default":100,"max":1000}, "layer":{"default":50,"max":500},
 "events":{"default":200,"max":2000}, "revisions":{"default":50,"max":500}, "maxResponseBytes":1048576, "maxTextBytes":262144, "maxRequestBytes":65536,
 "maxHave":1000, "maxScan":100000, "unknownFields":"echo", "hiddenTagPrefixes":["private/"]}
```

Every member is optional; an unknown member, a zero size or a default above its maximum is an error naming the member.

## 6. Unknown members

`unknownFields` decides what a request member this version does not know gets: `ignore` (answered as if absent, byte for byte), `echo` (the default: answered, and the names
listed, at most 16, each clipped to 64 characters, in the order they came; a member of `query` is named `query.<name>`), `reject` (error `unknown-field` naming the first).
`echo` is the forward-compatible default: a newer client can see what an older server dropped.

## 7. One projection (D11, R-AUTH-17)

`lib/src/projection.hpp` is the one definition of how a block, a block event, a layer item (its members) and a file reference (`revision`, `revisionId`, `textId`) become
JSON. `layeredJson` (the bundle, `layered-doc-1`) and the query answers both write through it, so a field added there reaches both and the bundle is "every query answered at
once". The test proves it on every turn of every mock attempt (`fixtures/mock`, 32 turns): the document is rebuilt from the snapshot, then the tree is walked and every file
and text read by content id, every file's blocks, every layer's items and the events from `eventsFrom` read back **through the queries alone**, and each equals the
snapshot's, member by member (the answer adds only the store's `source`, `state` and `tags` to an item). Not yet served: the bundle's `retired` list (the block-identity
events carry the same facts).

## 8. Limits of this slice

* `layer` filters restate the store's own rule (`Store::items`) over the request's lists, so a page costs its scan; a test checks the two agree on a battery of queries. A
  store-side index by layer, kind, state and tag (the store files were out of scope) would make a selective filter cost its matches.
* The document holds the texts it is given, by content id, for every revision: a server over a real repository would back `text` by git (the id is git's own blob id).
* A file removed or renamed is not modelled: `addRevision` only adds.
* Single-threaded: the process that owns the document serialises writes and reads.

## 9. The K0 budget (docs/K0.md)

Expanding a 1,588-file folder in 50 ms needs a listing the page can parse and render in that time. Measured (`query tree flat 1588 files`, deterministic): 142 bytes per file,
225,657 bytes for the whole folder in one page of 2,000, or four pages of at most 500 (71,172 bytes the first, 226,172 over all four); 1,589 nodes visited. The same 1,590 files
in 159 folders of ten: 11,765 bytes for the folder of 159 folders (160 nodes), 1,552 bytes for one folder of ten files (12 nodes). The page that windows a long list asks
for the rows on screen: a layer page of 50 costs 50 nodes and about 14 KB at item 4,000 as at item 0. Timing rows (local) are in `perf/baselines`. The client's cost to parse
and render a page is V1a-page's to measure against K0.

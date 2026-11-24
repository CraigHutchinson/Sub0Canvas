#pragma once
// The projection of the document's data into JSON: ONE definition (docs/reviews/2026-11-23-fabric-portal-store.md D11, docs/QUERY.md section 7). The snapshot
// (`layeredJson`, the bundle, `layered-doc-1`) and the live query answers (`query.hpp`, `query-1`) both write a block, a block event, an anchor, a layer item and
// a file's revision reference through these functions, so the bundle is "every query answered at once" by construction and a field added here reaches both.
// Internal: simdjson stays out of public headers.
#include "json_util.hpp"
#include "sub0canvas/doc_layers.hpp"

#include <string_view>

namespace sub0canvas::projection {

/// A block: `{id, path, name, signature, first, last}`. Never its token sets.
void writeBlock(json::Writer& w, const Block& b);
/// One line of the index's history: `{turn, carry, id, path, name, detail}`.
void writeBlockEvent(json::Writer& w, const BlockEvent& e);
/// The same from its parts (views, so a compact log writes without building a `BlockEvent` per line); `carry` is the carry's name.
void writeBlockEventParts(json::Writer& w, int turn, std::string_view carry, std::string_view id, std::string_view path, std::string_view name, std::string_view detail);
/// The members of a layer item, into the caller's open object: `id` (the key), `number` (its place in its layer), `kind`, `title`, `detail`, `anchors[]`. A caller may append its own (state, tags).
void writeLayerItemMembers(json::Writer& w, const LayerItem& item, int number);
/// The three members that name a file at a revision, written into the caller's open object: `revision`, `revisionId`, `textId`.
void writeFileRef(json::Writer& w, int revision, std::string_view revisionId, std::string_view textId);

}  // namespace sub0canvas::projection

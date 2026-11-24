#pragma once
/// @file anchor.hpp
/// Source anchors (docs/BACKBONE.md sections 3.2 and 5.1, R-SRC-1, R-SRC-2, R-AUTH-3): where a comment, a diagnostic or a note points IN SOURCE, written so it can be
/// found again after the source changed. An anchor is a W3C-style text-quote selector (the quoted text with a little context before and after) bound to the
/// revision it was made at, and, when the producer or the core knows it, to a block identity. It never holds a line number as its only address.
///
/// Named `SourceAnchor` because `Anchor` (doc_layers.hpp) is the layer item's resolved place; the resolver (resolver.hpp) turns the one into the other's fields.
/// Pure data and pure functions, no I/O.

#include "sub0canvas/doc_layers.hpp"

#include <string>
#include <string_view>

namespace sub0canvas {

/// A place in source as a producer observed it. Every field but `path` and `revision` is optional; the more is given, the more certain a resolution can be.
struct SourceAnchor
{
    std::string path;               ///< the file at `revision`
    std::string revision;           ///< opaque id of the revision `path`, the lines and the quote belong to (R-SRC-5)
    std::string quote;              ///< the anchored text, byte for byte (may span lines; empty: the whole block is anchored)
    std::string prefix, suffix;     ///< the text just before and after the quote (the context pair): tells equal quotes apart
    int line = 0, column = 0;       ///< where the quote starts at `revision` (1-based; 0: not known). A hint, never an authority
    int endLine = 0, endColumn = 0; ///< where it ends: the line, and the column just PAST its last character (0: not a range)
    std::string blockId;            ///< the block that held it at `revision`, in the identity space of the document it is resolved in; empty: derive it from `line`
    std::string textId;             ///< optional content id of the whole file text at `revision`: when the target text has the same id nothing changed and the
                                    ///< anchor resolves by position alone (duplicates included)
};

/// Build an anchor for the text of `text` from (`line`, `column`) to (`endLine`, `endColumn`) (the end column is just past the last character), taking up to
/// `contextChars` bytes of context each side and, when `index` is given, the id of the block holding `line`. Out-of-range positions are clamped. The result's
/// `line`/`column`/`endLine`/`endColumn` are as given after clamping.
[[nodiscard]] SourceAnchor makeSourceAnchor(const std::string& path, const std::string& revision, std::string_view text, int line, int column, int endLine, int endColumn,
                                            const BlockIndex* index = nullptr, std::size_t contextChars = 24);

/// Anchor the whole of line `line` (its text without the newline), the common case of a compiler diagnostic or a review comment on one line.
[[nodiscard]] SourceAnchor makeLineAnchor(const std::string& path, const std::string& revision, std::string_view text, int line, const BlockIndex* index = nullptr,
                                          std::size_t contextChars = 24);

}  // namespace sub0canvas

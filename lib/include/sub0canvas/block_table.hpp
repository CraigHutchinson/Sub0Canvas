#pragma once
/// @file block_table.hpp
/// Binary projection of live blocks from a scoped Git replay. The table is a read model of one state.

#include "sub0canvas/revision_index.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sub0canvas {

/// One row borrowed from the bytes passed to readBlockTable.
struct BlockRow
{
    std::string_view id, path, name, signature;
    int first = 0, last = 0;
};

/// A read model whose strings remain valid while its input buffer lives and is unchanged.
struct BlockTable
{
    std::string_view requestedRevision, indexedRevision;
    int ordinal = 0;
    bool historyCut = false;
    std::vector<std::pair<std::string_view, std::string_view>> unknown;  ///< revision and path, in replay order
    std::vector<BlockRow> blocks;
};

/// Write the versioned, little-endian block table. An error means a field exceeds the format limits.
[[nodiscard]] std::expected<std::string, std::string> writeBlockTable(std::string_view requestedRevision, const Replayed& state);

/// Read a complete block table, rejecting malformed or unsupported data. Rows borrow the input buffer.
[[nodiscard]] std::expected<BlockTable, std::string> readBlockTable(std::string_view bytes);

}  // namespace sub0canvas

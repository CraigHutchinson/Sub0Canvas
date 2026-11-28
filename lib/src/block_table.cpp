#include "sub0canvas/block_table.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace sub0canvas {
namespace {

constexpr std::string_view kMagic = "S0BLOCK1";

void putInt(std::string& out, std::size_t value)
{
    if (value > std::numeric_limits<std::uint32_t>::max()) throw std::length_error("block table field exceeds 32 bits");
    for (unsigned shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<char>((value >> shift) & 0xff));
}

void putText(std::string& out, std::string_view value)
{
    putInt(out, value.size());
    out.append(value);
}

class Reader
{
public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {}

    [[nodiscard]] std::size_t remaining() const { return bytes_.size(); }

    std::uint32_t number()
    {
        const auto bytes = take(4);
        std::uint32_t n = 0;
        for (unsigned i = 0; i < 4; ++i)
            n |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << (8 * i);
        return n;
    }

    int coordinate()
    {
        const auto n = number();
        if (n > static_cast<unsigned>(std::numeric_limits<int>::max())) throw std::runtime_error("invalid block table coordinate");
        return static_cast<int>(n);
    }

    std::string_view text() { return take(number()); }

    std::string_view take(std::size_t size)
    {
        if (size > bytes_.size()) throw std::runtime_error("truncated block table");
        const auto value = bytes_.substr(0, size);
        bytes_.remove_prefix(size);
        return value;
    }

private:
    std::string_view bytes_;
};

}  // namespace

std::expected<std::string, std::string> writeBlockTable(std::string_view requestedRevision, const Replayed& state)
{
    try {
        std::string out(kMagic);
        putText(out, requestedRevision);
        putText(out, state.revision);
        putInt(out, state.ordinal);
        putInt(out, state.historyCut ? 1 : 0);
        putInt(out, state.unknown.size());
        for (const auto& entry : state.unknown) {
            putText(out, entry.revision);
            putText(out, entry.path);
        }
        const auto blocks = state.index.all();
        putInt(out, blocks.size());
        std::string_view lastPath;
        for (const Block* block : blocks) {
            putText(out, block->id);
            putText(out, block->path == lastPath ? std::string_view{} : block->path);
            lastPath = block->path;
            putText(out, block->name);
            putText(out, block->signature);
            putInt(out, block->first);
            putInt(out, block->last);
        }
        return out;
    } catch (const std::length_error& err) {
        return std::unexpected(err.what());
    }
}

std::expected<BlockTable, std::string> readBlockTable(std::string_view bytes)
{
    try {
        Reader in(bytes);
        if (in.take(kMagic.size()) != kMagic) return std::unexpected("unsupported block table version");
        BlockTable table;
        table.requestedRevision = in.text();
        table.indexedRevision = in.text();
        table.ordinal = in.coordinate();
        const auto flags = in.number();
        if (flags > 1) return std::unexpected("invalid block table flags");
        table.historyCut = flags != 0;
        const auto unknown = in.number();
        if (unknown > in.remaining() / 8) return std::unexpected("invalid unknown object count");
        table.unknown.reserve(unknown);
        for (std::uint32_t i = 0; i < unknown; ++i) {
            const auto revision = in.text();
            table.unknown.emplace_back(revision, in.text());
        }
        const auto blocks = in.number();
        if (blocks > in.remaining() / 24) return std::unexpected("invalid block row count");
        table.blocks.reserve(blocks);
        std::string_view lastPath;
        for (std::uint32_t i = 0; i < blocks; ++i) {
            BlockRow row{in.text(), in.text(), in.text(), in.text(), in.coordinate(), in.coordinate()};
            if (!row.path.empty()) lastPath = row.path;
            if (lastPath.empty()) return std::unexpected("missing block path");
            row.path = lastPath;
            if (row.first < 1 || row.last < row.first) return std::unexpected("invalid block line range");
            table.blocks.push_back(row);
        }
        if (in.remaining() != 0) return std::unexpected("trailing bytes in block table");
        return table;
    } catch (const std::runtime_error& err) {
        return std::unexpected(err.what());
    }
}

}  // namespace sub0canvas

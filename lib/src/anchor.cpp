#include "sub0canvas/anchor.hpp"

#include <algorithm>
#include <vector>

namespace sub0canvas {

namespace {

std::vector<std::size_t> lineStartsOf(std::string_view text)
{
    std::vector<std::size_t> starts{0};
    for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\n' && i + 1 < text.size()) starts.push_back(i + 1);
    return starts;
}

/// The byte offset of (line, column), clamped into the text; a column past the line's end stops at the line's end (before its newline).
std::size_t offsetOf(std::string_view text, const std::vector<std::size_t>& starts, int line, int column)
{
    const std::size_t l = static_cast<std::size_t>(std::clamp(line, 1, static_cast<int>(starts.size()))) - 1;
    const std::size_t begin = starts[l];
    const std::size_t end = l + 1 < starts.size() ? starts[l + 1] - 1 : text.size() - (!text.empty() && text.back() == '\n' ? 1 : 0);
    const std::size_t want = begin + static_cast<std::size_t>(std::max(column, 1) - 1);
    return std::min(want, end);
}

}  // namespace

SourceAnchor makeSourceAnchor(const std::string& path, const std::string& revision, std::string_view text, int line, int column, int endLine, int endColumn,
                              const BlockIndex* index, std::size_t contextChars)
{
    SourceAnchor a;
    a.path = path;
    a.revision = revision;
    const auto starts = lineStartsOf(text);
    const std::size_t from = offsetOf(text, starts, line, column);
    const std::size_t to = std::max(from, offsetOf(text, starts, endLine, endColumn));
    a.quote = std::string(text.substr(from, to - from));
    // The context stays inside the block that holds the quote: what lies beyond the block's edges (its neighbours) changes when other blocks move, and a block moved
    // whole must keep its anchors' context.
    std::size_t floor = 0, ceiling = text.size();
    if (index) {
        if (const Block* b = index->at(path, std::clamp(line, 1, static_cast<int>(starts.size())))) {
            floor = starts[static_cast<std::size_t>(std::clamp(b->first, 1, static_cast<int>(starts.size()))) - 1];
            const std::size_t lastLine = static_cast<std::size_t>(std::clamp(b->last, 1, static_cast<int>(starts.size())));
            ceiling = lastLine < starts.size() ? starts[lastLine] : text.size();
        }
    }
    floor = std::min(floor, from);
    ceiling = std::max(ceiling, to);
    const std::size_t before = std::min(contextChars, from - floor), after = std::min(contextChars, ceiling - to);
    a.prefix = std::string(text.substr(from - before, before));
    a.suffix = std::string(text.substr(to, after));
    a.line = std::clamp(line, 1, static_cast<int>(starts.size()));
    a.column = std::max(column, 1);
    a.endLine = std::clamp(endLine, a.line, static_cast<int>(starts.size()));
    a.endColumn = std::max(endColumn, 1);
    if (index) {
        if (const Block* b = index->at(path, a.line)) a.blockId = b->id;
    }
    return a;
}

SourceAnchor makeLineAnchor(const std::string& path, const std::string& revision, std::string_view text, int line, const BlockIndex* index, std::size_t contextChars)
{
    const auto starts = lineStartsOf(text);
    const int l = std::clamp(line, 1, static_cast<int>(starts.size()));
    const std::size_t begin = starts[static_cast<std::size_t>(l) - 1];
    std::size_t end = static_cast<std::size_t>(l) < starts.size() ? starts[static_cast<std::size_t>(l)] - 1 : text.size() - (!text.empty() && text.back() == '\n' ? 1 : 0);
    if (end > begin && text[end - 1] == '\r') --end;
    return makeSourceAnchor(path, revision, text, l, 1, l, static_cast<int>(end - begin) + 1, index, contextChars);
}

}  // namespace sub0canvas

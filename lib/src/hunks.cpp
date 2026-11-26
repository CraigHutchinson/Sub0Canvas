#include "sub0canvas/hunks.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <unordered_map>

namespace sub0canvas {

namespace {

/// Reads "start[,len]" at `s`, advancing it; the length defaults to 1 as git writes it.
bool readRange(std::string_view& s, int& start, int& len)
{
    auto number = [&](int& out) {
        const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
        if (r.ec != std::errc{}) return false;
        s.remove_prefix(static_cast<std::size_t>(r.ptr - s.data()));
        return true;
    };
    if (!number(start)) return false;
    len = 1;
    if (!s.empty() && s.front() == ',') {
        s.remove_prefix(1);
        if (!number(len)) return false;
    }
    return true;
}

std::optional<DiffHunk> parseHeader(std::string_view line)
{
    if (!line.starts_with("@@ -")) return std::nullopt;
    line.remove_prefix(4);
    DiffHunk h;
    if (!readRange(line, h.oldStart, h.oldLen) || !line.starts_with(" +")) return std::nullopt;
    line.remove_prefix(2);
    if (!readRange(line, h.newStart, h.newLen)) return std::nullopt;
    return h;
}

/// The span of one side of a hunk inside a block, relative to it; nullopt when that side does not touch the block.
struct Side { int start = 0, len = 0; };

std::optional<Side> clip(int start, int len, const BlockSpan& b)
{
    if (len > 0) {
        const int from = std::max(start, b.first), to = std::min(start + len - 1, b.last);
        if (from > to) return std::nullopt;
        return Side{from - b.first + 1, to - from + 1};
    }
    if (start >= b.first && start < b.last) return Side{start - b.first + 1, 0};   // between two lines of the block
    return std::nullopt;
}

int relativePoint(int start, const BlockSpan& b) { return std::clamp(start - b.first + 1, 0, b.last - b.first + 1); }

}  // namespace

std::map<std::string, std::vector<DiffHunk>> parseHunks(std::string_view patch)
{
    std::map<std::string, std::vector<DiffHunk>> out;
    std::vector<DiffHunk>* current = nullptr;
    for (std::size_t at = 0; at < patch.size();) {
        auto eol = patch.find('\n', at);
        if (eol == std::string_view::npos) eol = patch.size();
        std::string_view line = patch.substr(at, eol - at);
        at = eol + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.starts_with("diff --git ")) {
            // `a/P b/P` with equal paths (no renames): P is the half left after the two prefixes and the separator.
            std::string_view rest = line.substr(11);
            current = nullptr;
            if (!rest.starts_with("a/") || rest.size() < 7 || (rest.size() - 5) % 2 != 0) continue;   // quoted paths and odd shapes are skipped
            const std::string_view path = rest.substr(2, (rest.size() - 5) / 2);
            if (rest.substr(2 + path.size()) != std::string{" b/"}.append(path)) continue;
            current = &out[std::string{path}];
        } else if (current != nullptr && line.starts_with("@@ ")) {
            if (const auto h = parseHeader(line)) current->push_back(*h);
        }
    }
    return out;
}

HunkMapping mapHunks(const std::vector<DiffHunk>& hunks, const std::vector<BlockSpan>& before, const std::vector<BlockSpan>& after)
{
    HunkMapping out;
    out.total = hunks.size();
    std::unordered_map<std::string_view, const BlockSpan*> old;
    for (const BlockSpan& b : before) old.emplace(b.id, &b);
    for (const DiffHunk& h : hunks) {
        bool placed = false;
        for (const BlockSpan& now : after) {
            const auto was = old.find(now.id);
            if (was == old.end()) continue;
            const BlockSpan& then = *was->second;
            const auto o = clip(h.oldStart, h.oldLen, then), n = clip(h.newStart, h.newLen, now);
            if (!o && !n) continue;
            // A side that does not touch the block inside it still reports where the change sits, as a zero-length point.
            const HunkSpan span{o ? o->start : relativePoint(h.oldStart, then), o ? o->len : 0, n ? n->start : relativePoint(h.newStart, now), n ? n->len : 0};
            out.byBlock[now.id].push_back(span);
            placed = true;
        }
        if (!placed) ++out.outside;
    }
    return out;
}

}  // namespace sub0canvas

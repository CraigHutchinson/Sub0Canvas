// Pure parsers of git plumbing output (git_plumbing.hpp). No I/O: the app runs git, tests read captured files.
#include "sub0canvas/git_plumbing.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <utility>

namespace sub0canvas::gitplumbing {

namespace {

using Error = std::unexpected<std::string>;

constexpr char kRecord = '\x01';
constexpr char kField = '\x02';

Error failAt(std::string_view what, std::size_t offset) { return Error(std::format("{} at byte {}", what, offset)); }

/// The text up to the next `stop` from `at`, consuming the stop; nullopt (and `at` unchanged) when there is none.
std::optional<std::string_view> takeUntil(std::string_view raw, std::size_t& at, char stop)
{
    const auto end = raw.find(stop, at);
    if (end == std::string_view::npos) return std::nullopt;
    const auto field = raw.substr(at, end - at);
    at = end + 1;
    return field;
}

/// The change a `--name-status` status token names: A, M, D, T, or R and C with a similarity score. Unmerged and broken entries are not a change of text.
std::optional<std::pair<ChangeKind, int>> statusOf(std::string_view token)
{
    if (token.empty()) return std::nullopt;
    int score = 0;
    if (token.size() > 1) {
        const auto [end, ec] = std::from_chars(token.data() + 1, token.data() + token.size(), score);
        if (ec != std::errc{} || end != token.data() + token.size()) return std::nullopt;
    }
    switch (token[0]) {
        case 'A': return std::pair{ChangeKind::added, 0};
        case 'M': return std::pair{ChangeKind::modified, 0};
        case 'D': return std::pair{ChangeKind::deleted, 0};
        case 'T': return std::pair{ChangeKind::typeChanged, 0};
        case 'R': return std::pair{ChangeKind::renamed, score};
        case 'C': return std::pair{ChangeKind::copied, score};
        default: return std::nullopt;
    }
}

/// Entries `status NUL path NUL [path NUL]` from `at` until `stop` (a record separator) or the end.
std::expected<std::vector<PathChange>, std::string> parseEntries(std::string_view raw, std::size_t& at, bool stopAtRecord)
{
    std::vector<PathChange> out;
    while (at < raw.size()) {
        if (raw[at] == '\n' || raw[at] == '\0') { ++at; continue; }   // the separator after a header, and an empty entry list
        if (stopAtRecord && raw[at] == kRecord) break;
        const std::size_t start = at;
        const auto token = takeUntil(raw, at, '\0');
        if (!token) return failAt("a status without its NUL", start);
        const auto status = statusOf(*token);
        if (!status) return failAt(std::format("an unsupported status '{}'", *token), start);
        PathChange change;
        change.kind = status->first;
        change.similarity = status->second;
        const auto first = takeUntil(raw, at, '\0');
        if (!first) return failAt("a path without its NUL", at);
        if (change.kind == ChangeKind::renamed || change.kind == ChangeKind::copied) {
            const auto second = takeUntil(raw, at, '\0');
            if (!second) return failAt("a rename or copy without its second path", at);
            change.from = std::string(*first);
            change.path = std::string(*second);
        } else {
            change.path = std::string(*first);
        }
        out.push_back(std::move(change));
    }
    return out;
}

std::vector<std::string> splitSpaces(std::string_view s)
{
    std::vector<std::string> out;
    for (std::size_t at = 0; at < s.size();) {
        const auto end = std::min(s.find(' ', at), s.size());
        if (end > at) out.emplace_back(s.substr(at, end - at));
        at = end + 1;
    }
    return out;
}

}  // namespace

std::expected<std::vector<Commit>, std::string> parseLog(std::string_view raw)
{
    std::vector<Commit> commits;
    std::size_t at = 0;
    while (at < raw.size() && (raw[at] == '\n' || raw[at] == '\0')) ++at;
    while (at < raw.size()) {
        if (raw[at] != kRecord) return failAt("a commit record must start with 0x01", at);
        ++at;
        Commit commit;
        std::string_view fields[4];
        for (auto& f : fields) {
            const auto v = takeUntil(raw, at, kField);
            if (!v) return failAt("a commit header with fewer fields than the format", at);
            f = *v;
        }
        const auto subject = takeUntil(raw, at, '\0');
        if (!subject) return failAt("a commit subject without its NUL", at);
        commit.info.id = std::string(fields[0]);
        commit.info.parents = splitSpaces(fields[1]);
        commit.info.author = std::string(fields[2]);
        commit.info.time = std::string(fields[3]);
        commit.info.message = std::string(*subject);
        if (commit.info.id.empty()) return failAt("a commit with no id", at);
        auto changes = parseEntries(raw, at, true);
        if (!changes) return std::unexpected(changes.error());
        commit.changes = std::move(*changes);
        commits.push_back(std::move(commit));
    }
    return commits;
}

std::expected<std::vector<PathChange>, std::string> parseNameStatus(std::string_view raw)
{
    std::size_t at = 0;
    return parseEntries(raw, at, false);
}

std::expected<std::vector<TreeEntry>, std::string> parseTree(std::string_view raw)
{
    std::vector<TreeEntry> out;
    std::size_t at = 0;
    while (at < raw.size()) {
        const std::size_t start = at;
        const auto record = takeUntil(raw, at, '\0');
        if (!record) return failAt("a tree entry without its NUL", start);
        const auto tab = record->find('\t');
        if (tab == std::string_view::npos) return failAt("a tree entry without a tab", start);
        const auto meta = splitSpaces(record->substr(0, tab));
        if (meta.size() != 3) return failAt("a tree entry that is not `mode type oid`", start);
        out.push_back({meta[0], meta[1], meta[2], std::string(record->substr(tab + 1))});
    }
    return out;
}

std::vector<std::string> documentPaths(const std::vector<TreeEntry>& entries)
{
    std::vector<std::string> out;
    for (const auto& e : entries)
        if (e.type == "blob" && (e.mode == "100644" || e.mode == "100755")) out.push_back(e.path);
    std::ranges::sort(out);
    return out;
}

std::expected<std::vector<FileText>, std::string> parseCatFileBatch(std::string_view raw, std::size_t expected)
{
    std::vector<FileText> out;
    out.reserve(expected);
    std::size_t at = 0;
    for (std::size_t n = 0; n < expected; ++n) {
        const std::size_t start = at;
        const auto header = takeUntil(raw, at, '\n');
        if (!header) return failAt(std::format("{} answers where {} were expected", n, expected), start);
        if (header->ends_with(" missing") || header->ends_with(" ambiguous")) { out.push_back(FileText::unknown()); continue; }
        const auto sizeAt = header->rfind(' ');
        const auto typeAt = sizeAt == std::string_view::npos || sizeAt == 0 ? std::string_view::npos : header->rfind(' ', sizeAt - 1);
        if (typeAt == std::string_view::npos) return failAt("a header that is not `oid type size`", start);
        const auto type = header->substr(typeAt + 1, sizeAt - typeAt - 1);
        const auto digits = header->substr(sizeAt + 1);
        std::size_t size = 0;
        const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), size);
        if (ec != std::errc{} || end != digits.data() + digits.size()) return failAt("a size that is not a number", start);
        if (size > raw.size() - at) return failAt("an object cut short", at);
        if (at + size >= raw.size() || raw[at + size] != '\n') return failAt("an object not followed by its line feed", at + size);
        out.push_back(type == "blob" ? FileText::known(std::string(raw.substr(at, size))) : FileText::missing());
        at += size + 1;
    }
    if (at != raw.size()) return failAt(std::format("more answers than the {} requested", expected), at);
    return out;
}

std::string catFileRequests(const std::vector<FileRef>& refs)
{
    std::string out;
    for (const auto& r : refs) {
        out += r.revision;
        out += ':';
        out += r.path;
        out += '\n';
    }
    return out;
}

}  // namespace sub0canvas::gitplumbing

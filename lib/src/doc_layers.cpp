// The layered document, slice 1 (docs/LAYERED_DOCUMENT.md): the block index that gives every function a stable id, and the issues layer bound to it.
#include "sub0canvas/doc_layers.hpp"

#include "json_util.hpp"
#include "projection.hpp"
#include "scan.hpp"
#include "sub0canvas/content_id.hpp"
#include "sub0canvas/outline.hpp"
#include "sub0canvas/store.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace sub0canvas {

namespace {

using scan::splitLines;

constexpr double kRenameJaccard = 0.5;    ///< body similarity at which an unmatched block is the same block renamed
constexpr double kReviveJaccard = 0.8;    ///< body similarity at which a new block brings back a retired id (a block written again)
constexpr double kMergeContainment = 0.6; ///< share of a retired block's tokens found in one live block for it to count as merged into that block
constexpr std::size_t kMinTokensToCompare = 3;   ///< below this a body is too small for similarity to mean anything

std::string leaf(std::string_view name)
{
    const auto c = name.rfind(':');
    return std::string{c == std::string_view::npos ? name : name.substr(c + 1)};
}

std::string lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const char* carryName(Carry c)
{
    switch (c) {
    case Carry::kept: return "kept";
    case Carry::resigned: return "resigned";
    case Carry::renamed: return "renamed";
    case Carry::revived: return "revived";
    case Carry::added: return "added";
    case Carry::retired: return "retired";
    case Carry::merged: return "merged";
    }
    return "?";
}

const char* confidenceName(Anchor::Confidence c)
{
    return c == Anchor::Confidence::exact ? "exact" : c == Anchor::Confidence::suspect ? "suspect" : "none";
}

/// Numbered lines `from..to` of `text` (clamped), at most `maxLines`.
std::string numbered(const std::string& text, int from, int to, int maxLines = 80)
{
    const auto lines = splitLines(text);
    from = std::max(1, from);
    to = std::min(static_cast<int>(lines.size()), to);
    std::string out;
    for (int k = from; k <= to && k < from + maxLines; ++k) {
        out += std::format("{:4}| ", k) + lines[static_cast<std::size_t>(k) - 1] + "\n";
    }
    return out;
}

/// How an anchor reads in a summary: "term() (lines 40-52)", "src/a.cpp:12", or that its block is gone.
std::string place(const Anchor& a, const BlockIndex& index)
{
    if (!a.blockId.empty()) {
        if (const Block* b = index.resolve(a.blockId)) return leaf(b->name) + "() (lines " + std::to_string(b->first) + "-" + std::to_string(b->last) + ")";
        return "a function that no longer exists";
    }
    if (!a.path.empty() && a.line > 0) return a.path + ":" + std::to_string(a.line);
    return {};
}

}  // namespace

// ---- the block index ----------------------------------------------------------------------------------------------------------------------------------
//
// S1 (docs/SCALE.md sections 3-4, docs/BACKBONE.md): the index keeps a compact record per block and nothing else per block. What it does not hold, it derives again
// from text it reads back through a `TextSource`; what it must find quickly, it finds through an index:
//   * ids: a flat table, id -> (file, position): `resolve` is O(1), `at` walks one file's blocks;
//   * strings: interned in a pool the copies of an index share, so a block holds three views and an int pair, never a string of its own (the id apart: 13 characters,
//     inside the small-string buffer);
//   * the retired: a revive probe consults a (signature, name) table and a prefix-filter posting index over token ranks, so only blocks that can reach the 0.8 threshold
//     are compared, and the comparison is the same exact rule as before (the filter has no false negatives, see `reviveProbe`);
//   * merge candidates: every live block carries a 128-bit token filter, an exact pre-test with no false negatives (a block holding 60% of a body's tokens passes), after
//     which the few survivors are confirmed from their text. The scan over live blocks remains (cheap: a few bit tests per block); S2's inverted index removes it.
// Identity decisions are those of the set-based implementation: the same rules, the same tie orders (by id, and by path then position), tested by test_doc_layers,
// test_golden_replay, test_consumer_contract and the corpus truth.

namespace {

using Hash = std::uint64_t;

constexpr Hash kMul = 0x9FB21C651E98DF25ULL;

[[nodiscard]] constexpr Hash mix64(Hash x)
{
    x ^= x >> 32;
    x *= 0xD6E8FEB86659FD93ULL;
    x ^= x >> 32;
    x *= 0xD6E8FEB86659FD93ULL;
    x ^= x >> 32;
    return x;
}

/// A 64-bit hash of bytes that is the same on every machine and run: little-endian reads composed by hand, no `std::hash` (A16: ids derived from it must agree
/// across clones).
[[nodiscard]] Hash hashBytes(const char* p, std::size_t n, Hash seed = 0)
{
    Hash h = seed ^ (static_cast<Hash>(n) * 0x9E3779B97F4A7C15ULL);
    const auto* u = reinterpret_cast<const unsigned char*>(p);
    while (n >= 8) {
        const Hash v = static_cast<Hash>(u[0]) | static_cast<Hash>(u[1]) << 8 | static_cast<Hash>(u[2]) << 16 | static_cast<Hash>(u[3]) << 24 | static_cast<Hash>(u[4]) << 32 |
                       static_cast<Hash>(u[5]) << 40 | static_cast<Hash>(u[6]) << 48 | static_cast<Hash>(u[7]) << 56;
        h = (h ^ v) * kMul;
        h ^= h >> 29;
        u += 8;
        n -= 8;
    }
    if (n > 0) {
        Hash v = 0;
        for (std::size_t i = 0; i < n; ++i) v |= static_cast<Hash>(u[i]) << (8 * i);
        h = (h ^ v) * kMul;
        h ^= h >> 29;
    }
    return mix64(h);
}

[[nodiscard]] Hash hashView(std::string_view s, Hash seed = 0) { return hashBytes(s.data(), s.size(), seed); }

constexpr Hash kTokenSeed = 0x70C3A5ULL;
constexpr Hash kIdMask = (Hash{1} << 48) - 1;

/// The id of a block as text: "b" and its 48-bit key in 12 lowercase hex digits (13 characters: inside the small-string buffer, no allocation).
/// `idText` into a caller's buffer (a writer needs no `std::string`).
inline void idChars(Hash key, char (&buf)[13])
{
    static constexpr char digits[] = "0123456789abcdef";
    buf[0] = 'b';
    for (int i = 0; i < 12; ++i) buf[1 + i] = digits[(key >> (44 - 4 * i)) & 15];
}

[[nodiscard]] std::string idText(Hash key)
{
    char buf[13];
    idChars(key, buf);
    return std::string(buf, 13);
}

/// The key of an id written by `idText`; 0 for any other text (an id of another scheme resolves to nothing).
[[nodiscard]] Hash idKey(std::string_view id)
{
    if (id.size() != 13 || id[0] != 'b') return 0;
    Hash key = 0;
    for (std::size_t i = 1; i < 13; ++i) {
        const char c = id[i];
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0) return 0;
        key = key << 4 | static_cast<Hash>(d);
    }
    return key;
}

/// `text` split at '\n' exactly as `std::getline` does (a final newline adds no empty line); views into `text`.
[[nodiscard]] std::vector<std::string_view> splitViews(std::string_view text)
{
    std::vector<std::string_view> out;
    for (std::size_t pos = 0; pos < text.size();) {
        const auto e = text.find('\n', pos);
        if (e == std::string_view::npos) { out.push_back(text.substr(pos)); break; }
        out.push_back(text.substr(pos, e - pos));
        pos = e + 1;
    }
    return out;
}

/// `collapse(lines joined by "\n")` of the lines `from..to` (1-based) into `out`, without building the joined text: whitespace runs become one space, trailing
/// spaces and braces go.
void collapseLines(const std::vector<std::string_view>& lines, int from, int to, std::string& out)
{
    out.clear();
    for (int k = from; k <= to; ++k) {
        for (const char c : lines[static_cast<std::size_t>(k) - 1]) {
            if (std::isspace(static_cast<unsigned char>(c))) { if (!out.empty() && out.back() != ' ') out += ' '; }
            else out += c;
        }
        if (!out.empty() && out.back() != ' ') out += ' ';   // the "\n" that ends the line
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '{')) out.pop_back();
}

/// Calls `emit(token)` for each identifier or number of `line` outside string and character literals; `//` starts a comment. (One scanner for tokens, one for literals.)
template <class Emit>
void scanTokens(std::string_view line, Emit&& emit)
{
    for (std::size_t i = 0; i < line.size();) {
        const char c = line[i];
        if (c == '"' || c == '\'') {
            std::size_t j = i + 1;
            for (; j < line.size() && line[j] != c; ++j) if (line[j] == '\\' && j + 1 < line.size()) ++j;
            i = j + 1;
            continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_' || line[j] == '.')) ++j;
            emit(line.substr(i, j - i));
            i = j;
            continue;
        }
        ++i;
    }
}

/// The contents of the string and character literals of `line`, into `literals`.
void scanLiterals(std::string_view line, std::set<std::string>& literals)
{
    for (std::size_t i = 0; i < line.size();) {
        const char c = line[i];
        if (c == '"' || c == '\'') {
            std::string lit;
            std::size_t j = i + 1;
            for (; j < line.size() && line[j] != c; ++j) {
                if (line[j] == '\\' && j + 1 < line.size()) ++j;
                lit += line[j];
            }
            if (!lit.empty()) literals.insert(lit);
            i = j + 1;
            continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_' || line[j] == '.')) ++j;
            i = j;
            continue;
        }
        ++i;
    }
}

/// A 128-bit one-hash token filter: no false negatives, so "a block holds at least k of these tokens" can only be over-reported (SCALE.md section 4: the exact rule
/// confirms every candidate).
struct Bloom
{
    std::uint64_t w[2] = {0, 0};
    void add(Hash h) { const unsigned b = static_cast<unsigned>(h >> 17) & 127U; w[b >> 6] |= std::uint64_t{1} << (b & 63U); }
    [[nodiscard]] bool has(Hash h) const { const unsigned b = static_cast<unsigned>(h >> 17) & 127U; return (w[b >> 6] >> (b & 63U) & 1U) != 0; }
};

/// A block as the index holds it: the public record plus what the rules need and the text cannot give cheaply (where the signature line is, a hash of the
/// collapsed text, the token filter).
struct LiveBlock : Block
{
    int sigLine = 0;       ///< the signature line (`first` is the first decorator when there is one); scanning for tokens skips lines up to it
    Hash bodyHash = 0;     ///< of the collapsed text of first..last: an edit changes it, a reflow does not
    Bloom bloom;           ///< of the tokens of the body
};

/// Sorted, de-duplicated token hashes: the exact token set of a block, derived from its text when a rule needs it.
using Tokens = std::vector<Hash>;

[[nodiscard]] std::size_t overlap(const Tokens& a, const Tokens& b)
{
    std::size_t both = 0;
    for (auto x = a.begin(), y = b.begin(); x != a.end() && y != b.end();) {
        if (*x < *y) ++x;
        else if (*y < *x) ++y;
        else { ++both; ++x; ++y; }
    }
    return both;
}

[[nodiscard]] double jaccardOf(const Tokens& a, const Tokens& b)
{
    if (a.size() < kMinTokensToCompare || b.size() < kMinTokensToCompare) return 0.0;
    const std::size_t both = overlap(a, b);
    return static_cast<double>(both) / static_cast<double>(a.size() + b.size() - both);
}

[[nodiscard]] double containmentOf(const Tokens& part, const Tokens& whole)
{
    if (part.size() < kMinTokensToCompare) return 0.0;
    return static_cast<double>(overlap(part, whole)) / static_cast<double>(part.size());
}

/// The tokens of block `b` from the lines of the text it was indexed at. The signature (and decorator) lines are not scanned unless the block is one line.
[[nodiscard]] Tokens tokensOfBlock(const std::vector<std::string_view>& lines, const LiveBlock& b)
{
    Tokens t;
    for (int k = std::max(1, b.first); k <= b.last && static_cast<std::size_t>(k) <= lines.size(); ++k)
        if (k > b.sigLine || b.first == b.last) scanTokens(lines[static_cast<std::size_t>(k) - 1], [&](std::string_view x) { t.push_back(hashView(x, kTokenSeed)); });
    std::sort(t.begin(), t.end());
    t.erase(std::unique(t.begin(), t.end()), t.end());
    return t;
}

/// The smallest overlap that `double(both) / double(n) >= threshold` allows, computed with the same arithmetic as the comparison so a filter built on it can never
/// disagree with the rule.
[[nodiscard]] std::size_t leastOverlap(std::size_t n, double threshold)
{
    std::size_t both = static_cast<std::size_t>(threshold * static_cast<double>(n));
    both = both > 1 ? both - 1 : 0;
    while (static_cast<double>(both) / static_cast<double>(n) < threshold) ++both;
    return both;
}

}  // namespace

namespace {

/// A file's blocks, as `update` found them and a copy of the cache may hold them (ids not yet assigned).
struct Analysis
{
    std::uint32_t pathId = 0;
    Hash textId = 0;
    std::vector<LiveBlock> blocks;
};

/// The compact history record of one event: the strings are interned, and the id and any block named in `detail` are keys.
struct Ev
{
    Hash id = 0;
    Hash detail = 0;                 ///< detailKind 1: an interned string id; 2: a block key
    std::uint32_t path = 0, name = 0;
    std::int32_t turn = 0;
    Carry carry = Carry::kept;
    std::uint8_t detailKind = 0;     ///< 0: none
};

/// One string pool: bytes in chunks that never move, so a view stays valid for the life of the pool; an open-addressing table over them. Also the token ranks (below).
struct Pool
{
    static constexpr std::size_t kChunk = 64 * 1024;
    std::vector<std::unique_ptr<char[]>> chunks;
    char* cur = nullptr;
    std::size_t left = 0;
    /// The strings by id, in fixed chunks (no doubling slack, a shift and a mask to index). Id 0 is the empty string.
    static constexpr std::size_t kSpanShift = 10, kSpanChunk = std::size_t{1} << kSpanShift;
    std::vector<std::unique_ptr<std::string_view[]>> spanChunks;
    std::size_t spanCount = 0;
    Pool() { addSpan(std::string_view{}); }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    void addSpan(std::string_view v)
    {
        if ((spanCount & (kSpanChunk - 1)) == 0) spanChunks.push_back(std::make_unique<std::string_view[]>(kSpanChunk));
        spanChunks[spanCount >> kSpanShift][spanCount & (kSpanChunk - 1)] = v;
        ++spanCount;
    }
    [[nodiscard]] std::string_view span(std::size_t id) const { return spanChunks[id >> kSpanShift][id & (kSpanChunk - 1)]; }
    std::vector<std::uint32_t> table = std::vector<std::uint32_t>(1024, 0);
    std::unordered_map<Hash, std::uint32_t> ranks;             // token hash -> rank, by first sight: the order of the revive prefix filter
    std::uint32_t nextRank = 1;

    std::uint32_t intern(std::string_view s)
    {
        if (s.empty()) return 0;
        const Hash h = hashView(s, 0x5EED);
        const std::size_t mask = table.size() - 1;
        std::size_t i = static_cast<std::size_t>(h) & mask;
        for (; table[i] != 0; i = (i + 1) & mask) if (span(table[i]) == s) return table[i];
        char* dst = allocate(s.size());
        std::memcpy(dst, s.data(), s.size());
        addSpan(std::string_view{dst, s.size()});
        const auto id = static_cast<std::uint32_t>(spanCount - 1);
        table[i] = id;
        if (spanCount * 10 > table.size() * 7) grow();
        return id;
    }
    [[nodiscard]] std::string_view view(std::uint32_t id) const { return span(id); }
    [[nodiscard]] std::string_view internView(std::string_view s) { return view(intern(s)); }
    /// The rank of a token: assigned the first time the token is asked about, so common tokens rank low and the first tokens a prefix filter keeps are the newest, which
    /// are the rarest in practice. Any total order is correct; this one keeps the postings short.
    [[nodiscard]] std::uint32_t rankOf(Hash token)
    {
        const auto [it, inserted] = ranks.try_emplace(token, nextRank);
        if (inserted) ++nextRank;
        return it->second;
    }

private:
    char* allocate(std::size_t n)
    {
        if (n > kChunk / 4) {
            chunks.push_back(std::make_unique<char[]>(n));
            return chunks.back().get();
        }
        if (left < n) {
            chunks.push_back(std::make_unique<char[]>(kChunk));
            cur = chunks.back().get();
            left = kChunk;
        }
        char* p = cur;
        cur += n;
        left -= n;
        return p;
    }
    void grow()
    {
        std::vector<std::uint32_t> bigger(table.size() * 2, 0);
        const std::size_t mask = bigger.size() - 1;
        for (std::size_t id = 1; id < spanCount; ++id) {
            std::size_t i = static_cast<std::size_t>(hashView(span(id), 0x5EED)) & mask;
            while (bigger[i] != 0) i = (i + 1) & mask;
            bigger[i] = static_cast<std::uint32_t>(id);
        }
        table = std::move(bigger);
    }
};

/// id key -> (file, position): open addressing with backward-shift deletion (no tombstones), keys are well-mixed already so the low bits index.
struct IdTable
{
    struct Slot { Hash key = 0; std::uint32_t file = 0, pos = 0; };
    std::vector<Slot> slots = std::vector<Slot>(16);
    std::size_t count = 0;

    [[nodiscard]] const Slot* find(Hash key) const
    {
        const std::size_t mask = slots.size() - 1;
        for (std::size_t i = static_cast<std::size_t>(key) & mask; slots[i].key != 0; i = (i + 1) & mask) if (slots[i].key == key) return &slots[i];
        return nullptr;
    }
    void put(Hash key, std::uint32_t file, std::uint32_t pos)
    {
        if ((count + 1) * 10 > slots.size() * 7) grow();
        const std::size_t mask = slots.size() - 1;
        std::size_t i = static_cast<std::size_t>(key) & mask;
        for (; slots[i].key != 0; i = (i + 1) & mask) if (slots[i].key == key) { slots[i] = {key, file, pos}; return; }
        slots[i] = {key, file, pos};
        ++count;
    }
    void erase(Hash key)
    {
        const std::size_t mask = slots.size() - 1;
        std::size_t i = static_cast<std::size_t>(key) & mask;
        for (; slots[i].key != key; i = (i + 1) & mask) if (slots[i].key == 0) return;
        --count;
        for (std::size_t j = (i + 1) & mask;; j = (j + 1) & mask) {   // shift the following run back over the hole
            if (slots[j].key == 0) break;
            const std::size_t home = static_cast<std::size_t>(slots[j].key) & mask;
            if (((j - home) & mask) >= ((j - i) & mask)) { slots[i] = slots[j]; i = j; }
        }
        slots[i] = Slot{};
    }
private:
    void grow()
    {
        std::vector<Slot> old = std::move(slots);
        slots.assign(old.size() * 2, Slot{});
        count = 0;
        for (const Slot& s : old) if (s.key != 0) put(s.key, s.file, s.pos);
    }
};

struct FileState
{
    std::string_view path;             ///< interned
    std::uint32_t pathId = 0;
    Hash textId = 0;                   ///< of the text the blocks were found in
    std::size_t textSize = 0;
    bool indexed = false;
    std::string retainedText;          ///< only when the index has no TextSource
    std::vector<LiveBlock> blocks;     ///< in file order
};

/// A retired block: what the revive and merge rules need of it, and where its items went.
struct Retired
{
    std::string_view path, name, signature;
    Hash mergedInto = 0;               ///< a block key; 0: none (an orphan)
    Tokens tokens;                     ///< exact, kept because the old text is gone by the time the rule asks (a revival after the file changed)
};

}  // namespace

struct BlockIndex::State
{
    std::shared_ptr<Pool> pool = std::make_shared<Pool>();
    const TextSource* source = nullptr;
    std::string birthRevision;
    std::vector<FileState> files;
    std::map<std::string_view, std::uint32_t> byPath;               // path order, as the std::map<std::string> it replaces
    IdTable live;
    std::size_t liveBlocks = 0;
    std::map<Hash, Retired> retired;                                // by key, which is by id text order
    std::unordered_multimap<Hash, Hash> bySignature;                // hash(signature, name) -> retired key
    std::unordered_map<std::uint32_t, std::vector<Hash>> postings;  // token rank -> retired keys whose revive prefix holds it
    std::deque<Ev> events;   // chunked: no doubling slack on a log that only grows
    std::unordered_map<Hash, std::shared_ptr<const Analysis>> cache;
    std::deque<Hash> cacheOrder;
    std::size_t cacheLimit = 0;
    BlockIndexStats stats;

    [[nodiscard]] static Hash signatureKey(std::string_view signature, std::string_view name) { return hashView(name, hashView(signature, 0x516)); }

    /// The text of file `f` as it was when its content id was `textId`, or nullopt. Views into the source's buffer or the retained text: use before the next call.
    [[nodiscard]] std::optional<std::string_view> fetch(std::uint32_t f, Hash textId)
    {
        ++stats.textFetches;
        const FileState& fs = files[f];
        if (source != nullptr) return source->text(fs.path, textId);
        if (fs.indexed && fs.textId == textId) return std::string_view{fs.retainedText};
        return std::nullopt;
    }

    /// The revive prefix of a token set: its highest-ranked tokens, `n - ceil(0.8 n) + 1` of them (the prefix-filter lemma: two sets whose Jaccard reaches t share a
    /// token among the first `n - ceil(t n) + 1` of each, in any one total order). Ranks are assigned here, so a probe and the retired block agree.
    [[nodiscard]] std::vector<std::uint32_t> prefixRanks(const Tokens& tokens)
    {
        std::vector<std::uint32_t> ranks;
        ranks.reserve(tokens.size());
        for (const Hash t : tokens) ranks.push_back(pool->rankOf(t));
        std::sort(ranks.begin(), ranks.end(), std::greater<>{});
        const std::size_t n = tokens.size();
        ranks.resize(n - (4 * n + 4) / 5 + 1);   // ceil(4n/5) in integers: kReviveJaccard is 4/5
        return ranks;
    }

    void addRetired(Hash key, Retired r)
    {
        bySignature.emplace(signatureKey(r.signature, r.name), key);
        if (r.tokens.size() >= kMinTokensToCompare) for (const std::uint32_t rank : prefixRanks(r.tokens)) postings[rank].push_back(key);
        retired.emplace(key, std::move(r));
    }

    void dropRetired(Hash key)
    {
        const auto it = retired.find(key);
        const Retired& r = it->second;
        const auto range = bySignature.equal_range(signatureKey(r.signature, r.name));
        for (auto b = range.first; b != range.second; ++b) if (b->second == key) { bySignature.erase(b); break; }
        if (r.tokens.size() >= kMinTokensToCompare)
            for (const std::uint32_t rank : prefixRanks(r.tokens)) {
                auto& list = postings[rank];
                list.erase(std::remove(list.begin(), list.end(), key), list.end());
                if (list.empty()) postings.erase(rank);
            }
        retired.erase(it);
    }

    /// The retired id a new block brings back, or 0. The rule of the scan it replaces, in its order: a retired block with the same signature and name (the lowest
    /// id first), else the retired block of the highest Jaccard of at least 0.8 (the highest id among equals). Only retired blocks that can reach 0.8 are compared:
    /// those sharing a token with the probe's revive prefix, within the size bound `5 min >= 4 max` that J >= 4/5 implies.
    template <class TokensOf>
    [[nodiscard]] Hash reviveProbe(const LiveBlock& fresh, TokensOf&& tokensOf)
    {
        if (retired.empty()) return 0;
        Hash lowest = 0;
        const auto range = bySignature.equal_range(signatureKey(fresh.signature, fresh.name));
        for (auto b = range.first; b != range.second; ++b) {
            const Retired& r = retired.at(b->second);
            if (r.signature == fresh.signature && r.name == fresh.name && (lowest == 0 || b->second < lowest)) lowest = b->second;
        }
        if (lowest != 0) return lowest;
        const Tokens& tokens = tokensOf();
        if (tokens.size() < kMinTokensToCompare) return 0;
        std::vector<Hash> candidates;
        for (const std::uint32_t rank : prefixRanks(tokens)) {
            const auto p = postings.find(rank);
            if (p != postings.end()) candidates.insert(candidates.end(), p->second.begin(), p->second.end());
        }
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
        double best = kReviveJaccard;
        Hash pick = 0;
        for (const Hash key : candidates) {
            const Retired& r = retired.at(key);
            const std::size_t lo = std::min(r.tokens.size(), tokens.size()), hi = std::max(r.tokens.size(), tokens.size());
            if (5 * lo < 4 * hi) continue;
            ++stats.reviveCandidates;
            if (const double j = jaccardOf(r.tokens, tokens); j >= best) { best = j; pick = key; }
        }
        return pick;
    }
};

namespace {

/// A file's text as lines, fetched at most once per update (an owned copy when it came from a source: the next fetch may reuse the source's buffer).
struct FileLines
{
    std::string owned;
    std::vector<std::string_view> lines;
    bool ok = false;
};

}  // namespace

BlockIndex::BlockIndex() : s_(std::make_unique<State>()) {}
BlockIndex::BlockIndex(const BlockIndex& other) : turn_(other.turn_), s_(std::make_unique<State>(*other.s_)) {}
BlockIndex::BlockIndex(BlockIndex&& other) noexcept = default;
BlockIndex& BlockIndex::operator=(const BlockIndex& other)
{
    if (this != &other) { turn_ = other.turn_; s_ = std::make_unique<State>(*other.s_); }
    return *this;
}
BlockIndex& BlockIndex::operator=(BlockIndex&& other) noexcept = default;
BlockIndex::~BlockIndex() = default;

std::uint64_t BlockIndex::textId(std::string_view text) { return hashView(text, 0x7E47); }

void BlockIndex::setBirthRevision(std::string_view revision) { s_->birthRevision = std::string{revision}; }

void BlockIndex::setTextSource(const TextSource* source)
{
    s_->source = source;
    if (source != nullptr) for (auto& f : s_->files) { f.retainedText.clear(); f.retainedText.shrink_to_fit(); }
}

void BlockIndex::setAnalysisCacheLimit(std::size_t files)
{
    s_->cacheLimit = files;
    while (s_->cacheOrder.size() > files) { s_->cache.erase(s_->cacheOrder.front()); s_->cacheOrder.pop_front(); }
}

BlockIndexStats BlockIndex::stats() const { return s_->stats; }
std::size_t BlockIndex::liveCount() const { return s_->liveBlocks; }
std::size_t BlockIndex::retiredCount() const { return s_->retired.size(); }

void BlockIndex::update(const std::string& path, const std::string& text)
{
    State& S = *s_;
    Pool& pool = *S.pool;
    const Hash tid = textId(text);

    std::uint32_t f;
    if (const auto found = S.byPath.find(std::string_view{path}); found != S.byPath.end()) f = found->second;
    else {
        f = static_cast<std::uint32_t>(S.files.size());
        FileState fs;
        fs.pathId = pool.intern(path);
        fs.path = pool.view(fs.pathId);
        S.files.push_back(std::move(fs));
        S.byPath.emplace(S.files.back().path, f);
    }
    if (S.files[f].indexed && S.files[f].textId == tid && S.files[f].textSize == text.size()) return;
    const std::string_view pathView = S.files[f].path;
    const std::uint32_t pathId = S.files[f].pathId;
    const bool hadOld = S.files[f].indexed;
    const Hash oldTextId = S.files[f].textId;

    // ---- the new text's blocks: the analysis of a blob is reused when this (path, content id) was folded before ----
    const auto lines = splitViews(text);
    const Hash cacheKey = mix64(tid ^ (static_cast<Hash>(pathId) * kMul));
    std::vector<LiveBlock> fresh;
    if (const auto hit = S.cacheLimit > 0 ? S.cache.find(cacheKey) : S.cache.end(); hit != S.cache.end() && hit->second->pathId == pathId && hit->second->textId == tid) {
        ++S.stats.analysisHits;
        fresh = hit->second->blocks;
    } else {
        ++S.stats.parses;
        std::string scratch;
        const auto regions = foldRegionsFor(path, text, true);
        fresh.reserve(regions.size());   // exact: a block vector is never over-allocated
        for (const auto& r : regions) {
            LiveBlock b;
            b.path = pathView;
            b.name = pool.internView(r.name);
            b.first = r.spanFirst > 0 ? r.spanFirst : r.first;   // decorators are inside the block (R-BLK-6)
            b.last = std::max(r.first, r.bodyTo);
            b.sigLine = r.first;
            collapseLines(lines, r.first, r.first, scratch);
            b.signature = pool.internView(scratch);
            for (int k = b.first; k <= b.last; ++k)
                if (k > r.first || b.first == b.last) scanTokens(lines[static_cast<std::size_t>(k) - 1], [&](std::string_view t) { b.bloom.add(hashView(t, kTokenSeed)); });
            collapseLines(lines, b.first, b.last, scratch);
            b.bodyHash = hashView(scratch);
            fresh.push_back(std::move(b));
        }
        if (S.cacheLimit > 0) {
            auto entry = std::make_shared<Analysis>();
            entry->pathId = pathId;
            entry->textId = tid;
            entry->blocks = fresh;
            if (S.cache.emplace(cacheKey, std::move(entry)).second) S.cacheOrder.push_back(cacheKey);
            while (S.cacheOrder.size() > S.cacheLimit) { S.cache.erase(S.cacheOrder.front()); S.cacheOrder.pop_front(); }
        }
    }

    std::vector<LiveBlock> old = std::move(S.files[f].blocks);
    S.files[f].blocks.clear();

    // Text read back by a rule, once per update: the old text of this file, and the current text of any other a merge candidate lives in.
    std::unordered_map<std::uint32_t, FileLines> fetched;
    const auto load = [&](std::uint32_t file, Hash id) -> const FileLines& {
        auto [it, inserted] = fetched.try_emplace(file);
        if (inserted) {
            if (const auto t = S.fetch(file, id)) {
                if (S.source != nullptr) { it->second.owned.assign(*t); it->second.lines = splitViews(it->second.owned); }   // the source may reuse its buffer
                else it->second.lines = splitViews(*t);   // the retained text stays put until the end of this update
                it->second.ok = true;
            }
        }
        return it->second;
    };
    const auto linesOfOld = [&]() -> const FileLines& {
        if (!hadOld) return fetched.try_emplace(f).first->second;
        return load(f, oldTextId);
    };
    std::vector<Tokens> freshTokens(fresh.size());
    std::vector<char> freshReady(fresh.size(), 0);
    const std::vector<LiveBlock>* freshBlocks = &fresh;   // the new blocks, wherever they stand (moved into the file's state below)
    const auto tokF = [&](std::size_t n) -> const Tokens& {
        if (!freshReady[n]) { freshTokens[n] = tokensOfBlock(lines, (*freshBlocks)[n]); freshReady[n] = 1; }
        return freshTokens[n];
    };
    std::vector<Tokens> oldTokens(old.size());
    bool oldReady = false;
    std::vector<bool> used(old.size(), false);
    const auto ensureOldTokens = [&] {   // the tokens of the old blocks no rule has matched yet, from the old text
        if (oldReady) return;
        oldReady = true;
        const FileLines& fl = linesOfOld();
        if (!fl.ok) return;
        for (std::size_t o = 0; o < old.size(); ++o) if (!used[o]) oldTokens[o] = tokensOfBlock(fl.lines, old[o]);
    };

    const auto same = [](std::string_view a, std::string_view b) { return a.data() == b.data() && a.size() == b.size(); };   // interned: identical text is the same bytes
    std::vector<int> match(fresh.size(), -1);   // index into `old`
    std::vector<Carry> how(fresh.size(), Carry::added);
    const auto take = [&](std::size_t n, std::size_t o, Carry c) { match[n] = static_cast<int>(o); used[o] = true; how[n] = c; };
    // 1. the same signature and name
    for (std::size_t n = 0; n < fresh.size(); ++n)
        for (std::size_t o = 0; o < old.size(); ++o)
            if (!used[o] && same(old[o].signature, fresh[n].signature) && same(old[o].name, fresh[n].name)) { take(n, o, Carry::kept); break; }
    // 2. the same name, unique on both sides
    for (std::size_t n = 0; n < fresh.size(); ++n) {
        if (match[n] >= 0) continue;
        int oldCount = 0, newCount = 0;
        std::size_t at = 0;
        for (std::size_t o = 0; o < old.size(); ++o) if (!used[o] && same(old[o].name, fresh[n].name)) { ++oldCount; at = o; }
        for (std::size_t m = 0; m < fresh.size(); ++m) if (match[m] < 0 && same(fresh[m].name, fresh[n].name)) ++newCount;
        if (oldCount == 1 && newCount == 1) take(n, at, Carry::resigned);
    }
    // 3. a similar body: the best pairs first (only when both sides have a block left: the old text is read only then)
    const auto anyLeft = [&](const std::vector<bool>& u) { return std::find(u.begin(), u.end(), false) != u.end(); };
    if (anyLeft(used) && std::find(match.begin(), match.end(), -1) != match.end()) {
        ensureOldTokens();
        while (true) {
            double best = kRenameJaccard;
            int bn = -1, bo = -1;
            for (std::size_t n = 0; n < fresh.size(); ++n) {
                if (match[n] >= 0) continue;
                for (std::size_t o = 0; o < old.size(); ++o) {
                    if (used[o]) continue;
                    if (const double j = jaccardOf(oldTokens[o], tokF(n)); j >= best) { best = j; bn = static_cast<int>(n); bo = static_cast<int>(o); }
                }
            }
            if (bn < 0) break;
            take(static_cast<std::size_t>(bn), static_cast<std::size_t>(bo), Carry::renamed);
        }
    }

    const std::int32_t turn = turn_;
    const auto event = [&](Carry carry, Hash id, std::uint32_t nameId, std::uint8_t detailKind, Hash detail) {
        S.events.push_back(Ev{id, detail, pathId, nameId, turn, carry, detailKind});
    };
    std::vector<Hash> keys(fresh.size(), 0);
    std::vector<char> born(fresh.size(), 0);   // blocks of this update that got a new id: the ordinal of A16 counts the identical ones before a birth
    for (std::size_t n = 0; n < fresh.size(); ++n) {
        if (match[n] >= 0) {
            const LiveBlock& o = old[static_cast<std::size_t>(match[n])];
            keys[n] = idKey(o.id);
            fresh[n].id = o.id;
            const bool edited = o.bodyHash != fresh[n].bodyHash;
            if (how[n] != Carry::kept || edited) {
                const std::string_view detail = how[n] == Carry::renamed ? o.name : how[n] == Carry::resigned ? o.signature : std::string_view{"edited"};
                event(how[n], keys[n], pool.intern(fresh[n].name), 1, pool.intern(detail));
            }
            continue;
        }
        // 4. a retired id comes back: the same signature, or a near-identical body (deleted and written again; moved from another file)
        if (const Hash revive = S.reviveProbe(fresh[n], [&]() -> const Tokens& { return tokF(n); }); revive != 0) {
            const std::string_view from = S.retired.at(revive).path;
            keys[n] = revive;
            fresh[n].id = idText(revive);
            event(Carry::revived, revive, pool.intern(fresh[n].name), 1, pool.intern(from));
            S.dropRetired(revive);
            S.live.put(revive, f, static_cast<std::uint32_t>(n));
            continue;
        }
        // 5. a block not seen before: its birth id (A16), a hash of the revision it was born in, its path, its signature and the ordinal among identical births
        const Hash base = hashView(fresh[n].signature, hashView(pathView, S.birthRevision.empty() ? mix64(static_cast<Hash>(turn) ^ 0xA16) : hashView(S.birthRevision, 0xA16)));
        Hash ordinal = 0;
        for (std::size_t m = 0; m < n; ++m) ordinal += born[m] && same(fresh[m].signature, fresh[n].signature);
        Hash key;
        for (;; ++ordinal) {   // a clash (48 bits) takes the next ordinal: deterministic as well
            key = mix64(base ^ (ordinal * kMul)) & kIdMask;
            if (key != 0 && S.live.find(key) == nullptr && S.retired.find(key) == S.retired.end()) break;
        }
        born[n] = 1;
        S.live.put(key, f, static_cast<std::uint32_t>(n));   // taken now, so a later birth of this update cannot choose it
        keys[n] = key;
        fresh[n].id = idText(key);
        event(Carry::added, key, pool.intern(fresh[n].name), 0, 0);
    }

    if (anyLeft(used)) ensureOldTokens();   // while the file still names its old text (a retained text is found by its content id)

    // The new blocks are live; the old ones' keys leave the table first (a carried id is put back at its new position).
    for (const LiveBlock& o : old) S.live.erase(idKey(o.id));
    S.liveBlocks -= old.size();
    S.files[f].blocks = std::move(fresh);
    std::vector<LiveBlock>& blocks = S.files[f].blocks;
    freshBlocks = &blocks;
    for (std::size_t n = 0; n < blocks.size(); ++n) S.live.put(keys[n], f, static_cast<std::uint32_t>(n));
    S.liveBlocks += blocks.size();
    S.files[f].textId = tid;
    S.files[f].textSize = text.size();
    S.files[f].indexed = true;

    // Old blocks left over are retired; one whose body now mostly lives in a single live block was merged into it.
    if (anyLeft(used)) {
        ensureOldTokens();
        for (std::size_t o = 0; o < old.size(); ++o) {
            if (used[o]) continue;
            Hash into = 0;
            const Tokens& body = oldTokens[o];
            if (body.size() >= kMinTokensToCompare) {
                double best = kMergeContainment;
                const std::size_t need = leastOverlap(body.size(), kMergeContainment);
                const std::size_t slack = body.size() - need;   // misses a candidate may show and still qualify
                for (const auto& [p, fileIndex] : S.byPath) {
                    for (std::size_t k = 0; k < S.files[fileIndex].blocks.size(); ++k) {
                        const LiveBlock& b = S.files[fileIndex].blocks[k];
                        std::size_t misses = 0;
                        for (const Hash t : body) if (!b.bloom.has(t) && ++misses > slack) break;
                        if (misses > slack) continue;
                        ++S.stats.mergeCandidates;
                        const Tokens* theirs;
                        Tokens local;
                        if (fileIndex == f) theirs = &tokF(k);
                        else {
                            const FileLines& theirLines = load(fileIndex, S.files[fileIndex].textId);
                            if (!theirLines.ok) continue;   // text not available: unknown, so not a merge target
                            local = tokensOfBlock(theirLines.lines, b);
                            theirs = &local;
                        }
                        if (const double c = containmentOf(body, *theirs); c >= best) { best = c; into = idKey(b.id); }
                    }
                }
            }
            const Hash key = idKey(old[o].id);
            event(into == 0 ? Carry::retired : Carry::merged, key, pool.intern(old[o].name), into == 0 ? std::uint8_t{0} : std::uint8_t{2}, into);
            S.addRetired(key, Retired{old[o].path, old[o].name, old[o].signature, into, std::move(oldTokens[o])});
        }
    }
    if (S.source == nullptr) S.files[f].retainedText = text;
}

const Block* BlockIndex::resolve(const std::string& id) const
{
    Hash at = idKey(id);
    for (int hops = 0; hops < 16 && at != 0; ++hops) {
        if (const auto* slot = s_->live.find(at)) return &s_->files[slot->file].blocks[slot->pos];
        const auto r = s_->retired.find(at);
        if (r == s_->retired.end()) return nullptr;
        at = r->second.mergedInto;
    }
    return nullptr;
}

const Block* BlockIndex::at(const std::string& path, int line) const
{
    const auto it = s_->byPath.find(std::string_view{path});
    if (it == s_->byPath.end()) return nullptr;
    for (const auto& b : s_->files[it->second].blocks) if (line >= b.first && line <= b.last) return &b;
    return nullptr;
}

std::vector<const Block*> BlockIndex::blocks(const std::string& path) const
{
    std::vector<const Block*> out;
    if (const auto it = s_->byPath.find(std::string_view{path}); it != s_->byPath.end()) for (const auto& b : s_->files[it->second].blocks) out.push_back(&b);
    return out;
}

std::vector<const Block*> BlockIndex::all() const
{
    std::vector<const Block*> out;
    out.reserve(s_->liveBlocks);
    for (const auto& [p, fileIndex] : s_->byPath) for (const auto& b : s_->files[fileIndex].blocks) out.push_back(&b);
    return out;
}

std::vector<std::set<std::string>> BlockIndex::literals(const std::string& path) const
{
    std::vector<std::set<std::string>> out;
    const auto it = s_->byPath.find(std::string_view{path});
    if (it == s_->byPath.end()) return out;
    State& S = *s_;   // the fetch counts itself; the index is otherwise untouched
    const FileState& fs = S.files[it->second];
    out.resize(fs.blocks.size());
    const auto text = S.fetch(it->second, fs.textId);
    if (!text) return out;
    const auto lines = splitViews(*text);
    for (std::size_t n = 0; n < fs.blocks.size(); ++n) {
        const LiveBlock& b = fs.blocks[n];
        for (int k = std::max(1, b.first); k <= b.last && static_cast<std::size_t>(k) <= lines.size(); ++k)
            if (k > b.sigLine || b.first == b.last) scanLiterals(lines[static_cast<std::size_t>(k) - 1], out[n]);
    }
    return out;
}

BlockEvent BlockIndex::eventAt(std::size_t i) const
{
    const Ev& e = s_->events[i];
    const Pool& pool = *s_->pool;
    BlockEvent out;
    out.carry = e.carry;
    out.id = idText(e.id);
    out.path = std::string{pool.view(e.path)};
    out.name = std::string{pool.view(e.name)};
    out.detail = e.detailKind == 1 ? std::string{pool.view(static_cast<std::uint32_t>(e.detail))} : e.detailKind == 2 ? idText(e.detail) : std::string{};
    out.turn = e.turn;
    return out;
}

std::size_t BlockIndex::eventCount() const { return s_->events.size(); }

BlockEvent EventLog::iterator::operator*() const { return x_->eventAt(i_); }
std::size_t EventLog::size() const { return x_->eventCount(); }
BlockEvent EventLog::operator[](std::size_t i) const { return x_->eventAt(i); }
std::vector<BlockEvent> EventLog::from(std::size_t from) const
{
    std::vector<BlockEvent> out;
    for (std::size_t i = from; i < size(); ++i) out.push_back(x_->eventAt(i));
    return out;
}

void BlockIndex::writeRetired(json::Writer& w) const
{
    w.beginArray();
    char id[13], into[13];
    for (const auto& [key, r] : s_->retired) {
        idChars(key, id);
        w.beginObject().key("id").strPlain(std::string_view{id, 13}).key("path").str(r.path).key("name").str(r.name);
        if (r.mergedInto == 0) w.kv("mergedInto", std::string_view{});
        else { idChars(r.mergedInto, into); w.kv("mergedInto", std::string_view{into, 13}); }
        w.endObject();
    }
    w.endArray();
}

namespace {

/// One logged event, written from its compact record without building a `BlockEvent`.
void writeEv(json::Writer& w, const Ev& e, const Pool& pool)
{
    char id[13], into[13];
    idChars(e.id, id);
    std::string_view detail;
    if (e.detailKind == 1) detail = pool.view(static_cast<std::uint32_t>(e.detail));
    else if (e.detailKind == 2) { idChars(e.detail, into); detail = std::string_view{into, 13}; }
    projection::writeBlockEventParts(w, e.turn, carryName(e.carry), std::string_view{id, 13}, pool.view(e.path), pool.view(e.name), detail);
}

}  // namespace

void BlockIndex::writeEventJson(json::Writer& w, std::size_t i) const { writeEv(w, s_->events[i], *s_->pool); }

void projection::writeIndexEvent(json::Writer& w, const BlockIndex& index, std::size_t i) { index.writeEventJson(w, i); }

void BlockIndex::writeJsonMembers(json::Writer& w, std::size_t eventsFrom) const
{
    w.key("blocks").beginArray();
    for (const auto& [p, fileIndex] : s_->byPath) for (const Block& b : s_->files[fileIndex].blocks) projection::writeBlock(w, b);
    w.endArray().key("retired");
    writeRetired(w);
    w.kv("eventsFrom", static_cast<std::int64_t>(eventsFrom));
    w.key("events").beginArray();
    for (std::size_t i = std::min(eventsFrom, s_->events.size()); i < s_->events.size(); ++i) writeEv(w, s_->events[i], *s_->pool);
    w.endArray();
}

std::size_t BlockIndex::jsonSizeHint(std::size_t eventsFrom) const
{
    // About 70 bytes of keys and punctuation plus the three strings of a block; about 90 plus the strings of an event; 60 per retired block.
    constexpr std::size_t kBlock = 80, kEvent = 110, kRetired = 70;
    std::size_t bytes = 64 + s_->retired.size() * kRetired;
    bytes += s_->liveBlocks * kBlock;
    bytes += (s_->events.size() - std::min(eventsFrom, s_->events.size())) * kEvent;
    return bytes;
}

std::string BlockIndex::eventLogNdjson(const std::string& document) const
{
    std::string out;
    {
        json::Writer w;
        w.beginObject().kv("schema", "canvas-events-1").kv("document", document).endObject();
        out += std::string{w.view()} + '\n';
    }
    for (std::size_t i = 0; i < s_->events.size(); ++i) {
        json::Writer w;
        w.beginObject().kv("seq", static_cast<std::int64_t>(i)).kv("kind", "block").kv("source", "canvas/index").kv("subject", idText(s_->events[i].id)).key("data");
        writeEv(w, s_->events[i], *s_->pool);
        w.endObject();
        out += std::string{w.view()} + '\n';
    }
    return out;
}

std::vector<BlockEvent> blockHistory(const BlockIndex& index, const std::string& id)
{
    std::set<std::string> ids{id};
    const auto log = index.events();
    for (bool grew = true; grew;) {   // every id merged into one of these, transitively
        grew = false;
        for (const auto& e : log)
            if (e.carry == Carry::merged && ids.count(e.detail) && !ids.count(e.id)) { ids.insert(e.id); grew = true; }
    }
    std::vector<BlockEvent> out;
    for (const auto& e : log) if (ids.count(e.id)) out.push_back(e);
    return out;
}

// ---- the issues layer ---------------------------------------------------------------------------------------------------------------------------------

namespace {

/// The tokens of an expression under test: identifiers, and the operator characters a parser names in its literals.
std::set<std::string> probeTokens(const std::string& expr)
{
    std::set<std::string> probe;
    for (std::size_t i = 0; i < expr.size();) {
        if (std::isalpha(static_cast<unsigned char>(expr[i])) || expr[i] == '_') {
            std::size_t j = i;
            while (j < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[j])) || expr[j] == '_')) ++j;
            probe.insert(expr.substr(i, j - i));
            i = j;
        } else {
            if (std::string_view{"%^!+-*/<>=&|,"}.contains(expr[i])) probe.insert(std::string(1, expr[i]));
            ++i;
        }
    }
    return probe;
}

/// The suspects of a failing test: the block named like a word of the test's name (3 points), plus for each identifier of the expression under test
/// 1/k points to each of the k blocks whose literals hold it (a rare token says more). An operator character counts only when ONE block holds it: `+`
/// in `expr()` and `unary()` says nothing about which is wrong, and a wrong pointer costs more than none (tier 0, bugs-10). Every block at the top
/// score, in file order, at most three; no evidence, no suspect.
std::vector<Anchor> suspects(const std::string& testName, const std::string& expr, const std::vector<const Block*>& blocks, const std::vector<std::set<std::string>>& literals)
{
    std::map<std::string, double> score;
    std::string words = testName;
    for (char& c : words) if (!std::isalnum(static_cast<unsigned char>(c))) c = ' ';
    std::istringstream ws(words);
    for (std::string w; ws >> w;) for (const Block* b : blocks) if (lower(leaf(b->name)) == lower(w)) score[b->id] += 3.0;
    for (const auto& t : probeTokens(expr)) {
        std::vector<const Block*> holders;
        for (std::size_t k = 0; k < blocks.size(); ++k) if (literals[k].count(t)) holders.push_back(blocks[k]);
        const bool word = std::isalpha(static_cast<unsigned char>(t[0])) || t[0] == '_';
        if (!word && holders.size() != 1) continue;
        for (const Block* b : holders) score[b->id] += 1.0 / static_cast<double>(holders.size());
    }
    double top = 0;
    for (const auto& [id, v] : score) top = std::max(top, v);
    std::vector<Anchor> out;
    if (top <= 0) return out;
    for (const Block* b : blocks)
        if (score.count(b->id) && score[b->id] >= top - 1e-9 && out.size() < 3) out.push_back({b->id, std::string{b->path}, b->first, Anchor::Confidence::suspect});
    return out;
}

}  // namespace

namespace {

/// GCC and clang underline a range under the source line: `      |     ~~~^~~` (a caret alone marks a point). When `raw` is such a line and its markers cover
/// the anchor's column, the anchor becomes the range they span (end just past the last marker). Only the first occurrence of a diagnostic is widened.
void widenByUnderline(Anchor& a, const std::string& raw)
{
    const auto bar = raw.find('|');
    if (bar == std::string::npos || a.column <= 0 || a.endColumn > 0 || raw.find_first_not_of(' ') != bar) return;
    const std::size_t text = bar + 2;   // the source text starts after "| "
    const auto first = raw.find_first_of("^~", text);
    const auto last = raw.find_last_of("^~");
    if (first == std::string::npos || raw.find_first_not_of("^~ ", first) != std::string::npos || raw.find('^', text) == std::string::npos) return;
    const int from = static_cast<int>(first - text) + 1, to = static_cast<int>(last - text) + 2;
    if (to - from > 1 && a.column >= from && a.column < to) { a.endLine = a.line; a.endColumn = to; }
}

}  // namespace

std::vector<LayerItem> parseIssues(const std::string& verdict, const BlockIndex& index, const std::map<std::string, std::string>& sources)
{
    static const std::regex diag(R"(([\w./+-]+\.[A-Za-z]+):(\d+):(?:(\d+)(?:-(\d+))?:)? ?(?:(fatal error|error|warning|note): )?(.*))");
    static const std::regex quoted(R"re("((?:[^"\\]|\\.)*)"|'((?:[^'\\]|\\.)*)')re");
    std::vector<LayerItem> items;
    std::map<std::size_t, std::string> testNames;   // item index -> the failing test's name
    std::map<std::string, std::size_t> byKey;      // a diagnostic's message without numbers -> its item
    std::optional<std::size_t> current;            // the item later context lines belong to (an index: the vector grows)
    std::string clean = verdict;                   // a Windows runner's output carries carriage returns, even inside a line (`test_modulo\r: ...`)
    std::erase(clean, '\r');
    for (const std::string& raw : splitLines(clean)) {
        const auto lead = raw.find_first_not_of(' ');
        const std::string line = lead == std::string::npos ? std::string{} : raw.substr(lead);
        if (line.empty() || line.starts_with("GATE ")) { current.reset(); continue; }
        if (line.starts_with("FAIL ")) {
            // A failing test: `FAIL 6 - modulo: CHECK_NEAR failed: ev("10%3") is 3.33 but 1 is 1`, or a runner's `FAIL name: error`.
            std::string rest = line.substr(5);
            if (const auto dash = rest.find(" - "); dash != std::string::npos && dash < 8 && std::isdigit(static_cast<unsigned char>(rest[0]))) rest = rest.substr(dash + 3);
            const auto colon = rest.find(": ");
            const std::string name = rest.substr(0, colon), message = colon == std::string::npos ? std::string{} : rest.substr(colon + 2);
            testNames[items.size()] = name;
            current = items.size();
            items.push_back({"issues", 0, "test", "test " + name + (message.empty() ? "" : ": " + message), line, {}, "issues/test/" + name});
            continue;
        }
        std::smatch m;
        if (std::regex_search(line, m, diag) && !line.contains(" | ") && !line.starts_with("location:") && !line.starts_with("the test expected")) {
            const std::string kindWord = m[5].str(), message = m[6].str();
            if (kindWord == "note") { if (current) items[*current].detail += "\n" + line; continue; }
            const std::string path = m[1].str();
            const int at = std::atoi(m[2].str().c_str());
            const std::string kind = kindWord == "warning" && !message.contains("-Werror") ? "warning" : "error";
            Anchor a{"", path, at, Anchor::Confidence::exact};
            if (m[3].matched) {   // `path:line:col:`, or `path:line:col-endcol:` where the compiler names the range
                a.column = std::atoi(m[3].str().c_str());
                if (m[4].matched && std::atoi(m[4].str().c_str()) > a.column) { a.endLine = at; a.endColumn = std::atoi(m[4].str().c_str()) + 1; }
            }
            if (const Block* blk = index.at(path, at)) a.blockId = blk->id;
            std::string key = kind + "|";
            for (const char c : message) if (!std::isdigit(static_cast<unsigned char>(c))) key += c;
            if (const auto seen = byKey.find(key); seen != byKey.end()) {
                LayerItem& it = items[seen->second];
                const bool same = std::any_of(it.anchors.begin(), it.anchors.end(), [&](const Anchor& x) { return x.path == path && x.line == at && x.column == a.column; });
                if (!same) it.anchors.push_back(a);
                current.reset();   // the context of a repeat is not added: the first occurrence carries it
                continue;
            }
            byKey[key] = items.size();
            current = items.size();
            const std::string hash = std::format("{:016x}", hashView(key));   // the fixed hash: std::hash<std::string> differs between libstdc++ and libc++
            items.push_back({"issues", 0, kind, kind + " " + path + ":" + std::to_string(at) + ": " + message, line, {a}, "issues/" + kind + "/" + hash});
            continue;
        }
        if (current && raw.size() > 1 && raw[0] == ' ') {   // a context line (the source line, the caret, expected/actual)
            items[*current].detail += "\n" + raw;
            if (!items[*current].anchors.empty()) widenByUnderline(items[*current].anchors.front(), raw);
        }
        else current.reset();
    }
    // Failing tests are localised once their context is in: the expression is the quoted text of the message, plus the quoted text of the test's own
    // function in the given sources (a runner such as Python's unittest reports `test_modulo: 30.0 != 1`, never the expression; the line it names is
    // usually a shared assertion helper, so the function is found by name).
    const auto blocks = index.all();
    std::vector<std::set<std::string>> literals;   // each block's string literals, read back from the text: only a failing test needs them
    if (!testNames.empty()) {
        literals.reserve(blocks.size());
        for (std::size_t k = 0; k < blocks.size();) {   // `all()` lists a file's blocks together, in file order
            const std::string path{blocks[k]->path};
            auto here = index.literals(path);
            for (std::size_t j = 0; j < here.size() && k < blocks.size(); ++j, ++k) literals.push_back(std::move(here[j]));
        }
    }
    for (const auto& [i, name] : testNames) {
        LayerItem& it = items[i];
        std::string expr;
        const std::string& d = it.detail;
        for (std::sregex_iterator q(d.begin(), d.end(), quoted), end; q != end; ++q) expr += (*q)[1].str() + (*q)[2].str() + " ";
        const std::string testLeaf = name.substr(name.find_last_of(".:") == std::string::npos ? 0 : name.find_last_of(".:") + 1);
        for (const auto& [path, text] : sources) {
            const auto srcLines = splitLines(text);
            for (const auto& r : foldRegionsFor(path, text, true)) {
                if (leaf(r.name) != testLeaf) continue;
                for (int k = r.first; k <= std::max(r.first, r.bodyTo) && k <= static_cast<int>(srcLines.size()); ++k) {
                    const std::string& src = srcLines[static_cast<std::size_t>(k) - 1];
                    for (std::sregex_iterator q(src.begin(), src.end(), quoted), end; q != end; ++q) expr += (*q)[1].str() + (*q)[2].str() + " ";
                }
            }
        }
        it.anchors = suspects(name, expr, blocks, literals);
    }
    for (std::size_t i = 0; i < items.size(); ++i) items[i].number = static_cast<int>(i) + 1;
    return items;
}


std::string layerSummary(const std::vector<LayerItem>& items, const BlockIndex& index, std::size_t lineChars)
{
    if (items.empty()) return {};
    const std::string layer = items.front().layer, noun = layer.size() > 1 && layer.back() == 's' ? layer.substr(0, layer.size() - 1) : layer;
    std::string keyword = noun;
    for (char& c : keyword) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::string s = std::to_string(items.size()) + " " + (items.size() == 1 ? noun : layer) + " (`=== READ: " + keyword + " n ===` shows one in full with its code):\n";
    for (const auto& it : items) {
        std::string where;
        if (it.anchors.size() == 1) where = place(it.anchors[0], index);
        else if (it.anchors.size() > 1) {
            std::set<std::string> names;
            std::string list;
            for (const auto& a : it.anchors) {
                const std::string p = place(a, index);
                const std::string shortName = p.substr(0, p.find(" ("));
                if (names.insert(shortName).second && names.size() <= 4) list += (list.empty() ? "" : ", ") + shortName;
            }
            where = std::to_string(it.anchors.size()) + " places: " + list + (names.size() > 4 ? ", ..." : "");
        }
        const std::string tail = where.empty() ? std::string{} : (it.anchors.front().confidence == Anchor::Confidence::suspect ? " - likely in " : " - in ") + where;
        std::string head = " " + std::to_string(it.number) + ". " + it.title;
        const std::size_t room = lineChars > tail.size() + 20 ? lineChars - tail.size() : 20;
        if (head.size() > room) head = head.substr(0, room - 3) + "...";
        s += head + tail + "\n";
    }
    return s;
}

namespace {

/// The layer's read keyword: "issues" is read as `ISSUE`.
std::string readKeyword(const std::string& layer)
{
    std::string keyword = layer.size() > 1 && layer.back() == 's' ? layer.substr(0, layer.size() - 1) : layer;
    for (char& c : keyword) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return keyword;
}

/// `layerDetail`, with the blocks an earlier item of the same message already showed (`blocksShown`, updated) named instead of repeated.
std::string detailOf(const LayerItem& item, const BlockIndex& index, const std::map<std::string, std::string>& files, std::set<std::string>& blocksShown)
{
    std::string s = "=== " + readKeyword(item.layer) + " " + std::to_string(item.number) + " (" + item.kind + ") ===\n" + item.detail + "\n";
    if (item.anchors.size() > 1) s += "It occurs at " + std::to_string(item.anchors.size()) + " places: the same fix applies at each.\n";
    int shown = 0;
    std::set<std::string> mine;   // this item's blocks, each once
    for (const auto& a : item.anchors) {
        const auto file = files.find(a.path);
        if (!a.blockId.empty()) {
            const Block* b = index.resolve(a.blockId);
            if (!b) { s += "(it was found in a function that no longer exists: the issue may be stale)\n"; continue; }
            if (!mine.insert(b->id).second) continue;
            const std::string lines = "(lines " + std::to_string(b->first) + "-" + std::to_string(b->last) + ")";
            if (!blocksShown.insert(b->id).second) {
                s += std::string{a.confidence == Anchor::Confidence::suspect ? "Likely in " : "In "} + leaf(b->name) + "() " + lines + ", shown above.\n";
                continue;
            }
            if (shown++ >= 3) { s += "... and in " + leaf(b->name) + "() " + lines + "\n"; continue; }
            const auto f = files.find(std::string{b->path});
            s += std::string{a.confidence == Anchor::Confidence::suspect ? "Likely in " : "In "} + std::string{b->path} + ", " + leaf(b->name) + "(), now at lines " + std::to_string(b->first) + "-" + std::to_string(b->last) + ":\n" +
                 (f == files.end() ? std::string{} : numbered(f->second, b->first, b->last));
        } else if (a.line > 0 && file != files.end()) {
            if (shown++ >= 3) { s += "... and at " + a.path + ":" + std::to_string(a.line) + "\n"; continue; }
            s += "At " + a.path + ":" + std::to_string(a.line) + " (outside any function):\n" + numbered(file->second, a.line - 3, a.line + 3);
        }
    }
    return s;
}

}  // namespace

std::string layerDetail(const LayerItem& item, const BlockIndex& index, const std::map<std::string, std::string>& files)
{
    std::set<std::string> blocksShown;
    return detailOf(item, index, files, blocksShown);
}

std::string layerInline(const std::vector<LayerItem>& items, const BlockIndex& index, const std::map<std::string, std::string>& files, std::size_t count, std::size_t maxChars)
{
    if (items.empty() || count == 0) return {};
    std::set<std::string> blocksShown;
    std::string body;
    std::size_t shown = 0;
    for (const auto& it : items) {
        if (shown >= count) break;
        std::set<std::string> trial = blocksShown;
        std::string d = detailOf(it, index, files, trial);
        if (shown > 0 && body.size() + d.size() > maxChars) break;   // the first is always shown: a verdict with no code at all helps least
        body += d;
        blocksShown = std::move(trial);
        ++shown;
    }
    std::string s = shown == items.size() ? std::string{"In full:\n"} : "The first " + std::to_string(shown) + " in full:\n";
    s += body;
    if (shown < items.size()) {
        const std::string rest = std::to_string(shown + 1) + (shown + 1 < items.size() ? "-" + std::to_string(items.size()) : std::string{});
        s += "The rest (" + rest + ") are listed above: `=== READ: " + readKeyword(items.front().layer) + " n ===` shows one in full.\n";
    }
    return s;
}

std::string layerFocus(const std::vector<LayerItem>& items, const BlockIndex& index)
{
    std::string s;
    for (const auto& it : items)
        for (const auto& a : it.anchors) {
            if (!a.blockId.empty()) { if (const Block* b = index.resolve(a.blockId)) s += std::string{b->path} + ":" + std::to_string(b->first) + "\n"; }
            else if (a.line > 0) s += a.path + ":" + std::to_string(a.line) + "\n";
        }
    return s;
}

std::vector<int> parseIssueReads(const std::string& reply)
{
    std::vector<int> out;
    for (std::size_t pos = 0; (pos = reply.find("=== READ:", pos)) != std::string::npos;) {
        const auto eol = reply.find('\n', pos);
        std::string head = reply.substr(pos + 9, (eol == std::string::npos ? reply.size() : eol) - pos - 9);
        pos += 9;
        if (const auto close = head.rfind("==="); close != std::string::npos) head.erase(close);
        std::istringstream in(head);
        std::string word;
        in >> word;
        if (word != "ISSUE" && word != "ISSUES") continue;
        for (std::string n; in >> n;) {
            for (char& c : n) if (c == ',' || c == '#') c = ' ';
            std::istringstream ns(n);
            for (int v = 0; ns >> v;) if (v > 0) out.push_back(v);
        }
    }
    return out;
}

const LayerMeta& issuesLayerMeta()
{
    static const LayerMeta issues{"Issues", "Compiler errors and failing tests from a gate's verdict, each bound to the blocks it names.", "sub0canvas parseIssues"};
    return issues;
}

std::string revisionFileName(const std::string& path, int revision)
{
    std::string name;
    name.reserve(path.size() + 16);
    for (const char c : path) {
        if (c == '%') name += "%25";
        else if (c == '/') name += "%2F";
        else name += c;
    }
    return name + ".r" + std::to_string(revision);
}

std::string layeredJson(const SnapshotSpec& spec, const BlockIndex& index)
{
    // The buffer is sized up front (a snapshot is mostly the texts and the blocks): growing by doubling copies the megabyte-sized document several times.
    std::size_t hint = 4096 + index.jsonSizeHint(spec.eventsFrom);
    for (const auto& [path, text] : spec.files) hint += path.size() + 160 + (spec.textsCarried ? 0 : text.size() + text.size() / 16);
    json::Writer w{hint};
    w.beginObject().kv("schema", "layered-doc-1").kv("turn", spec.turn).key("files").beginObject();
    std::map<std::string, std::string> ids;   // path -> content id
    for (const auto& [path, text] : spec.files) {
        const auto known = spec.textIds.find(path);
        ids[path] = known != spec.textIds.end() ? known->second : contentId(text);
        const int rev = spec.revisions.count(path) ? spec.revisions.at(path) : 0;
        const auto opaque = spec.revisionIds.find(path);
        w.key(path).beginObject();
        projection::writeFileRef(w, rev, opaque != spec.revisionIds.end() ? opaque->second : "r" + std::to_string(rev), ids[path]);
        w.endObject();
    }
    w.endObject();
    {   // each text once: those an earlier snapshot of the document carried are not repeated
        w.key("texts").beginObject();
        // The distinct ids not carried yet, sorted: a text is written at its first file, and the dedupe costs three allocations, not one per text.
        std::vector<std::string_view> fresh;
        fresh.reserve(spec.files.size());
        for (const auto& [path, text] : spec.files) {
            const std::string& id = ids[path];
            if (!spec.textsCarried || !spec.textsCarried->contains(id)) fresh.push_back(id);
        }
        std::ranges::sort(fresh);
        const auto [dupBegin, dupEnd] = std::ranges::unique(fresh);
        fresh.erase(dupBegin, dupEnd);
        std::vector<bool> written(fresh.size(), false);
        for (const auto& [path, text] : spec.files) {
            const std::string& id = ids[path];
            const auto at = std::ranges::lower_bound(fresh, std::string_view{id});
            if (at == fresh.end() || *at != id) continue;   // carried by an earlier snapshot
            const auto slot = written.begin() + (at - fresh.begin());
            if (*slot) continue;
            *slot = true;
            w.kv(id, text);
        }
        w.endObject();
        if (spec.textsCarried) for (const std::string_view id : fresh) spec.textsCarried->emplace(id);
    }
    // The index's own JSON holds blocks, retired and events: spliced in whole, so there is one writer of that shape.
    index.writeJsonMembers(w, spec.eventsFrom);
    if (!spec.policies.empty()) {
        w.key("policies").beginObject();
        for (const auto& [role, id] : spec.policies) w.kv(role, id);
        w.endObject();
    }
    std::map<std::string, LayerMeta> meta;
    for (const auto& [name, items] : spec.layers) {
        if (const auto given = spec.layerMeta.find(name); given != spec.layerMeta.end()) meta[name] = given->second;
    }
    if (!meta.empty()) {
        w.key("layerMeta").beginObject();
        for (const auto& [name, m] : meta) w.key(name).beginObject().kv("label", m.label).kv("description", m.description).kv("producer", m.producer).endObject();
        w.endObject();
    }
    w.key("layers").beginObject();
    for (const auto& [name, items] : spec.layers) {
        w.key(name).beginArray();
        for (const auto& it : items) {
            w.beginObject();
            projection::writeLayerItemMembers(w, it, it.number);
            w.endObject();
        }
        w.endArray();
    }
    w.endObject();
    // The store's own view of the layers (state, tags, links, authorship), beside the flat items: only when asked for, so a document written without it is byte for byte what it was.
    if (spec.store != nullptr) w.kvRaw("store", spec.store->snapshotJson(spec.storeQuery != nullptr ? *spec.storeQuery : Query{}));
    w.endObject();
    return std::string{w.view()};
}

namespace projection {

void writeBlock(json::Writer& w, const Block& b)
{
    // The id is hex by construction (idChars): written without the escaper. The three strings are interned text and may hold anything.
    w.beginObject().key("id").strPlain(b.id).key("path").str(b.path).key("name").str(b.name).key("signature").str(b.signature).key("first").num(static_cast<std::int64_t>(b.first))
        .key("last").num(static_cast<std::int64_t>(b.last)).endObject();
}

void writeBlockEvent(json::Writer& w, const BlockEvent& e) { writeBlockEventParts(w, e.turn, carryName(e.carry), e.id, e.path, e.name, e.detail); }

void writeBlockEventParts(json::Writer& w, int turn, std::string_view carry, std::string_view id, std::string_view path, std::string_view name, std::string_view detail)
{
    // carry names and ids are plain ASCII by construction; a detail may be an interned string (any text) or an id.
    w.beginObject().key("turn").num(static_cast<std::int64_t>(turn)).key("carry").strPlain(carry).key("id").strPlain(id).key("path").str(path).key("name").str(name)
        .key("detail").str(detail).endObject();
}

void writeLayerItemMembers(json::Writer& w, const LayerItem& it, int number)
{
    w.kv("id", it.key).kv("number", number).kv("kind", it.kind).kv("title", it.title).kv("detail", it.detail).key("anchors").beginArray();
    for (const auto& a : it.anchors) {
        w.beginObject().kv("blockId", a.blockId).kv("path", a.path).kv("line", a.line);
        if (a.column > 0) w.kv("column", a.column);
        if (a.endColumn > 0) w.kv("endLine", a.endLine).kv("endColumn", a.endColumn);
        w.kv("confidence", confidenceName(a.confidence)).endObject();
    }
    w.endArray();
}

void writeFileRef(json::Writer& w, int revision, std::string_view revisionId, std::string_view textId)
{
    w.kv("revision", revision).kv("revisionId", revisionId).kv("textId", textId);
}

}  // namespace projection

}  // namespace sub0canvas

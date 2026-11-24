#include "sub0canvas/resolver.hpp"

#include "sub0canvas/content_id.hpp"

#include "json_util.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace sub0canvas {

namespace {

constexpr bool isBlank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

/// `s` with every run of blanks one space and the ends trimmed.
std::string normalizeTrim(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        if (isBlank(c)) {
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

/// The character trigrams of a normalised text, sorted (a multiset: repeats count).
void trigramsOf(std::string_view s, std::vector<std::uint32_t>& out)
{
    out.clear();
    if (s.size() < 3) return;
    for (std::size_t i = 0; i + 2 < s.size(); ++i)
        out.push_back((static_cast<std::uint32_t>(static_cast<unsigned char>(s[i])) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(s[i + 1])) << 8) |
                      static_cast<std::uint32_t>(static_cast<unsigned char>(s[i + 2])));
    std::sort(out.begin(), out.end());
}

std::size_t intersectionSize(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b)
{
    std::size_t i = 0, j = 0, n = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] < b[j]) ++i;
        else if (b[j] < a[i]) ++j;
        else { ++n; ++i; ++j; }
    }
    return n;
}

struct Span
{
    std::size_t from = 0, to = 0;   ///< byte offsets, `to` exclusive
};

}  // namespace

/// One file as the resolver reads it: its lines, and, built when first needed, its whitespace-normalised form and content id.
struct AnchorResolver::Prepared
{
    const std::string* text = nullptr;
    std::vector<std::size_t> starts{0};                  ///< byte offset of each line's first character (no entry after a final newline)
    bool normBuilt = false;
    std::string norm;                                    ///< the text with blank runs as one space
    std::vector<std::uint32_t> origOf;                   ///< norm offset -> original offset (one more entry: text size)
    std::string id;

    explicit Prepared(const std::string& t) : text(&t)
    {
        for (std::size_t i = 0; i < t.size(); ++i)
            if (t[i] == '\n' && i + 1 < t.size()) starts.push_back(i + 1);
    }
    [[nodiscard]] int lines() const { return static_cast<int>(starts.size()); }
    [[nodiscard]] std::size_t lineBegin(int line) const { return starts[static_cast<std::size_t>(std::clamp(line, 1, lines())) - 1]; }
    /// End of a line, before its newline (and before a CR).
    [[nodiscard]] std::size_t lineEnd(int line) const
    {
        const int l = std::clamp(line, 1, lines());
        std::size_t e = static_cast<std::size_t>(l) < starts.size() ? starts[static_cast<std::size_t>(l)] - 1 : text->size() - (!text->empty() && text->back() == '\n' ? 1 : 0);
        if (e > lineBegin(l) && (*text)[e - 1] == '\r') --e;
        return e;
    }
    [[nodiscard]] int lineOf(std::size_t offset) const
    {
        return static_cast<int>(std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin());
    }
    [[nodiscard]] int columnOf(std::size_t offset) const { return static_cast<int>(offset - lineBegin(lineOf(offset))) + 1; }
    void buildNorm()
    {
        if (normBuilt) return;
        normBuilt = true;
        norm.reserve(text->size());
        origOf.reserve(text->size() + 1);
        for (std::size_t i = 0; i < text->size(); ++i) {
            const char c = (*text)[i];
            if (isBlank(c)) {
                if (!norm.empty() && norm.back() == ' ') continue;
                norm.push_back(' ');
            } else {
                norm.push_back(c);
            }
            origOf.push_back(static_cast<std::uint32_t>(i));
        }
        origOf.push_back(static_cast<std::uint32_t>(text->size()));
    }
    [[nodiscard]] const std::string& contentIdOf()
    {
        if (id.empty()) id = contentId(*text);
        return id;
    }
};

std::string_view toString(Outcome o)
{
    switch (o) {
    case Outcome::resolved: return "resolved";
    case Outcome::moved: return "moved";
    case Outcome::orphaned: return "orphaned";
    }
    return "orphaned";
}

std::string_view toString(Method m)
{
    switch (m) {
    case Method::unchanged: return "unchanged";
    case Method::exact: return "exact";
    case Method::context: return "context";
    case Method::whitespace: return "whitespace";
    case Method::fuzzy: return "fuzzy";
    case Method::block: return "block";
    case Method::none: return "none";
    }
    return "none";
}

std::string_view toString(Reason r)
{
    switch (r) {
    case Reason::none: return "none";
    case Reason::shifted: return "shifted";
    case Reason::blockFollowed: return "blockFollowed";
    case Reason::duplicates: return "duplicates";
    case Reason::duplicateByContext: return "duplicateByContext";
    case Reason::contextChanged: return "contextChanged";
    case Reason::whitespaceOnly: return "whitespaceOnly";
    case Reason::similar: return "similar";
    case Reason::ambiguousSimilar: return "ambiguousSimilar";
    case Reason::quoteLeftBlock: return "quoteLeftBlock";
    case Reason::noIdentity: return "noIdentity";
    case Reason::quoteNotInBlock: return "quoteNotInBlock";
    case Reason::blockRetired: return "blockRetired";
    case Reason::notFound: return "notFound";
    case Reason::fileGone: return "fileGone";
    case Reason::textUnavailable: return "textUnavailable";
    }
    return "notFound";
}

// ---- documents ---------------------------------------------------------------------------------------------------------------------------------------------

CanvasDocument::CanvasDocument(const BlockIndex& index, const std::map<std::string, std::string>& files, std::string revision)
    : index_(index), files_(files), revision_(std::move(revision))
{
}

AnchorDocument::TextRef CanvasDocument::text(const std::string& path) const
{
    const auto it = files_.find(path);
    if (it == files_.end()) return {Presence::absent, nullptr};
    return {Presence::present, &it->second};
}

AnchorDocument::Identity CanvasDocument::identity(const SourceAnchor& anchor) const
{
    Identity id;
    id.madeId = anchor.blockId;
    if (!anchor.blockId.empty()) id.block = index_.resolve(anchor.blockId);
    return id;
}

HistoryDocument::HistoryDocument(RevisionIndex& index, const DocumentSource& source, Scope scope, std::string revision)
    : index_(index), source_(source), scope_(std::move(scope)), revision_(std::move(revision))
{
}

const Replayed* HistoryDocument::target() const
{
    if (!targetTried_) {
        targetTried_ = true;
        if (auto at = index_.at(scope_, revision_)) target_ = *at;
    }
    return target_.get();
}

AnchorDocument::TextRef HistoryDocument::text(const std::string& path) const
{
    auto it = texts_.find(path);
    if (it == texts_.end()) {
        auto answers = source_.read({FileRef{revision_, path}});
        it = texts_.emplace(path, answers.empty() ? FileText::unknown() : std::move(answers.front())).first;
    }
    return {it->second.presence, it->second.presence == Presence::present ? &it->second.text : nullptr};
}

AnchorDocument::Identity HistoryDocument::identity(const SourceAnchor& anchor) const
{
    Identity id;
    id.madeId = anchor.blockId;
    const Replayed* now = target();
    if (!now) return id;
    if (id.madeId.empty()) {
        if (anchor.line <= 0) return id;
        auto made = index_.at(scope_, anchor.revision);
        if (!made) return id;
        const Block* b = (*made)->index.at(anchor.path, anchor.line);
        if (!b) return id;
        id.madeId = b->id;
    }
    auto carried = index_.carry(scope_, anchor.revision, id.madeId, revision_);
    if (!carried || !*carried) return id;
    id.block = now->index.resolve(**carried);
    return id;
}

const Block* HistoryDocument::blockAt(const std::string& path, int line) const
{
    const Replayed* now = target();
    return now ? now->index.at(path, line) : nullptr;
}

// ---- the resolver ------------------------------------------------------------------------------------------------------------------------------------------

AnchorResolver::AnchorResolver(const AnchorDocument& document, ResolvePolicy policy) : document_(document), policy_(std::move(policy)) {}
AnchorResolver::~AnchorResolver() = default;

AnchorResolver::Prepared* AnchorResolver::prepare(const std::string& path)
{
    auto it = prepared_.find(path);
    if (it != prepared_.end()) return it->second.get();
    const auto ref = document_.text(path);
    if (!ref.text) return nullptr;
    ++stats_.textsPrepared;
    return prepared_.emplace(path, std::make_unique<Prepared>(*ref.text)).first->second.get();
}

namespace {

/// Occurrences of `needle` in `hay[lo, hi)`, overlapping ones included, at most `cap` kept (the search stops there: more than one is all a caller needs to know).
std::vector<Span> findAll(std::string_view hay, std::size_t lo, std::size_t hi, std::string_view needle, std::size_t cap)
{
    std::vector<Span> out;
    if (needle.empty() || hi > hay.size() || lo >= hi) return out;
    const std::string_view region = hay.substr(lo, hi - lo);
    for (std::size_t at = region.find(needle); at != std::string_view::npos && out.size() < cap; at = region.find(needle, at + 1))
        out.push_back({lo + at, lo + at + needle.size()});
    return out;
}

}  // namespace

Resolution AnchorResolver::resolve(const SourceAnchor& a)
{
    ++stats_.resolves;
    std::size_t cand = 0;
    const ResolvePolicy& pol = policy_;

    const auto done = [&](Resolution r) {
        r.candidates = cand;
        stats_.candidates += cand;
        ++stats_.outcomes[static_cast<std::size_t>(r.outcome)];
        r.editable = r.outcome == Outcome::resolved && !a.quote.empty();
        return r;
    };
    const auto orphan = [&](Reason reason, std::string detail) {
        Resolution r;
        r.reason = reason;
        r.detail = std::move(detail);
        return done(std::move(r));
    };

    const AnchorDocument::Identity identity = document_.identity(a);
    const Block* blk = identity.block;
    const bool followed = blk && (blk->id != identity.madeId || blk->path != a.path);
    const bool elsewhere = blk && blk->path != a.path;   // the block moved to another file: only then is a path string built
    const std::string movedTo = elsewhere ? std::string{blk->path} : std::string{};
    const std::string& path = elsewhere ? movedTo : a.path;

    Prepared* prep = prepare(path);
    if (!prep) {
        const auto ref = document_.text(path);
        return ref.presence == Presence::unknown ? orphan(Reason::textUnavailable, "the text of " + path + " is not available here")
                                                 : orphan(Reason::fileGone, path + " does not exist at revision " + document_.revision());
    }
    const std::string& text = *prep->text;

    // Builds a resolution for [from, to) of the file; line-granular for whole-line results is the caller's choice of span.
    const auto place = [&](Span s, Outcome outcome, Method method, Reason reason, double confidence, std::string detail) {
        Resolution r;
        r.outcome = outcome;
        r.method = method;
        r.reason = reason;
        r.confidence = confidence;
        r.path = path;
        r.line = prep->lineOf(s.from);
        r.column = prep->columnOf(s.from);
        const std::size_t last = s.to > s.from ? s.to - 1 : s.from;
        r.endLine = prep->lineOf(last);
        r.endColumn = static_cast<int>(s.to - prep->lineBegin(r.endLine)) + 1;
        r.lineShift = a.line > 0 ? r.line - a.line : 0;
        const Block* here = document_.blockAt(path, r.line);
        r.blockId = here ? here->id : std::string{};
        r.detail = std::move(detail);
        return r;
    };

    // The byte range of the identity block (the whole file without one).
    Span region{0, text.size()};
    int regionFirst = 1, regionLast = prep->lines();
    if (blk) {
        regionFirst = std::clamp(blk->first, 1, prep->lines());
        regionLast = std::clamp(blk->last, regionFirst, prep->lines());
        region = {prep->lineBegin(regionFirst), prep->lineEnd(regionLast)};
    }

    // 0. Nothing about the document changed: the original position, verified.
    const bool sameDoc = !followed && path == a.path &&
                         (document_.revision() == a.revision || (!a.textId.empty() && prep->contentIdOf() == a.textId));
    if (sameDoc && a.line > 0 && !a.quote.empty()) {
        const std::size_t off = std::min(prep->lineBegin(a.line) + static_cast<std::size_t>(std::max(a.column, 1) - 1), text.size());
        if (text.compare(off, a.quote.size(), a.quote) == 0) {
            ++cand;
            Resolution r = place({off, off + a.quote.size()}, Outcome::resolved, Method::unchanged, Reason::none, 1.0, "the document is the one the anchor was made against");
            if (blk) r.blockId = blk->id;
            return done(std::move(r));
        }
    }

    // Whole block anchors (no quote).
    if (a.quote.empty()) {
        if (!blk) return orphan(identity.madeId.empty() ? Reason::noIdentity : Reason::blockRetired, "no quote and no live block to follow");
        const Span s{region.from, region.to};
        Resolution r = place(s, followed ? Outcome::moved : Outcome::resolved, Method::block, followed ? Reason::blockFollowed : Reason::none,
                             followed ? pol.identityMovedConfidence : 1.0, followed ? "the block was carried here by identity" : "the block, by identity");
        r.blockId = blk->id;
        return done(std::move(r));
    }

    // The choice among equal quotes: one with the full context pair, else the one nearest the old line.
    struct Pick
    {
        std::size_t index = 0;
        bool byContext = false;
    };
    const bool hasContext = !a.prefix.empty() || !a.suffix.empty();
    const auto contextHolds = [&](const Span& s) {
        const bool pre = a.prefix.empty() || (s.from >= a.prefix.size() && text.compare(s.from - a.prefix.size(), a.prefix.size(), a.prefix) == 0);
        const bool suf = a.suffix.empty() || (s.to + a.suffix.size() <= text.size() && text.compare(s.to, a.suffix.size(), a.suffix) == 0);
        return pre && suf;
    };
    const auto pickAmong = [&](const std::vector<Span>& hits) {
        Pick pick;
        if (hasContext) {
            std::size_t full = 0, which = 0;
            for (std::size_t i = 0; i < hits.size(); ++i)
                if (contextHolds(hits[i])) { ++full; which = i; }
            if (full == 1) { pick.index = which; pick.byContext = true; return pick; }
        }
        if (a.line > 0) {
            int best = -1;
            for (std::size_t i = 0; i < hits.size(); ++i) {
                const int d = std::abs(prep->lineOf(hits[i].from) - a.line);
                if (best < 0 || d < best) { best = d; pick.index = i; }
            }
        }
        return pick;
    };

    // Reports `hits` (one or several occurrences of the quote, exact or up to whitespace) found `inBlock` or elsewhere in the file.
    const auto report = [&](const std::vector<Span>& hits, bool whitespaceOnly, bool inBlock) {
        cand += hits.size();
        const double ceiling = inBlock ? 1.0 : pol.fileQuoteConfidence;   // outside the identity block nothing confirms the place
        if (hits.size() == 1) {
            const Span s = hits[0];
            if (!whitespaceOnly && inBlock && !followed && hasContext && !contextHolds(s)) {
                // The text is there once, but what surrounded it is not: it may be the anchored place edited around, or a copy left where the anchored one was
                // deleted. Not proven, so not `resolved`.
                return done(place(s, Outcome::moved, Method::exact, Reason::contextChanged, pol.contextChangedConfidence,
                                  "the quote, exactly, once, in its block, but its surroundings changed"));
            }
            if (!whitespaceOnly && inBlock && !followed) {
                Resolution r = place(s, Outcome::resolved, Method::exact, Reason::none, 1.0, "the quote, exactly, once, in its block");
                if (r.lineShift != 0) { r.reason = Reason::shifted; r.detail = "the quote, exactly, once, in its block; the lines moved by " + std::to_string(r.lineShift); }
                if (blk) r.blockId = blk->id;
                return done(std::move(r));
            }
            double conf = whitespaceOnly ? pol.whitespaceConfidence : followed ? pol.identityMovedConfidence : pol.fileQuoteConfidence;
            if (whitespaceOnly && followed) conf = std::min(conf, pol.identityMovedConfidence);
            conf = std::min(conf, ceiling);
            Reason reason = whitespaceOnly ? Reason::whitespaceOnly : followed && inBlock ? Reason::blockFollowed : blk ? Reason::quoteLeftBlock : Reason::noIdentity;
            Method method = whitespaceOnly ? Method::whitespace : Method::exact;
            std::string why = whitespaceOnly ? "the quote differs only in whitespace"
                              : reason == Reason::blockFollowed ? "the quote, exactly, in a block carried here by identity"
                              : reason == Reason::quoteLeftBlock ? "the quote, exactly, but no longer in its block"
                                                                 : "the quote, exactly, with no block identity to confirm it";
            return done(place(s, Outcome::moved, method, reason, conf, std::move(why)));
        }
        const Pick pick = pickAmong(hits);
        double conf = pick.byContext ? pol.contextConfidence : pol.duplicateConfidence;
        if (whitespaceOnly) conf = std::min(conf, pol.whitespaceConfidence);
        conf = std::min(conf, ceiling);
        Resolution r = place(hits[pick.index], Outcome::moved, pick.byContext ? Method::context : whitespaceOnly ? Method::whitespace : Method::exact,
                             pick.byContext ? Reason::duplicateByContext : Reason::duplicates, conf,
                             std::to_string(hits.size()) + " equal places" + (pick.byContext ? "; the context pair picks this one" : "; the nearest to the old line"));
        r.alternatives = hits.size() - 1;
        return done(std::move(r));
    };

    const std::string qn = normalizeTrim(a.quote);

    // 1. The quote, in the identity block: exactly, then up to whitespace.
    if (blk) {
        auto hits = findAll(text, region.from, region.to, a.quote, pol.maxOccurrences);
        if (!hits.empty()) return report(hits, false, true);
        if (!qn.empty()) {
            prep->buildNorm();
            const std::size_t nlo = static_cast<std::size_t>(std::lower_bound(prep->origOf.begin(), prep->origOf.end(), static_cast<std::uint32_t>(region.from)) - prep->origOf.begin());
            const std::size_t nhi = static_cast<std::size_t>(std::lower_bound(prep->origOf.begin(), prep->origOf.end(), static_cast<std::uint32_t>(region.to)) - prep->origOf.begin());
            std::vector<Span> ws;
            for (const Span s : findAll(prep->norm, nlo, std::min(nhi, prep->norm.size()), qn, pol.maxOccurrences)) ws.push_back({prep->origOf[s.from], prep->origOf[s.to - 1] + 1});
            if (!ws.empty()) return report(ws, true, true);
        }
    }

    // 2. The quote anywhere in the file (it left its block, or no block could be followed).
    {
        auto hits = findAll(text, 0, text.size(), a.quote, pol.maxOccurrences);
        if (!hits.empty()) return report(hits, false, false);
        if (!qn.empty()) {
            prep->buildNorm();
            std::vector<Span> ws;
            for (const Span s : findAll(prep->norm, 0, prep->norm.size(), qn, pol.maxOccurrences)) ws.push_back({prep->origOf[s.from], prep->origOf[s.to - 1] + 1});
            if (!ws.empty()) return report(ws, true, false);
        }
    }

    // 3. A similar text: whole-line windows of the quote's line count, nearest the old line first, within the window budget.
    std::size_t nonBlank = 0;
    for (const char c : qn) nonBlank += c != ' ' ? 1 : 0;
    bool truncated = false;
    if (nonBlank >= pol.fuzzyMinChars) {
        const int k0 = 1 + static_cast<int>(std::count(a.quote.begin(), a.quote.end(), '\n') - (a.quote.back() == '\n' ? 1 : 0));
        const int regionLines = regionLast - regionFirst + 1;
        const int k = std::min(std::max(k0, 1), regionLines);
        std::vector<std::uint32_t> qg, wg;
        trigramsOf(qn, qg);
        const int lastStart = regionLast - k + 1;
        const int hint = std::clamp(a.line > 0 ? a.line : regionFirst, regionFirst, lastStart);
        struct Scored { int start; double sim; };
        std::vector<Scored> scored;
        std::string window;
        std::size_t windows = 0;
        const auto score = [&](int start) {
            ++windows;
            const std::string_view raw(text.data() + prep->lineBegin(start), prep->lineEnd(start + k - 1) - prep->lineBegin(start));
            window = normalizeTrim(raw);
            trigramsOf(window, wg);
            if (qg.empty() || wg.empty()) return;
            const double inter = static_cast<double>(intersectionSize(qg, wg));
            const double denom = static_cast<double>(qg.size()) + (k == 1 ? static_cast<double>(std::min(wg.size(), qg.size() * 2)) : static_cast<double>(wg.size()));
            const double sim = 2.0 * inter / denom;
            if (sim >= pol.fuzzyMin) scored.push_back({start, sim});
        };
        for (int d = 0; regionLines > 0; ++d) {
            const bool lowOk = hint - d >= regionFirst, highOk = hint + d <= lastStart;
            if (!lowOk && !highOk) break;
            if (windows >= pol.fuzzyWindowBudget) { truncated = true; break; }
            if (lowOk) score(hint - d);
            if (d > 0 && highOk && windows < pol.fuzzyWindowBudget) score(hint + d);
        }
        cand += windows;
        stats_.fuzzyWindows += windows;
        if (!scored.empty()) {
            Scored best = scored.front();
            for (const Scored& s : scored) if (s.sim > best.sim) best = s;
            double second = 0.0;
            for (const Scored& s : scored) if (std::abs(s.start - best.start) >= k) second = std::max(second, s.sim);
            const bool ambiguous = second > 0.0 && best.sim - second < pol.fuzzyMargin;
            double conf = std::min(best.sim, blk ? pol.fuzzyCap : pol.fileQuoteConfidence);
            if (ambiguous) conf *= 0.5;
            std::size_t from = prep->lineBegin(best.start);
            while (from < text.size() && (text[from] == ' ' || text[from] == '\t')) ++from;
            Resolution r = place({from, prep->lineEnd(best.start + k - 1)}, Outcome::moved, Method::fuzzy, ambiguous ? Reason::ambiguousSimilar : Reason::similar, conf,
                                 "a similar text (similarity " + std::to_string(best.sim).substr(0, 4) + (blk ? ") in its block" : ") by quote alone"));
            if (ambiguous) r.alternatives = 1;
            r.truncated = truncated;
            return done(std::move(r));
        }
    }

    // 4. The block alone, or nothing.
    if (blk) {
        Resolution r = place({region.from, region.to}, Outcome::moved, Method::block, Reason::quoteNotInBlock, pol.blockOnlyConfidence,
                             "the block lives but its text no longer holds the quote");
        r.blockId = blk->id;
        r.truncated = truncated;
        return done(std::move(r));
    }
    Resolution gone;
    gone.reason = identity.madeId.empty() ? Reason::notFound : Reason::blockRetired;
    gone.detail = identity.madeId.empty() ? "nothing like the quote is in the file" : "the block no longer exists and nothing like the quote is in " + path;
    gone.truncated = truncated;
    return done(std::move(gone));
}

Resolution resolveAnchor(const SourceAnchor& anchor, const AnchorDocument& document, const ResolvePolicy& policy)
{
    AnchorResolver resolver(document, policy);
    return resolver.resolve(anchor);
}

std::string resolutionJson(const Resolution& r)
{
    json::Writer w;
    w.beginObject();
    w.kv("outcome", std::string(toString(r.outcome)));
    w.kv("method", std::string(toString(r.method)));
    w.kv("reason", std::string(toString(r.reason)));
    w.kv("confidence", r.confidence);
    w.kv("path", r.path);
    w.kv("blockId", r.blockId);
    w.kv("line", r.line);
    w.kv("column", r.column);
    w.kv("endLine", r.endLine);
    w.kv("endColumn", r.endColumn);
    w.kv("lineShift", r.lineShift);
    w.kv("alternatives", static_cast<std::int64_t>(r.alternatives));
    w.kvBool("editable", r.editable);
    w.kvBool("truncated", r.truncated);
    w.kv("detail", r.detail);
    w.endObject();
    return w.take();
}

}  // namespace sub0canvas

#pragma once
/// @file resolver.hpp
/// The source-anchor resolver (slice B4; docs/BACKBONE.md section 5.1, R-SRC-1, R-SRC-2, R-AUTH-3): maps a `SourceAnchor` made at one revision onto a document
/// at another, by the same rules for every source (a review comment, a gate failure, a planner's note).
///
/// Order of evidence, strongest first:
///   1. IDENTITY. The block the anchor belonged to is followed to the target by the block index (merges and moves included). The quote is looked for inside it.
///   2. THE QUOTE, exactly, then up to whitespace, in the identity block; when it left the block (or there is no identity), in the whole file.
///   3. THE QUOTE, fuzzily (character-trigram Dice similarity over whole-line windows, nearest the old line first), within a window budget.
///   4. The identity block alone, as a low-confidence location, when its text no longer holds the quote; else nothing: ORPHANED.
///
/// ZERO SILENT MOVES. `Outcome::resolved` is returned only when the quote's exact bytes were found ONCE, inside the block the anchor belongs to, with the block's
/// identity unchanged and, when the anchor carries a context pair, the same text around it (or, with the same revision or an equal `textId`, at the original position): such a location is an exact target (`Resolution::editable`) and
/// its line shift is reported, not hidden. Every other result is `moved` (a location was found but is not proven: a changed block, a changed file, text edited,
/// equal quotes in the block, a similar text) with a confidence and a reason, or `orphaned`. A display may show a `moved` location as probable; an edit must
/// require `editable` (docs/sessions/canvas-source-addressing.md, "Resolution and authority").
///
/// Policy as data (BACKBONE.md 14.4): every threshold and confidence is a field of `ResolvePolicy`, whose defaults are documented there; a client supplies its own
/// and records `policy.name` with what it stored. The resolver is pure: texts and identity come in through `AnchorDocument`, which has adapters for a canvas
/// (`CanvasDocument`) and for git or any `DocumentSource` history (`HistoryDocument`, over the `RevisionIndex`).

#include "sub0canvas/anchor.hpp"
#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/revision_index.hpp"
#include "sub0canvas/source.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sub0canvas {

/// What came of resolving an anchor. See the file comment for what `resolved` promises.
enum class Outcome { resolved, moved, orphaned };

/// What found the location.
enum class Method
{
    unchanged,   ///< same revision or same file content id: the original position, verified
    exact,       ///< the quote's bytes, once, in the identity block
    context,     ///< equal quotes told apart by the context pair
    whitespace,  ///< the quote up to runs of whitespace
    fuzzy,       ///< a similar text
    block,       ///< only the block: the quote is no longer in it
    none         ///< nothing found
};

/// Why a result is not simply `resolved` (or `none` for a plain exact hit), so a person or a policy can decide what to do with it.
enum class Reason
{
    none,               ///< exact, in place
    shifted,            ///< resolved, but at another line than the anchor's (text inserted or removed above)
    blockFollowed,      ///< the block's identity was carried here by a merge or a move to another file
    duplicates,         ///< the quote occurs more than once where it was looked for; the nearest to the old line is reported
    duplicateByContext, ///< equal quotes, one matches the context pair
    contextChanged,     ///< the quote is there once in its block, but the text around it (the context pair) is not what it was
    whitespaceOnly,     ///< the quote differs from the text only in whitespace
    similar,            ///< the text is similar, not the same
    ambiguousSimilar,   ///< two separate places are about equally similar
    quoteLeftBlock,     ///< the quote is no longer in its block; found elsewhere in the file
    noIdentity,         ///< no block identity could be followed; the quote alone placed it
    quoteNotInBlock,    ///< the block lives, the quoted text is gone from it: the location is the block
    blockRetired,       ///< the block no longer exists (deleted, or retired without a successor) and nothing like the quote is left
    notFound,           ///< nothing like the quote in the file
    fileGone,           ///< the file does not exist at the target revision
    textUnavailable     ///< the target text is unknown (a missing object): never an error, never a guess
};

[[nodiscard]] std::string_view toString(Outcome o);
[[nodiscard]] std::string_view toString(Method m);
[[nodiscard]] std::string_view toString(Reason r);

/// The result of resolving one anchor. `path`, the location and `blockId` are meaningful unless `outcome` is `orphaned`.
struct Resolution
{
    Outcome outcome = Outcome::orphaned;
    Method method = Method::none;
    Reason reason = Reason::notFound;
    double confidence = 0.0;          ///< 1.0 for `resolved`; below 1.0 for `moved` (see `ResolvePolicy`); 0 for `orphaned`
    std::string path;                 ///< where it is now (another file than the anchor's when its block moved there)
    std::string blockId;              ///< the block holding the location now ("" outside every block or when unknown)
    int line = 0, column = 0;         ///< the start of the location (1-based). A fuzzy or block location is line-granular: the column is the first non-blank one
    int endLine = 0, endColumn = 0;   ///< the end, the column just past the last character
    int lineShift = 0;                ///< `line` minus the anchor's line (0 when the anchor has none)
    std::size_t alternatives = 0;     ///< other equally good places that were found (equal quotes)
    std::size_t candidates = 0;       ///< what this resolution examined: occurrences found plus fuzzy windows scored (the deterministic cost, R-SCALE)
    bool truncated = false;           ///< the fuzzy window budget ran out before the region was covered
    bool editable = false;            ///< `resolved` with a quote: an exact, unique target an edit may be bound to
    std::string detail;               ///< one line for a person: what was found and how
};

/// The thresholds of resolution (BACKBONE.md 14.4). Defaults are the shipped policy "anchor-resolve@1".
struct ResolvePolicy
{
    std::string name = "anchor-resolve@1";
    double whitespaceConfidence = 0.95;      ///< the quote found up to whitespace, once, in the identity block
    double identityMovedConfidence = 0.95;   ///< exact once, but the block's identity was carried by a merge or into another file
    double contextConfidence = 0.85;         ///< equal quotes, exactly one with the full context pair
    double contextChangedConfidence = 0.8;   ///< exact once in the identity block, but the anchor's context pair no longer holds around it
    double duplicateConfidence = 0.5;        ///< equal quotes, none told apart: the nearest to the old line is reported
    double fileQuoteConfidence = 0.7;        ///< found by quote alone in the file (the quote left its block, or no identity); also the cap of a fuzzy match then
    double fuzzyMin = 0.6;                   ///< the least similarity (0-1) reported; below it the text counts as gone
    double fuzzyCap = 0.75;                  ///< the most confidence a similar text earns inside its block
    double fuzzyMargin = 0.05;               ///< a second, separate place within this of the best makes the match ambiguous (confidence halved)
    std::size_t fuzzyMinChars = 8;           ///< quotes with fewer non-blank characters are never matched fuzzily (too little evidence)
    std::size_t fuzzyWindowBudget = 4000;    ///< the most windows scored per resolution
    double blockOnlyConfidence = 0.3;        ///< the block lives but its text no longer holds the quote
    std::size_t maxOccurrences = 64;         ///< occurrences kept when a quote repeats (the count past it is not needed)
};

/// The target side of a resolution: a document at one revision. Contract: pure reads, deterministic, safe to call repeatedly; every returned pointer stays valid
/// for the document's life.
class AnchorDocument
{
public:
    virtual ~AnchorDocument() = default;

    /// The text of a file at the document's revision.
    struct TextRef
    {
        Presence presence = Presence::unknown;
        const std::string* text = nullptr;   ///< set when `presence` is `present`
    };
    /// What `identity` found: the live block now, and the id the anchor's block had when it was made (for telling a plain carry from a follow).
    struct Identity
    {
        const Block* block = nullptr;        ///< nullptr: no identity could be followed (none known, retired without successor, history not on one line)
        std::string madeId;
    };

    /// The opaque id of the document's revision (compared with `SourceAnchor::revision`).
    [[nodiscard]] virtual const std::string& revision() const = 0;
    [[nodiscard]] virtual TextRef text(const std::string& path) const = 0;
    /// Follow the anchor's block to this document: through the anchor's `blockId`, or, when it has none, the block that held its line at its revision.
    [[nodiscard]] virtual Identity identity(const SourceAnchor& anchor) const = 0;
    /// The innermost block holding `line` of `path` here, or nullptr.
    [[nodiscard]] virtual const Block* blockAt(const std::string& path, int line) const = 0;
};

/// A canvas as it stands: a `BlockIndex` fed every revision (so a block id made earlier is still followed) and the files' current texts. `SourceAnchor::blockId`
/// must be set (the index keeps no older state to derive one from); use `makeSourceAnchor` with the index when the anchor is made.
class CanvasDocument final : public AnchorDocument
{
public:
    /// `index` must outlive the document; `files` are copied.
    CanvasDocument(const BlockIndex& index, const std::map<std::string, std::string>& files, std::string revision);
    [[nodiscard]] const std::string& revision() const override { return revision_; }
    [[nodiscard]] TextRef text(const std::string& path) const override;
    [[nodiscard]] Identity identity(const SourceAnchor& anchor) const override;
    [[nodiscard]] const Block* blockAt(const std::string& path, int line) const override { return index_.at(path, line); }

private:
    const BlockIndex& index_;
    std::map<std::string, std::string> files_;
    std::string revision_;
};

/// A revision of a `DocumentSource`'s history (git, a canvas adapter): identity through `RevisionIndex::carry` over `scope` (the anchor's `revision` and this
/// document's must lie on one replay line of the scope, else no identity is followed and the quote alone decides), texts through `DocumentSource::read`.
/// Block ids belong to the replay of `scope`: an anchor made with another scope has none of its own and is derived from its line instead (leave `blockId` empty).
class HistoryDocument final : public AnchorDocument
{
public:
    /// `index` and `source` must outlive the document.
    HistoryDocument(RevisionIndex& index, const DocumentSource& source, Scope scope, std::string revision);
    [[nodiscard]] const std::string& revision() const override { return revision_; }
    [[nodiscard]] TextRef text(const std::string& path) const override;
    [[nodiscard]] Identity identity(const SourceAnchor& anchor) const override;
    [[nodiscard]] const Block* blockAt(const std::string& path, int line) const override;

private:
    [[nodiscard]] const Replayed* target() const;
    RevisionIndex& index_;
    const DocumentSource& source_;
    Scope scope_;
    std::string revision_;
    mutable std::shared_ptr<const Replayed> target_;
    mutable bool targetTried_ = false;
    mutable std::map<std::string, FileText> texts_;
};

/// What a resolver has done, for a benchmark or a test (R-SCALE-9: cost follows the evidence, not the size of the document).
struct ResolveStats
{
    std::size_t resolves = 0;
    std::size_t candidates = 0;        ///< occurrences found plus fuzzy windows scored, over every resolve
    std::size_t fuzzyWindows = 0;      ///< of those, the windows scored
    std::size_t textsPrepared = 0;     ///< files whose line table was built (warm resolves build none)
    std::size_t outcomes[3] = {0, 0, 0};   ///< by `Outcome`
};

/// Resolves anchors against one document, caching each file's line table, normalised text and content id so that a second anchor in the same file costs only
/// its own search. Not thread-safe.
class AnchorResolver
{
public:
    /// `document` must outlive the resolver.
    explicit AnchorResolver(const AnchorDocument& document, ResolvePolicy policy = {});
    ~AnchorResolver();
    AnchorResolver(const AnchorResolver&) = delete;
    AnchorResolver& operator=(const AnchorResolver&) = delete;

    /// Resolve one anchor. Deterministic: the same anchor, document and policy give the same bytes.
    [[nodiscard]] Resolution resolve(const SourceAnchor& anchor);
    [[nodiscard]] const ResolveStats& stats() const { return stats_; }
    [[nodiscard]] const ResolvePolicy& policy() const { return policy_; }

private:
    struct Prepared;
    [[nodiscard]] Prepared* prepare(const std::string& path);
    const AnchorDocument& document_;
    ResolvePolicy policy_;
    ResolveStats stats_;
    std::unordered_map<std::string, std::unique_ptr<Prepared>> prepared_;
};

/// One-shot resolution (builds a resolver for a single anchor).
[[nodiscard]] Resolution resolveAnchor(const SourceAnchor& anchor, const AnchorDocument& document, const ResolvePolicy& policy = {});

/// A resolution as a JSON object (for a transaction history or an inbox: the submitted address, what it resolved to and why, BACKBONE 5.1), keys in a fixed order.
[[nodiscard]] std::string resolutionJson(const Resolution& resolution);

}  // namespace sub0canvas

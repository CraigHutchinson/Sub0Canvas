#pragma once
/// @file doc_driver.hpp
/// The test DRIVER for requirement tests (docs/TESTING.md, "Requirements, not implementations"; docs/REQUIREMENTS.md).
///
/// Requirement tests say what a consumer can observe: "after a rename the block keeps its identity", "a failing test lands on the function holding the
/// literal under test", "the summary names the function and its current lines". They must not say how the library stores it. This header is the one
/// place that knows today's types (`BlockIndex`, `parseIssues`, `layeredJson`). When the architecture moves (a `Store` fed by events, B1; a compact
/// index, S1; revision ids that are strings, G1), this driver is rewritten and the requirement tests stay as they are.
///
/// Rules for what the driver exposes:
///   * identities are opaque strings: a test may compare two ids, never parse one or assume its form ("b7");
///   * blocks are found by what a person would name (a path and a function's last name), never by position in a container;
///   * carries are the FORMAT's names ("kept", "renamed", ...: docs/LAYERED_DOCUMENT.md), not the C++ enum;
///   * renderings a model sees are returned as text, and tests pin them exactly only where the text is itself the requirement (REQUIREMENTS.md, R-REN).
#include "sub0canvas/doc_layers.hpp"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace req {

using sub0canvas::Anchor;
using sub0canvas::LayerItem;
using Items = std::vector<LayerItem>;

/// Where a block stands now.
struct Span
{
    std::string path, name;
    int first = 0, last = 0;
};

/// The format's name of a carry (docs/LAYERED_DOCUMENT.md, "carry"): what the interchange file and the viewer use.
inline std::string carryName(sub0canvas::Carry c)
{
    using sub0canvas::Carry;
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

/// The last component of a qualified name (`Parser::term` -> `term`).
inline std::string leafOf(const std::string& name)
{
    const auto c = name.rfind(':');
    return c == std::string::npos ? name : name.substr(c + 1);
}

/// One layered document as a consumer drives it: texts written in turns, identities asked for by name, verdicts turned into issues, renderings.
class Doc
{
public:
    /// The text of `path` is now `text` (whatever made it: a SYMBOL replace, a whole-file rewrite, a move, a deletion).
    void write(const std::string& path, const std::string& text)
    {
        files_[path] = text;
        index_.update(path, text);
    }
    /// Later changes happen in turn `n`.
    void turn(int n)
    {
        turn_ = n;
        index_.setTurn(n);
    }

    /// The identity of the live block of `path` whose last name is `leaf`, or "" when there is none.
    [[nodiscard]] std::string id(const std::string& path, const std::string& leaf) const
    {
        for (const auto* b : index_.blocks(path))
            if (leafOf(std::string{b->name}) == leaf) return b->id;
        return {};
    }
    /// Where the block of identity `id` is now (following a merge to its survivor); nullopt when it is gone with no successor.
    [[nodiscard]] std::optional<Span> where(const std::string& id) const
    {
        const auto* b = index_.resolve(id);
        if (!b) return std::nullopt;
        return Span{std::string{b->path}, std::string{b->name}, b->first, b->last};
    }
    /// The identity a lookup of `id` lands on now: itself while live, its survivor after a merge, "" when gone.
    [[nodiscard]] std::string resolvesTo(const std::string& id) const
    {
        const auto* b = index_.resolve(id);
        return b ? b->id : std::string{};
    }
    /// The identity of the innermost block of `path` holding `line`, or "" for text outside every block.
    [[nodiscard]] std::string holder(const std::string& path, int line) const
    {
        const auto* b = index_.at(path, line);
        return b ? b->id : std::string{};
    }
    /// The live blocks of `path`, in file order.
    [[nodiscard]] std::vector<Span> blocks(const std::string& path) const
    {
        std::vector<Span> out;
        for (const auto* b : index_.blocks(path)) out.push_back({std::string{b->path}, std::string{b->name}, b->first, b->last});
        return out;
    }
    /// Whether the history records `carry` for `id`.
    [[nodiscard]] bool logged(const std::string& carry, const std::string& id) const
    {
        for (const auto& e : index_.events())
            if (e.id == id && carryName(e.carry) == carry) return true;
        return false;
    }
    /// One entry of a block's identity history, in the format's terms.
    struct HistoryEntry
    {
        std::string carry, id, detail;
        int turn = 0;
    };
    /// The identity history of `id` and of every id merged into it, in order.
    [[nodiscard]] std::vector<HistoryEntry> history(const std::string& id) const
    {
        std::vector<HistoryEntry> out;
        for (const auto& e : sub0canvas::blockHistory(index_, id)) out.push_back({carryName(e.carry), e.id, e.detail, e.turn});
        return out;
    }
    /// Every history entry of turn `n`, in order.
    [[nodiscard]] std::vector<HistoryEntry> historyOfTurn(int n) const
    {
        std::vector<HistoryEntry> out;
        for (const auto& e : index_.events())
            if (e.turn == n) out.push_back({carryName(e.carry), e.id, e.detail, e.turn});
        return out;
    }
    /// Whether a block of `path` holds the string or character literal `literal` (what a failing test is localised by).
    [[nodiscard]] bool holdsLiteral(const std::string& id, const std::string& literal) const
    {
        const auto* b = index_.resolve(id);
        if (!b) return false;
        const auto here = index_.blocks(std::string{b->path});
        const auto lits = index_.literals(std::string{b->path});
        for (std::size_t k = 0; k < here.size(); ++k) if (here[k] == b) return lits[k].count(literal) == 1;
        return false;
    }

    /// The issues of a gate's verdict, anchored to the document as it stands. `sources`: the test files, by path.
    [[nodiscard]] Items issues(const std::string& verdict, const std::map<std::string, std::string>& sources = {}) const
    {
        return sub0canvas::parseIssues(verdict, index_, sources);
    }
    /// The summary a model sees: one line per item.
    [[nodiscard]] std::string summary(const Items& items) const { return sub0canvas::layerSummary(items, index_); }
    /// One item in full, with its code at the CURRENT lines.
    [[nodiscard]] std::string detail(const LayerItem& item) const { return sub0canvas::layerDetail(item, index_, files_); }
    /// The first `count` items in full within `maxChars`.
    [[nodiscard]] std::string inlined(const Items& items, std::size_t count = 3, std::size_t maxChars = 3000) const
    {
        return sub0canvas::layerInline(items, index_, files_, count, maxChars);
    }
    /// The focus text a folded view reads, so anchored blocks stay open.
    [[nodiscard]] std::string focus(const Items& items) const { return sub0canvas::layerFocus(items, index_); }
    /// The interchange snapshot (`layered-doc-1`) of the document, written from a spec by field name (layers, revisions, revision ids, policies, `eventsFrom`,
    /// the texts already carried): the turn and the files are the document's.
    [[nodiscard]] std::string snapshot(sub0canvas::SnapshotSpec spec) const
    {
        spec.turn = turn_;
        spec.files = files_;
        return sub0canvas::layeredJson(spec, index_);
    }
    /// How many events the identity history holds now: what the next snapshot's `eventsFrom` is once this one is written.
    [[nodiscard]] std::size_t eventCount() const { return index_.events().size(); }

    [[nodiscard]] const std::map<std::string, std::string>& files() const { return files_; }

private:
    sub0canvas::BlockIndex index_;
    std::map<std::string, std::string> files_;
    int turn_ = 0;
};

/// An item made by hand (a review note, a prose description, an issue from elsewhere), by field name so that new fields never break a test.
struct ItemSpec
{
    std::string layer = "issues", kind = "test", title, detail, key;
    std::vector<Anchor> anchors;
};
inline LayerItem makeItem(const ItemSpec& s, int number = 1)
{
    LayerItem it;
    it.layer = s.layer;
    it.number = number;
    it.kind = s.kind;
    it.title = s.title;
    it.detail = s.detail;
    it.anchors = s.anchors;
    it.key = s.key;
    return it;
}
/// An anchor to a block (by identity), with the path and line it was found at.
inline Anchor blockAnchor(const std::string& id, const std::string& path, int line, Anchor::Confidence c = Anchor::Confidence::exact)
{
    Anchor a;
    a.blockId = id;
    a.path = path;
    a.line = line;
    a.confidence = c;
    return a;
}

}  // namespace req

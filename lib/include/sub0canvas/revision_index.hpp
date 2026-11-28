#pragma once
/// @file revision_index.hpp
/// The revision-aware index (docs/BACKBONE.md section 6, as SCALE.md section 4 changes it): block identity across a project's history, answered PER SCOPE
/// (a path or a few) so that resolving one anchor never replays the tree. A scope's REPLAY follows the first-parent line of its revisions, hands each
/// revision's changes to a `BlockIndex`, and keeps a checkpoint (a copy of the whole index state) every N revisions, so a question about any revision
/// replays at most N-1 revisions from the nearest checkpoint.
///
/// Mapping rules, each a decision (docs/BACKBONE.md section 6, "As built (G1)"):
///   * `BlockEvent.turn` is an int, so a replay numbers its revisions: ordinal 0 is the BASELINE (the state before the first replayed revision: empty at a
///     root, the listing at the parent when a `maxRevisions` window starts mid-history), ordinal k the k-th revision of the line. `history` gives the
///     revision id beside every event; the ordinal never leaves this class's answers except as `HistoryStep::ordinal` and `Replayed::ordinal`.
///   * A merge commit is its first-parent change, one revision (git blame --first-parent); the side branch's commits are not on the line.
///   * Git's rename detection hands whole-file moves over: a rename retires the old path's blocks and indexes the new path, so the revive rule keeps the
///     identities (a `retired` and a `revived` event at the revision). A single-path scope follows a rename BACKWARDS to the old name (`git log --follow`).
///     A function moved between files is followed only when both files are in the scope; the revision's files are applied smallest-growth first, so the
///     source file retires the block before the destination meets it.
///   * Ids are local to a replay (a scope's line): two scopes give different ids for one function, and a method that takes two scopes refuses to mix them.
///   * A file whose text is unknown (a missing object) keeps its blocks as of the last known text and is listed in `Replayed::unknown`: the view renders.

#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/source.hpp"

#include <cstddef>
#include <deque>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sub0canvas {

/// A (revision, path) the source could not supply. An empty path means the whole tree of the revision (the baseline listing).
struct Unknown
{
    std::string revision, path;
};

/// The identity state of a scope as of one revision of its replay line.
struct Replayed
{
    BlockIndex index;                 ///< the blocks of the scope's files; `BlockEvent.turn` holds replay ordinals
    std::string revision;             ///< the revision of the line this state is as of: the newest one at or before the one asked for that changed the scope
    int ordinal = 0;
    std::vector<Unknown> unknown;     ///< every unknown up to here, in order
    bool historyCut = false;          ///< older revisions exist that the source cannot supply: the oldest state is as of the cut, not as of the root
};

/// One identity event with the revision it happened at.
struct HistoryStep
{
    std::string revision;
    int ordinal = 0;
    BlockEvent event;                 ///< `event.turn == ordinal`
};

/// What a replay did: counts, so a test or a benchmark can say that cost followed the changes (R-SCALE-1, R-SCALE-9).
struct ReplayStats
{
    std::size_t historyCalls = 0, changesCalls = 0, readCalls = 0;
    std::size_t filesRead = 0;        ///< texts requested from the source (baseline and revisions)
    std::size_t commitsReplayed = 0;  ///< revisions applied, checkpoint replays included
    std::size_t updates = 0;          ///< `BlockIndex::update` calls
    std::size_t checkpoints = 0;      ///< states retained
    std::size_t linesBuilt = 0;
};

/// Block identity over a `DocumentSource`'s history. Not thread-safe; answers are cached, so ask the same questions freely.
class RevisionIndex
{
public:
    struct Options
    {
        std::size_t checkpointEvery = 50;   ///< a state is retained every this many revisions of a line (at least 1)
        std::size_t maxRevisions = 0;       ///< replay at most this many revisions back from the head (0: all); older ones form the baseline
        bool followRenames = true;          ///< chase a file added by a rename back to its old name (asks the source for the revision's whole diff)
        std::shared_ptr<const Languages> languages;  ///< a fixed registry for each replay and checkpoint (null uses the defaults)
    };

    template <class T> using Result = std::expected<T, std::string>;   ///< an error is a caller's mistake (an unresolvable revision), never a missing object

    explicit RevisionIndex(const DocumentSource& source) : RevisionIndex(source, Options{}) {}
    RevisionIndex(const DocumentSource& source, Options options);

    /// The state of `scope` as of `revision`: the newest revision of the line at or before it that changed the scope, replayed from the nearest checkpoint.
    /// The returned state is immutable and stays valid however the index is used afterwards.
    [[nodiscard]] Result<std::shared_ptr<const Replayed>> at(const Scope& scope, const std::string& revision);

    /// The live blocks at `revision` named `name` (a leaf like `term` or a qualified name like `Parser::term`), as ids; `path` narrows to one file. In file
    /// order; empty when none, several when the name is ambiguous (the caller decides).
    [[nodiscard]] Result<std::vector<std::string>> named(const Scope& scope, const std::string& revision, const std::string& name, const std::string& path = {});

    /// The identity history of block `id` up to `revision`: every event of the id and of ids merged into it, each with its revision, oldest first.
    [[nodiscard]] Result<std::vector<HistoryStep>> history(const Scope& scope, const std::string& revision, const std::string& id);

    /// The id at `to` of the block that was `id` at `from`: itself when it lives on, the id it was merged into when it was merged forward, nullopt when it was
    /// retired without a successor (or, looking back, did not exist yet). Both revisions must lie on one replay line of the scope, else an error.
    [[nodiscard]] Result<std::optional<std::string>> carry(const Scope& scope, const std::string& from, const std::string& id, const std::string& to);

    [[nodiscard]] const ReplayStats& stats() const { return stats_; }

private:
    struct State
    {
        Replayed view;
        std::map<std::string, std::size_t> sizes;   ///< by path: the length of the indexed text, to order a revision's files by growth
    };
    struct Line
    {
        Scope scope;                                 ///< the scope asked for, normalised
        Scope extended;                              ///< `scope` plus the old names a rename chase found
        std::vector<Commit> commits;                 ///< oldest first
        bool cut = false;
        std::string baseRevision;                    ///< the parent the first replayed revision follows when the window starts mid-history
        std::unordered_map<std::string, int> ordinalOf;   ///< revision id -> ordinal (1..n)
        std::map<std::string, int> askedAt;          ///< a revision that did not change the scope -> the ordinal of the newest one that did (0: before all)
        std::shared_ptr<State> frontier;             ///< the state after `frontier->view.ordinal` revisions
        std::map<int, std::shared_ptr<const State>> checkpoints;
        [[nodiscard]] const std::string& revisionAt(int ordinal) const { return ordinal == 0 ? baseRevision : commits[static_cast<std::size_t>(ordinal) - 1].info.id; }
    };
    struct Located
    {
        Line* line = nullptr;
        int ordinal = 0;
    };

    [[nodiscard]] Result<Located> locate(const Scope& scope, const std::string& revision);
    [[nodiscard]] Line& buildLine(const Scope& scope, const std::string& head);
    void applyBaseline(Line& line, State& state);
    /// Replays `line`'s revisions `from+1 .. to` into `state` (held by `holder` so that a retained checkpoint is never mutated), keeping checkpoints when asked.
    void advance(Line& line, std::shared_ptr<State>& holder, int to, bool retain);
    [[nodiscard]] std::shared_ptr<const State> stateAt(Line& line, int ordinal);

    const DocumentSource& source_;
    Options options_;
    ReplayStats stats_;
    std::map<std::string, std::deque<Line>> lines_;   ///< by normalised scope; a deque keeps every Line where it is
};

}  // namespace sub0canvas

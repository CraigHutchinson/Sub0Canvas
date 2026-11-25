#pragma once
/// @file view.hpp
/// Pinned and unpinned views (docs/VIEWS.md; requirements R-PIN-1..9, docs/REQUIREMENTS.md): a view is an observer of one document at a position. Pinned, it reads
/// at a fixed revision set and sees its own pending edits only through a labelled overlay; unpinned, it follows the tip and a refresh returns the delta since its
/// position in one of four nested forms (brief, elided, compacted, full). A view is DATA (`ViewRecord`), recorded as events (`view.put@1`) folded by reducers that
/// `registerViewReducers` adds, so replay gives the same views and positions; the library stays pure: no I/O, no threads, no clock reads (R-BB-7). Sessions, which
/// hold views, are in session.hpp.
///
/// Seams (each an interface with one implementation, VIEWS.md section 7): `ViewStore` (where records persist; `LogViewStore` writes events), `ScratchLayer` (a
/// view's pending edits; `FileScratch`), `Compactor` (how `compacted` merges; `BlockCompactor`), and, declared and empty, `PresenceProvider` and `ConflictView`.

#include "sub0canvas/anchor.hpp"
#include "sub0canvas/canvas.hpp"
#include "sub0canvas/claims.hpp"
#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/inbox.hpp"
#include "sub0canvas/store.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sub0canvas {

// ---- the pin ----------------------------------------------------------------------------------------------------------------------------------------------

/// The revision set a view reads at (VIEWS.md section 6, decision 1): `base` is the revision every path is read at, `overrides` names a path's own revision.
/// A canvas keeps a revision per file, so `pinAtTip` records one override per file in scope and a `sealed` pin: a path with no entry is then ABSENT (a file
/// born after the pin). A single-file document is simply `{base: r}`.
struct Pin
{
    int base = 0;
    std::map<std::string, int, std::less<>> overrides;
    bool sealed = false;   ///< true: a path with no override does not exist at this pin
    /// The revision `path` is read at; 0: absent at this pin.
    [[nodiscard]] int revisionOf(std::string_view path) const;
    bool operator==(const Pin&) const = default;
};

/// The pin at the tip: every file of the canvas whose path starts with one of `prefixes` (any file when empty) at its current revision; `base` is the highest.
[[nodiscard]] Pin pinAtTip(const Canvas& canvas, const std::set<std::string, std::less<>>& prefixes = {});

/// The text of `path` at the pin, or nullopt when the file is absent at it.
[[nodiscard]] std::optional<std::string> textAtPin(const Canvas& canvas, const Pin& pin, const std::string& path);

/// True when `path` starts with one of `prefixes` (or `prefixes` is empty).
[[nodiscard]] bool inScope(const std::set<std::string, std::less<>>& prefixes, std::string_view path);

/// Where a view stood when it last answered: the query layer's `at` (docs/QUERY.md section 1). For a canvas, `generation` is the sum of its files' revisions
/// (it grows by one for every accepted transaction and every new file).
struct Position
{
    std::uint64_t generation = 0, indexEvents = 0, storeHead = 0, storeEvents = 0;
    bool operator==(const Position&) const = default;
};
/// The position of a document now. `index` may be null.
[[nodiscard]] Position currentPosition(const Canvas& canvas, const Store& store, const BlockIndex* index);
[[nodiscard]] std::string positionJson(const Position& p);

// ---- the view as data -------------------------------------------------------------------------------------------------------------------------------------

enum class PinState { pinned, unpinned };
enum class Notify { none, flag };                              ///< `flag`: a content-free "you are stale" answer is available (decision 4)
enum class DeltaForm { brief, elided, compacted, full };       ///< nested: each form holds the one before it as its `brief` header
enum class CompactBy { block, blockItem };                     ///< decision 3: `compacted` merges by block identity; `blockItem` also folds item events under the block

[[nodiscard]] std::string_view toString(PinState s);
[[nodiscard]] std::string_view toString(Notify n);
[[nodiscard]] std::string_view toString(DeltaForm f);
[[nodiscard]] std::string_view toString(CompactBy c);
[[nodiscard]] std::optional<DeltaForm> parseDeltaForm(std::string_view name);

/// The rendering budget of a delta (R-AUTH-1: bounded by attention, not by the data). Data a client may supply.
struct DeltaBudget
{
    std::size_t maxChars = 8000;   ///< a form larger than this degrades one step towards `brief` (full, elided, compacted, brief)
    int context = 1;               ///< `elided`: unchanged lines kept each side of a changed region
    std::size_t briefTop = 3;      ///< `brief` names at most this many kinds and actors (the rest are counted), so its size does not follow the edit count
    std::size_t nameClip = 32;     ///< a kind, actor or path longer than this is clipped in `brief`
    std::size_t briefOverlaps = 2; ///< `brief` lists at most this many overlaps with pending work (the rest are counted)
    bool operator==(const DeltaBudget&) const = default;
};

/// A place a view holds that must survive the document changing (an issue, a note, a visible range). `key` is the address the session issued for it.
struct HeldAnchor
{
    std::string key;
    SourceAnchor anchor;
    std::string status = "editable";   ///< `editable`, `moved` (probable: no edit authority until re-read) or `orphaned`
    bool operator==(const HeldAnchor&) const = default;
};

/// What a session holds in `views`, as the log keeps it. Everything here replays; the pending edits (a `ScratchLayer`) are the session's memory, not the log's.
struct ViewRecord
{
    std::string id;                 ///< `<session>/v<n>`
    std::string session;
    WriterId owner;                 ///< the actor holding it
    std::string label;
    PinState state = PinState::pinned;
    Pin pin;                        ///< pinned: where it reads; unpinned: the revision set the last refresh left it at
    Position position;              ///< the `at` of the last answer; a pinned view's never moves until it is re-pinned
    SubscriptionQuery scope;        ///< paths (prefixes) select files; the rest selects store events, as an inbox does
    DeltaForm detail = DeltaForm::compacted;
    DeltaBudget budget;
    CompactBy compactBy = CompactBy::block;
    Notify notify = Notify::none;
    std::string parent;             ///< the view it was forked from
    std::string fingerprint;        ///< a fork's manifest fingerprint (`pinFingerprint`)
    std::vector<HeldAnchor> held;
    bool closed = false;
    [[nodiscard]] bool operator==(const ViewRecord& other) const;   ///< equal when their JSON is
};

/// Quotas of one session (VIEWS.md section 7), data.
struct SessionLimits
{
    std::uint64_t maxViews = 16, maxPendingBytes = std::uint64_t{1} << 20, maxSubscriptions = 32;
    Instant idleTtl = 3600;   ///< seconds of silence after which a session expires
    bool operator==(const SessionLimits&) const = default;
};

/// A session as the log keeps it: identity and clocks, never pending edits or presence.
struct SessionRecord
{
    std::string id;
    WriterId actor;
    SessionLimits limits;
    Instant created = 0, lastSeen = 0;
    bool closed = false;
    std::string closeReason;        ///< `closed` or `expired`
    bool operator==(const SessionRecord&) const = default;
};

[[nodiscard]] std::string viewJson(const ViewRecord& v);
[[nodiscard]] std::expected<ViewRecord, std::string> parseViewRecord(std::string_view json);
[[nodiscard]] std::string sessionJson(const SessionRecord& s);
[[nodiscard]] std::expected<SessionRecord, std::string> parseSessionRecord(std::string_view json);

/// Where views and sessions persist (decision 5). Reads return what the last `put` of that id held.
class ViewStore
{
public:
    virtual ~ViewStore() = default;
    virtual void putSession(const SessionRecord& record) = 0;
    virtual void putView(const ViewRecord& record) = 0;
    [[nodiscard]] virtual const SessionRecord* session(std::string_view id) const = 0;
    [[nodiscard]] virtual const ViewRecord* view(std::string_view id) const = 0;
    /// Every view of a session, closed ones included (an id is never reused), in id order.
    [[nodiscard]] virtual std::vector<const ViewRecord*> viewsOf(std::string_view session) const = 0;
    [[nodiscard]] virtual std::vector<const SessionRecord*> sessions() const = 0;
};

/// The records as the reducers fold them; `StoreState::ext<ViewsState>()`.
struct ViewsState
{
    std::map<std::string, SessionRecord, std::less<>> sessions;
    std::map<std::string, ViewRecord, std::less<>> views;
};

inline constexpr std::string_view kViewPutKind = "view.put@1";
inline constexpr std::string_view kSessionPutKind = "session.put@1";

/// Adds the reducers of the two kinds to `registry`, and the subscription reducers when the registry has none (a view owns one subscription). False when one of
/// the two kinds already had a reducer.
[[nodiscard]] bool registerViewReducers(ReducerRegistry& registry);

/// The log-backed `ViewStore`: every `put` is an event of the store's writer, folded into `ViewsState`; a second store replaying the log holds the same records.
class LogViewStore final : public ViewStore
{
public:
    /// `store` must outlive this object and have the view reducers (`registerViewReducers`).
    explicit LogViewStore(Store& store) : store_(store) {}
    void putSession(const SessionRecord& record) override;
    void putView(const ViewRecord& record) override;
    [[nodiscard]] const SessionRecord* session(std::string_view id) const override;
    [[nodiscard]] const ViewRecord* view(std::string_view id) const override;
    [[nodiscard]] std::vector<const ViewRecord*> viewsOf(std::string_view session) const override;
    [[nodiscard]] std::vector<const SessionRecord*> sessions() const override;

private:
    Store& store_;   // non-owning
};

// ---- line diffs -------------------------------------------------------------------------------------------------------------------------------------------

/// One changed region between two texts, in the numbering of the OLD text: `before` lines `baseFrom..baseTo` (1-based, inclusive) became `after`. An insertion has
/// `baseTo == baseFrom - 1` (the lines go after line `baseFrom - 1`), the same convention as the canvas's spans.
struct LineHunk
{
    int baseFrom = 0, baseTo = -1;
    int newFrom = 0, newTo = -1;                 ///< the same region in the new text (a deletion has `newTo == newFrom - 1`)
    std::vector<std::string> before, after;
    bool operator==(const LineHunk&) const = default;
};

/// The hunks that turn `oldText` into `newText`, by lines as the canvas splits them. Common head and tail are trimmed and the middle is an LCS (a middle too large
/// for it, over 4e6 line pairs, becomes one hunk), so the cost follows the change.
[[nodiscard]] std::vector<LineHunk> diffLines(const std::string& oldText, const std::string& newText);
/// `oldText` with only the hunks `chosen` applied (each must come from `diffLines(oldText, ...)`); lines end with a newline.
[[nodiscard]] std::string applyHunks(const std::string& oldText, const std::vector<LineHunk>& chosen);

// ---- scratch ----------------------------------------------------------------------------------------------------------------------------------------------

/// A view's pending edits (decision 2): what its owner sees laid over the base, and no one else. The first implementation holds the pending text of each file.
class ScratchLayer
{
public:
    virtual ~ScratchLayer() = default;
    /// The text of a file at the view's pin (nullopt: absent).
    using BaseText = std::function<std::optional<std::string>(const std::string& path)>;
    /// Applies a parsed reply to the pending state, in the view's own numbering (base plus what is already pending), all or nothing. `options.allowed` names the files
    /// that may be written; they are the only ones loaded.
    [[nodiscard]] virtual std::expected<ReplyApplied, std::string> stage(const CanvasReply& reply, const ApplyOptions& options, const BaseText& base) = 0;
    /// The pending text of `path`, or nullopt when nothing is pending there.
    [[nodiscard]] virtual std::optional<std::string> overlay(const std::string& path) const = 0;
    /// The files with pending text, sorted.
    [[nodiscard]] virtual std::vector<std::string> paths() const = 0;
    /// Replaces the pending text of a file; nullopt drops it.
    virtual void set(const std::string& path, std::optional<std::string> text) = 0;
    [[nodiscard]] virtual std::unique_ptr<ScratchLayer> clone() const = 0;
};

/// The first `ScratchLayer`: a map from path to pending text, edited through a private `Canvas` (so every reply form `applyReply` knows works here).
class FileScratch final : public ScratchLayer
{
public:
    [[nodiscard]] std::expected<ReplyApplied, std::string> stage(const CanvasReply& reply, const ApplyOptions& options, const BaseText& base) override;
    [[nodiscard]] std::optional<std::string> overlay(const std::string& path) const override;
    [[nodiscard]] std::vector<std::string> paths() const override;
    void set(const std::string& path, std::optional<std::string> text) override;
    [[nodiscard]] std::unique_ptr<ScratchLayer> clone() const override { return std::make_unique<FileScratch>(*this); }

private:
    std::map<std::string, std::string> pending_;
};

// ---- the delta --------------------------------------------------------------------------------------------------------------------------------------------

/// One changed region of a file between two positions, net, with the block it lies in at the new position.
struct Region
{
    LineHunk hunk;
    std::string blockId, blockName;   ///< empty: outside every block
};

/// One accepted canvas edit between two positions (an intermediate edit), with its own hunks when the form needs them (`full`).
struct EditStep
{
    int revision = 0;
    std::string by;
    int from = 0, to = 0, delta = 0;
    std::vector<LineHunk> hunks;
};

/// A file that changed in scope between two positions.
struct FileDelta
{
    std::string path;
    int fromRevision = 0, toRevision = 0;   ///< 0: the file did not exist at the old position
    std::string oldText, newText;
    std::vector<Region> regions;            ///< the net change
    std::vector<EditStep> steps;            ///< every edit between the positions, in revision order
};

/// An item the delta touched, as the store has it now.
struct ItemChange
{
    std::string key, layer, kind, title, state;
    std::string blockId;                    ///< the first anchor's block (for `CompactBy::blockItem`)
};

/// Where a pending edit meets a change made by someone else since the pin (`since`): the pending hunk's lines (base numbering) and who changed them.
struct Overlap
{
    std::string path;
    int from = 0, to = 0;
    std::string by;
    int revision = 0;
};

/// What changed in a view's scope between two positions: the input of every refresh form. Built by `makeDelta`, or by hand in a test.
struct Delta
{
    Position from, to;
    std::vector<FileDelta> files;
    std::vector<Event> events;              ///< the scoped store events in merge order
    std::vector<ItemChange> items;          ///< the items they are about, latest state, in layer order
    std::vector<Overlap> overlaps;          ///< filled by a pinned view's `since`
};

/// What the canvas and the store give a delta.
struct DeltaSources
{
    const Canvas* canvas = nullptr;
    const Store* store = nullptr;
    const BlockIndex* index = nullptr;      ///< optional: without it regions have no block
    std::optional<Inbox> inbox;             ///< the scope's store part (from an `InboxReader`); events of the view, session and subscription kinds are dropped here
};

/// The delta of `scope` from `since` (a revision set) and position `from` to the tip. `steps` also computes each edit's own hunks (the `full` form). Work follows the
/// changed files: nothing is read when the canvas has not moved.
[[nodiscard]] Delta makeDelta(const DeltaSources& sources, const Pin& since, const Position& from, const std::set<std::string, std::less<>>& scope, bool steps);

/// True for the kinds a delta never carries: the view and session records and the subscription bookkeeping.
[[nodiscard]] bool isViewBookkeeping(std::string_view kind);

// ---- compaction -------------------------------------------------------------------------------------------------------------------------------------------

struct CompactPolicy
{
    CompactBy by = CompactBy::block;
};

/// The net change of one block (or of a file's text outside every block): its hunks, intermediate states and reverted edits already gone.
struct CompactedChange
{
    std::string path, blockId, blockName;
    std::vector<LineHunk> hunks;
    std::vector<std::string> itemKeys;      ///< with `CompactBy::blockItem`: the items anchored here
};

struct Compacted
{
    std::vector<CompactedChange> changes;
    std::vector<ItemChange> items;          ///< items not folded under a block
};

/// How `compacted` merges a delta.
class Compactor
{
public:
    virtual ~Compactor() = default;
    [[nodiscard]] virtual Compacted compact(const Delta& delta, const CompactPolicy& policy) const = 0;
};

/// Merges by block identity: every region of one block in one file becomes one change; items fold to their latest state.
class BlockCompactor final : public Compactor
{
public:
    [[nodiscard]] Compacted compact(const Delta& delta, const CompactPolicy& policy) const override;
};

/// A delta rendered in one form. `form` is what was delivered; when the budget forced a step towards `brief`, `degraded` is true and `requested` says what was asked
/// (never a silently shortened diff). `json` is `view-delta-1`.
struct RenderedDelta
{
    DeltaForm requested = DeltaForm::brief, form = DeltaForm::brief;
    bool degraded = false;
    std::string json;
    Position to;     ///< what acknowledging this answer moves the view to
    std::size_t events = 0, files = 0;   ///< what the delta held (the measure of the refresh's cost), whichever form was delivered
};

/// The counts-and-keys header (the `brief` form's body), as JSON; bounded by `budget` whatever the size of the edits.
[[nodiscard]] std::string briefJson(const Delta& delta, const DeltaBudget& budget);
/// Renders `delta` in `form` (nested: elided, compacted and full each carry the brief), degrading towards `brief` while it exceeds `budget.maxChars`.
[[nodiscard]] RenderedDelta renderDelta(const Delta& delta, DeltaForm form, const DeltaBudget& budget, const Compactor& compactor, const CompactPolicy& policy);

/// The content id of a pin's manifest over `paths` (each path, its revision and its text id): what a fork records, so a checkpoint that no longer matches is reported.
[[nodiscard]] std::string pinFingerprint(const Canvas& canvas, const Pin& pin, const std::set<std::string, std::less<>>& prefixes);

/// The first change since `baseRevision` of `path` that overlaps lines `from..to` of that revision, by the canvas's own rule (a span wholly before shifts, wholly after
/// is left, anything else is a conflict), naming who changed them and in which revision.
struct Conflict
{
    std::string by;
    int revision = 0, from = 0, to = 0;
};
[[nodiscard]] std::optional<Conflict> conflictSince(const Canvas& canvas, const std::string& path, int baseRevision, int from, int to);

// ---- presence and conflict display: documented seams with default no-op implementations (VIEWS.md R-PIN-13) ------------------------------------------------

/// Another actor's live edit cursor over a region (presence, VIEWS.md R-PIN-13). A `PresenceProvider` supplies them; the default, `NoPresence`, reports none.
struct Peer
{
    WriterId actor;
    std::string path;
    int from = 0, to = 0;
};
class PresenceProvider
{
public:
    virtual ~PresenceProvider() = default;
    [[nodiscard]] virtual std::vector<Peer> peers(const ViewRecord& view) const = 0;
};
/// Two pending patches over the same lines, before either commits (VIEWS.md R-PIN-13). A `ConflictView` renders them; the default, `NoConflictView`, renders nothing.
struct PatchOverlap
{
    std::string path;
    int from = 0, to = 0;
    WriterId first, second;
};
class ConflictView
{
public:
    virtual ~ConflictView() = default;
    [[nodiscard]] virtual std::string render(const std::vector<PatchOverlap>& overlaps) const = 0;
};
/// The default providers: no peers, nothing to render.
class NoPresence final : public PresenceProvider
{
public:
    [[nodiscard]] std::vector<Peer> peers(const ViewRecord&) const override { return {}; }
};
class NoConflictView final : public ConflictView
{
public:
    [[nodiscard]] std::string render(const std::vector<PatchOverlap>&) const override { return {}; }
};

}  // namespace sub0canvas

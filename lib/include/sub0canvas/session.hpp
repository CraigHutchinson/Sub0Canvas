#pragma once
/// @file session.hpp
/// Sessions and the workspace that contains them (docs/VIEWS.md section 7). A client holds a SESSION, never a view: the session is the only holder of views, of
/// pending edits and of subscriptions, and a `Workspace` contains many sessions over one canvas and one store. A session sees other sessions' PUBLISHED state
/// (the canvas, the store's events, claims, notes) and never their pending edits or overlays; its quotas are data (`SessionPolicy`) and a refusal names the
/// limit; it is resumable by id with the same views and positions (they are events, view.hpp) and expires by policy against a number the caller passes in.
/// Identity (an actor and its capabilities) is supplied by the caller on every request: the library authenticates no one. Pure: no I/O, no threads, no clock
/// reads (R-BB-7).
///
/// Two seams are documented but have only their default implementation (VIEWS.md R-PIN-8 and R-PIN-13): presence and peer patches (`PresenceProvider` and
/// `ConflictView` in view.hpp, whose defaults report no peers and no overlaps) and the live channel (`openChannel` refuses a pinned view naming the pin, and an
/// unpinned one as `not-supported`). Not thread-safe: the process that owns the workspace serialises requests.

#include "sub0canvas/resolver.hpp"
#include "sub0canvas/view.hpp"

#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sub0canvas {

/// Who is asking, as the transport vouches for it.
struct Identity
{
    WriterId actor;
    std::set<std::string, std::less<>> capabilities;
};

/// A structured refusal. `code`: `unknown-session`, `not-your-session`, `closed`, `expired`, `unknown-view`, `unknown-subscription`, `quota`, `capability`,
/// `pinned`, `unpinned`, `pending-work`, `not-allowed`, `stage-refused`, `not-found`, `out-of-scope`, `unknown-address`, `foreign-address`, `not-editable`,
/// `ambiguous-address`, `orphaned`, `notify-off`, `not-supported`, `bad-request`. A quota refusal names the limit (`views`, `pendingBytes`, `subscriptions`) with its value.
struct SessionError
{
    std::string code, limit, message;
    std::uint64_t current = 0, max = 0;
};
template <class T>
using Result = std::expected<T, SessionError>;

/// The rules of containment, as data and behind a seam: quotas per actor, the capability an operation needs, how often a touch is written down.
class SessionPolicy
{
public:
    virtual ~SessionPolicy() = default;
    [[nodiscard]] virtual SessionLimits limitsFor(const Identity& who) const = 0;
    /// The capability `op` needs (`view.open`, `view.read`, `edit`, `advance`, `subscribe`); empty: none.
    [[nodiscard]] virtual std::string requiredCapability(std::string_view op) const = 0;
    /// A request refreshes a session's recorded `lastSeen` only when it moved by this many seconds (the log is not written on every request).
    [[nodiscard]] virtual Instant touchGranularity() const = 0;
};

/// The data-driven `SessionPolicy`: values from data. JSON: `{"limits":{"views":16,"pendingBytes":1048576,"subscriptions":32,"idleTtl":3600},
/// "actors":{"ns/writer":{...same members...}},"require":{"edit":"edit"},"touchGranularity":60}`; every member optional.
class DataSessionPolicy final : public SessionPolicy
{
public:
    SessionLimits defaults;
    std::map<std::string, SessionLimits, std::less<>> perActor;   ///< keyed `ns/writer`
    std::map<std::string, std::string, std::less<>> require;      ///< operation to capability
    Instant granularity = 60;

    [[nodiscard]] SessionLimits limitsFor(const Identity& who) const override;
    [[nodiscard]] std::string requiredCapability(std::string_view op) const override;
    [[nodiscard]] Instant touchGranularity() const override { return granularity; }
};
[[nodiscard]] std::expected<DataSessionPolicy, std::string> parseSessionPolicy(std::string_view json);

// ---- what a session returns -------------------------------------------------------------------------------------------------------------------------------

struct OpenSpec
{
    std::string label{};
    PinState state = PinState::pinned;
    SubscriptionQuery scope{};   ///< `paths` are file prefixes; the rest selects store events
    DeltaForm detail = DeltaForm::compacted;
    DeltaBudget budget{};
    CompactBy compactBy = CompactBy::block;
    Notify notify = Notify::none;
};

struct ReadOptions
{
    bool overlay = true;   ///< false: the pure base (`overlay=off`)
};
/// A file as a view reads it. `text` is bytes only; `overlaid` and `labelled` say whether the session's own pending edits are in it.
struct ReadResult
{
    std::string path;
    int revision = 0;
    std::string text;
    bool overlaid = false;
    std::string labelled;           ///< `[pending: lines 3-5, 9]` then `text`, when overlaid; else `text`
};

struct StageOptions
{
    std::set<std::string, std::less<>> allowed;   ///< the files the actor may write (the transport decides)
    bool partial = false;                          ///< `applyReply`'s mode: false all or nothing
};
struct StageResult
{
    std::vector<std::string> skipped;   ///< parts a partial stage could not apply
    std::uint64_t pendingBytes = 0;     ///< the session's pending total after this stage
    std::vector<Overlap> overlaps;      ///< where the pending work already meets someone else's change since the pin (advisory; landing decides)
};

/// A hunk that did not land: who changed the lines, in which revision, and what was to be written there (kept, so the author can redo it).
struct Refusal
{
    std::string path;
    int from = 0, to = 0;       ///< the lines in the view's base numbering
    std::string by;             ///< the other actor
    int revision = 0;           ///< their revision
    std::string message;
    std::string replacement;    ///< the text the hunk wrote
};
struct LandOptions
{
    bool atomic = false;   ///< true: the first refusal stops everything and nothing is applied
};
struct LandReport
{
    std::size_t applied = 0;                   ///< hunks that landed
    std::vector<Refusal> refused;
    std::map<std::string, int> revisions;      ///< landed files and the revision they are at
    bool rolledBack = false;                   ///< atomic mode refused: nothing was applied
};

struct AdvanceOptions
{
    bool atomic = false;
};
/// What happened to an anchor the view holds when it moved.
struct AnchorChange
{
    std::string key, status, outcome, reason, path;
    int line = 0;
    double confidence = 0;
};
struct AdvanceReport
{
    LandReport land;
    bool moved = false;                        ///< false: atomic advance refused and nothing changed
    Pin pin;                                   ///< where the view is now
    RenderedDelta delta;                       ///< what changed in scope since the old pin (the session's own landed edits included), in the view's form
    std::vector<AnchorChange> anchors;         ///< held anchors that are not `editable` now
};

struct RefreshOptions
{
    std::optional<DeltaForm> form{};           ///< default: the view's `detail`
    std::optional<DeltaBudget> budget{};
    bool acknowledge = false;                  ///< true: the view moves to the answer's position
};

struct ForkOptions
{
    bool withPending = false;                  ///< false: a viewpoint clone (the pin only); true: a scratch clone (the pending edits too)
    std::string label{};
};
struct ForkResult
{
    std::string id;
    std::map<std::string, std::string> addresses;   ///< the parent's held addresses to the fork's reissued ones
};
struct FingerprintCheck
{
    bool ok = false;
    std::string recorded, actual;
};
struct PendingSummary
{
    std::size_t files = 0, hunks = 0;
    std::uint64_t bytes = 0;
};

/// The `revision` a held anchor made at a view's position carries (`g<generation>`): the resolver compares it with the document's, so an anchor made at the
/// position the view stands at resolves by position alone.
[[nodiscard]] std::string anchorRevision(const ViewRecord& view);

class Workspace;

/// One client's context: its views, pending edits and subscriptions. Made and owned by a `Workspace`.
class Session
{
public:
    [[nodiscard]] const std::string& id() const { return id_; }
    [[nodiscard]] const Identity& identity() const { return who_; }
    [[nodiscard]] const SessionRecord& record() const;

    // views
    [[nodiscard]] Result<std::string> openView(const OpenSpec& spec);
    [[nodiscard]] Result<void> closeView(const std::string& view);
    [[nodiscard]] std::vector<const ViewRecord*> views() const;   ///< the open views, by id
    [[nodiscard]] Result<const ViewRecord*> view(const std::string& view) const;

    // reads (a pinned view reads at its pin, plus its own pending edits unless `overlay` is off)
    [[nodiscard]] Result<ReadResult> read(const std::string& view, const std::string& path, const ReadOptions& options = {}) const;
    /// The store's events as the view sees them: up to its pinned position (a tip view: all), verbatim, without the bookkeeping kinds.
    [[nodiscard]] Result<std::vector<Event>> events(const std::string& view) const;

    // pending work
    [[nodiscard]] Result<StageResult> stage(const std::string& view, const std::string& reply, const StageOptions& options);
    [[nodiscard]] Result<void> discard(const std::string& view);
    [[nodiscard]] Result<PendingSummary> pending(const std::string& view) const;
    /// Lands the pending edits on the canvas, rebased from the view's base or refused naming the other actor and revision; the pin does not move.
    [[nodiscard]] Result<LandReport> land(const std::string& view, const LandOptions& options = {});
    /// The edits that were refused by the last `advance`, kept so they can be redone.
    [[nodiscard]] Result<std::vector<Refusal>> kept(const std::string& view) const;

    // pin operations
    /// A brief of what changed in scope between the pin and the tip, with the overlaps with pending edits. Pull only; changes nothing.
    [[nodiscard]] Result<RenderedDelta> since(const std::string& view) const;
    /// Lands what can land (`atomic`: all or nothing), moves the pin to the tip, re-anchors held anchors, and returns the report.
    [[nodiscard]] Result<AdvanceReport> advance(const std::string& view, const AdvanceOptions& options = {});
    [[nodiscard]] Result<ForkResult> fork(const std::string& view, const ForkOptions& options = {});
    [[nodiscard]] Result<FingerprintCheck> checkFingerprint(const std::string& view) const;
    /// Becomes a tip view; its first refresh is the delta since the old pin. Refused while edits are pending.
    [[nodiscard]] Result<void> unpin(const std::string& view);

    // tip views
    [[nodiscard]] Result<RenderedDelta> refresh(const std::string& view, const RefreshOptions& options = {});
    /// With `notify = flag`: whether anything in scope changed since the position (no content).
    [[nodiscard]] Result<bool> isStale(const std::string& view) const;
    /// Refused for a pinned view naming the pin, and for an unpinned one as `not-supported`: the live channel is a seam with no transport behind it (VIEWS.md R-PIN-8).
    [[nodiscard]] Result<void> openChannel(const std::string& view) const;

    // held anchors and their authority
    [[nodiscard]] Result<std::string> hold(const std::string& view, const SourceAnchor& anchor);
    /// The anchor behind an address issued to THIS view, when it is editable; refusals name why (another branch's address, moved, orphaned).
    [[nodiscard]] Result<HeldAnchor> resolve(const std::string& view, const std::string& address) const;
    /// Re-anchors a moved anchor at where it resolves in the view's own text, restoring its authority.
    [[nodiscard]] Result<HeldAnchor> reread(const std::string& view, const std::string& address);

    // per-session subscriptions (cursors owned by the session)
    [[nodiscard]] Result<std::string> subscribe(const SubscriptionQuery& query);
    [[nodiscard]] Result<Inbox> inbox(const std::string& subscription) const;
    [[nodiscard]] Result<void> acknowledge(const std::string& subscription, std::uint64_t upTo);

private:
    friend class Workspace;
    struct Runtime
    {
        std::unique_ptr<ScratchLayer> scratch;
        std::vector<Refusal> kept;
    };
    Session(Workspace& ws, std::string id, Identity who) : ws_(ws), id_(std::move(id)), who_(std::move(who)) {}

    [[nodiscard]] Result<void> allow(std::string_view op) const;
    [[nodiscard]] Result<const ViewRecord*> mine(const std::string& view) const;
    [[nodiscard]] Runtime& runtime(const std::string& view) const;
    [[nodiscard]] Result<void> admitView() const;
    [[nodiscard]] std::uint64_t pendingBytesOf(const ViewRecord& rec, const ScratchLayer& scratch) const;
    [[nodiscard]] std::uint64_t pendingBytesTotal(const std::string& except, std::uint64_t replacement) const;
    [[nodiscard]] std::vector<Overlap> overlapsOf(const ViewRecord& rec, const ScratchLayer& scratch) const;
    struct Built;
    [[nodiscard]] Result<Built> build(const ViewRecord& rec, bool steps) const;
    [[nodiscard]] LandReport landImpl(const ViewRecord& rec, Runtime& rt, bool atomic);
    void save(const ViewRecord& rec) const;
    [[nodiscard]] std::vector<AnchorChange> reanchor(ViewRecord& rec, const std::set<std::string>& extraPaths) const;
    [[nodiscard]] std::string viewSub(const std::string& view) const { return "view:" + view; }

    Workspace& ws_;   // non-owning; the workspace owns the session
    std::string id_;
    Identity who_;
    Instant lastSeenLive_ = 0;
    mutable std::map<std::string, Runtime, std::less<>> runtimes_;
};

/// The server-side container of sessions over one canvas and one store (the `serve` process owns one). Holds references; every one must outlive it.
class Workspace
{
public:
    /// `store` needs `registerViewReducers` in its registry. `index` (optional) gives views their blocks and the resolver its identity.
    Workspace(Canvas& canvas, Store& store, ViewStore& views, const SessionPolicy& policy, const Compactor& compactor, const BlockIndex* index = nullptr);
    ~Workspace();
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;

    /// A new session for `who` at `now`; its id is the next free `s<n>`. Refused when the store has no view reducers.
    [[nodiscard]] Result<std::string> open(const Identity& who, Instant now);
    /// The session `id` for a request at `now` from `who`: refuses an unknown id, one that is not `who`'s (the same answer for a stranger's session as for none
    /// would leak nothing; the code differs only for the owner's own debugging), a closed one, and one idle past its limit (which it then closes as expired).
    /// A session known only to the log (a restarted process) is rebuilt: same views, same positions, pending edits lost.
    [[nodiscard]] Result<Session*> attach(std::string_view id, const Identity& who, Instant now);
    /// Ends a session: its views are closed and its subscriptions ended.
    [[nodiscard]] Result<void> close(std::string_view id, const Identity& who, Instant now);
    /// Expires every session idle past its limit at `now`; returns how many.
    [[nodiscard]] std::size_t sweep(Instant now);

private:
    friend class Session;
    void finish(const std::string& id, Instant now, const std::string& reason);

    Canvas& canvas_;
    Store& store_;
    ViewStore& views_;
    const SessionPolicy& policy_;
    const Compactor& compactor_;
    const BlockIndex* index_;
    BlockIndex emptyIndex_;
    std::unique_ptr<InboxReader> reader_;
    std::map<std::string, std::unique_ptr<Session>, std::less<>> sessions_;
};

}  // namespace sub0canvas

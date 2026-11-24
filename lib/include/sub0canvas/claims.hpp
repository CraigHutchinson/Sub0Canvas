#pragma once
// Claims and notes (BACKBONE.md section 3.5; slice B3; requirement R-BB-6): a claim is a time-limited lease on an item, a block or a file, held by a writer.
// Everything is an event folded by reducers registered through `registerClaimReducers` (store.cpp is not edited): `claim.take@1`, `claim.renew@1`,
// `claim.release@1`, `claim.expire@1`, `block.carry@1` (what became of a block id, from the `BlockIndex`'s events) and `note@1`.
//
// Time is a number the caller passes in (`Instant`, whole seconds from an epoch the client chooses), never read here and never parsed from the event's display
// `time` string: a `take` carries its own `at` and `ttl`, so the fold is a pure function of the log and replay equals live (R-BB-1, R-BB-7). A claim is live
// from `at` until `expires = at + ttl` (exclusive); nothing needs an `expire` event to stop a claim blocking others, the clock does that, and `expireDue` only
// writes the fact down (so an item held by an expired claim goes back to open).
//
// Concurrent takes by two writers settle by merge order: the earlier `(lamport, writer)` wins, the later one is kept in the log as a refused claim
// (`ClaimStatus::refused`, naming the claim that blocked it), identically on every replica. A refusal made before writing (`checkClaim`) costs no event.
//
// The rules the client may supply are data: `ClaimPolicy` (per mode: exclusive or shared, whether it holds the item, default and maximum ttl, and whether a
// file claim covers the file's block claims), with a JSON form (`parseClaimPolicy`). The same policy must be given to the registry and to the callers that
// write, as the fold is what enforces it.
#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/event.hpp"
#include "sub0canvas/store.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sub0canvas {

/// Whole seconds from an epoch the client chooses (unix seconds, a turn clock, a test counter). The numeric time seam: the core never reads a clock.
using Instant = std::int64_t;
/// A client's clock, for callers that want one; every function here takes the instant explicitly.
using Clock = std::function<Instant()>;

/// What a claim covers. An item is a store key, a block is a `BlockIndex` id (resolved through merges, splits and retirements), a file is a path.
enum class TargetKind { item, block, file };
[[nodiscard]] std::string_view targetKindName(TargetKind k);
[[nodiscard]] std::optional<TargetKind> parseTargetKind(std::string_view name);

/// How one mode of claim behaves.
struct ModePolicy
{
    bool exclusive = false;       ///< at most one live claim of this mode per target (held by someone else than the taker)
    bool holdsItem = false;       ///< a live claim of this mode on an item sets it `held`; the end of the claim sets it `open` again unless it moved on
    Instant defaultTtl = 300;     ///< seconds, when a request names none
    Instant maxTtl = 3600;        ///< seconds, a longer ttl is clamped by the fold (0: no limit)
};

/// The claim rules as data (AGENTS.md: data-driven). Modes are open strings; `standard()` has `write` (exclusive, holds the item) and `observe` (shared).
struct ClaimPolicy
{
    std::map<std::string, ModePolicy, std::less<>> modes;
    bool fileCoversBlocks = true;   ///< an exclusive claim on a file conflicts with one on a block of it (the block claim names its `path`)

    [[nodiscard]] static ClaimPolicy standard();
    [[nodiscard]] const ModePolicy* mode(std::string_view name) const;
};

/// `{"modes":{"write":{"exclusive":true,"holdsItem":true,"ttl":300,"maxTtl":3600},...},"fileCoversBlocks":true}`: members left out keep the standard value
/// (a `modes` object replaces the standard modes). The error names the first problem.
[[nodiscard]] std::expected<ClaimPolicy, std::string> parseClaimPolicy(std::string_view json);

enum class ClaimStatus { live, released, expired, retired, refused };
[[nodiscard]] std::string_view claimStatusName(ClaimStatus s);

/// A claim as the fold holds it. `holder` is the writer of the `take` event (authorship from the log, A6).
struct Claim
{
    std::string id;                 ///< the `take` event's subject: `ns/writer:lamport`
    TargetKind kind = TargetKind::item;
    std::string target;             ///< as named when taken (a block id stays the id it was taken on)
    std::string path;               ///< a block claim's file, for `fileCoversBlocks`; empty: none
    std::string mode;
    WriterId holder;
    Instant taken = 0, expires = 0;
    ClaimStatus status = ClaimStatus::live;
    std::string reason;             ///< why it ended: the release's reason, `expired`, `retired`; for a refusal, the claim that blocked it
    std::vector<std::string> covers;   ///< what it covers now, as index keys (`block:b7`): a merged block's claim covers the survivor, a split's both parts
    Stamp at;
};

/// What a refusal says (R-BB-6): who holds the target and until when.
struct ClaimRefusal
{
    enum class Why { conflict, retired, unknownMode, badRequest };
    Why why = Why::badRequest;
    std::string message;            ///< for a person: names the holder and the expiry
    std::string claimId;            ///< the blocking claim (conflict)
    WriterId holder;                ///< its holder
    Instant expires = 0;            ///< when it frees the target
};

/// A request for a lease. `ttl` empty: the mode's default.
struct ClaimRequest
{
    TargetKind kind = TargetKind::item;
    std::string target;
    std::string path;               ///< a block's file (optional)
    std::string mode = "write";
    std::optional<Instant> ttl;
};

/// Registers the reducers of every kind in this header; false when one of the kinds already has a reducer. The policy is copied into them.
[[nodiscard]] bool registerClaimReducers(ReducerRegistry& registry, ClaimPolicy policy = ClaimPolicy::standard());

// ---- writing --------------------------------------------------------------------------------------------------------------------------------------------
// `self` is the store's writer (`StoreOptions::writer`): the holder the claim will have. The checks read the folded state and make no event.

/// Whether `self` could take `request` at `now`: a refusal names the holder and expiry of the claim in the way (R-BB-6). A write claim by the same holder never
/// conflicts with its own.
[[nodiscard]] std::expected<void, ClaimRefusal> checkClaim(const Store& store, const ClaimPolicy& policy, const WriterId& self, const ClaimRequest& request, Instant now);
/// `checkClaim`, then the `claim.take@1` event; returns the claim as folded.
[[nodiscard]] std::expected<Claim, ClaimRefusal> takeClaim(Store& store, const ClaimPolicy& policy, const WriterId& self, const ClaimRequest& request, Instant now);
/// Extends a live claim of `self` to `now + ttl` (clamped; never shortened). An error when the claim is unknown, not live at `now`, or not `self`'s.
[[nodiscard]] std::expected<Claim, std::string> renewClaim(Store& store, const WriterId& self, const std::string& claimId, std::optional<Instant> ttl, Instant now,
                                                           const ClaimPolicy& policy);
/// Ends a live claim of `self`. An error when the claim is unknown, already over, or not `self`'s.
[[nodiscard]] std::expected<void, std::string> releaseClaim(Store& store, const WriterId& self, const std::string& claimId, Instant now, const std::string& reason = {});
/// Writes a `claim.expire@1` for every live claim whose time has passed at `now`; returns how many. Any writer may call it (the consumer's daemon decides
/// when to look); a claim that is merely past its time already blocks nobody.
std::size_t expireDue(Store& store, Instant now);

/// What happened to blocks, from the `BlockIndex`'s events at position `from` on (`BlockIndex::events()`): a `merged` block is carried to its survivor
/// (`detail`), a `retired` one ends its claims, a `revived` one is live again. Other carries keep the id, so claims need nothing. Returns the events written.
std::size_t emitBlockCarries(Store& store, const std::vector<BlockEvent>& events, std::size_t from = 0);
/// The same for a fact stated outside the index, such as a split: the block `id` is now the blocks `into` (several: a split, one: a merge or a move, none:
/// retired). A claim on `id` then covers all of them.
const Event& emitBlockCarry(Store& store, const std::string& id, const std::vector<std::string>& into);

/// A free-text note on an item or a block; the author is the event's writer. A block's notes follow it through merges and splits like its claims.
const Event& addNote(Store& store, TargetKind kind, const std::string& target, const std::string& text);

// ---- reading --------------------------------------------------------------------------------------------------------------------------------------------

/// One claim by id, in any status; null when unknown.
[[nodiscard]] const Claim* findClaim(const Store& store, std::string_view claimId);
/// The claims live at `now` on a target. A block id is resolved first (merged, split, retired), so asking about an old id finds the claims on what it became.
/// O(1) lookups plus the claims on the target.
[[nodiscard]] std::vector<Claim> claimsOn(const Store& store, TargetKind kind, std::string_view target, Instant now);
/// The live exclusive holder of a target in `mode` (the one `checkClaim` would name), when there is one.
[[nodiscard]] std::optional<Claim> holderOf(const Store& store, const ClaimPolicy& policy, TargetKind kind, std::string_view target, std::string_view mode, Instant now);
/// What a block id is now: itself, the survivors of a merge or split, or nothing when retired.
[[nodiscard]] std::vector<std::string> resolveBlock(const Store& store, std::string_view id);
/// The claims in every status counted (a store with many takes and releases keeps them all, as the log does).
[[nodiscard]] std::size_t claimCount(const Store& store);

struct Note
{
    TargetKind kind = TargetKind::item;
    std::string target;     ///< as named when written
    std::string text;
    Stamp at;               ///< author, lamport and display time from the log
};
/// The notes of a target in merge order; a block id is resolved first, and the notes of the blocks that became it are included.
[[nodiscard]] std::vector<Note> notesOn(const Store& store, TargetKind kind, std::string_view target);

/// The live claims at `now` as JSON (`claims-1`, the snapshot's `claims` member: BACKBONE section 5), sorted by claim id, deterministic: replay equals live.
[[nodiscard]] std::string claimsJson(const Store& store, Instant now);

}  // namespace sub0canvas

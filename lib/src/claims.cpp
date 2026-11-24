#include "sub0canvas/claims.hpp"

#include "json_util.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <memory>
#include <stdexcept>
#include <set>
#include <tuple>
#include <utility>

namespace sub0canvas {

// ---- names and the policy ------------------------------------------------------------------------------------------------------------------------------

std::string_view targetKindName(TargetKind k)
{
    switch (k) {
        case TargetKind::item: return "item";
        case TargetKind::block: return "block";
        case TargetKind::file: return "file";
    }
    return "item";
}

std::optional<TargetKind> parseTargetKind(std::string_view name)
{
    if (name == "item") return TargetKind::item;
    if (name == "block") return TargetKind::block;
    if (name == "file") return TargetKind::file;
    return std::nullopt;
}

std::string_view claimStatusName(ClaimStatus s)
{
    switch (s) {
        case ClaimStatus::live: return "live";
        case ClaimStatus::released: return "released";
        case ClaimStatus::expired: return "expired";
        case ClaimStatus::retired: return "retired";
        case ClaimStatus::refused: return "refused";
    }
    return "live";
}

ClaimPolicy ClaimPolicy::standard()
{
    ClaimPolicy p;
    p.modes["write"] = ModePolicy{.exclusive = true, .holdsItem = true, .defaultTtl = 300, .maxTtl = 3600};
    p.modes["observe"] = ModePolicy{.exclusive = false, .holdsItem = false, .defaultTtl = 60, .maxTtl = 600};
    return p;
}

const ModePolicy* ClaimPolicy::mode(std::string_view name) const
{
    const auto it = modes.find(name);
    return it == modes.end() ? nullptr : &it->second;
}

std::expected<ClaimPolicy, std::string> parseClaimPolicy(std::string_view text)
{
    ClaimPolicy p = ClaimPolicy::standard();
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = json::keyOf(f);
                auto v = f.value();
                if (k == "fileCoversBlocks") {
                    p.fileCoversBlocks = v.get_bool();
                } else if (k == "modes") {
                    p.modes.clear();
                    for (auto mf : v.get_object()) {
                        const std::string name{json::keyOf(mf)};
                        ModePolicy m;
                        for (auto pf : mf.value().get_object()) {
                            const std::string_view pk = json::keyOf(pf);
                            auto pv = pf.value();
                            if (pk == "exclusive") m.exclusive = pv.get_bool();
                            else if (pk == "holdsItem") m.holdsItem = pv.get_bool();
                            else if (pk == "ttl") m.defaultTtl = pv.get_int64();
                            else if (pk == "maxTtl") m.maxTtl = pv.get_int64();
                        }
                        if (name.empty() || m.defaultTtl <= 0 || m.maxTtl < 0 || (m.maxTtl > 0 && m.defaultTtl > m.maxTtl))
                            throw std::runtime_error(std::format("mode \"{}\": ttl must be positive and not above maxTtl", name));
                        p.modes[name] = m;
                    }
                }
            }
        });
    } catch (const simdjson::simdjson_error& e) {
        return std::unexpected(std::format("claim policy: {}", simdjson::error_message(e.error())));
    } catch (const std::runtime_error& e) {
        return std::unexpected(std::format("claim policy: {}", e.what()));
    }
    return p;
}

// ---- the folded state ----------------------------------------------------------------------------------------------------------------------------------

namespace {

constexpr std::string_view kTake = "claim.take@1", kRenew = "claim.renew@1", kRelease = "claim.release@1", kExpire = "claim.expire@1", kCarry = "block.carry@1",
                           kNote = "note@1";

using Ids = std::vector<std::string>;
template <class V>
using StringMap = std::unordered_map<std::string, V, StringHash, std::equal_to<>>;

/// Everything the claim and note reducers keep, in `StoreState::ext` (rebuilt with the rest on every replay). Lookups are hash lookups: a claim by id, the
/// live claims on a target by its key, so asking about one target never scans the others (SCALE.md).
struct ClaimState
{
    StringMap<Claim> claims;             ///< by id, every status (node-based: references stay valid)
    StringMap<Ids> byKey;                ///< cover key (`item:k`, `block:b7`, `file:p`) to the live claims on it
    StringMap<Ids> byPath;               ///< a file to the live block claims that name it
    StringMap<Ids> successors;           ///< block id to what it became (empty: retired)
    std::vector<Note> notes;             ///< in merge order
    StringMap<std::vector<std::size_t>> notesByKey;
};

const ClaimState& emptyState()
{
    static const ClaimState empty;
    return empty;
}

const ClaimState& stateOf(const Store& store)
{
    const ClaimState* cs = store.state().extIf<ClaimState>();
    return cs != nullptr ? *cs : emptyState();
}

std::string keyOf(TargetKind kind, std::string_view target) { return std::format("{}:{}", targetKindName(kind), target); }

Stamp stampOf(const Event& e) { return {e.writer, e.lamport, e.time}; }

/// A live claim is one not ended and not yet past its time at `at`. The taker's `at` is the judge in the fold, the caller's `now` in a query.
bool isLive(const Claim& c, Instant at) { return c.status == ClaimStatus::live && at < c.expires; }

/// What a block id is now, following merges and splits (`successors`); empty when retired. Depth-limited so a malformed log cannot loop.
void resolveInto(const ClaimState& cs, std::string_view id, Ids& out, int depth = 0)
{
    const auto it = cs.successors.find(id);
    if (it == cs.successors.end()) {
        if (std::find(out.begin(), out.end(), id) == out.end()) out.emplace_back(id);
        return;
    }
    if (depth > 64) return;
    for (const std::string& next : it->second) resolveInto(cs, next, out, depth + 1);
}

Ids coverKeys(const ClaimState& cs, TargetKind kind, std::string_view target)
{
    if (kind != TargetKind::block) return {keyOf(kind, target)};
    Ids ids, keys;
    resolveInto(cs, target, ids);
    for (const std::string& id : ids) keys.push_back(keyOf(TargetKind::block, id));
    return keys;
}

void eraseId(StringMap<Ids>& index, const std::string& key, const std::string& id)
{
    const auto it = index.find(key);
    if (it == index.end()) return;
    auto& ids = it->second;
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    if (ids.empty()) index.erase(it);
}

void indexClaim(ClaimState& cs, const Claim& c)
{
    for (const std::string& key : c.covers) cs.byKey[key].push_back(c.id);
    if (c.kind == TargetKind::block && !c.path.empty()) cs.byPath[c.path].push_back(c.id);
}

void unindexClaim(ClaimState& cs, const Claim& c)
{
    for (const std::string& key : c.covers) eraseId(cs.byKey, key, c.id);
    if (c.kind == TargetKind::block && !c.path.empty()) eraseId(cs.byPath, c.path, c.id);
}

/// The live claim in the way of a claim of `mode` on `covers` (and, when files cover blocks, on the file or the blocks of it), held by someone else than
/// `holder` (null: anyone), at `at`. One scan per cover key: the claims on that target only.
const Claim* findBlocker(const ClaimState& cs, const ClaimPolicy& policy, std::string_view mode, TargetKind kind, std::string_view target, std::string_view path,
                         const Ids& covers, const WriterId* holder, Instant at)
{
    const Claim* found = nullptr;
    auto scan = [&](const Ids* ids) {
        if (ids == nullptr) return;
        for (const std::string& id : *ids) {
            const Claim& o = cs.claims.find(id)->second;
            if (o.mode != mode || (holder != nullptr && o.holder == *holder) || !isLive(o, at)) continue;
            // Several candidates: the earliest in merge order is the one named, so the answer is the same on every replica.
            if (found == nullptr || std::tie(o.at.lamport, o.at.writer) < std::tie(found->at.lamport, found->at.writer)) found = &o;
        }
    };
    auto bucket = [](const StringMap<Ids>& m, std::string_view key) { const auto it = m.find(key); return it == m.end() ? nullptr : &it->second; };
    for (const std::string& key : covers) scan(bucket(cs.byKey, key));
    if (policy.fileCoversBlocks) {
        if (kind == TargetKind::file) scan(bucket(cs.byPath, target));
        else if (kind == TargetKind::block && !path.empty()) scan(bucket(cs.byKey, keyOf(TargetKind::file, path)));
    }
    return found;
}

// ---- reducers ------------------------------------------------------------------------------------------------------------------------------------------

/// The payload members of every kind in this header, read once. A member of the wrong type makes the event unusable (counted, kept in the log).
struct Fields
{
    std::string kind, target, path, mode, reason, text, carry;
    std::optional<std::int64_t> at, ttl;
    Ids into;
};

bool readFields(StoreState& s, const Event& e, Fields& f)
{
    try {
        json::withObject(e.payload, [&](simdjson::ondemand::object& obj) {
            for (auto field : obj) {
                const std::string_view k = json::keyOf(field);
                auto v = field.value();
                if (k == "kind") f.kind = json::toString(v);
                else if (k == "target") f.target = json::toString(v);
                else if (k == "path") f.path = json::toString(v);
                else if (k == "mode") f.mode = json::toString(v);
                else if (k == "reason") f.reason = json::toString(v);
                else if (k == "text") f.text = json::toString(v);
                else if (k == "carry") f.carry = json::toString(v);
                else if (k == "at") f.at = v.get_int64();
                else if (k == "ttl") f.ttl = v.get_int64();
                else if (k == "into") for (auto el : v.get_array()) f.into.push_back(json::toString(el));
            }
        });
        return true;
    } catch (const simdjson::simdjson_error&) {
        ++s.skipped;
        return false;
    }
}

Instant clampTtl(const ModePolicy& m, std::int64_t ttl) { return m.maxTtl > 0 ? std::min<Instant>(ttl, m.maxTtl) : ttl; }

/// A write claim that holds its item: the item goes `held` when it was open, and open again when its last such claim ends and it is still held.
void holdItem(StoreState& s, const Claim& c, const ModePolicy& m, const Event& e)
{
    if (c.kind != TargetKind::item || !m.holdsItem) return;
    StoredItem* it = s.find(c.target);
    if (it == nullptr || it->state != ItemState::open) return;
    it->lastTransition = Transition{it->state, ItemState::held, c.holder.writer, "claim " + c.id, stampOf(e)};
    it->state = ItemState::held;
}

void releaseItem(StoreState& s, const ClaimState& cs, const ClaimPolicy& policy, const Claim& c, const Event& e)
{
    if (c.kind != TargetKind::item || !policy.mode(c.mode)->holdsItem) return;
    StoredItem* it = s.find(c.target);
    if (it == nullptr || it->state != ItemState::held) return;
    if (const auto b = cs.byKey.find(keyOf(TargetKind::item, c.target)); b != cs.byKey.end())
        for (const std::string& id : b->second) {
            const Claim& o = cs.claims.find(id)->second;
            if (o.status == ClaimStatus::live && policy.mode(o.mode)->holdsItem) return;   // another claim still holds it
        }
    it->lastTransition = Transition{it->state, ItemState::open, c.holder.writer, c.reason, stampOf(e)};
    it->state = ItemState::open;
}

void endClaim(StoreState& s, ClaimState& cs, const ClaimPolicy& policy, Claim& c, ClaimStatus status, std::string reason, const Event& e)
{
    unindexClaim(cs, c);
    c.status = status;
    c.reason = std::move(reason);
    releaseItem(s, cs, policy, c, e);
}

void reduceTake(StoreState& s, const Event& e, const ClaimPolicy& policy)
{
    Fields f;
    if (!readFields(s, e, f)) return;
    const auto kind = parseTargetKind(f.kind);
    const ModePolicy* mode = policy.mode(f.mode);
    ClaimState& cs = s.ext<ClaimState>();
    if (!kind || f.target.empty() || mode == nullptr || !f.at || !f.ttl || *f.ttl <= 0 || e.subject.empty() || cs.claims.contains(e.subject)) { ++s.skipped; return; }

    Claim c;
    c.id = e.subject;
    c.kind = *kind;
    c.target = f.target;
    c.path = f.path;
    c.mode = f.mode;
    c.holder = e.writer;
    c.taken = *f.at;
    c.expires = *f.at + clampTtl(*mode, *f.ttl);
    c.at = stampOf(e);
    c.covers = coverKeys(cs, c.kind, c.target);
    const std::string id = c.id;
    if (c.covers.empty()) {   // a retired block: nothing to hold
        c.status = ClaimStatus::refused;
        c.reason = "retired";
        cs.claims.emplace(id, std::move(c));
        return;
    }
    if (mode->exclusive) {
        if (const Claim* blocker = findBlocker(cs, policy, c.mode, c.kind, c.target, c.path, c.covers, &c.holder, *f.at)) {
            c.status = ClaimStatus::refused;
            c.reason = blocker->id;
            c.covers.clear();
            cs.claims.emplace(id, std::move(c));
            return;
        }
    }
    Claim& stored = cs.claims.emplace(id, std::move(c)).first->second;
    indexClaim(cs, stored);
    holdItem(s, stored, *mode, e);
}

void reduceRenew(StoreState& s, const Event& e, const ClaimPolicy& policy)
{
    Fields f;
    if (!readFields(s, e, f)) return;
    ClaimState& cs = s.ext<ClaimState>();
    const auto it = cs.claims.find(e.subject);
    if (it == cs.claims.end() || !f.at) { ++s.skipped; return; }
    Claim& c = it->second;
    // Too late (already over, or past its time at the renewer's instant) or not the holder's: nothing changes. A late renewal must take the target again.
    if (e.writer != c.holder || !isLive(c, *f.at)) return;
    const ModePolicy& m = *policy.mode(c.mode);
    const Instant ttl = f.ttl && *f.ttl > 0 ? clampTtl(m, *f.ttl) : m.defaultTtl;
    c.expires = std::max(c.expires, *f.at + ttl);
}

void reduceRelease(StoreState& s, const Event& e, const ClaimPolicy& policy)
{
    Fields f;
    if (!readFields(s, e, f)) return;
    ClaimState& cs = s.ext<ClaimState>();
    const auto it = cs.claims.find(e.subject);
    if (it == cs.claims.end()) { ++s.skipped; return; }
    Claim& c = it->second;
    if (c.status != ClaimStatus::live) return;   // already over: a release that lost a race with an expiry is harmless
    if (e.writer != c.holder) { ++s.skipped; return; }
    endClaim(s, cs, policy, c, ClaimStatus::released, f.reason.empty() ? "released" : f.reason, e);
}

void reduceExpire(StoreState& s, const Event& e, const ClaimPolicy& policy)
{
    Fields f;
    if (!readFields(s, e, f)) return;
    ClaimState& cs = s.ext<ClaimState>();
    const auto it = cs.claims.find(e.subject);
    if (it == cs.claims.end() || !f.at) { ++s.skipped; return; }
    Claim& c = it->second;
    if (c.status == ClaimStatus::live && c.expires <= *f.at) endClaim(s, cs, policy, c, ClaimStatus::expired, "expired", e);
}

/// What became of block `e.subject`: its claims and notes move to what it became (a merge's survivor, a split's parts); with nothing, its claims end `retired`.
/// `carry: revived` brings a retired id back (its old claims stay ended).
void reduceCarry(StoreState& s, const Event& e, const ClaimPolicy& policy)
{
    Fields f;
    if (!readFields(s, e, f)) return;
    ClaimState& cs = s.ext<ClaimState>();
    const std::string& id = e.subject;
    if (id.empty() || std::find(f.into.begin(), f.into.end(), id) != f.into.end()) { ++s.skipped; return; }
    if (f.carry == "revived") { cs.successors.erase(id); return; }
    cs.successors[id] = f.into;
    Ids finals;
    for (const std::string& t : f.into) resolveInto(cs, t, finals);

    const std::string oldKey = keyOf(TargetKind::block, id);
    if (const auto b = cs.byKey.find(oldKey); b != cs.byKey.end()) {
        const Ids claimIds = b->second;
        cs.byKey.erase(b);
        for (const std::string& cid : claimIds) {
            Claim& c = cs.claims.find(cid)->second;
            c.covers.erase(std::remove(c.covers.begin(), c.covers.end(), oldKey), c.covers.end());
            for (const std::string& fin : finals) {
                const std::string key = keyOf(TargetKind::block, fin);
                if (std::find(c.covers.begin(), c.covers.end(), key) != c.covers.end()) continue;
                c.covers.push_back(key);
                cs.byKey[key].push_back(cid);
            }
            if (c.covers.empty()) endClaim(s, cs, policy, c, ClaimStatus::retired, "retired", e);
        }
    }
    if (const auto n = cs.notesByKey.find(oldKey); n != cs.notesByKey.end()) {
        const std::vector<std::size_t> moved = n->second;
        cs.notesByKey.erase(n);
        for (const std::string& fin : finals) {
            auto& bucket = cs.notesByKey[keyOf(TargetKind::block, fin)];
            bucket.insert(bucket.end(), moved.begin(), moved.end());
            std::sort(bucket.begin(), bucket.end());
            bucket.erase(std::unique(bucket.begin(), bucket.end()), bucket.end());
        }
    }
}

void reduceNote(StoreState& s, const Event& e)
{
    Fields f;
    if (!readFields(s, e, f)) return;
    const auto kind = parseTargetKind(f.kind);
    if (!kind || e.subject.empty() || f.text.empty()) { ++s.skipped; return; }
    ClaimState& cs = s.ext<ClaimState>();
    const std::size_t index = cs.notes.size();
    cs.notes.push_back(Note{*kind, e.subject, std::move(f.text), stampOf(e)});
    for (const std::string& key : coverKeys(cs, *kind, e.subject)) cs.notesByKey[key].push_back(index);
}

std::string payloadOf(const std::vector<std::pair<std::string, std::string>>& strings, const std::vector<std::pair<std::string, std::int64_t>>& numbers = {},
                      const Ids* into = nullptr, const char* carry = nullptr)
{
    json::Writer w;
    w.beginObject();
    for (const auto& [k, v] : strings) if (!v.empty()) w.kv(k, v);
    for (const auto& [k, v] : numbers) w.kv(k, v);
    if (carry != nullptr) w.kv("carry", carry);
    if (into != nullptr) {
        w.key("into").beginArray();
        for (const std::string& i : *into) w.str(i);
        w.endArray();
    }
    w.endObject();
    return w.take();
}

}  // namespace

bool registerClaimReducers(ReducerRegistry& registry, ClaimPolicy policy)
{
    const auto p = std::make_shared<const ClaimPolicy>(std::move(policy));
    bool ok = true;
    ok &= registry.add(std::string{kTake}, [p](StoreState& s, const Event& e) { reduceTake(s, e, *p); });
    ok &= registry.add(std::string{kRenew}, [p](StoreState& s, const Event& e) { reduceRenew(s, e, *p); });
    ok &= registry.add(std::string{kRelease}, [p](StoreState& s, const Event& e) { reduceRelease(s, e, *p); });
    ok &= registry.add(std::string{kExpire}, [p](StoreState& s, const Event& e) { reduceExpire(s, e, *p); });
    ok &= registry.add(std::string{kCarry}, [p](StoreState& s, const Event& e) { reduceCarry(s, e, *p); });
    ok &= registry.add(std::string{kNote}, [](StoreState& s, const Event& e) { reduceNote(s, e); });
    return ok;
}

// ---- writing -------------------------------------------------------------------------------------------------------------------------------------------

std::expected<void, ClaimRefusal> checkClaim(const Store& store, const ClaimPolicy& policy, const WriterId& self, const ClaimRequest& r, Instant now)
{
    auto bad = [](ClaimRefusal::Why why, std::string message) {
        ClaimRefusal refusal;
        refusal.why = why;
        refusal.message = std::move(message);
        return std::unexpected(std::move(refusal));
    };
    const ModePolicy* mode = policy.mode(r.mode);
    if (r.target.empty()) return bad(ClaimRefusal::Why::badRequest, "a claim needs a target");
    if (mode == nullptr) return bad(ClaimRefusal::Why::unknownMode, std::format("unknown claim mode \"{}\"", r.mode));
    if (r.ttl && *r.ttl <= 0) return bad(ClaimRefusal::Why::badRequest, "a claim's ttl must be positive");
    const ClaimState& cs = stateOf(store);
    const Ids covers = coverKeys(cs, r.kind, r.target);
    if (covers.empty()) return bad(ClaimRefusal::Why::retired, std::format("block {} is retired: there is nothing to claim", r.target));
    if (!mode->exclusive) return {};
    const Claim* blocker = findBlocker(cs, policy, r.mode, r.kind, r.target, r.path, covers, &self, now);
    if (blocker == nullptr) return {};
    return std::unexpected(ClaimRefusal{.why = ClaimRefusal::Why::conflict,
                                        .message = std::format("{} {} is held ({}) by {}/{} until {} (claim {})", targetKindName(r.kind), r.target, r.mode,
                                                               blocker->holder.ns, blocker->holder.writer, blocker->expires, blocker->id),
                                        .claimId = blocker->id,
                                        .holder = blocker->holder,
                                        .expires = blocker->expires});
}

std::expected<Claim, ClaimRefusal> takeClaim(Store& store, const ClaimPolicy& policy, const WriterId& self, const ClaimRequest& r, Instant now)
{
    if (auto ok = checkClaim(store, policy, self, r, now); !ok) return std::unexpected(std::move(ok.error()));
    const Instant ttl = r.ttl.value_or(policy.mode(r.mode)->defaultTtl);
    const std::string id = std::format("{}/{}:{}", self.ns, self.writer, store.head() + 1);
    (void)store.emit(std::string{kTake}, id,
                     payloadOf({{"kind", std::string{targetKindName(r.kind)}}, {"target", r.target}, {"path", r.path}, {"mode", r.mode}}, {{"at", now}, {"ttl", ttl}}));
    return *findClaim(store, id);
}

std::expected<Claim, std::string> renewClaim(Store& store, const WriterId& self, const std::string& claimId, std::optional<Instant> ttl, Instant now,
                                             const ClaimPolicy& policy)
{
    const Claim* c = findClaim(store, claimId);
    if (c == nullptr) return std::unexpected(std::format("no claim {}", claimId));
    if (c->holder != self) return std::unexpected(std::format("claim {} is held by {}/{}, not by {}/{}", claimId, c->holder.ns, c->holder.writer, self.ns, self.writer));
    if (!isLive(*c, now)) return std::unexpected(std::format("claim {} is over ({}): take the target again", claimId, c->status == ClaimStatus::live ? std::string_view{"expired"} : claimStatusName(c->status)));
    std::vector<std::pair<std::string, std::int64_t>> numbers{{"at", now}};
    numbers.emplace_back("ttl", ttl.value_or(policy.mode(c->mode)->defaultTtl));
    (void)store.emit(std::string{kRenew}, claimId, payloadOf({}, numbers));
    return *findClaim(store, claimId);
}

std::expected<void, std::string> releaseClaim(Store& store, const WriterId& self, const std::string& claimId, Instant now, const std::string& reason)
{
    const Claim* c = findClaim(store, claimId);
    if (c == nullptr) return std::unexpected(std::format("no claim {}", claimId));
    if (c->holder != self) return std::unexpected(std::format("claim {} is held by {}/{}, not by {}/{}", claimId, c->holder.ns, c->holder.writer, self.ns, self.writer));
    if (c->status != ClaimStatus::live) return std::unexpected(std::format("claim {} is already over ({})", claimId, claimStatusName(c->status)));
    (void)store.emit(std::string{kRelease}, claimId, payloadOf({{"reason", reason}}, {{"at", now}}));
    return {};
}

std::size_t expireDue(Store& store, Instant now)
{
    const ClaimState& cs = stateOf(store);
    Ids due;
    for (const auto& [id, c] : cs.claims) if (c.status == ClaimStatus::live && c.expires <= now) due.push_back(id);
    std::sort(due.begin(), due.end());   // the map's order is not deterministic; the events are
    for (const std::string& id : due) (void)store.emit(std::string{kExpire}, id, payloadOf({}, {{"at", now}}));
    return due.size();
}

const Event& emitBlockCarry(Store& store, const std::string& id, const Ids& into)
{
    return store.emit(std::string{kCarry}, id, payloadOf({}, {}, &into, into.empty() ? "retired" : into.size() == 1 ? "merged" : "split"));
}

std::size_t emitBlockCarries(Store& store, const std::vector<BlockEvent>& events, std::size_t from)
{
    std::size_t written = 0;
    for (std::size_t i = from; i < events.size(); ++i) {
        const BlockEvent& b = events[i];
        switch (b.carry) {
            case Carry::merged: if (!b.detail.empty()) { (void)emitBlockCarry(store, b.id, {b.detail}); ++written; } break;
            case Carry::retired: (void)emitBlockCarry(store, b.id, {}); ++written; break;
            case Carry::revived: {
                const Ids none;
                (void)store.emit(std::string{kCarry}, b.id, payloadOf({}, {}, &none, "revived"));
                ++written;
                break;
            }
            case Carry::kept: case Carry::resigned: case Carry::renamed: case Carry::added: break;   // the id stands: claims need nothing
        }
    }
    return written;
}

std::size_t emitBlockCarries(Store& store, const EventLog& events, std::size_t from)
{
    return emitBlockCarries(store, events.from(from), 0);
}

const Event& addNote(Store& store, TargetKind kind, const std::string& target, const std::string& text)
{
    return store.emit(std::string{kNote}, target, payloadOf({{"kind", std::string{targetKindName(kind)}}, {"text", text}}));
}

// ---- reading -------------------------------------------------------------------------------------------------------------------------------------------

const Claim* findClaim(const Store& store, std::string_view claimId)
{
    const ClaimState& cs = stateOf(store);
    const auto it = cs.claims.find(claimId);
    return it == cs.claims.end() ? nullptr : &it->second;
}

std::vector<Claim> claimsOn(const Store& store, TargetKind kind, std::string_view target, Instant now)
{
    const ClaimState& cs = stateOf(store);
    std::vector<Claim> out;
    for (const std::string& key : coverKeys(cs, kind, target)) {
        const auto b = cs.byKey.find(key);
        if (b == cs.byKey.end()) continue;
        for (const std::string& id : b->second) {
            const Claim& c = cs.claims.find(id)->second;
            // A split's claim sits under several keys: it is listed once (the claims on one target are few, so the scan is cheap).
            if (isLive(c, now) && std::none_of(out.begin(), out.end(), [&](const Claim& o) { return o.id == id; })) out.push_back(c);
        }
    }
    std::sort(out.begin(), out.end(), [](const Claim& a, const Claim& b) { return std::tie(a.at.lamport, a.at.writer) < std::tie(b.at.lamport, b.at.writer); });
    return out;
}

std::optional<Claim> holderOf(const Store& store, const ClaimPolicy& policy, TargetKind kind, std::string_view target, std::string_view mode, Instant now)
{
    const ClaimState& cs = stateOf(store);
    const ModePolicy* m = policy.mode(mode);
    if (m == nullptr || !m->exclusive) return std::nullopt;
    const Ids covers = coverKeys(cs, kind, target);
    if (const Claim* c = findBlocker(cs, policy, mode, kind, target, {}, covers, nullptr, now)) return *c;
    return std::nullopt;
}

std::vector<std::string> resolveBlock(const Store& store, std::string_view id)
{
    Ids out;
    resolveInto(stateOf(store), id, out);
    return out;
}

std::size_t claimCount(const Store& store) { return stateOf(store).claims.size(); }

std::vector<Note> notesOn(const Store& store, TargetKind kind, std::string_view target)
{
    const ClaimState& cs = stateOf(store);
    std::set<std::size_t> indices;
    for (const std::string& key : coverKeys(cs, kind, target))
        if (const auto b = cs.notesByKey.find(key); b != cs.notesByKey.end()) indices.insert(b->second.begin(), b->second.end());
    std::vector<Note> out;
    for (const std::size_t i : indices) out.push_back(cs.notes[i]);   // note indices are in merge order
    return out;
}

std::string claimsJson(const Store& store, Instant now)
{
    const ClaimState& cs = stateOf(store);
    std::map<std::string, const Claim*> live;
    for (const auto& [id, c] : cs.claims) if (isLive(c, now)) live.emplace(id, &c);
    json::Writer w;
    w.beginObject().kv("schema", "claims-1").kv("at", static_cast<std::int64_t>(now)).key("claims").beginArray();
    for (const auto& [id, c] : live) {
        w.beginObject().kv("id", id).kv("kind", targetKindName(c->kind)).kv("target", c->target);
        if (!c->path.empty()) w.kv("path", c->path);
        w.kv("mode", c->mode).key("holder").beginObject().kv("namespace", c->holder.ns).kv("writer", c->holder.writer).endObject();
        w.kv("taken", static_cast<std::int64_t>(c->taken)).kv("expires", static_cast<std::int64_t>(c->expires)).key("covers").beginArray();
        for (const std::string& key : c->covers) w.str(key);
        w.endArray().endObject();
    }
    w.endArray().endObject();
    return w.take();
}

}  // namespace sub0canvas

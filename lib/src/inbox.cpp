#include "sub0canvas/inbox.hpp"

#include "json_util.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace sub0canvas {

// ---- the query and its JSON ---------------------------------------------------------------------------------------------------------------------------

namespace {

void writeSet(json::Writer& w, std::string_view key, const Query::NameSet& set)
{
    w.key(key).beginArray();
    for (const std::string& s : set) w.str(s);
    w.endArray();
}

std::string subscribePayload(const std::string& owner, const SubscriptionQuery& q, std::uint64_t cursor)
{
    json::Writer w;
    w.beginObject().kv("owner", owner).kv("cursor", static_cast<std::int64_t>(cursor));
    w.key("query").beginObject();
    writeSet(w, "layers", q.layers);
    writeSet(w, "kinds", q.kinds);
    writeSet(w, "tags", q.tags);
    writeSet(w, "states", q.states);
    writeSet(w, "sources", q.sources);
    writeSet(w, "blocks", q.blocks);
    writeSet(w, "paths", q.paths);
    writeSet(w, "events", q.events);
    w.kv("text", q.text);
    w.key("hiddenTagPrefixes").beginArray();
    for (const std::string& p : q.hiddenTagPrefixes) w.str(p);
    w.endArray();
    w.endObject().endObject();
    return w.take();
}

template <class Array>
void readStrings(Array arr, const auto& insert)
{
    for (auto el : arr) insert(json::toString(el));
}

void readQuery(simdjson::ondemand::value v, SubscriptionQuery& q)
{
    q.hiddenTagPrefixes.clear();   // a payload that names none means none; the default only applies to a query made in code
    for (auto f : v.get_object()) {
        const std::string_view k = json::keyOf(f);
        auto fv = f.value();
        auto into = [&](Query::NameSet& set) { readStrings(fv.get_array(), [&](std::string s) { set.insert(std::move(s)); }); };
        if (k == "layers") into(q.layers);
        else if (k == "kinds") into(q.kinds);
        else if (k == "tags") into(q.tags);
        else if (k == "states") into(q.states);
        else if (k == "sources") into(q.sources);
        else if (k == "blocks") into(q.blocks);
        else if (k == "paths") into(q.paths);
        else if (k == "events") into(q.events);
        else if (k == "text") q.text = json::toString(fv);
        else if (k == "hiddenTagPrefixes") readStrings(fv.get_array(), [&](std::string s) { q.hiddenTagPrefixes.push_back(std::move(s)); });
    }
}

// ---- reducers ----------------------------------------------------------------------------------------------------------------------------------------

/// Runs `fn` over the payload's object; false (counted as skipped) when it is not readable.
template <class F>
bool readPayload(StoreState& s, const Event& e, F&& fn)
{
    try {
        json::withObject(e.payload, fn);
        return true;
    } catch (const simdjson::simdjson_error&) {
        ++s.skipped;
        return false;
    }
}

void reduceSubscribe(StoreState& s, const Event& e)
{
    if (e.subject.empty()) { ++s.skipped; return; }
    Subscription sub;
    sub.id = e.subject;
    if (!readPayload(s, e, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = json::keyOf(f);
                auto v = f.value();
                if (k == "owner") sub.owner = json::toString(v);
                else if (k == "cursor") sub.cursor = v.get_uint64();
                else if (k == "query") readQuery(v.value(), sub.query);
            }
        })) return;
    auto& subs = s.ext<SubscriptionState>().subscriptions;
    const auto it = subs.find(sub.id);
    if (it == subs.end()) { subs.emplace(sub.id, std::move(sub)); return; }
    it->second.owner = std::move(sub.owner);   // a second subscribe replaces what is asked for, never what has been seen
    it->second.query = std::move(sub.query);
}

void reduceUnsubscribe(StoreState& s, const Event& e) { s.ext<SubscriptionState>().subscriptions.erase(e.subject); }

void reduceAck(StoreState& s, const Event& e)
{
    std::uint64_t upTo = 0;
    if (!readPayload(s, e, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) if (json::keyOf(f) == "upTo") upTo = f.value().get_uint64();
        })) return;
    auto& subs = s.ext<SubscriptionState>().subscriptions;
    const auto it = subs.find(e.subject);
    if (it == subs.end()) { ++s.skipped; return; }
    it->second.cursor = std::max(it->second.cursor, upTo);   // monotone: replay order cannot move a cursor back
}

}  // namespace

bool registerSubscriptionReducers(ReducerRegistry& registry)
{
    const bool a = registry.add(std::string{kSubscribeKind}, reduceSubscribe);
    const bool b = registry.add(std::string{kUnsubscribeKind}, reduceUnsubscribe);
    const bool c = registry.add(std::string{kAckKind}, reduceAck);
    return a && b && c;
}

// ---- the mutations and lookups -----------------------------------------------------------------------------------------------------------------------

std::expected<void, std::string> subscribe(Store& store, const std::string& id, const std::string& owner, const SubscriptionQuery& query, std::uint64_t cursor)
{
    if (id.empty() || owner.empty()) return std::unexpected("a subscription names its id and its owner");
    (void)store.emit(std::string{kSubscribeKind}, id, subscribePayload(owner, query, cursor));
    if (findSubscription(store, id) == nullptr)   // the event is kept in the log either way (A7)
        return std::unexpected(std::format("{} has no reducer: call registerSubscriptionReducers where the store is made", kSubscribeKind));
    return {};
}

std::expected<bool, std::string> unsubscribe(Store& store, const std::string& id)
{
    if (findSubscription(store, id) == nullptr) return false;
    (void)store.emit(std::string{kUnsubscribeKind}, id, "{}");
    return true;
}

std::expected<bool, std::string> acknowledge(Store& store, const std::string& id, std::uint64_t position)
{
    const Subscription* sub = findSubscription(store, id);
    if (sub == nullptr) return std::unexpected(std::format("no subscription {}", id));
    if (position > store.head()) return std::unexpected(std::format("position {} is beyond the log's head {}", position, store.head()));
    if (position <= sub->cursor) return false;
    json::Writer w;
    w.beginObject().kv("upTo", static_cast<std::int64_t>(position)).endObject();
    (void)store.emit(std::string{kAckKind}, id, w.take());
    return true;
}

const Subscription* findSubscription(const Store& store, std::string_view id)
{
    const SubscriptionState* st = store.state().extIf<SubscriptionState>();
    if (st == nullptr) return nullptr;
    const auto it = st->subscriptions.find(id);
    return it == st->subscriptions.end() ? nullptr : &it->second;
}

std::vector<const Subscription*> subscriptionsOf(const Store& store, std::string_view owner)
{
    std::vector<const Subscription*> out;
    if (const SubscriptionState* st = store.state().extIf<SubscriptionState>())
        for (const auto& [id, sub] : st->subscriptions) if (sub.owner == owner) out.push_back(&sub);
    return out;
}

// ---- matching ----------------------------------------------------------------------------------------------------------------------------------------

namespace {

/// The item-level filters of a query. `checkLayers` is false for an event, whose layer is the one it was made in (see InboxReader).
bool itemPasses(const StoredItem& it, const Query& q, bool checkLayers)
{
    auto in = [](const Query::NameSet& set, std::string_view v) { return set.empty() || set.find(v) != set.end(); };
    if ((checkLayers && !in(q.layers, it.item.layer)) || !in(q.kinds, it.item.kind) || !in(q.states, stateName(it.state)) || !in(q.sources, it.source)) return false;
    if (!q.tags.empty() && std::none_of(it.tags.begin(), it.tags.end(), [&](const std::string& t) { return q.tags.contains(t); })) return false;
    if (!q.text.empty() && it.item.title.find(q.text) == std::string::npos && it.item.detail.find(q.text) == std::string::npos) return false;
    for (const std::string& t : it.tags)
        for (const std::string& prefix : q.hiddenTagPrefixes)
            if (t.starts_with(prefix) && !q.tags.contains(t)) return false;   // a private item is delivered only when asked for by name (R-BB-10)
    return true;
}

bool hasItemFilters(const Query& q) { return !q.kinds.empty() || !q.tags.empty() || !q.states.empty() || !q.sources.empty() || !q.text.empty(); }

}  // namespace

bool itemMatches(const StoredItem& item, const Query& q) { return itemPasses(item, q, true); }

// ---- the index ---------------------------------------------------------------------------------------------------------------------------------------

namespace {

constexpr std::uint32_t kNoVersion = std::numeric_limits<std::uint32_t>::max();

/// Where an item was when an event was made: its layer and anchors as of its latest `item.put`.
struct Placement
{
    std::string layer;
    std::vector<std::string> blocks, paths;   ///< distinct, in first-seen order
    bool operator==(const Placement&) const = default;
};

using Bucket = std::vector<std::uint32_t>;   ///< event positions, ascending: so lamport values ascend too
using StringBuckets = std::unordered_map<std::string, Bucket, StringHash, std::equal_to<>>;

void addDistinct(std::vector<std::string>& v, std::string s)
{
    if (!s.empty() && std::find(v.begin(), v.end(), s) == v.end()) v.push_back(std::move(s));
}

/// The layer and anchors of an `item.put` payload; false when it cannot be read (the store skipped the event too).
bool readPlacement(const Event& e, Placement& out)
{
    try {
        json::withObject(e.payload, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = json::keyOf(f);
                auto v = f.value();
                if (k == "layer") out.layer = json::toString(v);
                else if (k == "anchors") {
                    for (auto el : v.get_array())
                        for (auto af : el.get_object()) {
                            const std::string_view ak = json::keyOf(af);
                            if (ak == "blockId") addDistinct(out.blocks, json::toString(af.value()));
                            else if (ak == "path") addDistinct(out.paths, json::toString(af.value()));
                        }
                }
            }
        });
        return !out.layer.empty();
    } catch (const simdjson::simdjson_error&) {
        return false;
    }
}

bool isSubscriptionKind(std::string_view kind) { return kind == kSubscribeKind || kind == kUnsubscribeKind || kind == kAckKind; }

}  // namespace

struct InboxReader::Impl
{
    explicit Impl(const Store& s) : store(s) {}

    const Store& store;
    std::size_t indexed = 0;                    ///< events of `store.events()` held by the index
    std::uint64_t lastLamport = 0;              ///< the last indexed event, to notice an older one inserted before it
    WriterId lastWriter;
    std::vector<Placement> placements;
    std::vector<std::uint32_t> placementOf;     ///< per event position: its placement, or kNoVersion
    std::unordered_map<std::string, std::uint32_t, StringHash, std::equal_to<>> current;   ///< item key to its latest placement
    StringBuckets byLayer, byBlock, byKind, byKindName;
    std::map<std::string, Bucket, std::less<>> byPath;
    std::size_t candidates = 0;

    void reset()
    {
        indexed = 0;
        lastLamport = 0;
        lastWriter = {};
        placements.clear();
        placementOf.clear();
        current.clear();
        byLayer.clear();
        byBlock.clear();
        byKind.clear();
        byKindName.clear();
        byPath.clear();
    }

    void add(std::size_t pos)
    {
        const Event& e = store.events()[pos];
        const auto p = static_cast<std::uint32_t>(pos);
        std::uint32_t pl = kNoVersion;
        if (e.kind == "item.put@1") {
            Placement next;
            if (readPlacement(e, next)) {
                const auto it = current.find(e.subject);
                if (it != current.end() && placements[it->second] == next) pl = it->second;
                else {
                    pl = static_cast<std::uint32_t>(placements.size());
                    placements.push_back(std::move(next));
                    current.insert_or_assign(e.subject, pl);
                }
            }
        }
        if (pl == kNoVersion) {
            const auto it = current.find(e.subject);
            if (it != current.end()) pl = it->second;
        }
        placementOf.push_back(pl);
        byKind[e.kind].push_back(p);
        byKindName[std::string{splitKind(e.kind).name}].push_back(p);
        if (pl != kNoVersion) {
            const Placement& at = placements[pl];
            byLayer[at.layer].push_back(p);
            for (const std::string& b : at.blocks) byBlock[b].push_back(p);
            for (const std::string& path : at.paths) byPath[path].push_back(p);
        }
        lastLamport = e.lamport;
        lastWriter = e.writer;
    }

    /// Brings the index level with the store: extends it, or starts again when an event arrived out of order (it sorts before one already indexed, so the event
    /// at the last indexed position is no longer the one that was there).
    void sync()
    {
        const std::vector<Event>& ev = store.events();
        if (indexed > ev.size() || (indexed > 0 && (ev[indexed - 1].lamport != lastLamport || ev[indexed - 1].writer != lastWriter))) reset();
        for (; indexed < ev.size(); ++indexed) add(indexed);
    }

    /// Appends the positions of `bucket` at or after `first` to `out`.
    static void collect(const Bucket& bucket, std::uint32_t first, Bucket& out)
    {
        out.insert(out.end(), std::lower_bound(bucket.begin(), bucket.end(), first), bucket.end());
    }

    /// The positions after `first` that a query's narrowest bucket holds, ascending and distinct.
    Bucket candidatesFor(const SubscriptionQuery& q, std::uint32_t first) const
    {
        Bucket out;
        auto fromMap = [&](const StringBuckets& map, const Query::NameSet& keys) {
            for (const std::string& k : keys) if (const auto it = map.find(k); it != map.end()) collect(it->second, first, out);
        };
        if (!q.blocks.empty()) fromMap(byBlock, q.blocks);
        else if (!q.paths.empty()) {
            for (const std::string& prefix : q.paths)
                for (auto it = byPath.lower_bound(prefix); it != byPath.end() && it->first.starts_with(prefix); ++it) collect(it->second, first, out);
        } else if (!q.events.empty()) {
            for (const std::string& k : q.events) {
                const StringBuckets& map = k.find('@') == std::string::npos ? byKindName : byKind;
                if (const auto it = map.find(k); it != map.end()) collect(it->second, first, out);
            }
        } else if (!q.layers.empty()) fromMap(byLayer, q.layers);
        else {
            out.resize(store.events().size() - first);
            for (std::size_t i = 0; i < out.size(); ++i) out[i] = first + static_cast<std::uint32_t>(i);
            return out;   // already ascending and distinct
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    /// The final test of one candidate against the whole query.
    bool matches(const SubscriptionQuery& q, std::size_t pos) const
    {
        const Event& e = store.events()[pos];
        if (!q.events.empty()) {
            if (!q.events.contains(e.kind) && !q.events.contains(std::string{splitKind(e.kind).name})) return false;
        } else if (isSubscriptionKind(e.kind)) {
            return false;
        }
        const std::uint32_t pl = placementOf[pos];
        if (!q.layers.empty() && (pl == kNoVersion || !q.layers.contains(placements[pl].layer))) return false;
        if (!q.blocks.empty() && (pl == kNoVersion || std::none_of(placements[pl].blocks.begin(), placements[pl].blocks.end(),
                                                                    [&](const std::string& b) { return q.blocks.contains(b); })))
            return false;
        if (!q.paths.empty()) {
            if (pl == kNoVersion) return false;
            const auto prefixed = [&](const std::string& path) {
                return std::any_of(q.paths.begin(), q.paths.end(), [&](const std::string& prefix) { return path.starts_with(prefix); });
            };
            if (std::none_of(placements[pl].paths.begin(), placements[pl].paths.end(), prefixed)) return false;
        }
        if (const StoredItem* it = store.item(e.subject)) return itemPasses(*it, q, false);
        return !hasItemFilters(q);   // an event about no item (an unknown kind) can only be selected by what it is
    }
};

InboxReader::InboxReader(const Store& store) : impl_(std::make_unique<Impl>(store)) {}
InboxReader::~InboxReader() = default;

std::size_t InboxReader::lastCandidates() const { return impl_->candidates; }
std::size_t InboxReader::indexedEvents() const { return impl_->indexed; }

std::expected<Inbox, std::string> InboxReader::inbox(std::string_view id)
{
    const Store& store = impl_->store;
    const Subscription* sub = findSubscription(store, id);
    if (sub == nullptr) return std::unexpected(std::format("no subscription {}", id));
    impl_->sync();
    const std::vector<Event>& ev = store.events();
    const auto firstIt = std::partition_point(ev.begin(), ev.end(), [&](const Event& e) { return e.lamport <= sub->cursor; });
    const auto first = static_cast<std::uint32_t>(firstIt - ev.begin());
    const Bucket candidates = impl_->candidatesFor(sub->query, first);
    impl_->candidates = candidates.size();

    Inbox out;
    out.upTo = store.head();
    std::unordered_set<std::string_view> seen;
    std::vector<const StoredItem*> touched;
    for (const std::uint32_t pos : candidates) {
        if (!impl_->matches(sub->query, pos)) continue;
        out.events.push_back(ev[pos]);
        const StoredItem* it = store.item(ev[pos].subject);
        if (it != nullptr && seen.insert(it->item.key).second) touched.push_back(it);
    }
    // Layer order as `Store::items` gives it: by layer name, then first-put order, which is the order items were created in (an item moved between layers
    // is the one case where the two differ: the store appends it to the new layer).
    std::sort(touched.begin(), touched.end(), [](const StoredItem* a, const StoredItem* b) {
        if (const auto c = a->item.layer <=> b->item.layer; c != 0) return c < 0;
        if (a->created.lamport != b->created.lamport) return a->created.lamport < b->created.lamport;
        return a->created.writer < b->created.writer;
    });
    out.items.reserve(touched.size());
    for (const StoredItem* it : touched) {
        LayerItem item = it->item;
        item.number = store.numberOf(item.key);
        out.items.push_back(std::move(item));
    }
    return out;
}

std::expected<Inbox, std::string> inbox(const Store& store, std::string_view id)
{
    InboxReader reader(store);
    return reader.inbox(id);
}

}  // namespace sub0canvas

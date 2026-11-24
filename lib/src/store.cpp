#include "sub0canvas/store.hpp"

#include "json_util.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <utility>

namespace sub0canvas {

// ---- states ------------------------------------------------------------------------------------------------------------------------------------------

namespace {
constexpr std::array<std::pair<ItemState, std::string_view>, 5> kStateNames{{
    {ItemState::open, "open"}, {ItemState::held, "held"}, {ItemState::done, "done"}, {ItemState::verified, "verified"}, {ItemState::dropped, "dropped"}}};
}

std::string_view stateName(ItemState s)
{
    for (const auto& [state, name] : kStateNames) if (state == s) return name;
    return "open";
}

std::optional<ItemState> parseState(std::string_view name)
{
    for (const auto& [state, n] : kStateNames) if (n == name) return state;
    return std::nullopt;
}

bool isOrphaned(const StoredItem& item, const std::function<bool(const Anchor&)>& resolves)
{
    if (item.item.anchors.empty()) return false;
    return std::none_of(item.item.anchors.begin(), item.item.anchors.end(), resolves);
}

// ---- StoreState --------------------------------------------------------------------------------------------------------------------------------------

StoredItem* StoreState::find(std::string_view key)
{
    const auto it = items.find(key);
    return it == items.end() ? nullptr : &it->second;
}

const StoredItem* StoreState::find(std::string_view key) const
{
    const auto it = items.find(key);
    return it == items.end() ? nullptr : &it->second;
}

StoredItem& StoreState::create(std::string key, std::string layer)
{
    auto [it, inserted] = items.try_emplace(key);
    StoredItem& s = it->second;
    s.item.key = std::move(key);
    s.item.layer = layer;
    layers[std::move(layer)].push_back(&s);
    return s;
}

void StoreState::moveToLayer(StoredItem& item, const std::string& layer)
{
    if (item.item.layer == layer) return;
    auto& old = layers.find(item.item.layer)->second;
    old.erase(std::find(old.begin(), old.end(), &item));
    item.item.layer = layer;
    layers[layer].push_back(&item);
}

bool StoreState::addLink(StoredItem& item, Link link)
{
    if (std::find(item.links.begin(), item.links.end(), link) != item.links.end()) return false;
    backlinks[link.key].push_back({item.item.key, link.kind});
    item.links.push_back(std::move(link));
    return true;
}

bool StoreState::removeLink(StoredItem& item, const Link& link)
{
    const auto it = std::find(item.links.begin(), item.links.end(), link);
    if (it == item.links.end()) return false;
    item.links.erase(it);
    auto& back = backlinks[link.key];
    const auto b = std::find_if(back.begin(), back.end(), [&](const Backlink& x) { return x.fromKey == item.item.key && x.kind == link.kind; });
    if (b != back.end()) back.erase(b);
    return true;
}

// ---- the registry and the core reducers --------------------------------------------------------------------------------------------------------------

bool ReducerRegistry::add(std::string kind, Reducer reducer)
{
    return reducers_.try_emplace(std::move(kind), std::move(reducer)).second;
}

const Reducer* ReducerRegistry::find(std::string_view kind) const
{
    const auto it = reducers_.find(kind);
    return it == reducers_.end() ? nullptr : &it->second;
}

namespace {

constexpr std::string_view kPut = "item.put@1", kState = "item.state@1", kTag = "item.tag@1", kUntag = "item.untag@1", kLink = "item.link@1",
                           kUnlink = "item.unlink@1";

Stamp stampOf(const Event& e) { return {e.writer, e.lamport, e.time}; }

/// Runs `fn` over the payload's top-level object; false (and the event counted as skipped) when it is not an object or not readable.
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

std::string rawValue(std::string_view raw)
{
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\n' || raw.back() == '\t' || raw.back() == '\r')) raw.remove_suffix(1);
    return std::string{raw};
}

Anchor::Confidence parseConfidence(std::string_view c)
{
    if (c == "exact") return Anchor::Confidence::exact;
    if (c == "suspect") return Anchor::Confidence::suspect;
    return Anchor::Confidence::none;
}

std::string_view confidenceName(Anchor::Confidence c)
{
    switch (c) {
        case Anchor::Confidence::exact: return "exact";
        case Anchor::Confidence::suspect: return "suspect";
        case Anchor::Confidence::none: break;
    }
    return "none";
}

void writeAnchors(json::Writer& w, const std::vector<Anchor>& anchors)
{
    w.beginArray();
    for (const Anchor& a : anchors) {
        w.beginObject();
        if (!a.blockId.empty()) w.kv("blockId", a.blockId);
        if (!a.path.empty()) w.kv("path", a.path);
        if (a.line != 0) w.kv("line", a.line);
        if (a.column != 0) w.kv("column", a.column);
        if (a.endLine != 0) w.kv("endLine", a.endLine);
        if (a.endColumn != 0) w.kv("endColumn", a.endColumn);
        if (a.confidence != Anchor::Confidence::none) w.kv("confidence", confidenceName(a.confidence));
        w.endObject();
    }
    w.endArray();
}

void readAnchors(simdjson::ondemand::value v, std::vector<Anchor>& out)
{
    for (auto el : v.get_array()) {
        Anchor a;
        for (auto f : el.get_object()) {
            const std::string_view k = json::keyOf(f);
            auto fv = f.value();
            if (k == "blockId") a.blockId = json::toString(fv);
            else if (k == "path") a.path = json::toString(fv);
            else if (k == "line") a.line = static_cast<int>(fv.get_int64());
            else if (k == "column") a.column = static_cast<int>(fv.get_int64());
            else if (k == "endLine") a.endLine = static_cast<int>(fv.get_int64());
            else if (k == "endColumn") a.endColumn = static_cast<int>(fv.get_int64());
            else if (k == "confidence") a.confidence = parseConfidence(json::toString(fv));
        }
        out.push_back(std::move(a));
    }
}

void reducePut(StoreState& s, const Event& e)
{
    if (e.subject.empty()) { ++s.skipped; return; }
    std::string layer, source, kind, title, detail, format, data;
    std::vector<Anchor> anchors;
    const bool ok = readPayload(s, e, [&](simdjson::ondemand::object& obj) {
        for (auto f : obj) {
            const std::string_view k = json::keyOf(f);
            auto v = f.value();
            if (k == "layer") layer = json::toString(v);
            else if (k == "source") source = json::toString(v);
            else if (k == "kind") kind = json::toString(v);
            else if (k == "title") title = json::toString(v);
            else if (k == "detail") detail = json::toString(v);
            else if (k == "detailFormat") format = json::toString(v);
            else if (k == "anchors") readAnchors(v.value(), anchors);
            else if (k == "data") data = rawValue(v.raw_json());
        }
    });
    if (!ok) return;
    if (layer.empty()) { ++s.skipped; return; }
    StoredItem* it = s.find(e.subject);
    if (it == nullptr) {
        it = &s.create(e.subject, layer);
        it->created = stampOf(e);
    } else {
        s.moveToLayer(*it, layer);
    }
    it->source = std::move(source);
    it->item.kind = std::move(kind);
    it->item.title = std::move(title);
    it->item.detail = std::move(detail);
    it->item.anchors = std::move(anchors);
    it->detailFormat = std::move(format);
    it->data = std::move(data);
    it->updated = stampOf(e);
}

void reduceState(StoreState& s, const Event& e)
{
    StoredItem* it = s.find(e.subject);
    std::string to, actor, reason;
    if (!readPayload(s, e, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = json::keyOf(f);
                auto v = f.value();
                if (k == "to") to = json::toString(v);
                else if (k == "actor") actor = json::toString(v);
                else if (k == "reason") reason = json::toString(v);
            }
        })) return;
    const std::optional<ItemState> next = parseState(to);
    if (it == nullptr || !next) { ++s.skipped; return; }
    it->lastTransition = Transition{it->state, *next, std::move(actor), std::move(reason), stampOf(e)};
    it->state = *next;
}

void reduceTag(StoreState& s, const Event& e, bool add)
{
    StoredItem* it = s.find(e.subject);
    std::string tag;
    if (!readPayload(s, e, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) if (json::keyOf(f) == "tag") tag = json::toString(f.value());
        })) return;
    if (it == nullptr || tag.empty()) { ++s.skipped; return; }
    if (add) it->tags.insert(std::move(tag)); else it->tags.erase(tag);
}

void reduceLink(StoreState& s, const Event& e, bool add)
{
    StoredItem* it = s.find(e.subject);
    Link link;
    if (!readPayload(s, e, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = json::keyOf(f);
                auto v = f.value();
                if (k == "kind") link.kind = json::toString(v);
                else if (k == "key") link.key = json::toString(v);
            }
        })) return;
    if (it == nullptr || link.kind.empty() || link.key.empty()) { ++s.skipped; return; }
    if (add) (void)s.addLink(*it, std::move(link)); else (void)s.removeLink(*it, link);
}

}  // namespace

ReducerRegistry ReducerRegistry::core()
{
    ReducerRegistry r;
    (void)r.add(std::string{kPut}, reducePut);
    (void)r.add(std::string{kState}, reduceState);
    (void)r.add(std::string{kTag}, [](StoreState& s, const Event& e) { reduceTag(s, e, true); });
    (void)r.add(std::string{kUntag}, [](StoreState& s, const Event& e) { reduceTag(s, e, false); });
    (void)r.add(std::string{kLink}, [](StoreState& s, const Event& e) { reduceLink(s, e, true); });
    (void)r.add(std::string{kUnlink}, [](StoreState& s, const Event& e) { reduceLink(s, e, false); });
    return r;
}

// ---- the store ---------------------------------------------------------------------------------------------------------------------------------------

Store::Store(StoreOptions options) : options_(std::move(options)) {}

void Store::fold(const Event& e)
{
    if (const Reducer* r = options_.registry.find(e.kind)) (*r)(state_, e);
    else ++state_.unknownKinds[e.kind];
}

void Store::rebuild()
{
    state_ = StoreState{};
}

bool Store::append(Event e)
{
    if (events_.empty() || eventOrder(e, events_.back()) > 0) {
        events_.push_back(e);
        fold(events_.back());
    } else {
        const auto it = std::lower_bound(events_.begin(), events_.end(), e, [](const Event& a, const Event& b) { return eventOrder(a, b) < 0; });
        if (it != events_.end() && eventOrder(*it, e) == 0) return false;
        events_.insert(it, e);
        rebuild();
        for (const Event& held : events_) fold(held);
    }
    if (options_.sink != nullptr) options_.sink->append(e);
    return true;
}

void Store::merge(std::vector<Event> batch)
{
    std::stable_sort(batch.begin(), batch.end(), [](const Event& a, const Event& b) { return eventOrder(a, b) < 0; });
    batch.erase(std::unique(batch.begin(), batch.end(), [](const Event& a, const Event& b) { return eventOrder(a, b) == 0; }), batch.end());
    if (batch.empty()) return;
    if (events_.empty() || eventOrder(batch.front(), events_.back()) > 0) {
        events_.reserve(events_.size() + batch.size());
        for (Event& e : batch) { events_.push_back(std::move(e)); fold(events_.back()); }
        return;
    }
    std::vector<Event> merged;
    merged.reserve(events_.size() + batch.size());
    std::merge(std::make_move_iterator(events_.begin()), std::make_move_iterator(events_.end()), std::make_move_iterator(batch.begin()),
               std::make_move_iterator(batch.end()), std::back_inserter(merged), [](const Event& a, const Event& b) { return eventOrder(a, b) < 0; });
    merged.erase(std::unique(merged.begin(), merged.end(), [](const Event& a, const Event& b) { return eventOrder(a, b) == 0; }), merged.end());
    events_ = std::move(merged);
    rebuild();
    for (const Event& held : events_) fold(held);
}

void Store::replay(const EventSource& source, std::uint64_t from) { merge(source.since(from)); }

const Event& Store::emit(std::string kind, std::string subject, std::string payload)
{
    Event e;
    e.kind = std::move(kind);
    e.writer = options_.writer;
    e.lamport = head() + 1;
    e.subject = std::move(subject);
    e.time = now();
    e.payload = std::move(payload);
    (void)append(std::move(e));
    return events_.back();   // the new lamport sorts after every held event, so it is last
}

namespace {

std::string putPayload(const std::string& layer, const std::string& source, const ItemInput& in)
{
    json::Writer w;
    w.beginObject().kv("layer", layer).kv("source", source).kv("kind", in.item.kind).kv("title", in.item.title).kv("detail", in.item.detail);
    if (!in.detailFormat.empty()) w.kv("detailFormat", in.detailFormat);
    if (!in.item.anchors.empty()) { w.key("anchors"); writeAnchors(w, in.item.anchors); }
    if (!in.data.empty()) w.kvRaw("data", in.data);
    w.endObject();
    return w.take();
}

bool sameAnchors(const std::vector<Anchor>& a, const std::vector<Anchor>& b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const Anchor& x, const Anchor& y) {
        return x.blockId == y.blockId && x.path == y.path && x.line == y.line && x.column == y.column && x.endLine == y.endLine && x.endColumn == y.endColumn &&
               x.confidence == y.confidence;
    });
}

bool sameContent(const StoredItem& cur, const std::string& layer, const std::string& source, const ItemInput& in)
{
    return cur.item.layer == layer && cur.source == source && cur.item.kind == in.item.kind && cur.item.title == in.item.title &&
           cur.item.detail == in.item.detail && cur.detailFormat == in.detailFormat && cur.data == in.data && sameAnchors(cur.item.anchors, in.item.anchors);
}

bool isJsonObject(std::string_view text)
{
    simdjson::dom::parser parser;
    const simdjson::padded_string padded{text};
    simdjson::dom::element el;
    if (parser.parse(padded).get(el)) return false;
    return el.is_object();
}

std::string statePayload(ItemState from, ItemState to, const std::string& actor, const std::string& reason)
{
    json::Writer w;
    w.beginObject().kv("from", stateName(from)).kv("to", stateName(to)).kv("actor", actor).kv("reason", reason).endObject();
    return w.take();
}

bool validTag(std::string_view t)
{
    return !t.empty() && std::all_of(t.begin(), t.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '/'; });
}

}  // namespace

PutResult Store::putLayer(const std::string& layer, const std::string& source, const std::vector<ItemInput>& items)
{
    PutResult r;
    ++epoch_;
    for (const ItemInput& in : items) {
        const std::string& key = in.item.key;
        if (key.empty()) { ++r.rejected; continue; }
        StoredItem* cur = state_.find(key);
        if (cur != nullptr && sameContent(*cur, layer, source, in)) {
            ++r.unchanged;   // the stored data was validated when it came in, so an unchanged item costs no parse
        } else {
            if (!in.data.empty() && !isJsonObject(in.data)) {
                ++r.rejected;
                if (cur != nullptr) cur->touchedBy = epoch_;   // offered, only unusable: not vanished
                continue;
            }
            const bool isNew = cur == nullptr;
            (void)emit(std::string{kPut}, key, putPayload(layer, source, in));
            cur = state_.find(key);
            if (isNew) ++r.added; else ++r.changed;
        }
        if (cur->state == ItemState::done || cur->state == ItemState::verified) {
            (void)emit(std::string{kState}, key, statePayload(cur->state, ItemState::open, source, "reappeared"));
            ++r.reopened;
        }
        cur->touchedBy = epoch_;
    }
    const auto layerIt = state_.layers.find(layer);
    if (layerIt != state_.layers.end()) {
        // Index-based: emitting does not change the layer's vector, but a reducer registered later might, and an index stays valid where an iterator would not.
        for (std::size_t i = 0; i < layerIt->second.size(); ++i) {
            StoredItem* p = layerIt->second[i];
            if (p->source != source || p->touchedBy == epoch_ || (p->state != ItemState::open && p->state != ItemState::held)) continue;
            (void)emit(std::string{kState}, p->item.key, statePayload(p->state, ItemState::done, source, "vanished"));
            ++r.vanished;
        }
    }
    return r;
}

PutResult Store::putLayer(const std::string& layer, const std::string& source, const std::vector<LayerItem>& items)
{
    std::vector<ItemInput> inputs;
    inputs.reserve(items.size());
    for (const LayerItem& i : items) inputs.push_back({i, {}, {}});
    return putLayer(layer, source, inputs);
}

std::expected<bool, std::string> Store::setState(const std::string& key, ItemState state, const std::string& actor, const std::string& reason)
{
    if (actor.empty()) return std::unexpected("a transition names its actor");
    const StoredItem* it = state_.find(key);
    if (it == nullptr) return std::unexpected(std::format("no item {}", key));
    if (it->state == state) return false;
    (void)emit(std::string{kState}, key, statePayload(it->state, state, actor, reason));
    return true;
}

std::expected<bool, std::string> Store::tag(const std::string& key, const std::string& tag, const std::string& actor)
{
    if (!validTag(tag)) return std::unexpected(std::format("tag '{}' is not lower-case [a-z0-9-/]", tag));
    const StoredItem* it = state_.find(key);
    if (it == nullptr) return std::unexpected(std::format("no item {}", key));
    if (it->tags.contains(tag)) return false;
    json::Writer w;
    w.beginObject().kv("tag", tag).kv("actor", actor).endObject();
    (void)emit(std::string{kTag}, key, w.take());
    return true;
}

std::expected<bool, std::string> Store::untag(const std::string& key, const std::string& tag, const std::string& actor)
{
    const StoredItem* it = state_.find(key);
    if (it == nullptr) return std::unexpected(std::format("no item {}", key));
    if (!it->tags.contains(tag)) return false;
    json::Writer w;
    w.beginObject().kv("tag", tag).kv("actor", actor).endObject();
    (void)emit(std::string{kUntag}, key, w.take());
    return true;
}

std::expected<bool, std::string> Store::link(const std::string& key, const std::string& kind, const std::string& target, const std::string& actor)
{
    if (kind.empty() || target.empty()) return std::unexpected("a link names a kind and a target");
    const StoredItem* it = state_.find(key);
    if (it == nullptr) return std::unexpected(std::format("no item {}", key));
    if (std::find(it->links.begin(), it->links.end(), Link{kind, target}) != it->links.end()) return false;
    json::Writer w;
    w.beginObject().kv("kind", kind).kv("key", target).kv("actor", actor).endObject();
    (void)emit(std::string{kLink}, key, w.take());
    return true;
}

std::expected<bool, std::string> Store::unlink(const std::string& key, const std::string& kind, const std::string& target, const std::string& actor)
{
    const StoredItem* it = state_.find(key);
    if (it == nullptr) return std::unexpected(std::format("no item {}", key));
    if (std::find(it->links.begin(), it->links.end(), Link{kind, target}) == it->links.end()) return false;
    json::Writer w;
    w.beginObject().kv("kind", kind).kv("key", target).kv("actor", actor).endObject();
    (void)emit(std::string{kUnlink}, key, w.take());
    return true;
}

// ---- reads and the snapshot --------------------------------------------------------------------------------------------------------------------------

bool Store::matches(const StoredItem& it, const Query& q) const
{
    auto in = [](const Query::NameSet& set, std::string_view v) { return set.empty() || set.find(v) != set.end(); };
    if (!in(q.layers, it.item.layer) || !in(q.kinds, it.item.kind) || !in(q.states, stateName(it.state)) || !in(q.sources, it.source)) return false;
    if (!q.tags.empty() && std::none_of(it.tags.begin(), it.tags.end(), [&](const std::string& t) { return q.tags.contains(t); })) return false;
    if (!q.text.empty() && it.item.title.find(q.text) == std::string::npos && it.item.detail.find(q.text) == std::string::npos) return false;
    for (const std::string& t : it.tags)
        for (const std::string& prefix : q.hiddenTagPrefixes)
            if (t.starts_with(prefix) && !q.tags.contains(t)) return false;   // a private item leaves only when asked for by name (R-BB-10)
    return true;
}

std::vector<const StoredItem*> Store::items(const Query& q) const
{
    std::vector<const StoredItem*> out;
    for (const auto& [name, list] : state_.layers) {
        if (!q.layers.empty() && !q.layers.contains(name)) continue;
        for (const StoredItem* it : list) if (matches(*it, q)) out.push_back(it);
    }
    return out;
}

std::vector<Backlink> Store::linksTo(std::string_view key) const
{
    const auto it = state_.backlinks.find(key);
    return it == state_.backlinks.end() ? std::vector<Backlink>{} : it->second;
}

int Store::numberOf(std::string_view key) const
{
    const StoredItem* it = state_.find(key);
    if (it == nullptr) return 0;
    const auto& list = state_.layers.find(it->item.layer)->second;
    return static_cast<int>(std::find(list.begin(), list.end(), it) - list.begin()) + 1;
}

namespace {
void writeStamp(json::Writer& w, std::string_view name, const Stamp& s)
{
    w.key(name).beginObject();
    w.key("writer").beginObject().kv("namespace", s.writer.ns).kv("writer", s.writer.writer).endObject();
    w.kv("lamport", static_cast<std::int64_t>(s.lamport)).kv("time", s.time).endObject();
}
}  // namespace

std::string Store::snapshotJson(const Query& q) const
{
    json::Writer w;
    w.beginObject().kv("schema", "store-snapshot-1").kv("head", static_cast<std::int64_t>(head())).kv("events", static_cast<std::int64_t>(events_.size()));
    w.key("layers").beginObject();
    for (const auto& [name, list] : state_.layers) {
        if (!q.layers.empty() && !q.layers.contains(name)) continue;
        w.key(name).beginArray();
        int number = 0;
        for (const StoredItem* it : list) {
            ++number;   // a place in the whole layer, so a scoped export keeps the numbers READ ISSUE n names
            if (!matches(*it, q)) continue;
            w.beginObject().kv("number", number).kv("key", it->item.key).kv("source", it->source).kv("kind", it->item.kind).kv("title", it->item.title);
            w.kv("detail", it->item.detail);
            if (!it->detailFormat.empty()) w.kv("detailFormat", it->detailFormat);
            w.kv("state", stateName(it->state));
            w.key("tags").beginArray();
            for (const std::string& t : it->tags) w.str(t);
            w.endArray();
            w.key("links").beginArray();
            for (const Link& l : it->links) w.beginObject().kv("kind", l.kind).kv("key", l.key).endObject();
            w.endArray();
            w.key("anchors");
            writeAnchors(w, it->item.anchors);
            if (!it->data.empty()) w.kvRaw("data", it->data);
            writeStamp(w, "created", it->created);
            writeStamp(w, "updated", it->updated);
            if (it->lastTransition) {
                const Transition& t = *it->lastTransition;
                w.key("transition").beginObject().kv("from", stateName(t.from)).kv("to", stateName(t.to)).kv("actor", t.actor).kv("reason", t.reason);
                writeStamp(w, "at", t.at);
                w.endObject();
            }
            w.endObject();
        }
        w.endArray();
    }
    w.endObject().endObject();
    return w.take();
}

}  // namespace sub0canvas

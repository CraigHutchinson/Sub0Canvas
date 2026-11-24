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

// ---- the checkpoint ----------------------------------------------------------------------------------------------------------------------------------

namespace {

constexpr std::string_view kCheckpointSchema = "store-checkpoint-1";

/// What a checkpoint says besides the state: where in the merge order it was taken, and which events it covers.
struct CheckpointMeta
{
    std::uint64_t head = 0, events = 0;
    WriterId headWriter;
    std::map<WriterId, std::map<std::uint64_t, std::uint64_t>> seen;
};

void writeStampFull(json::Writer& w, std::string_view name, const Stamp& s)
{
    w.key(name).beginObject();
    w.key("writer").beginObject().kv("namespace", s.writer.ns).kv("writer", s.writer.writer).endObject();
    w.kv("lamport", static_cast<std::int64_t>(s.lamport)).kv("time", s.time).endObject();
}

void readWriter(simdjson::ondemand::value v, WriterId& out)
{
    for (auto f : v.get_object()) {
        const std::string_view k = json::keyOf(f);
        auto fv = f.value();
        if (k == "namespace") out.ns = json::toString(fv);
        else if (k == "writer") out.writer = json::toString(fv);
    }
}

void readStamp(simdjson::ondemand::value v, Stamp& out)
{
    for (auto f : v.get_object()) {
        const std::string_view k = json::keyOf(f);
        auto fv = f.value();
        if (k == "lamport") out.lamport = fv.get_uint64();
        else if (k == "time") out.time = json::toString(fv);
        else if (k == "writer") readWriter(fv.value(), out.writer);
    }
}

void readItem(simdjson::ondemand::value v, StoreState& s, const std::string& layer)
{
    StoredItem item;
    std::string key;
    for (auto f : v.get_object()) {
        const std::string_view k = json::keyOf(f);
        auto fv = f.value();
        if (k == "key") key = json::toString(fv);
        else if (k == "source") item.source = json::toString(fv);
        else if (k == "kind") item.item.kind = json::toString(fv);
        else if (k == "title") item.item.title = json::toString(fv);
        else if (k == "detail") item.item.detail = json::toString(fv);
        else if (k == "detailFormat") item.detailFormat = json::toString(fv);
        else if (k == "state") item.state = parseState(json::toString(fv)).value_or(ItemState::open);
        else if (k == "data") item.data = rawValue(fv.raw_json());
        else if (k == "anchors") readAnchors(fv.value(), item.item.anchors);
        else if (k == "created") readStamp(fv.value(), item.created);
        else if (k == "updated") readStamp(fv.value(), item.updated);
        else if (k == "tags") { for (auto t : fv.get_array()) item.tags.insert(json::toString(t.value())); }
        else if (k == "links") {
            for (auto l : fv.get_array()) {
                Link link;
                for (auto lf : l.get_object()) {
                    const std::string_view lk = json::keyOf(lf);
                    auto lv = lf.value();
                    if (lk == "kind") link.kind = json::toString(lv);
                    else if (lk == "key") link.key = json::toString(lv);
                }
                item.links.push_back(std::move(link));
            }
        }
        else if (k == "transition") {
            Transition t;
            for (auto tf : fv.get_object()) {
                const std::string_view tk = json::keyOf(tf);
                auto tv = tf.value();
                if (tk == "from") t.from = parseState(json::toString(tv)).value_or(ItemState::open);
                else if (tk == "to") t.to = parseState(json::toString(tv)).value_or(ItemState::open);
                else if (tk == "actor") t.actor = json::toString(tv);
                else if (tk == "reason") t.reason = json::toString(tv);
                else if (tk == "at") readStamp(tv.value(), t.at);
            }
            item.lastTransition = std::move(t);
        }
    }
    StoredItem& made = s.create(key, layer);
    item.item.key = made.item.key;
    item.item.layer = layer;
    made = std::move(item);
}

/// Reads a checkpoint into `state` (which must be empty). With `meta`, also where it was taken and what it saw. The error names the first problem.
std::expected<void, std::string> readCheckpoint(const std::string& text, StoreState& state, CheckpointMeta* meta)
{
    bool schemaOk = false;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = json::keyOf(f);
                auto v = f.value();
                if (k == "schema") schemaOk = json::toString(v) == kCheckpointSchema;
                else if (k == "head") { const std::uint64_t n = v.get_uint64(); if (meta != nullptr) meta->head = n; }
                else if (k == "events") { const std::uint64_t n = v.get_uint64(); if (meta != nullptr) meta->events = n; }
                else if (k == "headWriter") { WriterId w; readWriter(v.value(), w); if (meta != nullptr) meta->headWriter = std::move(w); }
                else if (k == "skipped") state.skipped = static_cast<std::size_t>(v.get_uint64());
                else if (k == "unknownKinds") {
                    for (auto u : v.get_object()) {
                        const std::string kind{json::keyOf(u)};
                        state.unknownKinds[kind] = static_cast<std::size_t>(u.value().get_uint64());
                    }
                }
                else if (k == "seen") {
                    for (auto entry : v.get_array()) {
                        WriterId w;
                        std::vector<std::pair<std::uint64_t, std::uint64_t>> runs;
                        for (auto ef : entry.get_object()) {
                            const std::string_view ek = json::keyOf(ef);
                            auto ev = ef.value();
                            if (ek == "namespace") w.ns = json::toString(ev);
                            else if (ek == "writer") w.writer = json::toString(ev);
                            else if (ek == "runs") {
                                for (auto run : ev.get_array()) {
                                    std::uint64_t pair[2] = {0, 0};
                                    int n = 0;
                                    for (auto num : run.get_array()) { if (n < 2) pair[n] = num.get_uint64(); ++n; }
                                    runs.emplace_back(pair[0], pair[1]);
                                }
                            }
                        }
                        if (meta != nullptr) for (const auto& [a, b] : runs) meta->seen[w].emplace(a, b);
                    }
                }
                else if (k == "layers") {
                    for (auto layerField : v.get_object()) {
                        const std::string layer{json::keyOf(layerField)};
                        state.layers[layer];   // an emptied layer is still listed by the snapshot
                        for (auto itemValue : layerField.value().get_array()) readItem(itemValue.value(), state, layer);
                    }
                }
                else if (k == "backlinks") {
                    for (auto target : v.get_object()) {
                        auto& list = state.backlinks[std::string{json::keyOf(target)}];
                        for (auto entry : target.value().get_array()) {
                            Backlink b;
                            int n = 0;
                            for (auto part : entry.get_array()) { (n == 0 ? b.fromKey : b.kind) = json::toString(part.value()); ++n; }
                            list.push_back(std::move(b));
                        }
                    }
                }
            }
        });
    } catch (const simdjson::simdjson_error& err) {
        return std::unexpected(std::format("checkpoint is not valid JSON of the expected shape: {}", err.what()));
    }
    if (!schemaOk) return std::unexpected(std::format("checkpoint is not schema {}", kCheckpointSchema));
    return {};
}

}  // namespace

Checkpoint Store::checkpoint() const
{
    json::Writer w;
    w.beginObject().kv("schema", kCheckpointSchema).kv("head", static_cast<std::int64_t>(head())).kv("events", static_cast<std::int64_t>(eventCount_));
    w.key("headWriter").beginObject().kv("namespace", headWriter_.ns).kv("writer", headWriter_.writer).endObject();
    w.kv("skipped", static_cast<std::int64_t>(state_.skipped));
    w.key("unknownKinds").beginObject();
    for (const auto& [kind, n] : state_.unknownKinds) w.kv(kind, static_cast<std::int64_t>(n));
    w.endObject();
    w.key("seen").beginArray();
    for (const auto& [writer, runs] : seen_.runs_) {
        w.beginObject().kv("namespace", writer.ns).kv("writer", writer.writer).key("runs").beginArray();
        for (const auto& [from, to] : runs) w.beginArray().num(static_cast<std::int64_t>(from)).num(static_cast<std::int64_t>(to)).endArray();
        w.endArray().endObject();
    }
    w.endArray();
    w.key("layers").beginObject();
    for (const auto& [name, list] : state_.layers) {
        w.key(name).beginArray();
        for (const StoredItem* it : list) {
            w.beginObject().kv("key", it->item.key).kv("source", it->source).kv("kind", it->item.kind).kv("title", it->item.title).kv("detail", it->item.detail);
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
            writeStampFull(w, "created", it->created);
            writeStampFull(w, "updated", it->updated);
            if (it->lastTransition) {
                const Transition& t = *it->lastTransition;
                w.key("transition").beginObject().kv("from", stateName(t.from)).kv("to", stateName(t.to)).kv("actor", t.actor).kv("reason", t.reason);
                writeStampFull(w, "at", t.at);
                w.endObject();
            }
            w.endObject();
        }
        w.endArray();
    }
    w.endObject();
    // The reverse index in its own order (the order links were stated), keys sorted because the map's own order is not defined.
    std::vector<const std::string*> targets;
    targets.reserve(state_.backlinks.size());
    for (const auto& entry : state_.backlinks) targets.push_back(&entry.first);
    std::sort(targets.begin(), targets.end(), [](const std::string* a, const std::string* b) { return *a < *b; });
    w.key("backlinks").beginObject();
    for (const std::string* target : targets) {
        w.key(*target).beginArray();
        for (const Backlink& b : state_.backlinks.find(*target)->second) w.beginArray().str(b.fromKey).str(b.kind).endArray();
        w.endArray();
    }
    w.endObject().endObject();
    return {head(), eventCount_, w.take()};
}

std::expected<void, std::string> Store::restore(const Checkpoint& cp)
{
    StoreState state;
    CheckpointMeta meta;
    if (auto read = readCheckpoint(cp.json, state, &meta); !read) return std::unexpected(read.error());
    state_ = std::move(state);
    events_.clear();
    seen_.runs_ = std::move(meta.seen);
    baseSeen_ = seen_;
    base_ = cp;
    base_->head = meta.head;
    base_->events = meta.events;
    headLamport_ = baseLamport_ = meta.head;
    headWriter_ = baseWriter_ = meta.headWriter;
    eventCount_ = meta.events;
    droppedLate_ = 0;
    return {};
}

// ---- the store ---------------------------------------------------------------------------------------------------------------------------------------

Store::Store(StoreOptions options) : options_(std::move(options)) {}

// ---- what has been seen ------------------------------------------------------------------------------------------------------------------------------

bool Store::SeenSet::contains(const WriterId& w, std::uint64_t lamport) const
{
    const auto it = runs_.find(w);
    if (it == runs_.end()) return false;
    auto run = it->second.upper_bound(lamport);   // the first run starting after `lamport`; the one before it may hold it
    if (run == it->second.begin()) return false;
    --run;
    return lamport <= run->second;
}

void Store::SeenSet::insert(const WriterId& w, std::uint64_t lamport)
{
    auto& runs = runs_[w];
    auto next = runs.upper_bound(lamport);
    auto prev = next;
    bool joinPrev = false;
    if (next != runs.begin()) {
        --prev;
        if (lamport <= prev->second) return;                       // already inside a run
        joinPrev = prev->second + 1 == lamport;
    }
    const bool joinNext = next != runs.end() && next->first == lamport + 1;
    if (joinPrev && joinNext) { prev->second = next->second; runs.erase(next); }
    else if (joinPrev) prev->second = lamport;
    else if (joinNext) { const std::uint64_t end = next->second; runs.erase(next); runs.emplace(lamport, end); }
    else runs.emplace(lamport, lamport);
}

// ---- folding and merging -----------------------------------------------------------------------------------------------------------------------------

void Store::fold(const Event& e)
{
    if (const Reducer* r = options_.registry.find(e.kind)) (*r)(state_, e);
    else ++state_.unknownKinds[e.kind];
}

bool Store::afterHead(const Event& e) const
{
    if (eventCount_ == 0) return true;
    if (const auto c = e.lamport <=> headLamport_; c != 0) return c > 0;
    return e.writer > headWriter_;
}

bool Store::beforeBase(const Event& e) const
{
    if (!base_) return false;
    if (const auto c = e.lamport <=> baseLamport_; c != 0) return c < 0;
    return e.writer <= baseWriter_;
}

void Store::note(const Event& e)
{
    if (afterHead(e)) { headLamport_ = e.lamport; headWriter_ = e.writer; }
    seen_.insert(e.writer, e.lamport);
    ++eventCount_;
}

void Store::accept(const Event& e)
{
    fold(e);
    note(e);
    if (options_.retainEvents) events_.push_back(e);
}

void Store::accept(Event&& e)
{
    fold(e);
    note(e);
    if (options_.retainEvents) events_.push_back(std::move(e));
}

void Store::resetToBase()
{
    state_ = StoreState{};
    if (base_) (void)readCheckpoint(base_->json, state_, nullptr);   // it was read once already, in `restore`
}

void Store::rebuild()
{
    resetToBase();
    for (const Event& held : events_) fold(held);
}

bool Store::append(const Event& e)
{
    if (seen_.contains(e.writer, e.lamport)) return false;
    if (afterHead(e)) {
        accept(e);
    } else {
        if (!options_.retainEvents || beforeBase(e)) { ++droppedLate_; return false; }   // no held events to fold it in among
        const auto it = std::lower_bound(events_.begin(), events_.end(), e, [](const Event& a, const Event& b) { return eventOrder(a, b) < 0; });
        events_.insert(it, e);
        note(e);
        rebuild();
    }
    if (options_.sink != nullptr) options_.sink->append(e);
    return true;
}

void Store::merge(std::vector<Event> batch)
{
    const auto before = [](const Event& a, const Event& b) { return eventOrder(a, b) < 0; };
    std::stable_sort(batch.begin(), batch.end(), before);
    batch.erase(std::unique(batch.begin(), batch.end(), [](const Event& a, const Event& b) { return eventOrder(a, b) == 0; }), batch.end());
    batch.erase(std::remove_if(batch.begin(), batch.end(), [&](const Event& e) { return seen_.contains(e.writer, e.lamport); }), batch.end());
    if (batch.empty()) return;
    const auto firstAfter = std::partition_point(batch.begin(), batch.end(), [&](const Event& e) { return !afterHead(e); });   // sorted: the late ones come first
    if (firstAfter == batch.begin() || !options_.retainEvents) {
        droppedLate_ += static_cast<std::size_t>(firstAfter - batch.begin());   // without held events there is nothing to fold a late one in among
        if (options_.retainEvents) events_.reserve(events_.size() + batch.size());
        for (auto it = firstAfter; it != batch.end(); ++it) accept(std::move(*it));
        return;
    }
    if (base_) {   // what sorts before the restored checkpoint is inside it already, and cannot be placed
        const auto keep = std::partition_point(batch.begin(), batch.end(), [&](const Event& e) { return beforeBase(e); });
        droppedLate_ += static_cast<std::size_t>(keep - batch.begin());
        batch.erase(batch.begin(), keep);
    }
    for (const Event& e : batch) { seen_.insert(e.writer, e.lamport); ++eventCount_; }
    std::vector<Event> merged;
    merged.reserve(events_.size() + batch.size());
    std::merge(std::make_move_iterator(events_.begin()), std::make_move_iterator(events_.end()), std::make_move_iterator(batch.begin()),
               std::make_move_iterator(batch.end()), std::back_inserter(merged), before);
    events_ = std::move(merged);
    if (!events_.empty() && afterHead(events_.back())) { headLamport_ = events_.back().lamport; headWriter_ = events_.back().writer; }
    rebuild();
}

void Store::replay(const EventSource& source, std::uint64_t from)
{
    std::vector<Event> batch = source.since(from);
    if (options_.retainEvents) { merge(std::move(batch)); return; }
    // Without held events a late event is not foldable in place: if the batch holds one, the fold is rebuilt by reading the source again from the restored
    // checkpoint (or the start), the one place the events still are.
    const bool late = std::any_of(batch.begin(), batch.end(), [&](const Event& e) { return !seen_.contains(e.writer, e.lamport) && !afterHead(e); });
    if (!late) { merge(std::move(batch)); return; }
    resetToBase();
    seen_ = baseSeen_;
    eventCount_ = base_ ? base_->events : 0;
    headLamport_ = base_ ? baseLamport_ : 0;
    headWriter_ = base_ ? baseWriter_ : WriterId{};
    merge(source.since(eventCount_));
}

const Event& Store::emit(std::string kind, std::string subject, std::string payload)
{
    Event e;
    e.kind = std::move(kind);
    e.writer = options_.writer;
    e.lamport = head() + 1;
    e.subject = std::move(subject);
    e.time = now();
    e.payload = std::move(payload);
    // The new lamport sorts after every event folded, so it is accepted in place and, when events are kept, is last.
    if (options_.retainEvents) {
        (void)append(e);
        return events_.back();
    }
    lastEmitted_ = std::move(e);
    (void)append(lastEmitted_);
    return lastEmitted_;
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
    w.beginObject().kv("schema", "store-snapshot-1").kv("head", static_cast<std::int64_t>(head())).kv("events", static_cast<std::int64_t>(eventCount_));
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

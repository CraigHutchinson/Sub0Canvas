#include "sub0canvas/view.hpp"

#include "json_util.hpp"
#include "scan.hpp"
#include "sub0canvas/content_id.hpp"

#include <algorithm>
#include <cstdio>
#include <format>
#include <sstream>
#include <unordered_map>

namespace sub0canvas {

// ---- the pin and positions --------------------------------------------------------------------------------------------------------------------------------

int Pin::revisionOf(std::string_view path) const
{
    if (const auto it = overrides.find(path); it != overrides.end()) return it->second;
    return sealed ? 0 : base;
}

bool inScope(const std::set<std::string, std::less<>>& prefixes, std::string_view path)
{
    if (prefixes.empty()) return true;
    return std::any_of(prefixes.begin(), prefixes.end(), [&](const std::string& p) { return path.starts_with(p); });
}

Pin pinAtTip(const Canvas& canvas, const std::set<std::string, std::less<>>& prefixes)
{
    Pin pin;
    pin.sealed = true;
    for (const std::string& path : canvas.paths()) {
        if (!inScope(prefixes, path)) continue;
        const int rev = canvas.revision(path);
        pin.overrides[path] = rev;
        pin.base = std::max(pin.base, rev);
    }
    return pin;
}

std::optional<std::string> textAtPin(const Canvas& canvas, const Pin& pin, const std::string& path)
{
    const int rev = pin.revisionOf(path);
    if (rev < 1 || !canvas.has(path) || rev > canvas.revision(path)) return std::nullopt;
    return canvas.textAt(path, rev);
}

Position currentPosition(const Canvas& canvas, const Store& store, const BlockIndex* index)
{
    Position p;
    for (const std::string& path : canvas.paths()) p.generation += static_cast<std::uint64_t>(canvas.revision(path));
    p.indexEvents = index != nullptr ? index->events().size() : 0;
    p.storeHead = store.head();
    p.storeEvents = store.eventCount();
    return p;
}

std::string positionJson(const Position& p)
{
    json::Writer w;
    w.beginObject()
        .kv("generation", static_cast<std::int64_t>(p.generation))
        .kv("indexEvents", static_cast<std::int64_t>(p.indexEvents))
        .kv("storeHead", static_cast<std::int64_t>(p.storeHead))
        .kv("storeEvents", static_cast<std::int64_t>(p.storeEvents))
        .endObject();
    return w.take();
}

std::string_view toString(PinState s) { return s == PinState::pinned ? "pinned" : "unpinned"; }
std::string_view toString(Notify n) { return n == Notify::none ? "none" : "flag"; }
std::string_view toString(CompactBy c) { return c == CompactBy::block ? "block" : "block+item"; }
std::string_view toString(DeltaForm f)
{
    switch (f) {
        case DeltaForm::brief: return "brief";
        case DeltaForm::elided: return "elided";
        case DeltaForm::compacted: return "compacted";
        case DeltaForm::full: return "full";
    }
    return "brief";
}
std::optional<DeltaForm> parseDeltaForm(std::string_view name)
{
    for (const DeltaForm f : {DeltaForm::brief, DeltaForm::elided, DeltaForm::compacted, DeltaForm::full})
        if (toString(f) == name) return f;
    return std::nullopt;
}

// ---- records as JSON --------------------------------------------------------------------------------------------------------------------------------------

namespace {

void writeSet(json::Writer& w, std::string_view key, const Query::NameSet& set)
{
    w.key(key).beginArray();
    for (const std::string& s : set) w.str(s);
    w.endArray();
}

void writeScope(json::Writer& w, const SubscriptionQuery& q)
{
    w.beginObject();
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
    w.endArray().endObject();
}

void writeAnchor(json::Writer& w, const SourceAnchor& a)
{
    w.beginObject()
        .kv("path", a.path).kv("revision", a.revision).kv("quote", a.quote).kv("prefix", a.prefix).kv("suffix", a.suffix)
        .kv("line", a.line).kv("column", a.column).kv("endLine", a.endLine).kv("endColumn", a.endColumn)
        .kv("blockId", a.blockId).kv("textId", a.textId)
        .endObject();
}

void writeWriter(json::Writer& w, const WriterId& id) { w.beginObject().kv("namespace", id.ns).kv("writer", id.writer).endObject(); }

template <class V>
void readStrings(V arr, const auto& insert)
{
    for (auto el : arr) insert(json::toString(el));
}

void readScope(simdjson::ondemand::value v, SubscriptionQuery& q)
{
    q.hiddenTagPrefixes.clear();
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

void readAnchor(simdjson::ondemand::value v, SourceAnchor& a)
{
    for (auto f : v.get_object()) {
        const std::string_view k = json::keyOf(f);
        auto fv = f.value();
        if (k == "path") a.path = json::toString(fv);
        else if (k == "revision") a.revision = json::toString(fv);
        else if (k == "quote") a.quote = json::toString(fv);
        else if (k == "prefix") a.prefix = json::toString(fv);
        else if (k == "suffix") a.suffix = json::toString(fv);
        else if (k == "line") a.line = static_cast<int>(fv.get_int64());
        else if (k == "column") a.column = static_cast<int>(fv.get_int64());
        else if (k == "endLine") a.endLine = static_cast<int>(fv.get_int64());
        else if (k == "endColumn") a.endColumn = static_cast<int>(fv.get_int64());
        else if (k == "blockId") a.blockId = json::toString(fv);
        else if (k == "textId") a.textId = json::toString(fv);
    }
}

void readWriter(simdjson::ondemand::value v, WriterId& id)
{
    for (auto f : v.get_object()) {
        const std::string_view k = json::keyOf(f);
        auto fv = f.value();
        if (k == "namespace") id.ns = json::toString(fv);
        else if (k == "writer") id.writer = json::toString(fv);
    }
}

void readPosition(simdjson::ondemand::value v, Position& p)
{
    for (auto f : v.get_object()) {
        const std::string_view k = json::keyOf(f);
        auto fv = f.value();
        if (k == "generation") p.generation = fv.get_uint64();
        else if (k == "indexEvents") p.indexEvents = fv.get_uint64();
        else if (k == "storeHead") p.storeHead = fv.get_uint64();
        else if (k == "storeEvents") p.storeEvents = fv.get_uint64();
    }
}

}  // namespace

std::string viewJson(const ViewRecord& v)
{
    json::Writer w;
    w.beginObject().kv("id", v.id).kv("session", v.session);
    w.key("owner");
    writeWriter(w, v.owner);
    w.kv("label", v.label).kv("state", toString(v.state));
    w.key("pin").beginObject().kv("base", v.pin.base).kvBool("sealed", v.pin.sealed).key("overrides").beginObject();
    for (const auto& [path, rev] : v.pin.overrides) w.kv(path, rev);
    w.endObject().endObject();
    w.kvRaw("position", positionJson(v.position));
    w.key("scope");
    writeScope(w, v.scope);
    w.kv("detail", toString(v.detail));
    w.key("budget").beginObject()
        .kv("maxChars", static_cast<std::int64_t>(v.budget.maxChars)).kv("context", v.budget.context)
        .kv("briefTop", static_cast<std::int64_t>(v.budget.briefTop)).kv("nameClip", static_cast<std::int64_t>(v.budget.nameClip))
        .kv("briefOverlaps", static_cast<std::int64_t>(v.budget.briefOverlaps))
        .endObject();
    w.kv("compactBy", toString(v.compactBy)).kv("notify", toString(v.notify)).kv("parent", v.parent).kv("fingerprint", v.fingerprint);
    w.key("held").beginArray();
    for (const HeldAnchor& h : v.held) {
        w.beginObject().kv("key", h.key).kv("status", h.status);
        w.key("anchor");
        writeAnchor(w, h.anchor);
        w.endObject();
    }
    w.endArray().kvBool("closed", v.closed).endObject();
    return w.take();
}

bool ViewRecord::operator==(const ViewRecord& other) const { return viewJson(*this) == viewJson(other); }

std::expected<ViewRecord, std::string> parseViewRecord(std::string_view text)
{
    ViewRecord v;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = json::keyOf(f);
                auto fv = f.value();
                if (k == "id") v.id = json::toString(fv);
                else if (k == "session") v.session = json::toString(fv);
                else if (k == "owner") readWriter(fv.value(), v.owner);
                else if (k == "label") v.label = json::toString(fv);
                else if (k == "state") v.state = json::toString(fv) == "unpinned" ? PinState::unpinned : PinState::pinned;
                else if (k == "pin") {
                    for (auto pf : fv.get_object()) {
                        const std::string_view pk = json::keyOf(pf);
                        auto pv = pf.value();
                        if (pk == "base") v.pin.base = static_cast<int>(pv.get_int64());
                        else if (pk == "sealed") v.pin.sealed = pv.get_bool();
                        else if (pk == "overrides")
                            for (auto of : pv.get_object()) {
                                const std::string path{json::keyOf(of)};
                                v.pin.overrides[path] = static_cast<int>(of.value().get_int64());
                            }
                    }
                } else if (k == "position") readPosition(fv.value(), v.position);
                else if (k == "scope") readScope(fv.value(), v.scope);
                else if (k == "detail") v.detail = parseDeltaForm(json::toString(fv)).value_or(DeltaForm::compacted);
                else if (k == "budget") {
                    for (auto bf : fv.get_object()) {
                        const std::string_view bk = json::keyOf(bf);
                        auto bv = bf.value();
                        if (bk == "maxChars") v.budget.maxChars = bv.get_uint64();
                        else if (bk == "context") v.budget.context = static_cast<int>(bv.get_int64());
                        else if (bk == "briefTop") v.budget.briefTop = bv.get_uint64();
                        else if (bk == "nameClip") v.budget.nameClip = bv.get_uint64();
                        else if (bk == "briefOverlaps") v.budget.briefOverlaps = bv.get_uint64();
                    }
                } else if (k == "compactBy") v.compactBy = json::toString(fv) == "block" ? CompactBy::block : CompactBy::blockItem;
                else if (k == "notify") v.notify = json::toString(fv) == "flag" ? Notify::flag : Notify::none;
                else if (k == "parent") v.parent = json::toString(fv);
                else if (k == "fingerprint") v.fingerprint = json::toString(fv);
                else if (k == "held") {
                    for (auto hv : fv.get_array()) {
                        HeldAnchor h;
                        for (auto hf : hv.get_object()) {
                            const std::string_view hk = json::keyOf(hf);
                            auto hfv = hf.value();
                            if (hk == "key") h.key = json::toString(hfv);
                            else if (hk == "status") h.status = json::toString(hfv);
                            else if (hk == "anchor") readAnchor(hfv.value(), h.anchor);
                        }
                        v.held.push_back(std::move(h));
                    }
                } else if (k == "closed") v.closed = fv.get_bool();
            }
        });
    } catch (const simdjson::simdjson_error& e) {
        return std::unexpected(std::format("view record: {}", simdjson::error_message(e.error())));
    }
    if (v.id.empty()) return std::unexpected("view record: no id");
    return v;
}

std::string sessionJson(const SessionRecord& s)
{
    json::Writer w;
    w.beginObject().kv("id", s.id);
    w.key("actor");
    writeWriter(w, s.actor);
    w.key("limits").beginObject()
        .kv("views", static_cast<std::int64_t>(s.limits.maxViews)).kv("pendingBytes", static_cast<std::int64_t>(s.limits.maxPendingBytes))
        .kv("subscriptions", static_cast<std::int64_t>(s.limits.maxSubscriptions)).kv("idleTtl", static_cast<std::int64_t>(s.limits.idleTtl))
        .endObject();
    w.kv("created", static_cast<std::int64_t>(s.created)).kv("lastSeen", static_cast<std::int64_t>(s.lastSeen)).kvBool("closed", s.closed).kv("closeReason", s.closeReason).endObject();
    return w.take();
}

std::expected<SessionRecord, std::string> parseSessionRecord(std::string_view text)
{
    SessionRecord s;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto f : obj) {
                const std::string_view k = json::keyOf(f);
                auto fv = f.value();
                if (k == "id") s.id = json::toString(fv);
                else if (k == "actor") readWriter(fv.value(), s.actor);
                else if (k == "limits") {
                    for (auto lf : fv.get_object()) {
                        const std::string_view lk = json::keyOf(lf);
                        auto lv = lf.value();
                        if (lk == "views") s.limits.maxViews = lv.get_uint64();
                        else if (lk == "pendingBytes") s.limits.maxPendingBytes = lv.get_uint64();
                        else if (lk == "subscriptions") s.limits.maxSubscriptions = lv.get_uint64();
                        else if (lk == "idleTtl") s.limits.idleTtl = lv.get_int64();
                    }
                } else if (k == "created") s.created = fv.get_int64();
                else if (k == "lastSeen") s.lastSeen = fv.get_int64();
                else if (k == "closed") s.closed = fv.get_bool();
                else if (k == "closeReason") s.closeReason = json::toString(fv);
            }
        });
    } catch (const simdjson::simdjson_error& e) {
        return std::unexpected(std::format("session record: {}", simdjson::error_message(e.error())));
    }
    if (s.id.empty()) return std::unexpected("session record: no id");
    return s;
}

// ---- the reducers and the log-backed store ----------------------------------------------------------------------------------------------------------------

bool registerViewReducers(ReducerRegistry& registry)
{
    if (registry.find(kSubscribeKind) == nullptr) (void)registerSubscriptionReducers(registry);
    const bool a = registry.add(std::string{kViewPutKind}, [](StoreState& s, const Event& e) {
        auto rec = parseViewRecord(e.payload);
        if (!rec) { ++s.skipped; return; }
        s.ext<ViewsState>().views[rec->id] = std::move(*rec);
    });
    const bool b = registry.add(std::string{kSessionPutKind}, [](StoreState& s, const Event& e) {
        auto rec = parseSessionRecord(e.payload);
        if (!rec) { ++s.skipped; return; }
        s.ext<ViewsState>().sessions[rec->id] = std::move(*rec);
    });
    return a && b;
}

void LogViewStore::putSession(const SessionRecord& record) { (void)store_.emit(std::string{kSessionPutKind}, record.id, sessionJson(record)); }
void LogViewStore::putView(const ViewRecord& record) { (void)store_.emit(std::string{kViewPutKind}, record.id, viewJson(record)); }

const SessionRecord* LogViewStore::session(std::string_view id) const
{
    const ViewsState* st = store_.state().extIf<ViewsState>();
    if (st == nullptr) return nullptr;
    const auto it = st->sessions.find(id);
    return it == st->sessions.end() ? nullptr : &it->second;
}

const ViewRecord* LogViewStore::view(std::string_view id) const
{
    const ViewsState* st = store_.state().extIf<ViewsState>();
    if (st == nullptr) return nullptr;
    const auto it = st->views.find(id);
    return it == st->views.end() ? nullptr : &it->second;
}

std::vector<const ViewRecord*> LogViewStore::viewsOf(std::string_view session) const
{
    std::vector<const ViewRecord*> out;
    const ViewsState* st = store_.state().extIf<ViewsState>();
    if (st == nullptr) return out;
    const std::string prefix = std::string{session} + "/";
    for (auto it = st->views.lower_bound(prefix); it != st->views.end() && it->first.starts_with(prefix); ++it) out.push_back(&it->second);
    return out;
}

std::vector<const SessionRecord*> LogViewStore::sessions() const
{
    std::vector<const SessionRecord*> out;
    if (const ViewsState* st = store_.state().extIf<ViewsState>())
        for (const auto& [id, rec] : st->sessions) out.push_back(&rec);
    return out;
}

// ---- line diffs -------------------------------------------------------------------------------------------------------------------------------------------

namespace {

using scan::joinLines;
using scan::splitLines;

constexpr std::size_t kMaxLcsCells = 4'000'000;

}  // namespace

std::vector<LineHunk> diffLines(const std::string& oldText, const std::string& newText)
{
    const auto a = splitLines(oldText), b = splitLines(newText);
    std::size_t head = 0;
    while (head < a.size() && head < b.size() && a[head] == b[head]) ++head;
    std::size_t tail = 0;
    while (tail < a.size() - head && tail < b.size() - head && a[a.size() - 1 - tail] == b[b.size() - 1 - tail]) ++tail;
    const std::size_t n = a.size() - head - tail, m = b.size() - head - tail;
    std::vector<LineHunk> out;
    if (n == 0 && m == 0) return out;

    const auto makeHunk = [&](std::size_t i0, std::size_t i1, std::size_t j0, std::size_t j1) {   // middle-relative, half-open
        LineHunk h;
        h.baseFrom = static_cast<int>(head + i0) + 1;
        h.baseTo = static_cast<int>(head + i1);
        h.newFrom = static_cast<int>(head + j0) + 1;
        h.newTo = static_cast<int>(head + j1);
        h.before.assign(a.begin() + static_cast<std::ptrdiff_t>(head + i0), a.begin() + static_cast<std::ptrdiff_t>(head + i1));
        h.after.assign(b.begin() + static_cast<std::ptrdiff_t>(head + j0), b.begin() + static_cast<std::ptrdiff_t>(head + j1));
        return h;
    };
    if (n == 0 || m == 0 || (n + 1) * (m + 1) > kMaxLcsCells) {
        out.push_back(makeHunk(0, n, 0, m));
        return out;
    }
    // lcs(i, j): the longest common subsequence of the middle's suffixes from i and j.
    std::vector<std::uint32_t> lcs((n + 1) * (m + 1), 0);
    const auto at = [&](std::size_t i, std::size_t j) -> std::uint32_t& { return lcs[i * (m + 1) + j]; };
    for (std::size_t i = n; i-- > 0;)
        for (std::size_t j = m; j-- > 0;)
            at(i, j) = a[head + i] == b[head + j] ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
    std::size_t i = 0, j = 0, i0 = 0, j0 = 0;
    bool open = false;
    const auto close = [&] {
        if (open) out.push_back(makeHunk(i0, i, j0, j));
        open = false;
    };
    while (i < n || j < m) {
        if (i < n && j < m && a[head + i] == b[head + j]) { close(); ++i; ++j; continue; }
        if (!open) { open = true; i0 = i; j0 = j; }
        if (j >= m || (i < n && at(i + 1, j) >= at(i, j + 1))) ++i;
        else ++j;
    }
    close();
    return out;
}

std::string applyHunks(const std::string& oldText, const std::vector<LineHunk>& chosen)
{
    const auto a = splitLines(oldText);
    std::vector<const LineHunk*> order;
    for (const LineHunk& h : chosen) order.push_back(&h);
    std::sort(order.begin(), order.end(), [](const LineHunk* x, const LineHunk* y) { return x->baseFrom < y->baseFrom; });
    std::vector<std::string> out;
    std::size_t pos = 0;
    for (const LineHunk* h : order) {
        for (; pos < static_cast<std::size_t>(h->baseFrom) - 1 && pos < a.size(); ++pos) out.push_back(a[pos]);
        out.insert(out.end(), h->after.begin(), h->after.end());
        pos = static_cast<std::size_t>(std::max(h->baseTo, 0));
    }
    for (; pos < a.size(); ++pos) out.push_back(a[pos]);
    return joinLines(out);
}

// ---- scratch ----------------------------------------------------------------------------------------------------------------------------------------------

std::expected<ReplyApplied, std::string> FileScratch::stage(const CanvasReply& reply, const ApplyOptions& options, const BaseText& base)
{
    Canvas work;
    std::map<std::string, std::string> loaded;
    for (const std::string& path : options.allowed) {
        std::optional<std::string> text;
        if (const auto it = pending_.find(path); it != pending_.end()) text = it->second;
        else text = base(path);
        if (!text) continue;
        work.addFile(path, *text);
        loaded[path] = *text;
    }
    ApplyOptions o = options;
    o.baseRevision = 0;   // the view's own numbering: base plus what is already pending
    auto applied = applyReply(work, reply, o);
    if (!applied) return std::unexpected(applied.error());
    for (const std::string& path : work.paths()) {
        const auto it = loaded.find(path);
        if (it == loaded.end() || it->second != work.text(path)) pending_[path] = work.text(path);
    }
    return applied;
}

std::optional<std::string> FileScratch::overlay(const std::string& path) const
{
    const auto it = pending_.find(path);
    return it == pending_.end() ? std::nullopt : std::optional<std::string>{it->second};
}

std::vector<std::string> FileScratch::paths() const
{
    std::vector<std::string> v;
    for (const auto& [path, text] : pending_) v.push_back(path);
    return v;
}

void FileScratch::set(const std::string& path, std::optional<std::string> text)
{
    if (text) pending_[path] = std::move(*text);
    else pending_.erase(path);
}

// ---- conflicts and fingerprints ---------------------------------------------------------------------------------------------------------------------------

std::optional<Conflict> conflictSince(const Canvas& canvas, const std::string& path, int baseRevision, int from, int to)
{
    int shift = 0;
    for (const CanvasLogEntry& e : canvas.log()) {
        if (e.path != path || e.revision <= baseRevision) continue;
        const int lo = from + shift, hi = to + shift;
        if (e.to < lo) shift += e.delta;
        else if (e.from > hi) continue;
        else return Conflict{e.by, e.revision, e.from, e.to};
    }
    return std::nullopt;
}

std::string pinFingerprint(const Canvas& canvas, const Pin& pin, const std::set<std::string, std::less<>>& prefixes)
{
    std::string manifest;
    for (const std::string& path : canvas.paths()) {
        if (!inScope(prefixes, path)) continue;
        const auto text = textAtPin(canvas, pin, path);
        if (!text) continue;
        manifest += std::format("{}\x01{}\x01{}\n", path, pin.revisionOf(path), contentId(*text));
    }
    return contentId(manifest);
}

// ---- the delta --------------------------------------------------------------------------------------------------------------------------------------------

bool isViewBookkeeping(std::string_view kind) { return kind.starts_with("view.") || kind.starts_with("session.") || kind.starts_with("sub."); }

Delta makeDelta(const DeltaSources& src, const Pin& since, const Position& from, const std::set<std::string, std::less<>>& scope, bool steps)
{
    Delta d;
    d.from = from;
    if (src.canvas != nullptr && src.store != nullptr) d.to = currentPosition(*src.canvas, *src.store, src.index);
    if (src.canvas != nullptr && d.to.generation != from.generation) {
        const Canvas& canvas = *src.canvas;
        std::unordered_map<std::string, std::vector<const CanvasLogEntry*>> logged;
        for (const CanvasLogEntry& e : canvas.log()) logged[e.path].push_back(&e);
        for (const std::string& path : canvas.paths()) {
            if (!inScope(scope, path)) continue;
            const int cur = canvas.revision(path), fr = since.revisionOf(path);
            if (fr == cur) continue;
            FileDelta fd;
            fd.path = path;
            fd.fromRevision = fr;
            fd.toRevision = cur;
            fd.oldText = fr >= 1 ? canvas.textAt(path, fr) : std::string{};
            fd.newText = canvas.text(path);
            for (LineHunk& h : diffLines(fd.oldText, fd.newText)) {
                Region r;
                if (src.index != nullptr) {
                    const Block* b = src.index->at(path, std::max(h.newFrom, 1));
                    if (b == nullptr && h.newFrom > 1) b = src.index->at(path, h.newFrom - 1);
                    if (b != nullptr) { r.blockId = b->id; r.blockName = b->name; }
                }
                r.hunk = std::move(h);
                fd.regions.push_back(std::move(r));
            }
            if (const auto it = logged.find(path); it != logged.end())
                for (const CanvasLogEntry* e : it->second) {
                    if (e->revision <= std::max(fr, 1)) continue;
                    EditStep s{e->revision, e->by, e->from, e->to, e->delta, {}};
                    if (steps) s.hunks = diffLines(e->revision > 1 ? canvas.textAt(path, e->revision - 1) : std::string{}, canvas.textAt(path, e->revision));
                    fd.steps.push_back(std::move(s));
                }
            d.files.push_back(std::move(fd));
        }
    }
    if (src.inbox) {
        for (const Event& e : src.inbox->events)
            if (!isViewBookkeeping(e.kind)) d.events.push_back(e);
        for (const LayerItem& li : src.inbox->items) {
            ItemChange c;
            c.key = li.key;
            c.layer = li.layer;
            c.kind = li.kind;
            c.title = li.title;
            if (src.store != nullptr)
                if (const StoredItem* si = src.store->item(li.key)) c.state = std::string{stateName(si->state)};
            if (!li.anchors.empty()) c.blockId = li.anchors.front().blockId;
            d.items.push_back(std::move(c));
        }
    }
    return d;
}

// ---- compaction -------------------------------------------------------------------------------------------------------------------------------------------

Compacted BlockCompactor::compact(const Delta& delta, const CompactPolicy& policy) const
{
    Compacted out;
    for (const FileDelta& f : delta.files) {
        std::map<std::string, std::size_t> groupOf;   // block id (within this file) to its change
        for (const Region& r : f.regions) {
            auto [it, fresh] = groupOf.try_emplace(r.blockId, out.changes.size());
            if (fresh) out.changes.push_back({f.path, r.blockId, r.blockName, {}, {}});
            out.changes[it->second].hunks.push_back(r.hunk);
        }
    }
    for (const ItemChange& item : delta.items) {
        if (policy.by == CompactBy::blockItem && !item.blockId.empty()) {
            const auto it = std::find_if(out.changes.begin(), out.changes.end(), [&](const CompactedChange& c) { return c.blockId == item.blockId; });
            if (it != out.changes.end()) { it->itemKeys.push_back(item.key); continue; }
        }
        out.items.push_back(item);
    }
    return out;
}

// ---- rendering --------------------------------------------------------------------------------------------------------------------------------------------

namespace {

std::string clip(const std::string& s, std::size_t n) { return s.size() <= n ? s : s.substr(0, n); }

void writeTop(json::Writer& w, std::string_view key, const std::map<std::string, std::size_t>& counts, const DeltaBudget& budget, std::size_t& more)
{
    std::vector<std::pair<std::string, std::size_t>> v(counts.begin(), counts.end());
    std::stable_sort(v.begin(), v.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
    w.key(key).beginObject();
    for (std::size_t i = 0; i < v.size() && i < budget.briefTop; ++i) w.kv(clip(v[i].first, budget.nameClip), static_cast<std::int64_t>(v[i].second));
    w.endObject();
    more = v.size() > budget.briefTop ? v.size() - budget.briefTop : 0;
}

std::string lineSpan(int from, int to)
{
    if (to < from) return std::format("after {}", to);
    return from == to ? std::to_string(from) : std::format("{}-{}", from, to);
}

void writeHunk(json::Writer& w, const LineHunk& h)
{
    w.beginObject().kv("at", h.baseFrom);
    w.key("before").beginArray();
    for (const std::string& l : h.before) w.str(l);
    w.endArray().key("after").beginArray();
    for (const std::string& l : h.after) w.str(l);
    w.endArray().endObject();
}

/// The new text with the changed lines and `context` lines around them, unchanged stretches as `... (n lines)`; numbered as `foldedView` numbers (`%4d| `), a
/// changed line marked `+` in place of the space after the bar, a removal as one row of its own.
std::string elidedText(const FileDelta& f, int context)
{
    const auto lines = splitLines(f.newText);
    const int n = static_cast<int>(lines.size());
    std::vector<bool> show(static_cast<std::size_t>(n) + 2, false), changed(static_cast<std::size_t>(n) + 2, false);
    std::map<int, int> removedAt;   // new line number the removal sits before to lines removed
    const auto mark = [&](int lo, int hi) {
        for (int i = std::max(lo, 1); i <= std::min(hi, n); ++i) show[static_cast<std::size_t>(i)] = true;
    };
    for (const Region& r : f.regions) {
        const LineHunk& h = r.hunk;
        if (h.newTo >= h.newFrom) {
            for (int i = h.newFrom; i <= h.newTo && i <= n; ++i) changed[static_cast<std::size_t>(i)] = true;
            mark(h.newFrom - context, h.newTo + context);
        } else mark(h.newFrom - context, h.newFrom - 1 + context);
        if (h.newTo < h.newFrom) removedAt[h.newFrom] += static_cast<int>(h.before.size());   // a pure deletion; a replacement shows its new lines
    }
    std::string out;
    const auto removal = [&](int at) {
        if (const auto it = removedAt.find(at); it != removedAt.end()) out += std::format("      - ({} removed)\n", it->second);
    };
    for (int i = 1; i <= n;) {
        if (show[static_cast<std::size_t>(i)]) {
            removal(i);
            out += std::format("{:4}|{}{}\n", i, changed[static_cast<std::size_t>(i)] ? '+' : ' ', lines[static_cast<std::size_t>(i) - 1]);
            ++i;
            continue;
        }
        int j = i;
        while (j <= n && !show[static_cast<std::size_t>(j)]) ++j;
        out += std::format("      ... ({} lines)\n", j - i);
        i = j;
    }
    removal(n + 1);
    return out;
}

void writeBriefMembers(json::Writer& w, const Delta& d, const DeltaBudget& budget)
{
    std::set<std::pair<std::string, std::string>> blocks;
    std::size_t edits = 0;
    std::map<std::string, std::size_t> kinds, actors;
    for (const FileDelta& f : d.files) {
        for (const Region& r : f.regions)
            if (!r.blockId.empty()) blocks.insert({f.path, r.blockId});
        for (const EditStep& s : f.steps) { ++edits; ++actors[s.by]; }
    }
    for (const Event& e : d.events) {
        ++kinds[e.kind];
        ++actors[e.writer.ns + "/" + e.writer.writer];
    }
    w.kv("files", static_cast<std::int64_t>(d.files.size()))
        .kv("blocks", static_cast<std::int64_t>(blocks.size()))
        .kv("edits", static_cast<std::int64_t>(edits))
        .kv("items", static_cast<std::int64_t>(d.items.size()))
        .kv("events", static_cast<std::int64_t>(d.events.size()));
    std::size_t moreKinds = 0, moreActors = 0;
    writeTop(w, "kinds", kinds, budget, moreKinds);
    writeTop(w, "actors", actors, budget, moreActors);
    if (moreKinds != 0 || moreActors != 0)
        w.key("more").beginObject().kv("kinds", static_cast<std::int64_t>(moreKinds)).kv("actors", static_cast<std::int64_t>(moreActors)).endObject();
    if (!d.overlaps.empty()) {
        w.key("overlaps").beginArray();
        for (std::size_t i = 0; i < d.overlaps.size() && i < budget.briefOverlaps; ++i) {
            const Overlap& o = d.overlaps[i];
            w.beginObject().kv("path", clip(o.path, budget.nameClip * 2)).kv("lines", lineSpan(o.from, o.to)).kv("by", clip(o.by, budget.nameClip)).kv("revision", o.revision).endObject();
        }
        w.endArray().kv("overlapCount", static_cast<std::int64_t>(d.overlaps.size()));
    }
}

std::string renderForm(const Delta& d, DeltaForm form, DeltaForm requested, const DeltaBudget& budget, const Compactor& compactor, const CompactPolicy& policy)
{
    json::Writer w;
    w.beginObject().kv("schema", "view-delta-1").kv("form", toString(form));
    if (form != requested) w.kv("requested", toString(requested)).kvBool("degraded", true);
    w.kvRaw("from", positionJson(d.from)).kvRaw("to", positionJson(d.to));
    w.key("brief").beginObject();
    writeBriefMembers(w, d, budget);
    w.endObject();
    if (form == DeltaForm::elided) {
        w.key("files").beginArray();
        for (const FileDelta& f : d.files)
            w.beginObject().kv("path", f.path).kv("from", f.fromRevision).kv("to", f.toRevision).kv("text", elidedText(f, budget.context)).endObject();
        w.endArray();
    } else if (form == DeltaForm::compacted) {
        const Compacted c = compactor.compact(d, policy);
        w.key("changes").beginArray();
        for (const CompactedChange& ch : c.changes) {
            w.beginObject().kv("path", ch.path);
            if (!ch.blockId.empty()) w.kv("block", ch.blockId).kv("name", ch.blockName);
            w.key("hunks").beginArray();
            for (const LineHunk& h : ch.hunks) writeHunk(w, h);
            w.endArray();
            if (!ch.itemKeys.empty()) {
                w.key("items").beginArray();
                for (const std::string& k : ch.itemKeys) w.str(k);
                w.endArray();
            }
            w.endObject();
        }
        w.endArray().key("items").beginArray();
        for (const ItemChange& it : c.items)
            w.beginObject().kv("key", it.key).kv("layer", it.layer).kv("kind", it.kind).kv("state", it.state).kv("title", it.title).endObject();
        w.endArray();
    } else if (form == DeltaForm::full) {
        w.key("files").beginArray();
        for (const FileDelta& f : d.files) {
            w.beginObject().kv("path", f.path).kv("from", f.fromRevision).kv("to", f.toRevision).key("steps").beginArray();
            for (const EditStep& s : f.steps) {
                w.beginObject().kv("revision", s.revision).kv("by", s.by).kv("lines", lineSpan(s.from, s.to)).kv("delta", s.delta).key("hunks").beginArray();
                for (const LineHunk& h : s.hunks) writeHunk(w, h);
                w.endArray().endObject();
            }
            w.endArray().endObject();
        }
        w.endArray().key("events").beginArray();
        for (const Event& e : d.events) w.raw(eventJson(e));
        w.endArray();
    }
    w.endObject();
    return w.take();
}

}  // namespace

std::string briefJson(const Delta& delta, const DeltaBudget& budget)
{
    json::Writer w;
    w.beginObject();
    writeBriefMembers(w, delta, budget);
    w.endObject();
    return w.take();
}

RenderedDelta renderDelta(const Delta& delta, DeltaForm form, const DeltaBudget& budget, const Compactor& compactor, const CompactPolicy& policy)
{
    // The ladder towards brief: each step is a whole form, never a diff cut short.
    static constexpr DeltaForm kLadder[] = {DeltaForm::full, DeltaForm::elided, DeltaForm::compacted, DeltaForm::brief};
    std::size_t at = 0;
    while (kLadder[at] != form) ++at;
    RenderedDelta out;
    out.requested = form;
    out.to = delta.to;
    out.events = delta.events.size();
    out.files = delta.files.size();
    for (; at < std::size(kLadder); ++at) {
        std::string text = renderForm(delta, kLadder[at], form, budget, compactor, policy);
        if (text.size() <= budget.maxChars || kLadder[at] == DeltaForm::brief) {
            out.form = kLadder[at];
            out.degraded = kLadder[at] != form;
            out.json = std::move(text);
            break;
        }
    }
    return out;
}

}  // namespace sub0canvas

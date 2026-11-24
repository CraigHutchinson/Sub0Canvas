// The query layer (docs/QUERY.md): parse a request, answer one bounded page, write the response through the shared projection (projection.hpp).
#include "sub0canvas/query.hpp"

#include "json_util.hpp"
#include "projection.hpp"
#include "sub0canvas/content_id.hpp"

#include <algorithm>
#include <charconv>
#include <optional>

namespace sub0canvas {

namespace {

using json::Writer;

std::int64_t num(std::size_t v) { return static_cast<std::int64_t>(v); }

/// A request or policy member that cannot be used: the error's code, the member, and what is wrong.
struct BadField
{
    std::string code, field, message;
};

constexpr std::string_view kOps = "info, tree, file, revisions, blocks, layers, layer, events";
constexpr std::size_t kMaxUnknownEchoed = 16, kMaxUnknownNameChars = 64, kMaxFilterValues = 64;

// ---- the policy --------------------------------------------------------------------------------------------------------------------------------------------

std::string_view unknownName(UnknownFields u)
{
    return u == UnknownFields::ignore ? "ignore" : u == UnknownFields::echo ? "echo" : "reject";
}

std::size_t sizeOf(simdjson::ondemand::value v, const std::string& field)
{
    std::int64_t n = 0;
    if (v.get_int64().get(n) != simdjson::SUCCESS || n < 1) throw BadField{"bad-policy", field, "must be a whole number of at least 1"};
    return static_cast<std::size_t>(n);
}

}  // namespace

std::expected<QueryPolicy, std::string> parseQueryPolicy(std::string_view text)
{
    QueryPolicy p;
    try {
        json::withObject(text, [&](simdjson::ondemand::object& obj) {
            for (auto field : obj) {
                const std::string key{json::keyOf(field)};
                simdjson::ondemand::value v = field.value();
                auto page = [&](QueryPolicy::Page& pg) {
                    simdjson::ondemand::object o;
                    if (v.get_object().get(o) != simdjson::SUCCESS) throw BadField{"bad-policy", key, "must be an object {default, max}"};
                    for (auto f : o) {
                        const std::string k{json::keyOf(f)};
                        if (k == "default") pg.defaultItems = sizeOf(f.value(), key + ".default");
                        else if (k == "max") pg.maxItems = sizeOf(f.value(), key + ".max");
                        else throw BadField{"bad-policy", key + "." + k, "unknown member"};
                    }
                    if (pg.defaultItems > pg.maxItems) throw BadField{"bad-policy", key, "default is above max"};
                };
                if (key == "tree") page(p.tree);
                else if (key == "blocks") page(p.blocks);
                else if (key == "layers") page(p.layers);
                else if (key == "layer") page(p.layer);
                else if (key == "events") page(p.events);
                else if (key == "revisions") page(p.revisions);
                else if (key == "maxResponseBytes") p.maxResponseBytes = sizeOf(v, key);
                else if (key == "maxTextBytes") p.maxTextBytes = sizeOf(v, key);
                else if (key == "maxRequestBytes") p.maxRequestBytes = sizeOf(v, key);
                else if (key == "maxHave") p.maxHave = sizeOf(v, key);
                else if (key == "maxScan") p.maxScan = sizeOf(v, key);
                else if (key == "unknownFields") {
                    std::string_view s;
                    if (v.get_string().get(s) != simdjson::SUCCESS) throw BadField{"bad-policy", key, "must be a string"};
                    if (s == "ignore") p.unknownFields = UnknownFields::ignore;
                    else if (s == "echo") p.unknownFields = UnknownFields::echo;
                    else if (s == "reject") p.unknownFields = UnknownFields::reject;
                    else throw BadField{"bad-policy", key, "must be ignore, echo or reject"};
                } else if (key == "hiddenTagPrefixes") {
                    simdjson::ondemand::array arr;
                    if (v.get_array().get(arr) != simdjson::SUCCESS) throw BadField{"bad-policy", key, "must be an array of strings"};
                    p.hiddenTagPrefixes.clear();
                    for (auto item : arr) {
                        std::string_view s;
                        if (item.get_string().get(s) != simdjson::SUCCESS) throw BadField{"bad-policy", key, "must be an array of strings"};
                        p.hiddenTagPrefixes.emplace_back(s);
                    }
                } else throw BadField{"bad-policy", key, "unknown member"};
            }
        });
    } catch (const BadField& b) {
        return std::unexpected(b.field + ": " + b.message);
    } catch (const simdjson::simdjson_error& e) {
        return std::unexpected(std::string{"not a JSON object: "} + simdjson::error_message(e.error()));
    }
    return p;
}

std::string queryPolicyJson(const QueryPolicy& p)
{
    Writer w;
    w.beginObject();
    auto page = [&](const char* name, const QueryPolicy::Page& pg) { w.key(name).beginObject().kv("default", num(pg.defaultItems)).kv("max", num(pg.maxItems)).endObject(); };
    page("tree", p.tree);
    page("blocks", p.blocks);
    page("layers", p.layers);
    page("layer", p.layer);
    page("events", p.events);
    page("revisions", p.revisions);
    w.kv("maxResponseBytes", num(p.maxResponseBytes)).kv("maxTextBytes", num(p.maxTextBytes)).kv("maxRequestBytes", num(p.maxRequestBytes));
    w.kv("maxHave", num(p.maxHave)).kv("maxScan", num(p.maxScan)).kv("unknownFields", unknownName(p.unknownFields));
    w.key("hiddenTagPrefixes").beginArray();
    for (const std::string& s : p.hiddenTagPrefixes) w.str(s);
    w.endArray().endObject();
    return w.take();
}

// ---- the document ------------------------------------------------------------------------------------------------------------------------------------------

namespace {

/// The segments of `path`; nullopt when the path is non-empty and has an empty segment. The empty path has no segments (the root).
std::optional<std::vector<std::string_view>> segments(std::string_view path)
{
    std::vector<std::string_view> out;
    if (path.empty()) return out;
    for (std::size_t start = 0;;) {
        const std::size_t slash = path.find('/', start);
        const std::string_view seg = path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        if (seg.empty()) return std::nullopt;
        out.push_back(seg);
        if (slash == std::string_view::npos) return out;
        start = slash + 1;
    }
}

}  // namespace

bool QueryDocument::addRevision(const std::string& path, int revision, std::string revisionId, const std::string& text)
{
    const auto segs = segments(path);
    if (!segs || segs->empty()) return false;
    Folder* folder = &root_;
    std::vector<Folder*> chain{folder};
    for (std::size_t i = 0; i + 1 < segs->size(); ++i) {
        auto it = folder->folders.find((*segs)[i]);
        if (it == folder->folders.end()) it = folder->folders.emplace(std::string{(*segs)[i]}, std::make_unique<Folder>()).first;
        folder = it->second.get();
        chain.push_back(folder);
    }
    auto file = folder->files.find(segs->back());
    const bool isNew = file == folder->files.end();
    if (!isNew && revision <= file->second.back().number) return false;
    FileRevision r;
    r.number = revision;
    r.id = revisionId.empty() ? "r" + std::to_string(revision) : std::move(revisionId);
    r.textId = contentId(text);
    r.bytes = text.size();
    r.lines = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + (!text.empty() && text.back() != '\n' ? 1 : 0);
    texts_.try_emplace(r.textId, text);
    if (isNew) {
        file = folder->files.emplace(std::string{segs->back()}, std::vector<FileRevision>{}).first;
        for (Folder* f : chain) ++f->filesBelow;
    }
    file->second.push_back(std::move(r));
    ++generation_;
    return true;
}

const Folder* QueryDocument::folder(std::string_view path, std::size_t& steps) const
{
    const auto segs = segments(path);
    if (!segs) return nullptr;
    const Folder* f = &root_;
    for (const std::string_view seg : *segs) {
        ++steps;
        const auto it = f->folders.find(seg);
        if (it == f->folders.end()) return nullptr;
        f = it->second.get();
    }
    return f;
}

const std::vector<FileRevision>* QueryDocument::revisions(std::string_view path, std::size_t& steps) const
{
    const auto segs = segments(path);
    if (!segs || segs->empty()) return nullptr;
    const Folder* f = &root_;
    for (std::size_t i = 0; i + 1 < segs->size(); ++i) {
        ++steps;
        const auto it = f->folders.find((*segs)[i]);
        if (it == f->folders.end()) return nullptr;
        f = it->second.get();
    }
    ++steps;
    const auto it = f->files.find(segs->back());
    return it == f->files.end() ? nullptr : &it->second;
}

const std::string* QueryDocument::text(std::string_view textId) const
{
    const auto it = texts_.find(std::string{textId});
    return it == texts_.end() ? nullptr : &it->second;
}

// ---- the request -------------------------------------------------------------------------------------------------------------------------------------------

namespace {

struct Filter
{
    std::vector<std::string> kinds, states, tags, sources;
    std::string text;
};

struct Request
{
    std::string op, idJson, path, cursor, textId, layer, source = "index";
    std::optional<std::int64_t> limit, revision, offset, from, maxBytes;
    std::vector<std::string> have, unknown;
    Filter filter;
    bool count = false;
    bool hasPath = false;
};

std::vector<std::string> stringList(simdjson::ondemand::value v, const std::string& field, std::size_t most)
{
    std::vector<std::string> out;
    simdjson::ondemand::array arr;
    if (v.get_array().get(arr) != simdjson::SUCCESS) throw BadField{"bad-request", field, "must be an array of strings"};
    for (auto item : arr) {
        std::string_view s;
        if (item.get_string().get(s) != simdjson::SUCCESS) throw BadField{"bad-request", field, "must be an array of strings"};
        if (out.size() >= most) throw BadField{"bad-request", field, "too many values (at most " + std::to_string(most) + ")"};
        out.emplace_back(s);
    }
    return out;
}

std::string stringValue(simdjson::ondemand::value v, const std::string& field)
{
    std::string_view s;
    if (v.get_string().get(s) != simdjson::SUCCESS) throw BadField{"bad-request", field, "must be a string"};
    return std::string{s};
}

std::int64_t whole(simdjson::ondemand::value v, const std::string& field)
{
    std::int64_t n = 0;
    if (v.get_int64().get(n) != simdjson::SUCCESS || n < 0) throw BadField{"bad-request", field, "must be a whole number, zero or more"};
    return n;
}

void parseFilter(simdjson::ondemand::value v, Filter& f, std::vector<std::string>& unknown)
{
    simdjson::ondemand::object o;
    if (v.get_object().get(o) != simdjson::SUCCESS) throw BadField{"bad-request", "query", "must be an object"};
    for (auto field : o) {
        const std::string key{json::keyOf(field)};
        simdjson::ondemand::value fv = field.value();
        if (key == "kinds") f.kinds = stringList(fv, "query.kinds", kMaxFilterValues);
        else if (key == "states") f.states = stringList(fv, "query.states", kMaxFilterValues);
        else if (key == "tags") f.tags = stringList(fv, "query.tags", kMaxFilterValues);
        else if (key == "sources") f.sources = stringList(fv, "query.sources", kMaxFilterValues);
        else if (key == "text") f.text = stringValue(fv, "query.text");
        else unknown.push_back("query." + key);
    }
}

/// Parses `text` into a request. The errors it throws are `BadField`s; a malformed document is a `simdjson_error`.
Request parseRequest(std::string_view text)
{
    Request r;
    json::withObject(text, [&](simdjson::ondemand::object& obj) {
        for (auto field : obj) {
            const std::string key{json::keyOf(field)};
            simdjson::ondemand::value v = field.value();
            if (key == "schema") {
                if (const std::string s = stringValue(v, key); s != kQuerySchema) throw BadField{"unsupported-schema", key, "this server speaks " + std::string{kQuerySchema} + ", not " + s};
            } else if (key == "op") r.op = stringValue(v, key);
            else if (key == "id") {
                Writer w;
                simdjson::ondemand::json_type t{};
                if (v.type().get(t) != simdjson::SUCCESS) throw BadField{"bad-request", key, "must be a string or a whole number"};
                std::string_view s;
                std::int64_t n = 0;
                if (t == simdjson::ondemand::json_type::string && v.get_string().get(s) == simdjson::SUCCESS) w.str(s);
                else if (t == simdjson::ondemand::json_type::number && v.get_int64().get(n) == simdjson::SUCCESS) w.num(n);
                else throw BadField{"bad-request", key, "must be a string or a whole number"};
                r.idJson = w.take();
            } else if (key == "path") { r.path = stringValue(v, key); r.hasPath = true; }
            else if (key == "cursor") r.cursor = stringValue(v, key);
            else if (key == "limit") r.limit = whole(v, key);
            else if (key == "revision") r.revision = whole(v, key);
            else if (key == "offset") r.offset = whole(v, key);
            else if (key == "from") r.from = whole(v, key);
            else if (key == "maxBytes") r.maxBytes = whole(v, key);
            else if (key == "textId") r.textId = stringValue(v, key);
            else if (key == "layer") r.layer = stringValue(v, key);
            else if (key == "source") r.source = stringValue(v, key);
            else if (key == "have") r.have = stringList(v, key, std::size_t{1} << 20);
            else if (key == "count") {
                bool b = false;
                if (v.get_bool().get(b) != simdjson::SUCCESS) throw BadField{"bad-request", key, "must be true or false"};
                r.count = b;
            } else if (key == "query") parseFilter(v, r.filter, r.unknown);
            else r.unknown.push_back(key);
        }
    });
    return r;
}

// ---- answering ---------------------------------------------------------------------------------------------------------------------------------------------

/// Everything a handler needs, and where it writes.
struct Ctx
{
    const QueryDocument& doc;
    const QueryPolicy& policy;
    const Request& req;
    QueryStats& stats;
    Writer w;
};

struct Failure
{
    std::string code, field, message;
};

std::string errorResponse(const std::string& op, const std::string& idJson, const Failure& f)
{
    Writer w;
    w.beginObject().kv("schema", kQuerySchema).kvBool("ok", false);
    if (!op.empty()) w.kv("op", op);
    if (!idJson.empty()) w.kvRaw("id", idJson);
    w.key("error").beginObject().kv("code", f.code);
    if (!f.field.empty()) w.kv("field", f.field);
    w.kv("message", f.message).endObject().endObject();
    return w.take();
}

/// The position the answer was computed at: the document's generation, the index's event count and the store's head and event count (those that are attached).
void writeAt(Ctx& c)
{
    c.w.key("at").beginObject().kv("generation", num(c.doc.generation()));
    if (c.doc.index() != nullptr) c.w.kv("indexEvents", num(c.doc.index()->events().size()));
    if (c.doc.store() != nullptr) c.w.kv("storeHead", static_cast<std::int64_t>(c.doc.store()->head())).kv("storeEvents", static_cast<std::int64_t>(c.doc.store()->eventCount()));
    c.w.endObject();
}

void begin(Ctx& c)
{
    c.w.beginObject().kv("schema", kQuerySchema).kvBool("ok", true).kv("op", c.req.op);
    if (!c.req.idJson.empty()) c.w.kvRaw("id", c.req.idJson);
    writeAt(c);
}

void finish(Ctx& c)
{
    if (c.policy.unknownFields == UnknownFields::echo && !c.req.unknown.empty()) {
        c.w.key("unknownFields").beginArray();
        for (std::size_t i = 0; i < c.req.unknown.size() && i < kMaxUnknownEchoed; ++i) c.w.str(c.req.unknown[i].substr(0, kMaxUnknownNameChars));
        c.w.endArray();
    }
    c.w.endObject();
}

/// The page size of a request: its `limit` clamped to the policy's maximum (the response says it was), the policy's default when none is named.
std::size_t pageSize(Ctx& c, const QueryPolicy::Page& page)
{
    std::size_t n = page.defaultItems;
    bool clamped = false;
    if (c.req.limit) {
        if (*c.req.limit < 1) throw Failure{"bad-request", "limit", "must be at least 1"};
        n = static_cast<std::size_t>(*c.req.limit);
        if (n > page.maxItems) { n = page.maxItems; clamped = true; }
    }
    c.w.kv("limit", num(n));
    if (clamped) c.w.kvBool("clamped", true);
    return n;
}

bool full(Ctx& c) { return c.w.view().size() >= c.policy.maxResponseBytes; }

void nextCursor(Ctx& c, const std::optional<std::string>& cursor)
{
    c.w.key("next");
    if (cursor) c.w.str(*cursor); else c.w.null();
}

std::size_t cursorNumber(const std::string& cursor)
{
    std::size_t n = 0;
    const auto [end, ec] = std::from_chars(cursor.data(), cursor.data() + cursor.size(), n);
    if (cursor.empty() || ec != std::errc{} || end != cursor.data() + cursor.size()) throw Failure{"bad-cursor", "cursor", "not a cursor this server issued"};
    return n;
}

const std::string& needPath(const Ctx& c)
{
    if (!c.req.hasPath) throw Failure{"bad-request", "path", "required"};
    return c.req.path;
}

// ---- info ---------------------------------------------------------------------------------------------------------------------------------------------------

void opInfo(Ctx& c)
{
    begin(c);
    c.w.key("ops").beginArray();
    for (const char* op : {"info", "tree", "file", "revisions", "blocks", "layers", "layer", "events"}) c.w.str(op);
    c.w.endArray().kvRaw("policy", queryPolicyJson(c.policy)).kv("files", num(c.doc.fileCount()));
    c.w.kvBool("index", c.doc.index() != nullptr).kvBool("store", c.doc.store() != nullptr);
    if (c.doc.store() != nullptr) c.w.kv("layerCount", num(c.doc.store()->state().layers.size()));
    c.stats.visited += 1;
    finish(c);
}

// ---- tree ---------------------------------------------------------------------------------------------------------------------------------------------------

void opTree(Ctx& c)
{
    std::size_t steps = 0;
    const Folder* folder = c.doc.folder(c.req.path, steps);
    c.stats.visited += steps;
    if (folder == nullptr) {
        std::size_t s2 = 0;
        if (c.doc.revisions(c.req.path, s2) != nullptr) throw Failure{"not-a-folder", "path", c.req.path + " is a file"};
        throw Failure{"not-found", "path", "no folder " + c.req.path};
    }
    begin(c);
    c.w.kv("path", c.req.path).kv("folders", num(folder->folders.size())).kv("files", num(folder->files.size())).kv("filesBelow", num(folder->filesBelow));
    const std::size_t limit = pageSize(c, c.policy.tree);

    // The children are the folders (by name) then the files (by name); a cursor is the last name sent, so a page costs its entries, not its offset.
    auto fit = folder->folders.begin();
    auto lit = folder->files.begin();
    if (!c.req.cursor.empty()) {
        const std::string_view cur = c.req.cursor;
        if (cur.size() < 2 || cur[1] != ':' || (cur[0] != 'd' && cur[0] != 'f')) throw Failure{"bad-cursor", "cursor", "not a cursor this server issued"};
        const std::string name{cur.substr(2)};
        if (cur[0] == 'd') fit = folder->folders.upper_bound(name);
        else { fit = folder->folders.end(); lit = folder->files.upper_bound(name); }
    }
    c.w.key("entries").beginArray();
    std::size_t sent = 0;
    std::optional<std::string> next;
    std::string last;
    while ((fit != folder->folders.end() || lit != folder->files.end())) {
        if (sent >= limit || (sent > 0 && full(c))) { next = last; break; }
        if (fit != folder->folders.end()) {
            const Folder& f = *fit->second;
            c.w.beginObject().kv("name", fit->first).kv("kind", "folder").kv("folders", num(f.folders.size())).kv("files", num(f.files.size())).kv("filesBelow", num(f.filesBelow)).endObject();
            last = "d:" + fit->first;
            ++fit;
        } else {
            const FileRevision& r = lit->second.back();
            c.w.beginObject().kv("name", lit->first).kv("kind", "file");
            projection::writeFileRef(c.w, r.number, r.id, r.textId);
            c.w.kv("bytes", num(r.bytes)).kv("lines", num(r.lines)).endObject();
            last = "f:" + lit->first;
            ++lit;
        }
        ++sent;
    }
    c.w.endArray();
    nextCursor(c, next);
    c.stats.visited += sent;
    c.stats.entries = sent;
    finish(c);
}

// ---- file and revisions ---------------------------------------------------------------------------------------------------------------------------------------

bool validUtf8(std::string_view s) { return simdjson::validate_utf8(s.data(), s.size()); }

void opFile(Ctx& c)
{
    const bool byPath = c.req.hasPath, byId = !c.req.textId.empty();
    if (byPath == byId) throw Failure{"bad-request", byPath ? "textId" : "path", "give a path (with an optional revision) or a textId, not both and not neither"};
    if (c.req.have.size() > c.policy.maxHave) throw Failure{"bad-request", "have", "too many content ids (at most " + std::to_string(c.policy.maxHave) + ")"};
    const FileRevision* rev = nullptr;
    std::string textId = c.req.textId;
    if (byPath) {
        std::size_t steps = 0;
        const auto* revs = c.doc.revisions(c.req.path, steps);
        c.stats.visited += steps;
        if (revs == nullptr) throw Failure{"not-found", "path", "no file " + c.req.path};
        if (c.req.revision) {
            const auto it = std::find_if(revs->begin(), revs->end(), [&](const FileRevision& r) { return r.number == *c.req.revision; });
            if (it == revs->end()) throw Failure{"not-found", "revision", "no revision " + std::to_string(*c.req.revision) + " of " + c.req.path};
            rev = &*it;
        } else rev = &revs->back();
        textId = rev->textId;
    }
    const std::string* text = c.doc.text(textId);
    if (text == nullptr) throw Failure{"not-found", "textId", "no text with content id " + textId};
    c.stats.visited += 1;
    begin(c);
    if (byPath) {
        c.w.kv("path", c.req.path);
        projection::writeFileRef(c.w, rev->number, rev->id, rev->textId);
        c.w.kv("lines", num(rev->lines));
    } else c.w.kv("textId", textId);
    c.w.kv("bytes", num(text->size()));
    // R-AUTH-14: a text the client holds is not sent again.
    if (std::find(c.req.have.begin(), c.req.have.end(), textId) != c.req.have.end()) {
        c.w.kvBool("have", true);
        finish(c);
        return;
    }
    const std::size_t offset = c.req.offset ? static_cast<std::size_t>(*c.req.offset) : 0;
    if (offset > text->size()) throw Failure{"bad-request", "offset", "past the end of the text (" + std::to_string(text->size()) + " bytes)"};
    if (offset < text->size() && (static_cast<unsigned char>((*text)[offset]) & 0xC0) == 0x80) throw Failure{"bad-request", "offset", "splits a character: use a `next` this server issued"};
    std::size_t most = c.policy.maxTextBytes;
    if (c.req.maxBytes) most = std::min(most, std::max<std::size_t>(1, static_cast<std::size_t>(*c.req.maxBytes)));
    std::size_t end = std::min(text->size(), offset + most);
    while (end < text->size() && end > offset && (static_cast<unsigned char>((*text)[end]) & 0xC0) == 0x80) --end;   // never split a character
    if (end == offset) {   // one character longer than the bound: send it whole, so the reader always advances
        end = std::min(text->size(), offset + 1);
        while (end < text->size() && (static_cast<unsigned char>((*text)[end]) & 0xC0) == 0x80) ++end;
    }
    const std::string_view chunk = std::string_view{*text}.substr(offset, end - offset);
    c.w.kv("offset", num(offset)).kv("length", num(chunk.size()));
    if (validUtf8(chunk)) c.w.kv("text", chunk);
    else c.w.kvBool("binary", true);
    c.w.key("next");
    if (end < text->size()) c.w.num(num(end)); else c.w.null();
    c.stats.entries = 1;
    finish(c);
}

void opRevisions(Ctx& c)
{
    std::size_t steps = 0;
    const auto* revs = c.doc.revisions(needPath(c), steps);
    c.stats.visited += steps;
    if (revs == nullptr) throw Failure{"not-found", "path", "no file " + c.req.path};
    begin(c);
    c.w.kv("path", c.req.path).kv("total", num(revs->size()));
    const std::size_t limit = pageSize(c, c.policy.revisions);
    auto it = revs->begin();
    if (!c.req.cursor.empty()) {
        const std::size_t after = cursorNumber(c.req.cursor);
        it = std::upper_bound(revs->begin(), revs->end(), after, [](std::size_t n, const FileRevision& r) { return n < static_cast<std::size_t>(r.number); });
    }
    c.w.key("entries").beginArray();
    std::size_t sent = 0;
    std::optional<std::string> next;
    for (; it != revs->end(); ++it) {
        if (sent >= limit || (sent > 0 && full(c))) { next = std::to_string(std::prev(it)->number); break; }
        c.w.beginObject();
        projection::writeFileRef(c.w, it->number, it->id, it->textId);
        c.w.kv("bytes", num(it->bytes)).kv("lines", num(it->lines)).endObject();
        ++sent;
    }
    c.w.endArray();
    nextCursor(c, next);
    c.stats.visited += sent;
    c.stats.entries = sent;
    finish(c);
}

// ---- blocks -------------------------------------------------------------------------------------------------------------------------------------------------

void opBlocks(Ctx& c)
{
    const std::string& path = needPath(c);
    if (c.doc.index() == nullptr) throw Failure{"not-available", "", "this document has no block index"};
    const std::vector<const Block*> blocks = c.doc.index()->blocks(path);
    c.stats.visited += blocks.size();
    if (blocks.empty()) {
        std::size_t steps = 0;
        if (c.doc.revisions(path, steps) == nullptr) throw Failure{"not-found", "path", "no file " + path};
    }
    begin(c);
    c.w.kv("path", path).kv("total", num(blocks.size()));
    const std::size_t limit = pageSize(c, c.policy.blocks);
    std::size_t at = c.req.cursor.empty() ? 0 : cursorNumber(c.req.cursor);
    if (at > blocks.size()) throw Failure{"bad-cursor", "cursor", "past the end of the blocks"};
    c.w.key("entries").beginArray();
    std::size_t sent = 0;
    std::optional<std::string> next;
    for (; at < blocks.size(); ++at) {
        if (sent >= limit || (sent > 0 && full(c))) { next = std::to_string(at); break; }
        projection::writeBlock(c.w, *blocks[at]);
        ++sent;
    }
    c.w.endArray();
    nextCursor(c, next);
    c.stats.entries = sent;
    finish(c);
}

// ---- layers and one layer -----------------------------------------------------------------------------------------------------------------------------------

void opLayers(Ctx& c)
{
    if (c.doc.store() == nullptr) throw Failure{"not-available", "", "this document has no store"};
    const auto& layers = c.doc.store()->state().layers;
    begin(c);
    c.w.kv("total", num(layers.size()));
    const std::size_t limit = pageSize(c, c.policy.layers);
    auto it = layers.begin();
    if (!c.req.cursor.empty()) it = layers.upper_bound(c.req.cursor);
    c.w.key("entries").beginArray();
    std::size_t sent = 0;
    std::optional<std::string> next;
    for (; it != layers.end(); ++it) {
        if (sent >= limit || (sent > 0 && full(c))) { next = std::prev(it)->first; break; }
        c.w.beginObject().kv("name", it->first).kv("items", num(it->second.size()));
        if (const auto m = c.doc.layerMeta().find(it->first); m != c.doc.layerMeta().end())
            c.w.kv("label", m->second.label).kv("description", m->second.description).kv("producer", m->second.producer);
        c.w.endObject();
        ++sent;
    }
    c.w.endArray();
    nextCursor(c, next);
    c.stats.visited += sent;
    c.stats.entries = sent;
    finish(c);
}

bool inList(const std::vector<std::string>& list, std::string_view v) { return list.empty() || std::find(list.begin(), list.end(), v) != list.end(); }

/// The store's own rule (`Store::items`, A11), restated over the request's lists so that a layer page costs its scan and not a pass over the whole layer; the test
/// `layer filters agree with Store::items` keeps the two the same.
bool matches(const StoredItem& it, const Filter& q, const std::vector<std::string>& hidden)
{
    if (!inList(q.kinds, it.item.kind) || !inList(q.states, stateName(it.state)) || !inList(q.sources, it.source)) return false;
    if (!q.tags.empty() && std::none_of(it.tags.begin(), it.tags.end(), [&](const std::string& t) { return inList(q.tags, t); })) return false;
    if (!q.text.empty() && it.item.title.find(q.text) == std::string::npos && it.item.detail.find(q.text) == std::string::npos) return false;
    for (const std::string& t : it.tags)
        for (const std::string& prefix : hidden)
            if (t.starts_with(prefix) && std::find(q.tags.begin(), q.tags.end(), t) == q.tags.end()) return false;
    return true;
}

void opLayer(Ctx& c)
{
    if (c.doc.store() == nullptr) throw Failure{"not-available", "", "this document has no store"};
    if (c.req.layer.empty()) throw Failure{"bad-request", "layer", "required"};
    const auto& layers = c.doc.store()->state().layers;
    const auto found = layers.find(c.req.layer);
    if (found == layers.end()) throw Failure{"not-found", "layer", "no layer " + c.req.layer};
    const std::vector<StoredItem*>& list = found->second;
    begin(c);
    c.w.kv("layer", c.req.layer).kv("total", num(list.size()));
    if (c.req.count) {   // an explicit full pass, itself bounded by the scan limit
        if (list.size() > c.policy.maxScan) c.w.key("matched").null();
        else {
            std::size_t n = 0;
            for (const StoredItem* it : list) n += matches(*it, c.req.filter, c.policy.hiddenTagPrefixes) ? 1 : 0;
            c.w.kv("matched", num(n));
            c.stats.visited += list.size();
        }
    }
    const std::size_t limit = pageSize(c, c.policy.layer);
    // The cursor is the layer number of the last item examined: numbers are places in the layer, so a page costs its scan, and new items (appended) and items
    // that change state between pages cannot make a page repeat or skip one.
    std::size_t at = c.req.cursor.empty() ? 0 : cursorNumber(c.req.cursor);
    if (at > list.size()) throw Failure{"bad-cursor", "cursor", "past the end of the layer"};
    c.w.key("entries").beginArray();
    std::size_t sent = 0, scanned = 0;
    std::optional<std::string> next;
    for (; at < list.size(); ++at) {
        if (sent >= limit || scanned >= c.policy.maxScan || (sent > 0 && full(c))) { next = std::to_string(at); break; }
        ++scanned;
        const StoredItem& it = *list[at];
        if (!matches(it, c.req.filter, c.policy.hiddenTagPrefixes)) continue;
        c.w.beginObject();
        projection::writeLayerItemMembers(c.w, it.item, static_cast<int>(at + 1));
        c.w.kv("source", it.source).kv("state", stateName(it.state)).key("tags").beginArray();
        for (const std::string& t : it.tags) c.w.str(t);
        c.w.endArray().endObject();
        ++sent;
    }
    c.w.endArray();
    c.w.kv("scanned", num(scanned));
    nextCursor(c, next);
    c.stats.visited += scanned;
    c.stats.entries = sent;
    finish(c);
}

// ---- events -------------------------------------------------------------------------------------------------------------------------------------------------

void opEvents(Ctx& c)
{
    const std::uint64_t from = c.req.from ? static_cast<std::uint64_t>(*c.req.from) : 0;
    if (c.req.source != "index" && c.req.source != "store") throw Failure{"bad-request", "source", "must be index or store"};
    const bool fromStore = c.req.source == "store";
    const std::uint64_t end = fromStore ? (c.doc.store() ? c.doc.store()->eventCount() : 0) : (c.doc.index() ? c.doc.index()->events().size() : 0);
    if (fromStore ? c.doc.store() == nullptr : c.doc.index() == nullptr) throw Failure{"not-available", "source", "this document has no " + c.req.source};
    std::uint64_t base = 0;   // the position of the first event held (a restored store holds only those since its checkpoint)
    if (fromStore) base = end - c.doc.store()->events().size();
    if (from > end) throw Failure{"bad-request", "from", "past the end of the log (" + std::to_string(end) + " events)"};
    if (from < base) throw Failure{"gone", "from", "events before position " + std::to_string(base) + " are not held; read a snapshot and continue from its position"};
    begin(c);
    c.w.kv("source", c.req.source).kv("from", static_cast<std::int64_t>(from)).kv("end", static_cast<std::int64_t>(end));
    const std::size_t limit = pageSize(c, c.policy.events);
    c.w.key("entries").beginArray();
    std::size_t sent = 0;
    std::uint64_t pos = from;
    for (; pos < end; ++pos) {
        if (sent >= limit || (sent > 0 && full(c))) break;
        if (fromStore) c.w.raw(eventJson(c.doc.store()->events()[static_cast<std::size_t>(pos - base)]));
        else projection::writeBlockEvent(c.w, c.doc.index()->events()[static_cast<std::size_t>(pos)]);
        ++sent;
    }
    c.w.endArray().kv("next", static_cast<std::int64_t>(pos));   // always a position: a caught-up reader keeps it and asks again
    c.stats.visited += sent;
    c.stats.entries = sent;
    finish(c);
}

}  // namespace

std::string QueryServer::handle(std::string_view requestText, QueryStats* stats) const
{
    QueryStats local;
    QueryStats& st = stats != nullptr ? *stats : local;
    st = {};
    auto out = [&](std::string response) {
        st.responseBytes = response.size();
        return response;
    };
    if (requestText.size() > policy_.maxRequestBytes)
        return out(errorResponse({}, {}, {"too-large", "", "the request is " + std::to_string(requestText.size()) + " bytes; at most " + std::to_string(policy_.maxRequestBytes)}));
    Request req;
    try {
        req = parseRequest(requestText);
    } catch (const BadField& b) {
        return out(errorResponse({}, {}, {b.code, b.field, b.message}));
    } catch (const simdjson::simdjson_error& e) {
        return out(errorResponse({}, {}, {"bad-request", "", std::string{"not a JSON object: "} + simdjson::error_message(e.error())}));
    }
    if (req.op.empty()) return out(errorResponse({}, req.idJson, {"bad-request", "op", "required; one of " + std::string{kOps}}));
    if (policy_.unknownFields == UnknownFields::reject && !req.unknown.empty())
        return out(errorResponse(req.op, req.idJson, {"unknown-field", req.unknown.front(), "this server does not know the member (policy: reject)"}));
    Ctx c{document_, policy_, req, st, {}};
    try {
        if (req.op == "info") opInfo(c);
        else if (req.op == "tree") opTree(c);
        else if (req.op == "file") opFile(c);
        else if (req.op == "revisions") opRevisions(c);
        else if (req.op == "blocks") opBlocks(c);
        else if (req.op == "layers") opLayers(c);
        else if (req.op == "layer") opLayer(c);
        else if (req.op == "events") opEvents(c);
        else return out(errorResponse(req.op, req.idJson, {"unknown-op", "op", "one of " + std::string{kOps}}));
    } catch (const Failure& f) {
        st = {};
        return out(errorResponse(req.op, req.idJson, f));
    }
    return out(c.w.take());
}

}  // namespace sub0canvas

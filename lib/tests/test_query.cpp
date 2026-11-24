// The query layer (slice V1a-q, docs/QUERY.md): requirements R-AUTH-1 (scoped, bounded answers), R-AUTH-14 (texts by content id, once), R-AUTH-16 (a versioned,
// structured protocol) and R-AUTH-17 (the bundle is every query answered at once: one projection definition, review D11).
// Everything is asked and read as JSON text (the in-process contract test, R-AUTH-14's transport-independent half): the only C++ objects are the document that is
// being served and the server. Expected values are written by hand from the requirements, never read back from the code under test, except where a test says it
// compares two answers of the system to each other (paging against the unpaged list, the store's own filter against ours).
#include "sub0platform/testing/check.hpp"
#include "sub0canvas/content_id.hpp"
#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/query.hpp"
#include "sub0canvas/store.hpp"

#include <simdjson.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// Our own CHECK_EQ: it compares copies, because the DOM accessors return references into temporaries that the helper's reference binding would warn about.
#undef CHECK_EQ
#define CHECK_EQ(a, b) do { const auto _a = (a); const auto _b = (b); if (!(_a == _b)) { std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s != %s\n", __FILE__, __LINE__, #a, #b); ++gFailures; } } while (0)

using namespace sub0canvas;
namespace fs = std::filesystem;
using Strings = std::vector<std::string>;

namespace {

// By-value unwrapping of simdjson results: a range-for over `x.get_array().value()` binds a reference into a temporary, which gcc 14 reports
// (-Wdangling-reference); returning the view by value is the same thing without the warning.
simdjson::dom::object objectOf(simdjson::dom::element e) { return e.get_object().value(); }
simdjson::dom::array arrayOf(simdjson::dom::element e) { return e.get_array().value(); }

/// One parsed response. Not movable: the DOM points into the padded text this object holds.
struct Resp
{
    simdjson::padded_string text;
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    explicit Resp(const std::string& t) : text(t) { root = parser.parse(text); }
    Resp(const Resp&) = delete;
    Resp& operator=(const Resp&) = delete;
    [[nodiscard]] bool ok() const { return root["ok"].get_bool().value(); }
    [[nodiscard]] std::string str(const char* k) const { return std::string{std::string_view{root[k]}}; }
    [[nodiscard]] std::int64_t num(const char* k) const { return root[k].get_int64().value(); }
    [[nodiscard]] bool has(const char* k) const { return !root[k].error(); }
    [[nodiscard]] bool isNull(const char* k) const { return root[k].is_null(); }
    [[nodiscard]] std::string errorCode() const { return std::string{std::string_view{root["error"]["code"]}}; }
    [[nodiscard]] simdjson::dom::array entries() const { return arrayOf(root["entries"]); }
    [[nodiscard]] Strings names(const char* member = "name") const
    {
        Strings out;
        for (simdjson::dom::element e : entries()) out.emplace_back(std::string_view{e[member]});
        return out;
    }
};

std::unique_ptr<Resp> ask(const QueryServer& s, const std::string& request, QueryStats* stats = nullptr)
{
    return std::make_unique<Resp>(s.handle(request, stats));
}

/// Every entry of a paged answer, following `next` from `request` (a JSON object without its closing brace and `cursor`): the names of `member`.
Strings allPages(const QueryServer& s, const std::string& head, const char* member, std::size_t* pages = nullptr)
{
    Strings out;
    std::string cursor;
    std::size_t n = 0;
    for (;;) {
        auto r = ask(s, head + (cursor.empty() ? "" : ",\"cursor\":\"" + cursor + "\"") + "}");
        if (!r->ok()) { std::fprintf(stderr, "allPages: %s\n", std::string{std::string_view{r->text.data(), r->text.size()}}.c_str()); CHECK(false); break; }
        ++n;
        const Strings page = r->names(member);
        out.insert(out.end(), page.begin(), page.end());
        if (r->isNull("next")) break;
        cursor = r->str("next");
        if (n > 100000) { CHECK(false); break; }
    }
    if (pages != nullptr) *pages = n;
    return out;
}

std::string pad(int n, int width = 5)
{
    std::string s = std::to_string(n);
    return std::string(static_cast<std::size_t>(width) - s.size(), '0') + s;
}

std::string cppFile(int functions, const std::string& tag = "")
{
    std::string s;
    for (int i = 0; i < functions; ++i) s += "int fn" + tag + std::to_string(i) + "(int a)\n{\n    return a * " + std::to_string(i + 2) + " + " + std::to_string(i) + ";\n}\n\n";
    return s;
}

/// A served document and everything it borrows, in one place.
struct World
{
    BlockIndex index;
    Store store;
    QueryDocument doc{&index, &store};
    World() : store([] { StoreOptions o; o.writer = {"proj", "w-1"}; return o; }()) {}
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    void file(const std::string& path, int rev, const std::string& text)
    {
        CHECK(doc.addRevision(path, rev, {}, text));
        index.update(path, text);
    }
};

LayerItem item(const std::string& key, const std::string& kind, const std::string& title, const std::string& blockId = "b1")
{
    LayerItem it;
    it.key = key;
    it.kind = kind;
    it.title = title;
    it.detail = "detail of " + key + " \"quoted\"\n\tand a tab";
    Anchor a;
    a.blockId = blockId;
    a.path = "src/a.cpp";
    a.line = 3;
    a.confidence = Anchor::Confidence::exact;
    it.anchors = {a};
    return it;
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        {
            // R-AUTH-1: a folder listing is scoped. src/a holds 10 files and src/b thousands; listing src/a visits 10 entries and the 2 steps of its path, however
            // much lies beside it, and the answer is the same bytes whatever lies beside it.
            auto build = [](int inB) {
                auto w = std::make_unique<World>();
                for (int i = 0; i < 10; ++i) w->file("src/a/f" + pad(i) + ".cpp", 1, "int x" + std::to_string(i) + ";\n");
                for (int i = 0; i < inB; ++i) w->file("src/b/g" + pad(i) + ".cpp", 1, "int y" + std::to_string(i) + ";\n");
                w->file("docs/x.md", 1, "# x\n");
                w->file("README.md", 1, "read me\n");
                return w;
            };
            auto small = build(50);
            auto large = build(5000);
            const std::string req = R"({"op":"tree","path":"src/a"})";
            QueryStats s1, s2;
            const std::string a1 = QueryServer(small->doc).handle(req, &s1);
            const std::string a2 = QueryServer(large->doc).handle(req, &s2);
            CHECK_EQ(s1.visited, std::size_t{12});    // 2 path steps + 10 entries: nothing of src/b, docs or the root
            CHECK_EQ(s2.visited, std::size_t{12});
            CHECK_EQ(s1.entries, std::size_t{10});
            CHECK_EQ(s1.responseBytes, a1.size());
            // The only difference between the two answers is the position they were computed at (the large document has more revisions).
            Resp r1(a1), r2(a2);
            CHECK(r1.ok() && r1.num("filesBelow") == 10 && r2.num("filesBelow") == 10);
            CHECK_EQ(r1.names(), r2.names());
            CHECK_EQ(simdjson::minify(r1.root["entries"]), simdjson::minify(r2.root["entries"]));
            CHECK_EQ(r1.num("folders"), 0);
            CHECK_EQ(r1.num("files"), 10);
            const Strings firstNames = r1.names();   // named: CHECK_EQ binds references, which must not point into a temporary
            CHECK_EQ(firstNames.front(), std::string{"f00000.cpp"});

            // The root: folders first (by name), then files; each folder entry carries its counts, no text and no child.
            QueryStats root;
            auto rr = ask(QueryServer(large->doc), R"({"op":"tree","id":7})", &root);
            CHECK(rr->ok());
            CHECK_EQ(rr->names(), (Strings{"docs", "src", "README.md"}));
            CHECK_EQ(root.visited, std::size_t{3});   // the root costs its three children, not the 5,012 files below
            CHECK_EQ(rr->num("filesBelow"), 5012);
            CHECK_EQ(rr->num("id"), 7);
            const auto entries = rr->entries();
            auto it = entries.begin();
            CHECK_EQ(std::string{std::string_view{(*it)["kind"]}}, std::string{"folder"});
            ++it;   // src: two subfolders, no direct file, 5,010 below
            CHECK_EQ((*it)["folders"].get_int64().value(), 2);
            CHECK_EQ((*it)["files"].get_int64().value(), 0);
            CHECK_EQ((*it)["filesBelow"].get_int64().value(), 5010);
            ++it;   // README.md: "read me\n" is 8 bytes and one line, at revision 1
            CHECK_EQ(std::string{std::string_view{(*it)["kind"]}}, std::string{"file"});
            CHECK_EQ((*it)["bytes"].get_int64().value(), 8);
            CHECK_EQ((*it)["lines"].get_int64().value(), 1);
            CHECK_EQ((*it)["revisionId"].get_string().value(), std::string_view{"r1"});
            const std::string readMeId = contentId("read me\n");
            CHECK_EQ((*it)["textId"].get_string().value(), std::string_view{readMeId});

            // Paging: the pages of src/b, followed by their cursors, are exactly the sorted list; the page size is the request's, clamped to the policy's.
            Strings want;
            for (int i = 0; i < 5000; ++i) want.push_back("g" + pad(i) + ".cpp");
            std::size_t pages = 0;
            CHECK_EQ(allPages(QueryServer(large->doc), R"({"op":"tree","path":"src/b","limit":700)", "name", &pages), want);
            CHECK_EQ(pages, std::size_t{8});
            // The default page of a tree is 500: 5,000 files are ten pages; a request for more than the maximum (2,000) is clamped and says so.
            pages = 0;
            CHECK_EQ(allPages(QueryServer(large->doc), R"({"op":"tree","path":"src/b")", "name", &pages), want);
            CHECK_EQ(pages, std::size_t{10});
            auto big = ask(QueryServer(large->doc), R"({"op":"tree","path":"src/b","limit":100000})");
            CHECK_EQ(big->num("limit"), 2000);
            CHECK(big->root["clamped"].get_bool().value());
            CHECK_EQ(big->names().size(), std::size_t{2000});
            CHECK(!big->isNull("next"));
            // Folders then files across a page boundary: a cursor inside the folders and one inside the files.
            auto mixed = std::make_unique<World>();
            for (const char* d : {"d1", "d2", "d3"}) mixed->file(std::string{d} + "/f.txt", 1, "x\n");
            for (const char* f : {"a.txt", "b.txt", "c.txt"}) mixed->file(f, 1, "x\n");
            CHECK_EQ(allPages(QueryServer(mixed->doc), R"({"op":"tree","limit":2)", "name"), (Strings{"d1", "d2", "d3", "a.txt", "b.txt", "c.txt"}));
            // The response's own byte bound ends a page early: a policy of 600 bytes cannot hold 10 entries of about 150 bytes.
            QueryPolicy tight;
            tight.maxResponseBytes = 600;
            Strings bounded = allPages(QueryServer(small->doc, tight), R"({"op":"tree","path":"src/a")", "name", &pages);
            CHECK_EQ(bounded.size(), std::size_t{10});
            CHECK_GE(pages, std::size_t{3});
            auto firstPage = ask(QueryServer(small->doc, tight), R"({"op":"tree","path":"src/a"})");
            CHECK(firstPage->names().size() < 10 && !firstPage->isNull("next"));
            CHECK(firstPage->text.size() < 600 + 400);   // the cap plus at most one entry and the tail
        }
        {
            // The position every answer carries: the document's generation, the index's events and the store's head.
            World w;
            w.file("a.cpp", 1, cppFile(2));
            (void)w.store.putLayer("issues", "squad/gate", std::vector<LayerItem>{item("k1", "error", "t1")});
            auto r = ask(QueryServer(w.doc), R"({"op":"tree"})");
            CHECK_EQ(r->root["at"]["generation"].get_int64().value(), 1);
            CHECK_EQ(r->root["at"]["indexEvents"].get_int64().value(), 2);
            CHECK_EQ(r->root["at"]["storeHead"].get_int64().value(), 1);
            CHECK_EQ(r->root["at"]["storeEvents"].get_int64().value(), 1);
            CHECK_EQ(r->str("schema"), std::string{"query-1"});
            CHECK_EQ(r->str("op"), std::string{"tree"});
            w.file("b.cpp", 1, cppFile(1));
            auto r2 = ask(QueryServer(w.doc), R"({"op":"tree"})");
            CHECK_EQ(r2->root["at"]["generation"].get_int64().value(), 2);
            CHECK_EQ(r2->root["at"]["indexEvents"].get_int64().value(), 3);
            // A document with no index and no store says nothing about them.
            QueryDocument bare;
            CHECK(bare.addRevision("x.txt", 1, "", "x"));
            auto rb = ask(QueryServer(bare), R"({"op":"info"})");
            CHECK(rb->root["at"]["indexEvents"].error() != simdjson::SUCCESS && rb->root["at"]["storeHead"].error() != simdjson::SUCCESS);
            CHECK(rb->root["index"].get_bool().value() == false && rb->root["store"].get_bool().value() == false);
            CHECK_EQ(ask(QueryServer(bare), R"({"op":"blocks","path":"x.txt"})")->errorCode(), std::string{"not-available"});
            CHECK_EQ(ask(QueryServer(bare), R"({"op":"layer","layer":"issues"})")->errorCode(), std::string{"not-available"});
            CHECK_EQ(ask(QueryServer(bare), R"({"op":"events"})")->errorCode(), std::string{"not-available"});
        }
        {
            // R-AUTH-14: a text travels by content id, once. The id is git's blob id ("hello\n" is ce013625..., what `git hash-object` prints).
            World w;
            w.file("hello.txt", 1, "hello\n");
            w.file("copy.txt", 1, "hello\n");
            const std::string kHello = "ce013625030ba8dba906f756967f9e9ca394464a";
            const QueryServer server(w.doc);
            auto byPath = ask(server, R"({"op":"file","path":"hello.txt"})");
            CHECK(byPath->ok());
            CHECK_EQ(byPath->str("textId"), kHello);
            CHECK_EQ(byPath->str("text"), std::string{"hello\n"});
            CHECK_EQ(byPath->num("revision"), 1);
            CHECK_EQ(byPath->str("revisionId"), std::string{"r1"});
            CHECK(byPath->isNull("next"));
            // The same text by id; two paths with identical content share one id, so the second costs the client nothing.
            auto byId = ask(server, R"({"op":"file","textId":")" + kHello + R"("})");
            CHECK(byId->ok() && byId->str("text") == "hello\n");
            CHECK_EQ(ask(server, R"({"op":"file","path":"copy.txt"})")->str("textId"), kHello);
            // `have`: an id the client holds is not sent again.
            auto have = ask(server, R"({"op":"file","path":"copy.txt","have":["0000","ce013625030ba8dba906f756967f9e9ca394464a"]})");
            CHECK(have->ok() && have->root["have"].get_bool().value() && !have->has("text") && have->num("bytes") == 6);
            // Revisions: every revision stays available by number, with the text of that revision.
            w.file("hello.txt", 2, "hello world\n");
            CHECK_EQ(ask(server, R"({"op":"file","path":"hello.txt","revision":1})")->str("text"), std::string{"hello\n"});
            CHECK_EQ(ask(server, R"({"op":"file","path":"hello.txt"})")->str("text"), std::string{"hello world\n"});
            CHECK_EQ(ask(server, R"({"op":"file","path":"hello.txt","revision":9})")->errorCode(), std::string{"not-found"});
            auto revs = ask(server, R"({"op":"revisions","path":"hello.txt"})");
            CHECK_EQ(revs->num("total"), 2);
            CHECK_EQ(revs->names("revisionId"), (Strings{"r1", "r2"}));
            const Strings textIds = revs->names("textId");
            CHECK_EQ(textIds[0], kHello);
            CHECK_EQ(allPages(server, R"({"op":"revisions","path":"hello.txt","limit":1)", "revisionId"), (Strings{"r1", "r2"}));
            // A revision that does not rise, and a malformed path, are refused by the document.
            CHECK(!w.doc.addRevision("hello.txt", 2, {}, "again"));
            CHECK(!w.doc.addRevision("a//b", 1, {}, "x") && !w.doc.addRevision("/a", 1, {}, "x") && !w.doc.addRevision("a/", 1, {}, "x") && !w.doc.addRevision("", 1, {}, "x"));
            // A long text is read in bounded chunks that never split a character: "é" is 2 bytes, "日" 3, and the chunk bound is 7 bytes.
            std::string wide;
            for (int i = 0; i < 40; ++i) wide += std::string{"a\xC3\xA9\xE6\x97\xA5"};   // 6 bytes of 3 characters, 40 times: 240 bytes
            w.file("wide.txt", 1, wide);
            QueryPolicy small;
            small.maxTextBytes = 7;
            const QueryServer chunky(w.doc, small);
            std::string joined;
            std::int64_t offset = 0;
            std::size_t chunks = 0;
            for (;;) {
                auto c = ask(chunky, R"({"op":"file","path":"wide.txt","offset":)" + std::to_string(offset) + "}");
                CHECK(c->ok() && !c->has("binary"));
                if (!c->ok()) break;
                const std::string t = c->str("text");
                CHECK(t.size() <= 7 && !t.empty());
                CHECK_EQ(c->num("length"), static_cast<std::int64_t>(t.size()));
                joined += t;
                ++chunks;
                if (c->isNull("next")) break;
                offset = c->num("next");
            }
            CHECK_EQ(joined, wide);
            CHECK_GE(chunks, std::size_t{35});
            CHECK_EQ(ask(chunky, R"({"op":"file","path":"wide.txt","offset":2})")->errorCode(), std::string{"bad-request"});   // inside a character
            CHECK_EQ(ask(chunky, R"({"op":"file","path":"wide.txt","offset":9999})")->errorCode(), std::string{"bad-request"});
            // A request's own maxBytes below the policy's narrows the chunk; a character longer than the bound goes whole so the reader advances.
            QueryPolicy one;
            one.maxTextBytes = 1;
            CHECK_EQ(ask(QueryServer(w.doc, one), R"({"op":"file","path":"wide.txt","offset":1})")->str("text"), std::string{"\xC3\xA9"});
            // Binary content is flagged, never emitted as invalid JSON.
            w.file("blob.bin", 1, std::string{"\xFF\xFE\x00\x01", 4});
            auto bin = ask(server, R"({"op":"file","path":"blob.bin"})");
            CHECK(bin->ok() && bin->root["binary"].get_bool().value() && !bin->has("text"));
            // Not both a path and an id, and not neither.
            CHECK_EQ(ask(server, R"({"op":"file"})")->errorCode(), std::string{"bad-request"});
            CHECK_EQ(ask(server, R"({"op":"file","path":"hello.txt","textId":"x"})")->errorCode(), std::string{"bad-request"});
            CHECK_EQ(ask(server, R"({"op":"file","textId":"deadbeef"})")->errorCode(), std::string{"not-found"});
        }
        {
            // The blocks of ONE file, paged. 12 functions in a.cpp and 3 in b.cpp: a request for b.cpp examines 3 blocks and returns them whatever else is indexed.
            World w;
            w.file("src/a.cpp", 1, cppFile(12, "a"));
            w.file("src/b.cpp", 1, cppFile(3, "b"));
            w.file("src/empty.cpp", 1, "// nothing here\n");
            CHECK_EQ(w.index.blocks("src/a.cpp").size(), std::size_t{12});
            const QueryServer server(w.doc);
            QueryStats st;
            auto b = ask(server, R"({"op":"blocks","path":"src/b.cpp"})", &st);
            CHECK(b->ok());
            CHECK_EQ(b->num("total"), 3);
            CHECK_EQ(b->names(), (Strings{"fnb0", "fnb1", "fnb2"}));
            CHECK_EQ(st.visited, std::size_t{3});
            // The entry is the bundle's block: id, path, name, signature, first, last, and nothing else (no token sets).
            const auto first = b->entries().at(0).value();
            CHECK_EQ(simdjson::minify(first), "{\"id\":\"" + w.index.blocks("src/b.cpp").front()->id + R"j(","path":"src/b.cpp","name":"fnb0","signature":"int fnb0(int a)","first":1,"last":4})j");   // a birth id (A16): read from the index, never spelled
            // Paging: pages of 5 concatenate to the file's blocks in order.
            Strings want;
            for (int i = 0; i < 12; ++i) want.push_back("fna" + std::to_string(i));
            std::size_t pages = 0;
            CHECK_EQ(allPages(server, R"({"op":"blocks","path":"src/a.cpp","limit":5)", "name", &pages), want);
            CHECK_EQ(pages, std::size_t{3});
            CHECK(ask(server, R"({"op":"blocks","path":"src/empty.cpp"})")->entries().size() == 0);
            CHECK_EQ(ask(server, R"({"op":"blocks","path":"src/none.cpp"})")->errorCode(), std::string{"not-found"});
            CHECK_EQ(ask(server, R"({"op":"blocks"})")->errorCode(), std::string{"bad-request"});
            CHECK_EQ(ask(server, R"({"op":"blocks","path":"src/a.cpp","cursor":"99"})")->errorCode(), std::string{"bad-cursor"});
        }
        {
            // R-AUTH-1 for a layer: scoped by a query, windowed by a cursor, and the work done is the items examined.
            World w;
            std::vector<LayerItem> items;
            for (int i = 0; i < 40; ++i) items.push_back(item("issues/" + pad(i, 3), i % 4 == 0 ? "test" : "error", "item " + std::to_string(i) + (i % 5 == 0 ? " flaky" : "")));
            (void)w.store.putLayer("issues", "squad/gate", items);
            (void)w.store.putLayer("review", "person/craig", std::vector<LayerItem>{item("review/1", "note", "a note"), item("review/2", "note", "another")});
            for (int i = 0; i < 40; i += 3) (void)w.store.tag("issues/" + pad(i, 3), "area/parser", "me");
            (void)w.store.tag("issues/035", "private/me", "me");
            (void)w.store.setState("issues/002", ItemState::done, "me", "fixed");
            const QueryServer server(w.doc);

            // Summary: the layers and their counts.
            auto ls = ask(server, R"({"op":"layers"})");
            CHECK_EQ(ls->names(), (Strings{"issues", "review"}));
            CHECK_EQ(ls->entries().at(0)["items"].get_int64().value(), 40);
            // A page of the layer, in layer order, numbered by place in the layer; two pages for one request's scope never touch the other layer.
            QueryStats st;
            auto p1 = ask(server, R"({"op":"layer","layer":"issues","limit":10})", &st);
            CHECK(p1->ok());
            const Strings pageIds = p1->names("id");
            CHECK_EQ(pageIds.front(), std::string{"issues/000"});
            CHECK_EQ(p1->entries().at(9)["number"].get_int64().value(), 10);
            CHECK_EQ(st.visited, std::size_t{10});   // ten items examined of forty, none of the review layer
            CHECK_EQ(p1->num("total"), 40);
            CHECK_EQ(p1->str("next"), std::string{"10"});
            // The item is the bundle's item (id, number, kind, title, detail, anchors) plus the store's source, state and tags.
            const auto it0 = p1->entries().at(0).value();
            CHECK_EQ(std::string{std::string_view{it0["kind"]}}, std::string{"test"});
            CHECK_EQ(std::string{std::string_view{it0["detail"]}}, std::string{"detail of issues/000 \"quoted\"\n\tand a tab"});
            CHECK_EQ(simdjson::minify(it0["anchors"]), std::string{R"([{"blockId":"b1","path":"src/a.cpp","line":3,"confidence":"exact"}])"});
            CHECK_EQ(std::string{std::string_view{it0["state"]}}, std::string{"open"});
            CHECK_EQ(simdjson::minify(it0["tags"]), std::string{R"(["area/parser"])"});
            // Paging concatenates to the unpaged list (item 35 is private, so it is in neither).
            Strings all;
            for (int i = 0; i < 40; ++i) if (i != 35) all.push_back("issues/" + pad(i, 3));
            CHECK_EQ(allPages(server, R"({"op":"layer","layer":"issues","limit":7)", "id"), all);
            CHECK_EQ(ask(server, R"({"op":"layer","layer":"issues","limit":500})")->names("id"), all);
            // A query scopes it: kinds, states, tags, text; across sets all must match.
            CHECK_EQ(allPages(server, R"({"op":"layer","layer":"issues","query":{"kinds":["test"]},"limit":4)", "id").size(), std::size_t{10});
            CHECK_EQ(allPages(server, R"({"op":"layer","layer":"issues","query":{"states":["done"]})", "id"), (Strings{"issues/002"}));
            CHECK_EQ(allPages(server, R"({"op":"layer","layer":"issues","query":{"kinds":["test"],"tags":["area/parser"]})", "id"),
                     (Strings{"issues/000", "issues/012", "issues/024", "issues/036"}));
            CHECK_EQ(allPages(server, R"({"op":"layer","layer":"issues","query":{"text":"flaky"},"limit":3)", "id").size(), std::size_t{7});   // items 0, 5, ..., 35 say flaky; 35 is private
            // A private item is never in an answer unless the request names its tag exactly (R-BB-10).
            const Strings flaky = allPages(server, R"({"op":"layer","layer":"issues","query":{"text":"flaky"})", "id");
            CHECK(std::ranges::find(flaky, "issues/035") == flaky.end());
            CHECK_EQ(allPages(server, R"({"op":"layer","layer":"issues","query":{"tags":["private/me"]})", "id"),(Strings{"issues/035"}));
            auto counted = ask(server, R"({"op":"layer","layer":"issues","count":true,"limit":1})");
            CHECK_EQ(counted->num("matched"), 39);   // 40 items, one private
            CHECK_EQ(counted->num("total"), 40);
            // The filter is the store's own: every combination agrees with `Store::items` on the same query (the guard against the two drifting apart).
            struct Case { std::string json; Query q; };
            std::vector<Case> cases;
            { Query q; cases.push_back({R"({})", q}); }
            { Query q; q.kinds = {"test"}; cases.push_back({R"({"kinds":["test"]})", q}); }
            { Query q; q.states = {"done", "held"}; cases.push_back({R"({"states":["done","held"]})", q}); }
            { Query q; q.tags = {"area/parser"}; cases.push_back({R"({"tags":["area/parser"]})", q}); }
            { Query q; q.tags = {"private/me"}; cases.push_back({R"({"tags":["private/me"]})", q}); }
            { Query q; q.sources = {"squad/gate"}; q.text = "flaky"; cases.push_back({R"({"sources":["squad/gate"],"text":"flaky"})", q}); }
            { Query q; q.kinds = {"error"}; q.tags = {"area/parser"}; q.text = "item 1"; cases.push_back({R"({"kinds":["error"],"tags":["area/parser"],"text":"item 1"})", q}); }
            for (const Case& c : cases) {
                Query q = c.q;
                q.layers = {"issues"};
                Strings expect;
                for (const StoredItem* it : w.store.items(q)) expect.push_back(it->item.key);
                CHECK_EQ(allPages(server, R"({"op":"layer","layer":"issues","query":)" + c.json + R"(,"limit":6)", "id"), expect);
            }
            // The scan bound: a filter that matches nothing examines at most maxScan items per request and hands back a cursor, until the layer is done.
            QueryPolicy scan;
            scan.maxScan = 15;
            const QueryServer bounded(w.doc, scan);
            QueryStats ss;
            auto none = ask(bounded, R"({"op":"layer","layer":"issues","query":{"text":"no such text"}})", &ss);
            CHECK(none->entries().size() == 0 && !none->isNull("next") && none->num("scanned") == 15 && ss.visited == 15);
            std::size_t pages = 0;
            CHECK(allPages(bounded, R"({"op":"layer","layer":"issues","query":{"text":"no such text"})", "id", &pages).empty());
            CHECK_EQ(pages, std::size_t{3});
            // A cursor is a place in the layer: items appended between pages neither repeat nor skip one, and a state change cannot move a page's start.
            auto a = ask(server, R"({"op":"layer","layer":"issues","limit":30})");
            const std::string resume = a->str("next");
            (void)w.store.putLayer("issues", "squad/gate", [&] { auto more = items; more.push_back(item("issues/new", "error", "appended")); return more; }());
            (void)w.store.setState("issues/001", ItemState::dropped, "me", "x");
            Strings rest;
            for (simdjson::dom::element e : ask(server, R"({"op":"layer","layer":"issues","limit":50,"cursor":")" + resume + R"("})")->entries()) rest.emplace_back(std::string_view{e["id"]});
            Strings expectRest;
            for (int i = 30; i < 40; ++i) if (i != 35) expectRest.push_back("issues/" + pad(i, 3));
            expectRest.push_back("issues/new");
            CHECK_EQ(rest, expectRest);
            CHECK_EQ(ask(server, R"({"op":"layer","layer":"nope"})")->errorCode(), std::string{"not-found"});
            CHECK_EQ(ask(server, R"({"op":"layer"})")->errorCode(), std::string{"bad-request"});
            CHECK_EQ(ask(server, R"({"op":"layer","layer":"issues","cursor":"abc"})")->errorCode(), std::string{"bad-cursor"});
            CHECK_EQ(ask(server, R"({"op":"layer","layer":"issues","cursor":"99"})")->errorCode(), std::string{"bad-cursor"});
            CHECK_EQ(ask(server, R"({"op":"layer","layer":"issues","query":{"kinds":"test"}})")->errorCode(), std::string{"bad-request"});
        }
        {
            // Events from a position: the block-identity log of the index (the bundle's `events`) and the store's log. `next` is always a position: a reader that
            // is caught up keeps it and asks again.
            World w;
            w.index.setTurn(0);
            w.file("a.cpp", 1, cppFile(3));
            w.index.setTurn(1);
            w.file("a.cpp", 2, cppFile(2));   // fn2 is retired
            CHECK_EQ(w.index.events().size(), std::size_t{4});
            const QueryServer server(w.doc);
            auto all = ask(server, R"({"op":"events"})");
            CHECK(all->ok());
            CHECK_EQ(all->num("end"), 4);
            CHECK_EQ(all->num("next"), 4);
            CHECK_EQ(all->names("carry"), (Strings{"added", "added", "added", "merged"}));
            CHECK_EQ(all->entries().at(3)["id"].get_string().value(), std::string_view{w.index.events()[3].id});   // the retired fn2, merged into a survivor
            auto tail = ask(server, R"({"op":"events","from":3})");
            CHECK_EQ(tail->names("carry"), (Strings{"merged"}));
            CHECK_EQ(tail->num("from"), 3);
            auto caught = ask(server, R"({"op":"events","from":4})");
            CHECK(caught->ok() && caught->entries().size() == 0 && caught->num("next") == 4);
            w.index.setTurn(2);
            w.file("a.cpp", 3, cppFile(2) + "int zz(int q)\n{\n    return q - 12345;\n}\n\n");   // one block added after the reader caught up
            auto again = ask(server, R"({"op":"events","from":4})");
            CHECK_EQ(again->names("carry"), (Strings{"added"}));
            CHECK_EQ(again->num("next"), 5);
            // Paged by a limit, the pieces are the whole.
            Strings pieces;
            std::int64_t from = 0;
            for (int guard = 0; guard < 10; ++guard) {
                auto r = ask(server, R"({"op":"events","limit":2,"from":)" + std::to_string(from) + "}");
                for (const std::string& c : r->names("carry")) pieces.push_back(c);
                if (r->num("next") == r->num("end")) break;
                from = r->num("next");
            }
            CHECK_EQ(pieces, (Strings{"added", "added", "added", "merged", "added"}));
            CHECK_EQ(ask(server, R"({"op":"events","from":99})")->errorCode(), std::string{"bad-request"});
            CHECK_EQ(ask(server, R"({"op":"events","source":"mars"})")->errorCode(), std::string{"bad-request"});
            // The store's events: each event as the log holds it; a store restored from a checkpoint holds only the tail, and an older position is `gone`.
            (void)w.store.putLayer("issues", "squad/gate", std::vector<LayerItem>{item("k1", "error", "t")});
            (void)w.store.tag("k1", "t", "me");
            auto se = ask(server, R"({"op":"events","source":"store"})");
            CHECK_EQ(se->num("end"), 2);
            CHECK_EQ(se->names("kind"), (Strings{"item.put@1", "item.tag@1"}));
            CHECK_EQ(se->entries().at(1)["subject"].get_string().value(), std::string_view{"k1"});
            Store restored([] { StoreOptions o; o.writer = {"proj", "w-2"}; return o; }());
            CHECK(restored.restore(w.store.checkpoint()).has_value());
            (void)restored.tag("k1", "u", "me");
            QueryDocument d2(nullptr, &restored);
            const QueryServer s2(d2);
            auto gone = ask(s2, R"({"op":"events","source":"store","from":0})");
            CHECK(!gone->ok() && gone->errorCode() == "gone");
            auto ok2 = ask(s2, R"({"op":"events","source":"store","from":2})");
            CHECK(ok2->ok() && ok2->names("kind") == (Strings{"item.tag@1"}) && ok2->num("end") == 3);
        }
        {
            // R-AUTH-16: every failure is a structured error, in the same envelope, naming the operation and echoing the id when they are known.
            World w;
            w.file("a.txt", 1, "x\n");
            const QueryServer server(w.doc);
            struct Bad { std::string request, code, field; };
            const std::vector<Bad> bad = {
                {"", "bad-request", ""},
                {"not json", "bad-request", ""},
                {"[1,2]", "bad-request", ""},
                {"{}", "bad-request", "op"},
                {R"({"op":5})", "bad-request", "op"},
                {R"({"op":"nope","id":"q1"})", "unknown-op", "op"},
                {R"({"schema":"query-2","op":"info"})", "unsupported-schema", "schema"},
                {R"({"op":"tree","limit":0})", "bad-request", "limit"},
                {R"({"op":"tree","limit":-3})", "bad-request", "limit"},
                {R"({"op":"tree","limit":"ten"})", "bad-request", "limit"},
                {R"({"op":"tree","path":"a.txt"})", "not-a-folder", "path"},
                {R"({"op":"tree","path":"missing/dir"})", "not-found", "path"},
                {R"({"op":"tree","path":"a//b"})", "not-found", "path"},
                {R"({"op":"tree","cursor":"zzz"})", "bad-cursor", "cursor"},
                {R"({"op":"tree","id":[1]})", "bad-request", "id"},
                {R"({"op":"file","path":"a.txt","have":[1]})", "bad-request", "have"},
            };
            for (const Bad& b : bad) {
                QueryStats st;
                auto r = ask(server, b.request, &st);
                CHECK(!r->ok());
                if (r->ok()) continue;
                CHECK_EQ(r->str("schema"), std::string{"query-1"});
                CHECK_EQ(r->errorCode(), b.code);
                CHECK(!std::string{std::string_view{r->root["error"]["message"]}}.empty());
                if (!b.field.empty()) CHECK_EQ(std::string{std::string_view{r->root["error"]["field"]}}, b.field);
                CHECK_EQ(st.entries, std::size_t{0});
            }
            auto unknownOp = ask(server, R"({"op":"nope","id":"q1"})");
            CHECK_EQ(unknownOp->str("op"), std::string{"nope"});
            CHECK_EQ(unknownOp->str("id"), std::string{"q1"});   // the id of a request that failed is echoed
            // A request over the bound is refused unparsed.
            QueryPolicy tiny;
            tiny.maxRequestBytes = 20;
            CHECK_EQ(ask(QueryServer(w.doc, tiny), R"({"op":"tree","path":"aaaaaaaaaaaaaaaaaaaaaaaaaa"})")->errorCode(), std::string{"too-large"});
            // Too many `have` ids.
            QueryPolicy few;
            few.maxHave = 1;
            CHECK_EQ(ask(QueryServer(w.doc, few), R"({"op":"file","path":"a.txt","have":["a","b"]})")->errorCode(), std::string{"bad-request"});
            // A request that names the schema it speaks is fine.
            CHECK(ask(server, R"({"schema":"query-1","op":"info"})")->ok());
        }
        {
            // Unknown members: the policy decides (docs/QUERY.md section 6).
            World w;
            w.file("a.txt", 1, "x\n");
            const std::string req = R"({"op":"tree","zeta":1,"query":{"what":2},"alpha":{"deep":[1,2]}})";
            // echo (the default): answered, and the dropped names listed in the order they came.
            auto e = ask(QueryServer(w.doc), req);
            CHECK(e->ok());
            Strings echoed;
            for (simdjson::dom::element n : arrayOf(e->root["unknownFields"])) echoed.emplace_back(std::string_view{n});
            CHECK_EQ(echoed, (Strings{"zeta", "query.what", "alpha"}));
            // ignore: answered as if absent, byte for byte as the same request without them.
            QueryPolicy ignore;
            ignore.unknownFields = UnknownFields::ignore;
            auto i = ask(QueryServer(w.doc, ignore), req);
            CHECK(i->ok() && !i->has("unknownFields"));
            CHECK_EQ(QueryServer(w.doc, ignore).handle(req), QueryServer(w.doc, ignore).handle(R"({"op":"tree"})"));
            // reject: an error naming the first.
            QueryPolicy reject;
            reject.unknownFields = UnknownFields::reject;
            auto r = ask(QueryServer(w.doc, reject), req);
            CHECK(!r->ok() && r->errorCode() == "unknown-field" && std::string{std::string_view{r->root["error"]["field"]}} == "zeta");
            // The echo is bounded: a request with a hundred unknown members lists at most sixteen, names clipped.
            std::string many = R"({"op":"tree")";
            for (int k = 0; k < 100; ++k) many += ",\"" + std::string(100, 'n') + std::to_string(k) + "\":1";
            many += "}";
            auto m = ask(QueryServer(w.doc), many);
            CHECK_EQ(arrayOf(m->root["unknownFields"]).size(), std::size_t{16});
            CHECK_EQ(std::string_view{m->root["unknownFields"].get_array().at(0).value()}.size(), std::size_t{64});
        }
        {
            // The policy is data: parsed from JSON, written back, and refused with the member named when it is wrong.
            const auto p = parseQueryPolicy(R"({"tree":{"default":10,"max":20},"layer":{"max":60},"maxResponseBytes":4096,"unknownFields":"reject","hiddenTagPrefixes":["secret/","private/"]})");
            CHECK(p.has_value());
            CHECK_EQ(p->tree.defaultItems, std::size_t{10});
            CHECK_EQ(p->tree.maxItems, std::size_t{20});
            CHECK_EQ(p->layer.defaultItems, std::size_t{50});   // untouched members keep the defaults
            CHECK_EQ(p->layer.maxItems, std::size_t{60});
            CHECK_EQ(p->maxResponseBytes, std::size_t{4096});
            CHECK(p->unknownFields == UnknownFields::reject);
            CHECK_EQ(p->hiddenTagPrefixes, (Strings{"secret/", "private/"}));
            const auto again = parseQueryPolicy(queryPolicyJson(*p));
            CHECK(again.has_value() && queryPolicyJson(*again) == queryPolicyJson(*p));
            CHECK_EQ(queryPolicyJson(QueryPolicy{}), queryPolicyJson(*parseQueryPolicy("{}")));
            for (const char* bad : {R"({"tree":{"default":0}})", R"({"tree":{"default":9,"max":3}})", R"({"bogus":1})", R"({"tree":{"huge":1}})", R"({"unknownFields":"shout"})",
                                    R"({"maxHave":"many"})", "nope", R"({"layer":5})"})
                CHECK(!parseQueryPolicy(bad).has_value());
            CHECK(parseQueryPolicy(R"({"bogus":1})").error().contains("bogus"));
            // A hidden prefix from the policy governs the layer answers, and the policy's page sizes govern every answer.
            World w;
            (void)w.store.putLayer("issues", "s", std::vector<LayerItem>{item("a", "error", "a"), item("b", "error", "b"), item("c", "error", "c")});
            (void)w.store.tag("b", "secret/x", "me");
            CHECK_EQ(ask(QueryServer(w.doc, *p), R"({"op":"layer","layer":"issues"})")->names("id"), (Strings{"a", "c"}));
            CHECK_EQ(ask(QueryServer(w.doc), R"({"op":"layer","layer":"issues"})")->names("id"), (Strings{"a", "b", "c"}));
            // `info` reports the policy it enforces.
            auto info = ask(QueryServer(w.doc, *p), R"({"op":"info"})");
            CHECK_EQ(info->root["policy"]["tree"]["max"].get_int64().value(), 20);
            CHECK_EQ(info->root["policy"]["unknownFields"].get_string().value(), std::string_view{"reject"});
        }

        // R-AUTH-17 (review D11): the bundle is "every query answered at once". For every turn of every mock attempt, the document is rebuilt from the
        // snapshot the viewer holds (files and their texts, blocks by replaying the index, the layer items), and the data read BACK THROUGH THE QUERIES alone
        // (walking `tree`, `file` by content id, `blocks` per file, `layers`/`layer`, `events` from `eventsFrom`) must equal what the snapshot holds, member by member.
        if (argc >= 2) {
            const fs::path mock = fs::path(argv[1]) / "mock";
            std::size_t turnsChecked = 0, itemsChecked = 0, filesChecked = 0, blocksChecked = 0, eventsChecked = 0;
            for (const auto& attempt : fs::directory_iterator(mock)) {
                const fs::path layers = attempt.path() / "layers";
                if (!fs::is_directory(layers)) continue;
                std::vector<std::pair<int, fs::path>> turns;
                for (const auto& f : fs::directory_iterator(layers)) {
                    const std::string stem = f.path().stem().string();   // turn-3
                    turns.emplace_back(std::stoi(stem.substr(5)), f.path());
                }
                std::ranges::sort(turns);
                BlockIndex index;
                QueryDocument doc(&index, nullptr);
                std::map<std::string, std::string> carried;   // content id -> text, across the turns of this attempt (texts by id carry only what is new)
                for (const auto& [turn, file] : turns) {
                    std::ifstream in(file, std::ios::binary);
                    std::stringstream ss;
                    ss << in.rdbuf();
                    const std::string bundleText = ss.str();
                    simdjson::dom::parser bp;
                    simdjson::dom::element bundle = bp.parse(simdjson::padded_string{bundleText});
                    if (auto texts = bundle["texts"].get_object(); !texts.error())
                        for (auto kv : texts.value()) carried[std::string{kv.key}] = std::string{std::string_view{kv.value}};
                    // The document: files in path order at their revisions, the index replayed as the loop did (every file, every turn).
                    index.setTurn(turn);
                    std::map<std::string, std::string> textsNow;
                    for (auto kv : objectOf(bundle["files"])) {
                        const std::string path{kv.key};
                        std::string text;
                        if (!kv.value["text"].error()) text = std::string{std::string_view{kv.value["text"]}};
                        else text = carried.at(std::string{std::string_view{kv.value["textId"]}});
                        textsNow[path] = text;
                        (void)doc.addRevision(path, static_cast<int>(kv.value["revision"].get_int64().value()), std::string{std::string_view{kv.value["revisionId"]}}, text);
                    }
                    for (const auto& [path, text] : textsNow) index.update(path, text);
                    // The layers: this turn's items, in order, in a store of their own (a store keeps what vanished; the snapshot lists the turn's items).
                    Store store([] { StoreOptions o; o.writer = {"mock", "w-1"}; return o; }());
                    std::map<std::string, std::size_t> layerSizes;
                    for (auto layer : objectOf(bundle["layers"])) {
                        std::vector<LayerItem> items;
                        for (simdjson::dom::element e : arrayOf(layer.value)) {
                            LayerItem li;
                            li.key = std::string{std::string_view{e["id"]}};
                            li.kind = std::string{std::string_view{e["kind"]}};
                            li.title = std::string{std::string_view{e["title"]}};
                            li.detail = std::string{std::string_view{e["detail"]}};
                            for (simdjson::dom::element a : arrayOf(e["anchors"])) {
                                Anchor an;
                                an.blockId = std::string{std::string_view{a["blockId"]}};
                                an.path = std::string{std::string_view{a["path"]}};
                                an.line = static_cast<int>(a["line"].get_int64().value());
                                if (!a["column"].error()) an.column = static_cast<int>(a["column"].get_int64().value());
                                if (!a["endColumn"].error()) { an.endLine = static_cast<int>(a["endLine"].get_int64().value()); an.endColumn = static_cast<int>(a["endColumn"].get_int64().value()); }
                                const std::string_view c = a["confidence"];
                                an.confidence = c == "exact" ? Anchor::Confidence::exact : c == "suspect" ? Anchor::Confidence::suspect : Anchor::Confidence::none;
                                li.anchors.push_back(an);
                            }
                            items.push_back(std::move(li));
                        }
                        layerSizes[std::string{layer.key}] = items.size();
                        (void)store.putLayer(std::string{layer.key}, "mock", items);
                    }
                    QueryDocument full(&index, &store);
                    for (const auto& [path, text] : textsNow) (void)full.addRevision(path, 1, {}, text);   // revisions are checked through `doc` below; this one serves the layers
                    const QueryServer server(doc);
                    const QueryServer layerServer(full);
                    const std::string where = attempt.path().filename().string() + " turn " + std::to_string(turn);

                    // files: walk the tree (folders then files, each page by its cursor), compare each file with the snapshot's, and its text by content id.
                    std::map<std::string, simdjson::dom::element> bundleFiles;
                    for (auto kv : objectOf(bundle["files"])) bundleFiles.emplace(std::string{kv.key}, kv.value);
                    std::set<std::string> seen;
                    std::function<void(const std::string&)> walk = [&](const std::string& dir) {
                        std::string cursor;
                        for (;;) {
                            auto r = ask(server, R"({"op":"tree","path":")" + dir + R"(","limit":2)" + (cursor.empty() ? "" : R"(,"cursor":")" + cursor + "\"") + "}");
                            CHECK(r->ok());
                            if (!r->ok()) return;
                            for (simdjson::dom::element e : r->entries()) {
                                const std::string name{std::string_view{e["name"]}};
                                const std::string path = dir.empty() ? name : dir + "/" + name;
                                if (std::string_view{e["kind"]} == "folder") { walk(path); continue; }
                                seen.insert(path);
                                const auto b = bundleFiles.find(path);
                                CHECK(b != bundleFiles.end());
                                if (b == bundleFiles.end()) continue;
                                for (const char* m : {"revision", "revisionId", "textId"}) {
                                    if (simdjson::minify(e[m]) != simdjson::minify(b->second[m])) { std::fprintf(stderr, "%s: %s of %s differs\n", where.c_str(), m, path.c_str()); CHECK(false); }
                                }
                                auto f = ask(server, R"({"op":"file","textId":")" + std::string{std::string_view{e["textId"]}} + R"("})");
                                const std::string want = !b->second["text"].error() ? std::string{std::string_view{b->second["text"]}} : carried.at(std::string{std::string_view{b->second["textId"]}});
                                CHECK(f->ok() && f->str("text") == want && f->isNull("next"));
                                ++filesChecked;
                            }
                            if (r->isNull("next")) break;
                            cursor = r->str("next");
                        }
                    };
                    walk("");
                    CHECK_EQ(seen.size(), bundleFiles.size());

                    // blocks: per file, paged, equal to the snapshot's blocks of that file, object by object; no block of another file appears.
                    std::map<std::string, std::vector<std::string>> bundleBlocks;
                    for (simdjson::dom::element b : arrayOf(bundle["blocks"])) bundleBlocks[std::string{std::string_view{b["path"]}}].push_back(simdjson::minify(b));
                    std::size_t blockTotal = 0;
                    for (const auto& [path, _] : bundleFiles) {
                        std::vector<std::string> got;
                        std::string cursor;
                        for (;;) {
                            auto r = ask(server, R"({"op":"blocks","path":")" + path + R"(","limit":3)" + (cursor.empty() ? "" : R"(,"cursor":")" + cursor + "\"") + "}");
                            CHECK(r->ok());
                            if (!r->ok()) break;
                            for (simdjson::dom::element e : r->entries()) got.push_back(simdjson::minify(e));
                            if (r->isNull("next")) break;
                            cursor = r->str("next");
                        }
                        CHECK(got == bundleBlocks[path]);
                        blockTotal += got.size();
                    }
                    std::size_t inBundle = 0;
                    for (const auto& [_, v] : bundleBlocks) inBundle += v.size();
                    CHECK_EQ(blockTotal, inBundle);
                    blocksChecked += blockTotal;

                    // layers: the names and counts, then each layer's items, paged: every member of the snapshot's item is the answer's, and the answer adds only the
                    // store's own (source, state, tags).
                    auto ls = ask(layerServer, R"({"op":"layers"})");
                    CHECK_EQ(ls->entries().size(), objectOf(bundle["layers"]).size());
                    for (auto layer : objectOf(bundle["layers"])) {
                        const std::string name{layer.key};
                        std::vector<simdjson::dom::element> want;
                        for (simdjson::dom::element e : arrayOf(layer.value)) want.push_back(e);
                        std::size_t at = 0;
                        std::string cursor;
                        std::vector<std::unique_ptr<Resp>> pagesHeld;
                        for (;;) {
                            pagesHeld.push_back(ask(layerServer, R"({"op":"layer","layer":")" + name + R"(","limit":2)" + (cursor.empty() ? "" : R"(,"cursor":")" + cursor + "\"") + "}"));
                            Resp& r = *pagesHeld.back();
                            CHECK(r.ok());
                            if (!r.ok()) break;
                            for (simdjson::dom::element e : r.entries()) {
                                CHECK(at < want.size());
                                if (at >= want.size()) break;
                                std::set<std::string> have;
                                const simdjson::dom::object eObj = objectOf(e);
                                for (auto kv : eObj) have.insert(std::string{kv.key});
                                const simdjson::dom::object wantObj = objectOf(want[at]);
                                for (auto kv : wantObj) {
                                    if (simdjson::minify(e[kv.key]) != simdjson::minify(kv.value)) { std::fprintf(stderr, "%s: %s item %zu member %s differs\n", where.c_str(), name.c_str(), at, std::string{kv.key}.c_str()); CHECK(false); }
                                    have.erase(std::string{kv.key});
                                }
                                CHECK_EQ(have, (std::set<std::string>{"source", "state", "tags"}));
                                ++at;
                                ++itemsChecked;
                            }
                            if (r.isNull("next")) break;
                            cursor = r.str("next");
                        }
                        CHECK_EQ(at, want.size());
                    }

                    // events: from the snapshot's `eventsFrom`, the answer is the snapshot's events. (The document replays the index from the first turn, so its
                    // log holds every event; the snapshot carries those from `eventsFrom` on.)
                    const std::int64_t from = bundle["eventsFrom"].get_int64().value();
                    auto ev = ask(server, R"({"op":"events","limit":2000,"from":)" + std::to_string(from) + "}");
                    CHECK(ev->ok());
                    const auto want = arrayOf(bundle["events"]);
                    CHECK_EQ(ev->entries().size(), want.size());
                    std::size_t k = 0;
                    for (simdjson::dom::element e : want) {
                        if (k < ev->entries().size()) CHECK_EQ(simdjson::minify(ev->entries().at(k).value()), simdjson::minify(e));
                        ++k;
                        ++eventsChecked;
                    }
                    ++turnsChecked;
                }
            }
            CHECK_GE(turnsChecked, std::size_t{16});   // fifteen attempts' turn files and more: the fixture is present and was read
            CHECK_GE(itemsChecked, std::size_t{16});
            CHECK_GE(filesChecked, std::size_t{16});
            CHECK_GE(blocksChecked, std::size_t{16});
            CHECK_GE(eventsChecked, std::size_t{16});
            std::printf("R-AUTH-17: %zu turns, %zu files, %zu blocks, %zu layer items, %zu events read back through the queries equal the snapshots\n", turnsChecked, filesChecked, blocksChecked,
                        itemsChecked, eventsChecked);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "test_query: uncaught exception: %s\n", e.what());
        CHECK(false);
    }
    return finish();
}

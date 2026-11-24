// Query benchmarks (slice V1a-q, docs/QUERY.md; requirements R-AUTH-1, R-AUTH-13): the cost of one answer against the size of what it names, never of the document.
// The deterministic metrics are the nodes a request visited and the bytes it sent, on the 1e5-line corpus (bench/corpus.hpp) and on the shapes K0 measured (docs/K0.md:
// a 1,588-file folder, 1,590 files in 159 folders); the timing rows are the same requests as a local run. The response sizes are the budget's input: a folder expands in
// 50 ms only if its listing is a few hundred kilobytes and parses in a few milliseconds (docs/QUERY.md section 9). Compiled into Sub0Canvas_Bench (bench_canvas.cpp
// calls `runQueryBenchmarks` and owns the counting operator new). Everything is generated from loops and a seeded corpus: the same bytes on every machine.
#include "bench_runner.hpp"

#include "bench/corpus.hpp"
#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/query.hpp"
#include "sub0canvas/store.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace sub0canvas;
namespace bench = sub0canvas::bench;
using sub0bench::MetricKind;

namespace {

struct Once
{
    bool done = false;
    void operator()(const std::function<void()>& body)
    {
        if (done) return;
        done = true;
        body();
    }
};

std::string pad(int n, int width = 4)
{
    std::string s = std::to_string(n);
    return std::string(static_cast<std::size_t>(width) - std::min<std::size_t>(s.size(), static_cast<std::size_t>(width)), '0') + s;
}

/// The 1e5-line corpus as a served document: files, the index over them, and an issues layer with one item per block, half of them tests.
struct Served
{
    BlockIndex index;
    std::unique_ptr<Store> store;
    QueryDocument doc;
    std::string largestPath, firstFolder;
    std::size_t files = 0, blocks = 0, largestBlocks = 0, largestBytes = 0;
    Served() : store(std::make_unique<Store>([] { StoreOptions o; o.writer = {"bench", "w-1"}; return o; }())), doc(&index, store.get())
    {
        bench::CorpusSpec spec;
        spec.seed = 1;
        spec.targetLines = 100'000;
        auto corpus = bench::generateCorpus(spec);
        if (!corpus) { std::fprintf(stderr, "corpus: %s\n", corpus.error().c_str()); std::exit(2); }
        for (const auto& [path, text] : corpus->files) {
            const bool code = path.ends_with(".cpp") || path.ends_with(".hpp") || path.ends_with(".py");
            if (!doc.addRevision(path, 1, {}, text)) continue;
            ++files;
            if (firstFolder.empty() && path.find('/') != std::string::npos) firstFolder = path.substr(0, path.rfind('/'));
            if (!code) continue;
            index.update(path, text);
            const std::size_t n = index.blocks(path).size();
            if (n > largestBlocks) { largestBlocks = n; largestPath = path; largestBytes = text.size(); }
        }
        std::vector<LayerItem> items;
        for (const Block* b : index.all()) {
            LayerItem it;
            it.key = "issues/" + b->id;
            it.kind = items.size() % 2 == 0 ? "test" : "error";
            it.title = "case in " + std::string{b->name};
            it.detail = "FAIL - " + std::string{b->name} + ": expected 1 got 2";
            Anchor a;
            a.blockId = b->id;
            a.path = b->path;
            a.line = b->first;
            a.confidence = Anchor::Confidence::suspect;
            it.anchors = {a};
            items.push_back(std::move(it));
        }
        blocks = items.size();
        (void)store->putLayer("issues", "squad/gate", items);
        doc.setLayerMeta("issues", {"Issues", "Failing tests, one per block", "bench"});
    }
};

/// The shapes K0 timed (docs/K0.md): 1,588 files in one folder, and 1,590 files in 159 folders of ten.
struct K0Shapes
{
    QueryDocument flat, tree;
    K0Shapes()
    {
        for (int i = 0; i < 1588; ++i) (void)flat.addRevision("flat/file" + pad(i) + ".cpp", 1, {}, "int f" + std::to_string(i) + "();\n");
        for (int d = 0; d < 159; ++d)
            for (int i = 0; i < 10; ++i) (void)tree.addRevision("src/dir" + pad(d, 3) + "/file" + std::to_string(i) + ".cpp", 1, {}, "int g" + std::to_string(d * 10 + i) + "();\n");
    }
};

}  // namespace

void runQueryBenchmarks(sub0bench::BenchRunner& runner, const std::size_t& allocations, const std::size_t& /*liveBytes*/)
{
    ankerl::nanobench::Bench& bench = runner.bench();
    auto heavy = [&](std::size_t epochs) { bench.warmup(1).epochs(epochs).minEpochIterations(1); };
    auto light = [&] { bench.warmup(20).epochs(11).minEpochIterations(5); };
    auto metric = [&](const std::string& name, std::size_t v, const char* unit) { runner.metric(name.c_str(), static_cast<double>(v), unit); };

    // The K0 shapes: what a folder of 1,588 files costs to list, in bytes and in pages, and how a listing of 159 folders compares.
    {
        light();
        Once once;
        static K0Shapes shapes;
        const QueryServer flat(shapes.flat);
        const QueryServer tree(shapes.tree);
        const std::string onePage = R"({"op":"tree","path":"flat","limit":2000})";
        const std::string defaultPage = R"({"op":"tree","path":"flat"})";
        runner.run("query tree flat folder of 1,588 files, one page of 2,000", [&] {
            once([&] {
                QueryStats st;
                (void)flat.handle(onePage, &st);
                metric("query tree flat 1588 files: nodes visited, one page", st.visited, "n");
                metric("query tree flat 1588 files: entries, one page", st.entries, "n");
                metric("query tree flat 1588 files: response bytes, one page", st.responseBytes, "B");
                metric("query tree flat 1588 files: response bytes per file", st.responseBytes / st.entries, "B");
                // The default page (500) drained: pages and bytes in all (each page repeats the header, so the total is a little above the one page).
                std::size_t pages = 0, bytes = 0, entries = 0, firstPage = 0;
                std::string cursor;
                for (;;) {
                    const std::string req = R"({"op":"tree","path":"flat")" + (cursor.empty() ? std::string{} : R"(,"cursor":")" + cursor + "\"") + "}";
                    QueryStats p;
                    const std::string r = flat.handle(req, &p);
                    ++pages;
                    bytes += p.responseBytes;
                    entries += p.entries;
                    if (pages == 1) firstPage = p.responseBytes;
                    const auto at = r.find(R"("next":")");
                    if (at == std::string::npos) break;
                    const auto from = at + 8;
                    cursor = r.substr(from, r.find('"', from) - from);
                }
                metric("query tree flat 1588 files: pages at the default of 500", pages, "n");
                metric("query tree flat 1588 files: entries over all pages", entries, "n");
                metric("query tree flat 1588 files: response bytes of the first default page", firstPage, "B");
                metric("query tree flat 1588 files: response bytes over all default pages", bytes, "B");
                QueryStats root, one;
                (void)tree.handle(R"({"op":"tree","path":"src"})", &root);
                (void)tree.handle(R"({"op":"tree","path":"src/dir100"})", &one);
                metric("query tree 1590 files in 159 folders: nodes visited, the folder of 159 folders", root.visited, "n");
                metric("query tree 1590 files in 159 folders: response bytes, the folder of 159 folders", root.responseBytes, "B");
                metric("query tree 1590 files in 159 folders: nodes visited, one folder of ten files", one.visited, "n");
                metric("query tree 1590 files in 159 folders: response bytes, one folder of ten files", one.responseBytes, "B");
            });
            auto r = flat.handle(onePage);
            ankerl::nanobench::doNotOptimizeAway(r);
        });
        runner.run("query tree flat folder of 1,588 files, one page of 500", [&] {
            auto r = flat.handle(defaultPage);
            ankerl::nanobench::doNotOptimizeAway(r);
        });
        runner.run("query tree folder of 159 folders", [&] {
            auto r = tree.handle(R"({"op":"tree","path":"src"})");
            ankerl::nanobench::doNotOptimizeAway(r);
        });
    }

    // The 1e5-line corpus: a root listing, a folder, the blocks of one file, a deep layer page, a filtered one, events, a text.
    {
        heavy(1);
        static Served served;
        const QueryServer server(served.doc);
        Once once;
        runner.run("query setup, the served 1e5-line corpus (documents only)", [&] {
            once([&] {
                metric("query corpus 1e5: files served", served.files, "n");
                metric("query corpus 1e5: blocks served", served.blocks, "n");
                QueryStats root, folder, blocks, deep, shallow, filtered, events, file;
                (void)server.handle(R"({"op":"tree"})", &root);
                (void)server.handle(R"({"op":"tree","path":")" + served.firstFolder + R"("})", &folder);
                (void)server.handle(R"({"op":"blocks","path":")" + served.largestPath + R"(","limit":2000})", &blocks);
                (void)server.handle(R"({"op":"layer","layer":"issues","limit":50})", &shallow);
                (void)server.handle(R"({"op":"layer","layer":"issues","limit":50,"cursor":"4000"})", &deep);
                (void)server.handle(R"({"op":"layer","layer":"issues","limit":50,"query":{"kinds":["error"]}})", &filtered);
                (void)server.handle(R"({"op":"events","limit":200,"from":2000})", &events);
                (void)server.handle(R"({"op":"file","path":")" + served.largestPath + R"("})", &file);
                metric("query tree root 1e5: nodes visited", root.visited, "n");
                metric("query tree root 1e5: response bytes", root.responseBytes, "B");
                metric("query tree one folder 1e5: nodes visited", folder.visited, "n");
                metric("query tree one folder 1e5: entries", folder.entries, "n");
                metric("query tree one folder 1e5: response bytes", folder.responseBytes, "B");
                metric("query blocks of the largest file 1e5: blocks in the file", served.largestBlocks, "n");
                metric("query blocks of the largest file 1e5: nodes visited", blocks.visited, "n");
                metric("query blocks of the largest file 1e5: response bytes", blocks.responseBytes, "B");
                metric("query layer 4,800 items, page of 50 at the start: nodes visited", shallow.visited, "n");
                metric("query layer 4,800 items, page of 50 at the start: response bytes", shallow.responseBytes, "B");
                metric("query layer 4,800 items, page of 50 at item 4,000: nodes visited", deep.visited, "n");   // the depth of the window costs nothing
                metric("query layer 4,800 items, page of 50 at item 4,000: response bytes", deep.responseBytes, "B");
                metric("query layer 4,800 items, page of 50 of one kind in two: nodes visited", filtered.visited, "n");
                metric("query events page of 200 from 2,000: entries", events.entries, "n");
                metric("query events page of 200 from 2,000: response bytes", events.responseBytes, "B");
                metric("query file the largest text 1e5: response bytes", file.responseBytes, "B");
                metric("query file the largest text 1e5: text bytes", served.largestBytes, "B");
                // The cost of a request in allocations: a page of the layer, counted.
                const std::size_t a0 = allocations;
                (void)server.handle(R"({"op":"layer","layer":"issues","limit":50,"cursor":"4000"})");
                runner.metric("query layer page of 50: allocations", static_cast<double>(allocations - a0), "n", MetricKind::max);
            });
        });
        light();
        runner.run("query tree root 1e5 lines", [&] {
            auto r = server.handle(R"({"op":"tree"})");
            ankerl::nanobench::doNotOptimizeAway(r);
        });
        const std::string folderReq = R"({"op":"tree","path":")" + served.firstFolder + R"("})";
        runner.run("query tree one folder 1e5 lines", [&] {
            auto r = server.handle(folderReq);
            ankerl::nanobench::doNotOptimizeAway(r);
        });
        const std::string blocksReq = R"({"op":"blocks","path":")" + served.largestPath + R"(","limit":2000})";
        runner.run("query blocks of the largest file 1e5 lines", [&] {
            auto r = server.handle(blocksReq);
            ankerl::nanobench::doNotOptimizeAway(r);
        });
        runner.run("query layer page of 50 at item 4,000", [&] {
            auto r = server.handle(R"({"op":"layer","layer":"issues","limit":50,"cursor":"4000"})");
            ankerl::nanobench::doNotOptimizeAway(r);
        });
        runner.run("query layer page of 50, filtered to one kind in two", [&] {
            auto r = server.handle(R"({"op":"layer","layer":"issues","limit":50,"query":{"kinds":["error"]}})");
            ankerl::nanobench::doNotOptimizeAway(r);
        });
        runner.run("query events page of 200", [&] {
            auto r = server.handle(R"({"op":"events","limit":200,"from":2000})");
            ankerl::nanobench::doNotOptimizeAway(r);
        });
        const std::string fileReq = R"({"op":"file","path":")" + served.largestPath + R"("})";
        runner.run("query file the largest text 1e5 lines", [&] {
            auto r = server.handle(fileReq);
            ankerl::nanobench::doNotOptimizeAway(r);
        });
        runner.run("query refused request (a structured error)", [&] {
            auto r = server.handle(R"({"op":"tree","limit":0})");
            ankerl::nanobench::doNotOptimizeAway(r);
        });
    }
}

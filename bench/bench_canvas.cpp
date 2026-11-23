// Sub0Canvas micro-benchmarks, run through the Sub0 family's sub0perf tooling (docs/TESTING.md, "Performance"; docs/SCALE.md section 6).
// Library only, on the seeded synthetic corpus (bench/corpus.hpp), so every input is the same bytes on every machine. Naming follows sub0perf: a
// "hand-written" reference and its library twin share the text before the first comma and form a pair group; the hand-written rows never call the library,
// so they are the environment controls.
//
// Two kinds of result. Timing rows are measured locally only (ctest label `timing`, never in CI). The metrics are deterministic facts (sizes, counts,
// block counts, bytes written, heap bytes and allocations counted by the replaced operator new below) judged against perf/metrics.json by the `_metrics` test,
// which runs in every tier. Metrics are registered inside the workloads, once per process: `--metrics-only` and `--smoke` run each workload once.
#define ANKERL_NANOBENCH_IMPLEMENT
#include "bench_runner.hpp"

#include "bench/corpus.hpp"
#include "sub0canvas/canvas.hpp"
#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/outline.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <vector>

using namespace sub0canvas;
namespace bench = sub0canvas::bench;
using sub0bench::MetricKind;

// ---- a counting allocator: allocation counts and live heap bytes are deterministic facts about the library (single-threaded, plain counters). On MinGW the
// executable links the C++ runtime statically (CMakeLists.txt) so that this operator new is the only one ----------------------------------------------
namespace {
std::size_t gAllocations = 0;   ///< calls to operator new so far
std::size_t gLiveBytes = 0;     ///< bytes requested and not yet freed
constexpr std::size_t kHeader = 16;   ///< keeps 16-byte alignment and holds the requested size

void* countedAlloc(std::size_t size)
{
    void* raw = std::malloc(size + kHeader);
    if (raw == nullptr) throw std::bad_alloc();
    std::memcpy(raw, &size, sizeof size);
    ++gAllocations;
    gLiveBytes += size;
    return static_cast<char*>(raw) + kHeader;
}

void countedFree(void* p) noexcept
{
    if (p == nullptr) return;
    char* raw = static_cast<char*>(p) - kHeader;
    std::size_t size = 0;
    std::memcpy(&size, raw, sizeof size);
    gLiveBytes -= size;
    std::free(raw);
}
}  // namespace

void* operator new(std::size_t size) { return countedAlloc(size); }
void* operator new[](std::size_t size) { return countedAlloc(size); }
void operator delete(void* p) noexcept { countedFree(p); }
void operator delete[](void* p) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { countedFree(p); }

namespace {

/// Runs `body` once per process however often the workload repeats (a metric is a fact, not a rate; timed iterations do not recompute it).
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

bool isCode(const std::string& path) { return path.ends_with(".cpp") || path.ends_with(".hpp") || path.ends_with(".py"); }

bench::Corpus makeCorpus(std::size_t lines, std::uint64_t seed = 1)
{
    bench::CorpusSpec spec;
    spec.seed = seed;
    spec.targetLines = lines;
    auto corpus = bench::generateCorpus(spec);
    if (!corpus) { std::fprintf(stderr, "corpus: %s\n", corpus.error().c_str()); std::exit(2); }
    return std::move(*corpus);
}

/// A corpus of about `lines` lines with the commits an edit stream makes over it, built once per process on first use.
struct Fixture
{
    bench::Corpus corpus;
    std::vector<std::string> cppPaths, pyPaths;
    std::size_t textBytes = 0;
    explicit Fixture(std::size_t lines) : corpus(makeCorpus(lines))
    {
        for (const auto& [path, text] : corpus.files)
        {
            if (!isCode(path)) continue;
            (isPythonPath(path) ? pyPaths : cppPaths).push_back(path);
            textBytes += text.size();
        }
    }
    [[nodiscard]] BlockIndex coldIndex() const
    {
        BlockIndex index;
        for (const auto& [path, text] : corpus.files)
            if (isCode(path)) index.update(path, text);
        return index;
    }
};

Fixture& fixture1e5() { static Fixture f(100'000); return f; }
Fixture& fixture1e4() { static Fixture f(10'000); return f; }
Fixture& fixture2e4() { static Fixture f(20'000); return f; }

// ---- the hand-written controls: no library call, plain scans of the same texts ---------------------------------------------------------------------------

/// Brace depth transitions over every C++ text: the work a brace-matching fold has to do at least.
std::size_t braceScan(const Fixture& f)
{
    std::size_t regions = 0;
    for (const auto& path : f.cppPaths)
    {
        int depth = 0;
        for (const char c : f.corpus.files.at(path))
        {
            if (c == '{') { if (depth++ == 0) ++regions; }
            else if (c == '}') --depth;
        }
    }
    return regions;
}

/// Lines that start in column 0 with `def ` over every Python text.
std::size_t defScan(const Fixture& f)
{
    std::size_t defs = 0;
    for (const auto& path : f.pyPaths)
    {
        const std::string& t = f.corpus.files.at(path);
        for (std::size_t p = 0; p < t.size();)
        {
            if (t.compare(p, 4, "def ") == 0) ++defs;
            const std::size_t nl = t.find('\n', p);
            if (nl == std::string::npos) break;
            p = nl + 1;
        }
    }
    return defs;
}

// ---- layered JSON over eight turns of a corpus --------------------------------------------------------------------------------------------------------

/// Eight snapshots' inputs (the index and files as they stood each turn), built once: the benchmark times only `layeredJson`.
struct Turns
{
    std::vector<BlockIndex> indexes;
    std::vector<SnapshotSpec> specs;      ///< per-turn events (`eventsFrom`), texts by content id
    std::vector<SnapshotSpec> inlineSpecs;   ///< the same turns, cumulative: every event from the start, every text inline
    std::size_t filesChanged = 0;
};

Turns buildTurns(const Fixture& f, int turns)
{
    Turns t;
    bench::EditStream stream(f.corpus, 7);
    std::map<std::string, std::string> files;
    std::map<std::string, int> revisions;
    for (const auto& [path, text] : f.corpus.files)
        if (isCode(path)) { files[path] = text; revisions[path] = 1; }
    BlockIndex index;
    std::size_t eventsFrom = 0;
    for (int turn = 1; turn <= turns; ++turn)
    {
        index.setTurn(turn);
        if (turn == 1)
        {
            for (const auto& [path, text] : files) index.update(path, text);
        }
        else
        {
            const bench::Commit commit = stream.next();
            for (const auto& change : commit.changes)
            {
                if (change.text) { files[change.path] = *change.text; ++revisions[change.path]; index.update(change.path, *change.text); }
                else { files.erase(change.path); index.update(change.path, ""); }
                ++t.filesChanged;
            }
        }
        SnapshotSpec spec;
        spec.turn = turn;
        spec.files = files;
        spec.revisions = revisions;
        // An issues layer: a warning anchored to every 40th block, the shape a compiler's verdict gives.
        const auto blocks = index.all();
        std::vector<LayerItem> items;
        for (std::size_t i = 0; i < blocks.size(); i += 40)
        {
            LayerItem item;
            item.layer = "issues";
            item.number = static_cast<int>(items.size()) + 1;
            item.kind = "warning";
            item.title = "unused variable in " + blocks[i]->name;
            item.detail = blocks[i]->path + ": warning: unused variable in " + blocks[i]->name;
            item.key = "unused-" + blocks[i]->name;
            Anchor a;
            a.blockId = blocks[i]->id;
            a.path = blocks[i]->path;
            a.line = blocks[i]->first;
            a.confidence = Anchor::Confidence::exact;
            item.anchors.push_back(a);
            items.push_back(std::move(item));
        }
        spec.layers["issues"] = std::move(items);
        SnapshotSpec cumulative = spec;
        cumulative.eventsFrom = 0;
        cumulative.textsById = false;
        spec.eventsFrom = eventsFrom;
        spec.textsById = true;
        eventsFrom = index.events().size();
        t.indexes.push_back(index);
        t.specs.push_back(std::move(spec));
        t.inlineSpecs.push_back(std::move(cumulative));
    }
    return t;
}

Turns& turns2e4() { static Turns t = buildTurns(fixture2e4(), 8); return t; }

std::size_t allTurnsBytes(Turns& t, bool perTurn)
{
    std::set<std::string> carried;
    std::size_t bytes = 0;
    for (std::size_t k = 0; k < t.indexes.size(); ++k)
    {
        SnapshotSpec& spec = perTurn ? t.specs[k] : t.inlineSpecs[k];
        spec.textsCarried = perTurn ? &carried : nullptr;
        bytes += layeredJson(spec, t.indexes[k]).size();
        spec.textsCarried = nullptr;
    }
    return bytes;
}

// ---- the commits of the incremental benchmark ---------------------------------------------------------------------------------------------------------

struct Incremental
{
    std::vector<bench::Commit> commits;
    BlockIndex index;       ///< the cold index of the corpus, then advanced one commit per timed iteration
    std::size_t next = 0;
};

constexpr std::size_t kCommits = 100, kBatch = 20;

Incremental& incremental()
{
    static Incremental inc = [] {
        Incremental i;
        const Fixture& f = fixture1e5();
        bench::EditStream stream(f.corpus, 11);
        for (std::size_t n = 0; n < kCommits; ++n) i.commits.push_back(stream.next());
        i.index = f.coldIndex();
        return i;
    }();
    return inc;
}

void applyCommit(BlockIndex& index, const bench::Commit& commit)
{
    for (const auto& change : commit.changes) index.update(change.path, change.text ? *change.text : std::string());
}

// ---- a retired set, for the revive scan ---------------------------------------------------------------------------------------------------------------

struct Revive
{
    BlockIndex index;                      ///< every block of the corpus retired (each file updated to empty)
    std::string novelPath, novelText;      ///< a file of functions unlike any retired one: every block scans the whole retired set and finds nothing
    std::size_t retired = 0, novelBlocks = 0;
};

Revive makeRevive(const Fixture& f)
{
    Revive r;
    r.index = f.coldIndex();
    const std::size_t before = r.index.all().size();
    for (const auto& [path, text] : f.corpus.files)
        if (isCode(path)) r.index.update(path, "");
    r.retired = before;
    const bench::Corpus novel = makeCorpus(600, 777);
    for (const auto& [path, text] : novel.files)
    {
        if (!path.ends_with(".cpp")) continue;
        r.novelPath = "novel/new.cpp";
        r.novelText = text;
        r.novelBlocks = foldRegions(text, false, true).size();
        break;
    }
    return r;
}

Revive& revive1e4() { static Revive r = makeRevive(fixture1e4()); return r; }
Revive& revive1e5() { static Revive r = makeRevive(fixture1e5()); return r; }

// ---- applyReply fixtures ------------------------------------------------------------------------------------------------------------------------------

struct Replies
{
    Canvas canvas;
    std::map<int, CanvasReply> byOps;
    std::set<std::string> allowed;
};

Replies makeReplies()
{
    Replies r;
    const Fixture& f = fixture1e5();
    std::vector<std::string> picked;
    for (const auto& path : f.cppPaths)
    {
        const std::string& text = f.corpus.files.at(path);
        if (std::count(text.begin(), text.end(), '\n') >= 200) picked.push_back(path);
        if (picked.size() == 5) break;
    }
    for (const auto& path : picked) { r.canvas.addFile(path, f.corpus.files.at(path)); r.allowed.insert(path); }
    for (const int ops : {1, 10, 50})
    {
        std::string text;
        for (int i = 0; i < ops; ++i)
        {
            const std::string& path = picked[static_cast<std::size_t>(i) % picked.size()];
            const int line = 5 + 3 * (i / static_cast<int>(picked.size()));   // disjoint lines per file
            text += "=== EDIT: " + path + " LINES " + std::to_string(line) + "-" + std::to_string(line) + " ===\n    // edited " + std::to_string(i) + "\n=== END EDIT ===\n";
        }
        r.byOps[ops] = parseCanvasReply(text);
    }
    return r;
}

Replies& replies() { static Replies r = makeReplies(); return r; }

void metricsOf(sub0bench::BenchRunner& runner, const std::string& prefix, std::size_t allocations)
{
    runner.metric((prefix + ": allocations").c_str(), static_cast<double>(allocations), "n", MetricKind::max);
}

}  // namespace

int main(int argc, char** argv)
{
    sub0bench::BenchRunner runner(argc, argv, "sub0canvas");
    ankerl::nanobench::Bench& bench = runner.bench();
    // The heavy rows: one operation is milliseconds to a second, so few iterations per epoch.
    auto heavy = [&](std::size_t epochs) { bench.warmup(1).epochs(epochs).minEpochIterations(1); };
    auto light = [&] { bench.warmup(100).epochs(11).minEpochIterations(10); };

    // BlockIndex cold build over every file of a corpus of about 1e5 lines.
    {
        heavy(5);
        Once once;
        runner.run("index cold build 1e5 lines", [&] {
            const Fixture& f = fixture1e5();
            once([&] {
                const std::size_t a0 = gAllocations, live0 = gLiveBytes;
                const BlockIndex index = f.coldIndex();
                const std::size_t allocs = gAllocations - a0, live = gLiveBytes - live0;
                const std::size_t blocks = index.all().size();
                runner.metric("corpus 1e5: lines", static_cast<double>(f.corpus.stats.codeLines), "lines");
                runner.metric("corpus 1e5: code files", static_cast<double>(f.corpus.stats.codeFiles), "files");
                runner.metric("corpus 1e5: blocks (generator truth)", static_cast<double>(f.corpus.stats.blocks), "blocks");
                runner.metric("index cold 1e5: blocks found", static_cast<double>(blocks), "blocks");
                runner.metric("index cold 1e5: events", static_cast<double>(index.events().size()), "n");
                metricsOf(runner, "index cold 1e5", allocs);
                runner.metric("index cold 1e5: live heap bytes", static_cast<double>(live), "B", MetricKind::max);
                // The held texts (`texts_`) are one copy of the sources; S1 drops them, so the per-block figure is given with and without.
                runner.metric("index cold 1e5: live heap bytes per block", static_cast<double>(live) / static_cast<double>(blocks), "B/block", MetricKind::max);
                runner.metric("index cold 1e5: heap bytes per block, held text removed",
                              static_cast<double>(live - std::min(live, f.textBytes + f.cppPaths.size() + f.pyPaths.size())) / static_cast<double>(blocks), "B/block",
                              MetricKind::max);
            });
            const BlockIndex index = f.coldIndex();
            ankerl::nanobench::doNotOptimizeAway(index);
        });
    }

    // BlockIndex incremental update: commits of an edit stream (1-4 edits to a few files each), kBatch per iteration so that one unusually large commit does not
    // dominate; the stream cycles after kCommits commits, and an epoch is whole laps (a later lap writes earlier texts back, which the index sees as changes like any other).
    {
        heavy(7);
        bench.minEpochIterations(kCommits / kBatch).batch(kBatch).unit("commit");   // an epoch is one whole lap of the stream: the same work every epoch
        Once once;
        runner.run("index incremental update, 1e5 lines, per commit", [&] {
            Incremental& inc = incremental();
            once([&] {
                BlockIndex copy = inc.index;
                const std::size_t events0 = copy.events().size(), a0 = gAllocations;
                std::size_t files = 0;
                for (const auto& commit : inc.commits) { applyCommit(copy, commit); files += commit.changes.size(); }
                runner.metric("incremental 100 commits: files changed", static_cast<double>(files), "files");
                runner.metric("incremental 100 commits: events", static_cast<double>(copy.events().size() - events0), "n");
                runner.metric("incremental 100 commits: live blocks after", static_cast<double>(copy.all().size()), "blocks");
                metricsOf(runner, "incremental 100 commits", gAllocations - a0);
            });
            for (std::size_t i = 0; i < kBatch; ++i)
            {
                applyCommit(inc.index, inc.commits[inc.next]);
                inc.next = (inc.next + 1) % inc.commits.size();
            }
        });
        bench.batch(1).unit("op");
    }

    // Warm resolve: ids and (path, line) pairs against a built index (built on first use, so a filtered run does not pay for it).
    {
        light();
        struct Warm
        {
            BlockIndex index = fixture1e5().coldIndex();
            std::vector<std::string> ids;
            Warm()
            {
                const auto all = index.all();
                for (std::size_t i = 0; i < all.size() && ids.size() < 1000; i += std::max<std::size_t>(1, all.size() / 1000)) ids.push_back(all[i]->id);
            }
        };
        static std::unique_ptr<Warm> warm;
        Once once;
        bench.batch(1000).unit("resolve");
        runner.run("index warm resolve and at, x1000", [&] {
            if (!warm) warm = std::make_unique<Warm>();
            once([&] {
                std::size_t found = 0;
                for (const auto& id : warm->ids) found += warm->index.resolve(id) != nullptr;
                runner.metric("warm resolve: ids sampled", static_cast<double>(warm->ids.size()), "n");
                runner.metric("warm resolve: ids resolved", static_cast<double>(found), "n");
            });
            std::size_t sum = 0;
            for (const auto& id : warm->ids)
            {
                const Block* b = warm->index.resolve(id);
                const Block* at = warm->index.at(b->path, (b->first + b->last) / 2);
                sum += static_cast<std::size_t>(b->first) + (at ? 1U : 0U);
            }
            ankerl::nanobench::doNotOptimizeAway(sum);
        });
        bench.batch(1).unit("op");
    }

    // foldRegions over every file of one language of the 1e5 corpus, each paired with a hand-written scan of the same text (the environment controls).
    {
        heavy(11);
        bench.minEpochIterations(3);
        Once onceCpp, oncePy;
        runner.run("foldRegions C++ 1e5 lines, hand-written brace scan", [&] {
            std::size_t r = braceScan(fixture1e5());
            ankerl::nanobench::doNotOptimizeAway(r);
        });
        runner.run("foldRegions C++ 1e5 lines, foldRegions()", [&] {
            const Fixture& f = fixture1e5();
            std::size_t regions = 0;
            for (const auto& path : f.cppPaths) regions += foldRegions(f.corpus.files.at(path), false, true).size();
            onceCpp([&] {
                runner.metric("foldRegions C++ 1e5: files", static_cast<double>(f.cppPaths.size()), "files");
                runner.metric("foldRegions C++ 1e5: regions", static_cast<double>(regions), "n");
            });
            ankerl::nanobench::doNotOptimizeAway(regions);
        });
        runner.run("foldRegions Python 1e5 lines, hand-written def scan", [&] {
            std::size_t r = defScan(fixture1e5());
            ankerl::nanobench::doNotOptimizeAway(r);
        });
        runner.run("foldRegions Python 1e5 lines, foldRegions()", [&] {
            const Fixture& f = fixture1e5();
            std::size_t regions = 0;
            for (const auto& path : f.pyPaths) regions += foldRegions(f.corpus.files.at(path), true, true).size();
            oncePy([&] {
                runner.metric("foldRegions Python 1e5: files", static_cast<double>(f.pyPaths.size()), "files");
                runner.metric("foldRegions Python 1e5: regions", static_cast<double>(regions), "n");
            });
            ankerl::nanobench::doNotOptimizeAway(regions);
        });
    }

    // applyReply: a canvas of five corpus files (copied each iteration, so the copy is in the time), a reply of N single-line replaceLines operations.
    for (const int ops : {1, 10, 50})
    {
        light();
        bench.minEpochIterations(5);
        Once once;
        const std::string name = "applyReply " + std::to_string(ops) + " ops";
        runner.run(name.c_str(), [&, ops] {
            Replies& r = replies();
            once([&] {
                Canvas canvas = r.canvas;
                ApplyOptions options;
                options.allowed = r.allowed;
                options.by = "bench";
                const std::size_t a0 = gAllocations;
                const auto applied = applyReply(canvas, r.byOps.at(ops), options);
                const std::string prefix = "applyReply " + std::to_string(ops) + " ops";
                metricsOf(runner, prefix, gAllocations - a0);
                runner.metric((prefix + ": applied").c_str(), applied ? 1.0 : 0.0, "bool");
                runner.metric((prefix + ": log entries").c_str(), static_cast<double>(canvas.log().size()), "n");
            });
            Canvas canvas = r.canvas;
            ApplyOptions options;
            options.allowed = r.allowed;
            options.by = "bench";
            auto applied = applyReply(canvas, r.byOps.at(ops), options);
            ankerl::nanobench::doNotOptimizeAway(applied);
        });
    }

    // layeredJson: one turn, and eight turns written per-turn (events from the previous snapshot on, texts by content id) or cumulatively (all events, every text).
    {
        heavy(7);
        Once once;
        runner.run("layeredJson 1 turn 2e4 lines", [&] {
            Turns& t = turns2e4();
            once([&] {
                const std::size_t a0 = gAllocations;
                SnapshotSpec spec = t.inlineSpecs[0];
                const std::string out = layeredJson(spec, t.indexes[0]);
                metricsOf(runner, "layeredJson 1 turn 2e4", gAllocations - a0);
                runner.metric("layeredJson 1 turn 2e4: output bytes", static_cast<double>(out.size()), "B");
                runner.metric("layeredJson 1 turn 2e4: files", static_cast<double>(t.inlineSpecs[0].files.size()), "files");
                runner.metric("layeredJson 1 turn 2e4: blocks", static_cast<double>(t.indexes[0].all().size()), "blocks");
            });
            auto out = layeredJson(t.inlineSpecs[0], t.indexes[0]);
            ankerl::nanobench::doNotOptimizeAway(out);
        });
        Once once8a, once8b;
        runner.run("layeredJson 8 turns 2e4 lines, per-turn events and texts by id", [&] {
            Turns& t = turns2e4();
            once8a([&] {
                const std::size_t a0 = gAllocations;
                const std::size_t bytes = allTurnsBytes(t, true);
                metricsOf(runner, "layeredJson 8 turns 2e4 per-turn", gAllocations - a0);
                runner.metric("layeredJson 8 turns 2e4: bytes, per-turn events, texts by id", static_cast<double>(bytes), "B");
                runner.metric("layeredJson 8 turns 2e4: files changed over the turns", static_cast<double>(t.filesChanged), "files");
            });
            auto bytes = allTurnsBytes(t, true);
            ankerl::nanobench::doNotOptimizeAway(bytes);
        });
        runner.run("layeredJson 8 turns 2e4 lines, cumulative events and inline texts", [&] {
            Turns& t = turns2e4();
            once8b([&] {
                const std::size_t a0 = gAllocations;
                const std::size_t bytes = allTurnsBytes(t, false);
                metricsOf(runner, "layeredJson 8 turns 2e4 cumulative", gAllocations - a0);
                runner.metric("layeredJson 8 turns 2e4: bytes, cumulative events, inline texts", static_cast<double>(bytes), "B");
                // The two intermediate forms: per-turn events with inline texts, and cumulative events with texts by id.
                std::size_t perTurnInline = 0, cumulativeById = 0;
                std::set<std::string> carried;
                for (std::size_t k = 0; k < t.indexes.size(); ++k)
                {
                    SnapshotSpec a = t.inlineSpecs[k];
                    a.eventsFrom = t.specs[k].eventsFrom;
                    perTurnInline += layeredJson(a, t.indexes[k]).size();
                    SnapshotSpec b = t.specs[k];
                    b.eventsFrom = 0;
                    b.textsCarried = &carried;
                    cumulativeById += layeredJson(b, t.indexes[k]).size();
                }
                runner.metric("layeredJson 8 turns 2e4: bytes, per-turn events, inline texts", static_cast<double>(perTurnInline), "B");
                runner.metric("layeredJson 8 turns 2e4: bytes, cumulative events, texts by id", static_cast<double>(cumulativeById), "B");
            });
            auto bytes = allTurnsBytes(t, false);
            ankerl::nanobench::doNotOptimizeAway(bytes);
        });
    }

    // The revive scan: a file of novel functions indexed while R blocks are retired; none can be revived, so each new block compares against every retired
    // one. The index is copied for each iteration (an update leaves the new blocks behind), so the copy is timed beside it and is the row to subtract.
    for (const bool large : {false, true})
    {
        heavy(large ? 5 : 7);
        Once once;
        const std::string label = large ? "1e5 lines" : "1e4 lines";
        const std::string copyName = "revive scan " + label + ", copy of the retired index only";
        const std::string scanName = "revive scan " + label + ", update of a file of novel blocks";
        auto rv = [large]() -> Revive& { return large ? revive1e5() : revive1e4(); };
        runner.run(copyName.c_str(), [&] {
            BlockIndex copy = rv().index;
            ankerl::nanobench::doNotOptimizeAway(copy);
        });
        runner.run(scanName.c_str(), [&] {
            Revive& r = rv();
            once([&] {
                BlockIndex copy = r.index;
                const std::size_t events0 = copy.events().size(), a0 = gAllocations;
                copy.update(r.novelPath, r.novelText);
                const std::string prefix = std::string("revive scan ") + (large ? "1e5" : "1e4");
                runner.metric((prefix + ": retired blocks").c_str(), static_cast<double>(r.retired), "blocks");
                runner.metric((prefix + ": novel blocks indexed").c_str(), static_cast<double>(r.novelBlocks), "blocks");
                runner.metric((prefix + ": events from the update").c_str(), static_cast<double>(copy.events().size() - events0), "n");
                metricsOf(runner, prefix, gAllocations - a0);
            });
            BlockIndex copy = r.index;
            copy.update(r.novelPath, r.novelText);
            ankerl::nanobench::doNotOptimizeAway(copy);
        });
    }
    light();

    return runner.finish();
}

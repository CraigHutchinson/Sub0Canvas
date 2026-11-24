// Source-anchor resolver benchmarks (slice B4, docs/BACKBONE.md section 5.1; budget: a warm resolution under 10 ms, docs/SCALE.md): 2,000 anchors made on body lines
// of a 1e5-line seeded corpus, resolved after 20 commits of its edit stream. The metrics are deterministic (the outcome counts, the candidates examined per
// resolution, the allocations per resolution) and gate in every tier; the two timed rows (cold: a fresh resolver, files prepared on first use; warm: a resolver whose
// files are prepared) are local only. Compiled into Sub0Canvas_Bench (bench_canvas.cpp calls `runResolverBenchmarks` and owns the counting operator new).
#include "bench_runner.hpp"

#include "bench/corpus.hpp"
#include "sub0canvas/anchor.hpp"
#include "sub0canvas/doc_layers.hpp"
#include "sub0canvas/resolver.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace sub0canvas;
using sub0bench::MetricKind;
namespace corpus = sub0canvas::bench;

namespace {

constexpr std::size_t kAnchors = 2000;
constexpr int kCommits = 20;

struct World
{
    BlockIndex index;
    std::map<std::string, std::string> files;     // the corpus after the commits
    std::vector<SourceAnchor> anchors;            // made on the starting corpus
};

/// The corpus, its anchors (every Nth function's first long, unique body line), then the commits applied to the index and the files.
std::unique_ptr<World> makeWorld()
{
    auto w = std::make_unique<World>();
    corpus::CorpusSpec spec;
    spec.seed = 1;
    spec.targetLines = 100'000;
    auto made = corpus::generateCorpus(spec);
    if (!made) return w;
    BlockIndex start;
    for (const auto& [path, text] : made->files) start.update(path, text);
    std::size_t functions = 0;
    for (const auto& [path, model] : made->models) functions += model.functions.size();
    const std::size_t stride = std::max<std::size_t>(1, functions / kAnchors);
    std::size_t n = 0;
    for (const auto& [path, model] : made->models) {
        const std::string& text = made->files.at(path);
        for (const corpus::Function& f : model.functions) {
            if (n++ % stride != 0 || w->anchors.size() >= kAnchors) continue;
            for (const std::string& body : f.body) {
                if (body.find_first_not_of(" {}") == std::string::npos || body.size() < 16) continue;
                const std::string needle = "\n" + body + "\n";
                const std::size_t first = text.find(needle);
                if (first == std::string::npos || text.find(needle, first + 1) != std::string::npos) continue;
                const int line = 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(first) + 1, '\n'));
                w->anchors.push_back(makeLineAnchor(path, "c0", text, line, &start));
                break;
            }
        }
    }
    // The same block ids in the resolving index: it is the starting index, updated by the commits.
    w->index = start;
    w->files = made->files;
    corpus::EditStream stream(*made, 5);
    for (int c = 0; c < kCommits; ++c) {
        corpus::Commit commit = stream.next();
        std::vector<const corpus::FileChange*> order;
        for (const corpus::FileChange& f : commit.changes) order.push_back(&f);
        const auto growth = [&](const corpus::FileChange* f) {
            const auto it = w->files.find(f->path);
            return static_cast<long>(f->text ? f->text->size() : 0) - (it == w->files.end() ? 0L : static_cast<long>(it->second.size()));
        };
        std::ranges::stable_sort(order, [&](const auto* x, const auto* y) { return growth(x) < growth(y); });
        for (const corpus::FileChange* f : order) {
            if (f->text) { w->files[f->path] = *f->text; w->index.update(f->path, *f->text); }
            else { w->files.erase(f->path); w->index.update(f->path, ""); }
        }
    }
    return w;
}

World& world()
{
    static const std::unique_ptr<World> w = makeWorld();
    return *w;
}

struct Once
{
    bool done = false;
    template <class F>
    void operator()(F&& body)
    {
        if (done) return;
        done = true;
        body();
    }
};

}  // namespace

void runResolverBenchmarks(sub0bench::BenchRunner& runner, const std::size_t& allocations, const std::size_t&)
{
    ankerl::nanobench::Bench& bench = runner.bench();
    World& w = world();
    const CanvasDocument view(w.index, w.files, "c20");
    Once once;

    // Cold: a fresh resolver, each file prepared (line table) the first time an anchor lands in it. The facts about the outcomes are taken from this run.
    bench.warmup(1).epochs(5).minEpochIterations(1);
    runner.run("resolver cold, 2000 anchors at 1e5 lines", [&] {
        AnchorResolver resolver(view);
        std::size_t resolved = 0, moved = 0, orphaned = 0, wrong = 0, maxCandidates = 0, editable = 0;
        const std::size_t a0 = allocations;
        for (const SourceAnchor& a : w.anchors) {
            const Resolution r = resolver.resolve(a);
            (r.outcome == Outcome::resolved ? resolved : r.outcome == Outcome::moved ? moved : orphaned)++;
            maxCandidates = std::max(maxCandidates, r.candidates);
            editable += r.editable ? 1 : 0;
            if (r.outcome == Outcome::resolved) {
                // A resolved location must hold the quote's exact bytes.
                const std::string& text = w.files.at(r.path);
                std::size_t off = 0;
                for (int l = 1; l < r.line; ++l) off = text.find('\n', off) + 1;
                off += static_cast<std::size_t>(r.column - 1);
                if (text.compare(off, a.quote.size(), a.quote) != 0) ++wrong;
            }
        }
        const std::size_t allocs = allocations - a0;
        once([&] {
            runner.metric("resolver 2e3 anchors at 1e5 lines: anchors", static_cast<double>(w.anchors.size()), "n");
            runner.metric("resolver 2e3 anchors at 1e5 lines: resolved", static_cast<double>(resolved), "n");
            runner.metric("resolver 2e3 anchors at 1e5 lines: moved", static_cast<double>(moved), "n");
            runner.metric("resolver 2e3 anchors at 1e5 lines: orphaned", static_cast<double>(orphaned), "n");
            runner.metric("resolver 2e3 anchors at 1e5 lines: resolved at a place that does not hold the quote", static_cast<double>(wrong), "n");
            runner.metric("resolver 2e3 anchors at 1e5 lines: editable", static_cast<double>(editable), "n");
            runner.metric("resolver 2e3 anchors at 1e5 lines: candidates examined, most in one resolution", static_cast<double>(maxCandidates), "n", MetricKind::max);
            runner.metric("resolver 2e3 anchors at 1e5 lines: candidates examined in all", static_cast<double>(resolver.stats().candidates), "n", MetricKind::max);
            runner.metric("resolver 2e3 anchors at 1e5 lines: fuzzy windows scored in all", static_cast<double>(resolver.stats().fuzzyWindows), "n", MetricKind::max);
            runner.metric("resolver 2e3 anchors at 1e5 lines: files prepared", static_cast<double>(resolver.stats().textsPrepared), "n", MetricKind::max);
            runner.metric("resolver 2e3 anchors at 1e5 lines: allocations per resolve", static_cast<double>(allocs) / static_cast<double>(w.anchors.size()), "n", MetricKind::max);
        });
        ankerl::nanobench::doNotOptimizeAway(resolved);
    });

    // Warm: the files are prepared; what remains is the search. The per-resolution time is the row's time over 2000.
    {
        AnchorResolver resolver(view);
        for (const SourceAnchor& a : w.anchors) (void)resolver.resolve(a);
        bench.warmup(1).epochs(7).minEpochIterations(1);
        runner.run("resolver warm, x2000 resolves at 1e5 lines", [&] {
            std::size_t sum = 0;
            for (const SourceAnchor& a : w.anchors) sum += resolver.resolve(a).line;
            ankerl::nanobench::doNotOptimizeAway(sum);
        });
    }
}

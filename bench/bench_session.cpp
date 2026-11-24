// Session and view benchmarks (docs/VIEWS.md; R-PIN-6, R-PIN-7, R-PIN-9): what a refresh costs against what changed in scope (events visited, bytes per form), the bound
// on a brief, views per session under the default quota, and the replay of 1e4 events that include sessions and views. Compiled into Sub0Canvas_Bench
// (bench_canvas.cpp calls `runSessionBenchmarks`). Everything is generated from loops: the same bytes on every machine.
#include "bench_runner.hpp"

#include "sub0canvas/session.hpp"
#include "sub0canvas/view.hpp"

#include <format>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace sub0canvas;

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

/// A file of `functions` small functions, each three lines: the blocks a refresh names.
std::string fileOf(int functions)
{
    std::string t;
    for (int i = 0; i < functions; ++i) t += std::format("int f{}() {{\n    return {};\n}}\n", i, i);
    return t;
}

struct Rig
{
    Canvas canvas;
    BlockIndex index;
    MemoryLog log;
    std::unique_ptr<Store> store;
    std::unique_ptr<LogViewStore> views;
    DataSessionPolicy policy;
    BlockCompactor compactor;
    std::unique_ptr<Workspace> ws;
    Rig(int functions)
    {
        StoreOptions o;
        o.writer = {"bench", "srv"};
        o.sink = &log;
        registerViewReducers(o.registry);
        store = std::make_unique<Store>(std::move(o));
        views = std::make_unique<LogViewStore>(*store);
        canvas.addFile("a.cpp", fileOf(functions));
        index.update("a.cpp", canvas.text("a.cpp"));
        ws = std::make_unique<Workspace>(canvas, *store, *views, policy, compactor, &index);
    }
    void edit(int n, const std::string& by)
    {
        const int line = 2 + 3 * (n % 50);
        (void)canvas.apply({"a.cpp", canvas.revision("a.cpp"), by, {CanvasOp{CanvasOp::Kind::replaceLines, line, line, "", std::format("    return {};\n", 1000 + n)}}});
    }
};

Identity who(const std::string& name) { return Identity{{"b", name}, {}}; }

}  // namespace

void runSessionBenchmarks(sub0bench::BenchRunner& runner, const std::size_t&, const std::size_t&)
{
    ankerl::nanobench::Bench& bench = runner.bench();
    bench.warmup(1).epochs(3).minEpochIterations(1);
    auto metric = [&](const std::string& name, std::size_t v, const char* unit) { runner.metric(name.c_str(), static_cast<double>(v), unit); };

    // A tip view over 50 blocks: 20 edits by 4 actors, and 1e4 store events of which 100 are in the view's layer. The refresh visits the events in scope, not the log;
    // the bytes of each form are the budget's input (brief stays flat as the edits grow).
    {
        static Once once;
        static Rig rig(50);
        static Session* session = [] {
            Identity alice = who("alice");
            auto id = rig.ws->open(alice, 0);
            return *rig.ws->attach(*id, alice, 0);
        }();
        static std::string tip, tipBrief;
        runner.run("session refresh of a tip view (50 blocks, 20 edits, 100 events in scope of 1e4)", [&] {
            once([&] {
                SubscriptionQuery q;
                q.layers = {"issues"};
                tip = *session->openView({.state = PinState::unpinned, .scope = q});
                tipBrief = *session->openView({.state = PinState::unpinned});
                for (int i = 0; i < 20; ++i) rig.edit(i, std::format("w{}", i % 4));
                for (int l = 0; l < 100; ++l) {
                    std::vector<LayerItem> items;
                    for (int i = 0; i < 100; ++i) {
                        LayerItem it;
                        it.key = std::format("k{}-{}", l, i);
                        it.kind = "error";
                        it.title = "t";
                        items.push_back(std::move(it));
                    }
                    (void)rig.store->putLayer(l == 0 ? "issues" : std::format("layer{}", l), "bench", items);
                }
                for (const DeltaForm f : {DeltaForm::brief, DeltaForm::elided, DeltaForm::compacted, DeltaForm::full}) {
                    const auto r = session->refresh(tip, {.form = f, .budget = DeltaBudget{.maxChars = std::size_t{1} << 30}});
                    metric(std::format("session refresh {}: bytes (20 edits, 100 events)", toString(f)), r->json.size(), "B");
                    if (f == DeltaForm::brief) {
                        metric("session refresh: store events visited (100 in scope of 1e4)", r->events, "n");
                        metric("session refresh: files in the delta", r->files, "n");
                    }
                }
                // Events per acknowledged refresh: the ack and the view record.
                const std::uint64_t before = rig.store->eventCount();
                (void)session->refresh(tip, {.acknowledge = true});
                metric("session refresh acknowledged: events written", static_cast<std::size_t>(rig.store->eventCount() - before), "n");
                // The brief does not follow the edit count.
                const std::size_t small = session->refresh(tipBrief, {.form = DeltaForm::brief})->json.size();
                for (int i = 20; i < 1020; ++i) rig.edit(i, std::format("w{}", i % 12));
                const std::size_t big = session->refresh(tipBrief, {.form = DeltaForm::brief})->json.size();
                runner.metric("session brief: bytes after 20 edits by 4 actors", static_cast<double>(small), "B", sub0bench::MetricKind::max);
                runner.metric("session brief: bytes after 1,020 edits by 12 actors", static_cast<double>(big), "B", sub0bench::MetricKind::max);
            });
            auto r = session->refresh(tip, {.form = DeltaForm::compacted});
            ankerl::nanobench::doNotOptimizeAway(r);
        });
    }

    // Views per session: the default quota, and the structured refusal after it. Then the replay of 1e4 events that include sessions and views.
    {
        static Once once;
        static std::unique_ptr<Rig> rig;
        runner.run("session replay of 1e4 events (sessions, views and item events)", [&] {
            once([&] {
                rig = std::make_unique<Rig>(10);
                std::size_t opened = 0;
                {
                    Identity who1 = who("solo");
                    Session* s = *rig->ws->attach(*rig->ws->open(who1, 0), who1, 0);
                    while (s->openView({}).has_value()) ++opened;
                    metric("session views per session under the default quota", opened, "n");
                    metric("session views refused at the limit (named limit: views)", s->openView({}).error().limit == "views" ? 1 : 0, "n");
                }
                for (int i = 0; i < 100; ++i) {
                    Identity id = who(std::format("u{}", i));
                    Session* s = *rig->ws->attach(*rig->ws->open(id, 0), id, 0);
                    for (int v = 0; v < 8; ++v) (void)s->openView({});
                }
                std::vector<LayerItem> items;
                for (int i = 0; items.size() < 4000; ++i) {
                    LayerItem it;
                    it.key = std::format("k{}", i);
                    it.kind = "error";
                    it.title = "t";
                    items.push_back(std::move(it));
                }
                (void)rig->store->putLayer("issues", "bench", items);
                while (rig->store->eventCount() < 10000) (void)rig->store->tag(std::format("k{}", rig->store->eventCount() % 4000), std::format("t{}", rig->store->eventCount()), "bench");
                // The replay's result against the live state, once (the timed part below is the replay alone).
                StoreOptions o;
                o.writer = {"bench", "replica"};
                registerViewReducers(o.registry);
                Store replica(std::move(o));
                replica.replay(rig->log);
                LogViewStore rv(replica);
                bool same = rv.sessions().size() == rig->views->sessions().size();
                std::size_t views = 0;
                for (const SessionRecord* s : rig->views->sessions())
                    for (const ViewRecord* v : rig->views->viewsOf(s->id)) {
                        ++views;
                        same = same && rv.view(v->id) != nullptr && viewJson(*rv.view(v->id)) == viewJson(*v);
                    }
                metric("session replay: events replayed", static_cast<std::size_t>(replica.eventCount()), "n");
                metric("session replay: sessions", rv.sessions().size(), "n");
                metric("session replay: views", views, "n");
                metric("session replay: views equal to live (1 = all)", same ? 1 : 0, "n");
            });
            StoreOptions o;
            o.writer = {"bench", "replica"};
            registerViewReducers(o.registry);
            Store replica(std::move(o));
            replica.replay(rig->log);
            ankerl::nanobench::doNotOptimizeAway(replica.eventCount());
        });
    }
}

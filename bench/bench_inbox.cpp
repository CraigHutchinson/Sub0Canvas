// Inbox benchmarks (slice B2, docs/BACKBONE.md section 3.4; R-BB-4): 1e5 events and 1e3 subscriptions, and the cost of reading an inbox against the events it returns,
// not the log's length. Compiled into Sub0Canvas_Bench (bench_canvas.cpp calls `runInboxBenchmarks` and owns the counting operator new, so the counters are passed
// in). Everything is generated from loops and arithmetic: the same bytes on every machine.
#include "bench_runner.hpp"

#include "sub0canvas/inbox.hpp"
#include "sub0canvas/store.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace sub0canvas;
using sub0bench::MetricKind;

namespace {

constexpr int kLayers = 1000;          // one subscription per layer
constexpr int kItemsPerLayer = 10;
constexpr int kEvents = 100000;        // item events; the subscriptions come after

std::string layerName(int l) { return "layer" + std::to_string(l); }
std::string keyOf(int l, int i) { return "k" + std::to_string(l) + "/" + std::to_string(i); }

/// A store of 1e4 items in 1e3 layers and 1e5 events in all (the puts, then tags and transitions spread over the items), and 1e3 subscriptions, one per layer.
std::unique_ptr<Store> makeStore()
{
    StoreOptions o;
    o.writer = {"bench", "w-1"};
    registerSubscriptionReducers(o.registry);
    auto s = std::make_unique<Store>(std::move(o));
    for (int l = 0; l < kLayers; ++l) {
        std::vector<ItemInput> items;
        for (int i = 0; i < kItemsPerLayer; ++i) {
            ItemInput in;
            in.item.key = keyOf(l, i);
            in.item.kind = "test";
            in.item.title = "case " + std::to_string(i) + " fails";
            Anchor a;
            a.blockId = "b" + std::to_string(l * kItemsPerLayer + i);
            a.path = "src/area" + std::to_string(l % 50) + "/file" + std::to_string(l) + ".cpp";
            in.item.anchors = {a};
            items.push_back(std::move(in));
        }
        (void)s->putLayer(layerName(l), "squad/gate", items);
    }
    for (int n = kLayers * kItemsPerLayer; n < kEvents; ++n) {
        const std::string key = keyOf((n * 7919) % kLayers, (n / kLayers) % kItemsPerLayer);
        if (n % 2 == 0) (void)s->emit("item.tag@1", key, R"({"tag":"owner/area)" + std::to_string(n % 97) + R"(","actor":"bench"})");
        else (void)s->emit("item.state@1", key, std::string{R"({"to":")"} + (n % 4 == 1 ? "held" : "open") + R"(","actor":"bench","reason":"r"})");
    }
    for (int l = 0; l < kLayers; ++l) {
        SubscriptionQuery q;
        q.layers = {layerName(l)};
        (void)subscribe(*s, "sub" + std::to_string(l), "agent" + std::to_string(l % 10), q);
    }
    return s;
}

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

}  // namespace

void runInboxBenchmarks(sub0bench::BenchRunner& runner, const std::size_t& allocations, const std::size_t& /*liveBytes*/)
{
    ankerl::nanobench::Bench& bench = runner.bench();
    auto heavy = [&](std::size_t epochs) { bench.warmup(1).epochs(epochs).minEpochIterations(1); };

    // Every subscription reads its inbox from the start of 1e5 events: the events returned add up to the log (every event is in exactly one layer), and the events
    // examined equal the events returned. Then each acknowledges: the inboxes are empty and examine nothing. Then 100 new events on ten layers: the next reads
    // return 100 in all and examine 100, whatever the log's length. The cost is the events returned, not the log or the subscriptions.
    {
        heavy(3);
        Once once;
        std::unique_ptr<Store> store = makeStore();
        runner.run("inbox cold read of one subscription (index built over 1e5 events)", [&] {
            once([&] {
                InboxReader reader(*store);
                std::size_t returned = 0, examined = 0, items = 0;
                const std::size_t a0 = allocations;
                for (int l = 0; l < kLayers; ++l) {
                    auto in = reader.inbox("sub" + std::to_string(l));
                    returned += in->events.size();
                    items += in->items.size();
                    examined += reader.lastCandidates();
                }
                const std::size_t allocsFirstPass = allocations - a0;
                runner.metric("inbox 1e5 events x 1e3 subscriptions: events indexed", static_cast<double>(reader.indexedEvents()), "n");
                runner.metric("inbox 1e5 events x 1e3 subscriptions: events returned", static_cast<double>(returned), "n");
                runner.metric("inbox 1e5 events x 1e3 subscriptions: items returned", static_cast<double>(items), "n");
                runner.metric("inbox 1e5 events x 1e3 subscriptions: events examined per event returned", static_cast<double>(examined) / static_cast<double>(returned), "n");
                runner.metric("inbox 1e5 events x 1e3 subscriptions: allocations per event returned, index build included", static_cast<double>(allocsFirstPass) / static_cast<double>(returned), "n",
                              MetricKind::max);

                for (int l = 0; l < kLayers; ++l) (void)acknowledge(*store, "sub" + std::to_string(l), store->head());
                std::size_t afterReturned = 0, afterExamined = 0;
                for (int l = 0; l < kLayers; ++l) {
                    auto in = reader.inbox("sub" + std::to_string(l));
                    afterReturned += in->events.size();
                    afterExamined += reader.lastCandidates();
                }
                runner.metric("inbox after acknowledge: events returned", static_cast<double>(afterReturned), "n");
                runner.metric("inbox after acknowledge: events examined", static_cast<double>(afterExamined), "n");

                for (int n = 0; n < 100; ++n) (void)store->emit("item.tag@1", keyOf((n % 10) * 3, n % kItemsPerLayer), R"({"tag":"fresh","actor":"bench"})");
                std::size_t nextReturned = 0, nextExamined = 0;
                const std::uint64_t eventsBefore = reader.indexedEvents();
                for (int l = 0; l < kLayers; ++l) {
                    auto in = reader.inbox("sub" + std::to_string(l));
                    nextReturned += in->events.size();
                    nextExamined += reader.lastCandidates();
                }
                runner.metric("inbox 100 new events: events returned", static_cast<double>(nextReturned), "n");
                runner.metric("inbox 100 new events: events examined", static_cast<double>(nextExamined), "n");
                runner.metric("inbox 100 new events: events indexed by the extension", static_cast<double>(reader.indexedEvents() - eventsBefore), "n");
            });
            // The timed work: the first read of one subscriber, which builds the index over the whole log.
            InboxReader reader(*store);
            auto in = reader.inbox("sub500");
            ankerl::nanobench::doNotOptimizeAway(in);
        });
    }

    // The same read through a warm reader: the index is built once, a read costs its events.
    {
        heavy(5);
        std::unique_ptr<Store> store = makeStore();
        InboxReader reader(*store);
        (void)reader.inbox("sub0");   // builds the index
        int next = 0;
        runner.run("inbox read, warm index, one layer of 1e5 events", [&] {
            auto in = reader.inbox("sub" + std::to_string(next++ % kLayers));
            ankerl::nanobench::doNotOptimizeAway(in);
        });
    }
}

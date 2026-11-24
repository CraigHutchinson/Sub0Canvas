// Store benchmarks (slice B1, docs/BACKBONE.md section 4; budgets: docs/SCALE.md and the review's section 5): replaying 1e5 events, appending in order (O(1)), a
// re-derived layer's cost against the items that changed, and heap per item. Compiled into Sub0Canvas_Bench (bench_canvas.cpp calls `runStoreBenchmarks` and owns the
// counting operator new, so the counters are passed in). Everything is generated from loops and arithmetic: the same bytes on every machine.
#include "bench_runner.hpp"

#include "sub0canvas/event.hpp"
#include "sub0canvas/store.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace sub0canvas;
using sub0bench::MetricKind;

namespace {

constexpr int kItems = 10000;
constexpr int kEvents = 100000;

std::vector<ItemInput> makeItems(int n)
{
    std::vector<ItemInput> items;
    items.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        ItemInput in;
        in.item.key = "squad/issues/test/case" + std::to_string(i);
        in.item.kind = "test";
        in.item.title = "case " + std::to_string(i) + " fails";
        in.item.detail = "FAIL " + std::to_string(i) + " - modulo: expected 3, got 4";
        Anchor a;
        a.blockId = "b" + std::to_string(i);
        a.path = "src/file" + std::to_string(i % 200) + ".cpp";
        a.line = 10 + i % 90;
        a.confidence = Anchor::Confidence::suspect;
        in.item.anchors = {a};
        in.data = R"({"durationMs":)" + std::to_string(i % 500) + "}";
        items.push_back(std::move(in));
    }
    return items;
}

/// The log of a store with kItems items and `kEvents` events in all: the puts, then tags, transitions and links spread over the items.
std::unique_ptr<MemoryLog> makeLog()
{
    auto log = std::make_unique<MemoryLog>();
    StoreOptions o;
    o.writer = {"bench", "w-1"};
    o.sink = log.get();
    Store s(o);
    (void)s.putLayer("issues", "squad/gate", makeItems(kItems));
    for (int i = kItems; i < kEvents; ++i) {
        const std::string key = "squad/issues/test/case" + std::to_string((i * 7919) % kItems);
        switch (i % 3) {
            case 0: (void)s.emit("item.tag@1", key, R"({"tag":"owner/area)" + std::to_string(i % 97) + R"(","actor":"bench"})"); break;
            case 1: (void)s.emit("item.state@1", key, std::string{R"({"to":")"} + (i % 2 ? "held" : "open") + R"(","actor":"bench","reason":"r"})"); break;
            default: (void)s.emit("item.link@1", key, R"({"kind":"causes","key":"squad/issues/test/case)" + std::to_string(i % kItems) + R"(","actor":"bench"})"); break;
        }
    }
    return log;
}

const MemoryLog& sharedLog()
{
    static const std::unique_ptr<MemoryLog> log = makeLog();
    return *log;
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

void runStoreBenchmarks(sub0bench::BenchRunner& runner, const std::size_t& allocations, const std::size_t& liveBytes)
{
    ankerl::nanobench::Bench& bench = runner.bench();
    auto heavy = [&](std::size_t epochs) { bench.warmup(1).epochs(epochs).minEpochIterations(1); };

    // Replay of 1e5 events into a fresh store: merge order, fold, and the snapshot against the live store's (R-BB-1).
    {
        heavy(5);
        Once once;
        runner.run("store replay 1e5 events", [&] {
            const MemoryLog& log = sharedLog();
            once([&] {
                const std::size_t a0 = allocations, live0 = liveBytes;
                auto replayed = std::make_unique<Store>();
                replayed->replay(log);
                const std::size_t allocs = allocations - a0, live = liveBytes - live0;
                StoreOptions o;
                o.writer = {"bench", "w-1"};
                Store again(o);
                again.merge(log.events());   // a second path to the same store, to compare against
                runner.metric("store replay 1e5: events", static_cast<double>(replayed->events().size()), "n");
                runner.metric("store replay 1e5: items", static_cast<double>(replayed->items().size()), "n");
                runner.metric("store replay 1e5: replays agree", replayed->snapshotJson() == again.snapshotJson() ? 1.0 : 0.0, "bool");
                runner.metric("store replay 1e5: allocations", static_cast<double>(allocs), "n", MetricKind::max);
                runner.metric("store replay 1e5: live heap bytes", static_cast<double>(live), "B", MetricKind::max);
                runner.metric("store replay 1e5: live heap bytes per item", static_cast<double>(live) / static_cast<double>(replayed->items().size()), "B/item", MetricKind::max);
            });
            Store s;
            s.replay(log);
            ankerl::nanobench::doNotOptimizeAway(s);
        });
    }

    // Append in order is O(1): the store's own writes fold in place. One round makes 1e4 events, so the time per round is the cost of 1e4 appends.
    {
        heavy(7);
        runner.run("store emit 1e4 tags in order", [&] {
            Store s;
            (void)s.putLayer("issues", "squad/gate", makeItems(100));
            for (int i = 0; i < 10000; ++i) (void)s.emit("item.tag@1", "squad/issues/test/case" + std::to_string(i % 100), R"({"tag":"t","actor":"b"})");
            ankerl::nanobench::doNotOptimizeAway(s);
        });
    }

    // putLayer cost against the items that changed: a layer of 1e4 items resubmitted with none, 10 and 1000 changed. Events and allocations follow the changes,
    // not the layer's size (the pass over the layer is allocation-free).
    {
        heavy(7);
        Once once;
        Store s;
        std::vector<ItemInput> items = makeItems(kItems);
        (void)s.putLayer("issues", "squad/gate", items);
        int round = 0;
        auto resubmit = [&](int changed) {
            ++round;
            for (int i = 0; i < changed; ++i) items[static_cast<std::size_t>(i) * 7 % kItems].item.title = "round " + std::to_string(round) + " item " + std::to_string(i);
            return s.putLayer("issues", "squad/gate", items);
        };
        runner.run("store putLayer 1e4 items 10 changed", [&] {
            once([&] {
                auto measure = [&](const char* name, int changed) {
                    const std::size_t before = s.events().size(), a0 = allocations;
                    const PutResult r = resubmit(changed);
                    const std::size_t allocs = allocations - a0;
                    const std::string prefix = std::string{"store putLayer 1e4 items, "} + name;
                    runner.metric((prefix + ": events").c_str(), static_cast<double>(s.events().size() - before), "n");
                    runner.metric((prefix + ": items changed").c_str(), static_cast<double>(r.changed), "n");
                    runner.metric((prefix + ": allocations").c_str(), static_cast<double>(allocs), "n", MetricKind::max);
                };
                measure("none changed", 0);
                measure("10 changed", 10);
                measure("1000 changed", 1000);
            });
            const PutResult r = resubmit(10);
            ankerl::nanobench::doNotOptimizeAway(r);
        });
    }

    // The snapshot of 1e4 items.
    {
        heavy(7);
        Store s;
        s.replay(sharedLog());
        runner.run("store snapshotJson 1e4 items", [&] {
            const std::string out = s.snapshotJson();
            ankerl::nanobench::doNotOptimizeAway(out);
        });
    }
}

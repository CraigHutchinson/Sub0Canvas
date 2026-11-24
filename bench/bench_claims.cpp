// Claim benchmarks (slice B3, docs/BACKBONE.md section 3.5; budgets: docs/SCALE.md): looking up the claims on one target and checking a conflicting claim must
// not depend on how many claims the store holds (a hash lookup on the target's key, then the claims on that one target), replaying 1e5 claims, and block claims
// following merges. Compiled into Sub0Canvas_Bench (bench_canvas.cpp calls `runClaimsBenchmarks` and owns the counting operator new). Everything is generated from
// loops and arithmetic: the same bytes on every machine, and every instant is a number (no clock).
#include "bench_runner.hpp"

#include "sub0canvas/claims.hpp"
#include "sub0canvas/event.hpp"
#include "sub0canvas/store.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace sub0canvas;
using sub0bench::MetricKind;

namespace {

constexpr int kClaims = 100000;
constexpr Instant kNow = 1500;   // claim i is taken at 1000 + i % 1000 for 300 s: live at kNow when i % 1000 > 200, so 799 of every 1000

const WriterId kHolder{"bench", "holder"}, kOther{"bench", "other"};

StoreOptions options(const WriterId& who, Log* sink = nullptr)
{
    StoreOptions o;
    o.writer = who;
    o.sink = sink;
    (void)registerClaimReducers(o.registry);
    return o;
}

std::string itemKey(int i) { return "squad/issues/test/case" + std::to_string(i); }

ClaimRequest itemRequest(int i)
{
    ClaimRequest r;
    r.target = itemKey(i);
    return r;
}

/// A log of `n` write claims on `n` distinct items, taken at 1000 + i % 1000.
std::unique_ptr<MemoryLog> makeLog(int n)
{
    auto log = std::make_unique<MemoryLog>();
    Store s(options(kHolder, log.get()));
    const ClaimPolicy policy = ClaimPolicy::standard();
    for (int i = 0; i < n; ++i) (void)takeClaim(s, policy, kHolder, itemRequest(i), 1000 + i % 1000);
    return log;
}

const MemoryLog& sharedLog()
{
    static const std::unique_ptr<MemoryLog> log = makeLog(kClaims);
    return *log;
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

void runClaimsBenchmarks(sub0bench::BenchRunner& runner, const std::size_t& allocations, const std::size_t& liveBytes)
{
    ankerl::nanobench::Bench& bench = runner.bench();
    auto heavy = [&](std::size_t epochs) { bench.warmup(1).epochs(epochs).minEpochIterations(1); };
    const ClaimPolicy policy = ClaimPolicy::standard();

    // Replay of 1e5 claims into a fresh store, the facts about the result, and the memory it holds.
    {
        heavy(5);
        Once once;
        runner.run("claims replay 1e5 takes", [&] {
            const MemoryLog& log = sharedLog();
            once([&] {
                const std::size_t a0 = allocations, live0 = liveBytes;
                auto replica = std::make_unique<Store>(options(kOther));
                replica->replay(log);
                const std::size_t allocs = allocations - a0, live = liveBytes - live0;
                Store again(options(kOther));
                again.merge(log.events());   // a second path to the same store, to compare against
                runner.metric("claims replay 1e5: events", static_cast<double>(replica->events().size()), "n");
                runner.metric("claims replay 1e5: claims", static_cast<double>(claimCount(*replica)), "n");
                runner.metric("claims replay 1e5: replays agree", claimsJson(*replica, kNow) == claimsJson(again, kNow) ? 1.0 : 0.0, "bool");
                runner.metric("claims replay 1e5: allocations", static_cast<double>(allocs), "n", MetricKind::max);
                runner.metric("claims replay 1e5: live heap bytes per claim", static_cast<double>(live) / kClaims, "B/claim", MetricKind::max);
            });
            Store s(options(kOther));
            s.replay(log);
            ankerl::nanobench::doNotOptimizeAway(s);
        });
    }

    // Lookup and conflict check at 1e5 claims (the metrics) and the time of the same number of each at 1e3 and at 1e5: the two must match, which is the O(1) claim.
    {
        Store big(options(kOther));
        big.replay(sharedLog());
        Store small(options(kOther));
        small.replay(*makeLog(1000));
        Once once;
        auto lookups = [&](const Store& s, int n) {
            std::size_t live = 0;
            for (int i = 0; i < n; ++i) live += claimsOn(s, TargetKind::item, itemKey(i), kNow).size();
            return live;
        };
        auto checks = [&](const Store& s, int n) {
            std::size_t refused = 0;
            for (int i = 0; i < n; ++i) refused += checkClaim(s, policy, kOther, itemRequest(i), kNow).has_value() ? 0 : 1;
            return refused;
        };
        heavy(7);
        runner.run("claims lookup 1e3 targets at 1e5 claims", [&] {
            once([&] {
                const std::size_t a0 = allocations;
                const std::size_t live = lookups(big, 1000);
                const std::size_t allocs = allocations - a0;
                runner.metric("claims lookup 1e5: live claims found in 1000 lookups", static_cast<double>(live), "n");
                runner.metric("claims lookup 1e5: allocations per lookup", static_cast<double>(allocs) / 1000.0, "n", MetricKind::max);
                runner.metric("claims lookup 1e5: live claims found in all", static_cast<double>(lookups(big, kClaims)), "n");
                const std::size_t c0 = allocations;
                const std::size_t refused = checks(big, kClaims);
                runner.metric("claims check 1e5: conflicts refused", static_cast<double>(refused), "n");
                runner.metric("claims check 1e5: allocations per check", static_cast<double>(allocations - c0) / kClaims, "n", MetricKind::max);
            });
            ankerl::nanobench::doNotOptimizeAway(lookups(big, 1000));
        });
        heavy(7);
        runner.run("claims lookup 1e3 targets at 1e3 claims", [&] { ankerl::nanobench::doNotOptimizeAway(lookups(small, 1000)); });
        heavy(7);
        runner.run("claims check 1e3 targets at 1e5 claims", [&] { ankerl::nanobench::doNotOptimizeAway(checks(big, 1000)); });
        heavy(7);
        runner.run("claims check 1e3 targets at 1e3 claims", [&] { ankerl::nanobench::doNotOptimizeAway(checks(small, 1000)); });
    }

    // Block claims following merges: 1e4 block claims, then block 2k merged into 2k+1 for the first 5000 pairs; each survivor then holds both claims.
    {
        heavy(5);
        Once once;
        runner.run("claims 1e4 block claims, 5e3 merges", [&] {
            Store s(options(kHolder));
            for (int i = 0; i < 10000; ++i) {
                ClaimRequest r;
                r.kind = TargetKind::block;
                r.target = "b" + std::to_string(i);
                (void)takeClaim(s, policy, kHolder, r, 1000);
            }
            std::vector<BlockEvent> merges;
            for (int k = 0; k < 5000; ++k) merges.push_back(BlockEvent{Carry::merged, "b" + std::to_string(2 * k), "src/a.cpp", "f", "b" + std::to_string(2 * k + 1), 1});
            const std::size_t a0 = allocations;
            const std::size_t written = emitBlockCarries(s, merges);
            const std::size_t allocs = allocations - a0;
            once([&] {
                std::size_t onSurvivors = 0, viaOldIds = 0;
                for (int k = 0; k < 5000; ++k) {
                    onSurvivors += claimsOn(s, TargetKind::block, "b" + std::to_string(2 * k + 1), 1100).size();
                    viaOldIds += claimsOn(s, TargetKind::block, "b" + std::to_string(2 * k), 1100).size();
                }
                runner.metric("claims merges 5e3: carries written", static_cast<double>(written), "n");
                runner.metric("claims merges 5e3: claims found on the survivors", static_cast<double>(onSurvivors), "n");
                runner.metric("claims merges 5e3: claims found by the old ids", static_cast<double>(viaOldIds), "n");
                runner.metric("claims merges 5e3: allocations per merge", static_cast<double>(allocs) / 5000.0, "n", MetricKind::max);
            });
            ankerl::nanobench::doNotOptimizeAway(s);
        });
    }
}

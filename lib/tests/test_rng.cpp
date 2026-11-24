// The shared deterministic generator (bench/rng.hpp, A1 of docs/sessions/cxx23-adoption.md). The pinned values were captured from the generator as it stood in
// bench/corpus.cpp before it moved there, so a change to them moves the corpus bytes (fixtures, perf/metrics.json): that is what this test is for.
#include "sub0platform/testing/check.hpp"
#include "bench/rng.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>

using namespace sub0canvas::rng;

namespace {

constexpr std::size_t kDraws = 8;

/// The first kDraws outputs of a generator, each draw its own statement.
std::array<std::uint64_t, kDraws> firstOutputs(std::uint64_t seed)
{
    SplitMix64 g(seed);
    std::array<std::uint64_t, kDraws> out{};
    for (std::uint64_t& v : out) v = g.next();
    return out;
}

}   // namespace

// The published SplitMix64 reference: seed 0 first gives 0xE220A8397B1DCDAF, and the generator is usable at compile time.
static_assert(SplitMix64{0}.next() == 0xE220A8397B1DCDAFull);
static_assert(mix(42, 0xED17) == 0x284d798cfb955936ull);

int main()
{
    using Outputs = std::array<std::uint64_t, kDraws>;
    {   // the outputs of seeds 0, 1 and 42, and of the corpus's edit-stream seed
        const Outputs seed0 = {0xe220a8397b1dcdafull, 0x6e789e6aa1b965f4ull, 0x06c45d188009454full, 0xf88bb8a8724c81ecull,
                               0x1b39896a51a8749bull, 0x53cb9f0c747ea2eaull, 0x2c829abe1f4532e1ull, 0xc584133ac916ab3cull};
        const Outputs seed1 = {0x910a2dec89025cc1ull, 0xbeeb8da1658eec67ull, 0xf893a2eefb32555eull, 0x71c18690ee42c90bull,
                               0x71bb54d8d101b5b9ull, 0xc34d0bff90150280ull, 0xe099ec6cd7363ca5ull, 0x85e7bb0f12278575ull};
        const Outputs seed42 = {0xbdd732262feb6e95ull, 0x28efe333b266f103ull, 0x47526757130f9f52ull, 0x581ce1ff0e4ae394ull,
                                0x09bc585a244823f2ull, 0xde4431fa3c80db06ull, 0x37e9671c45376d5dull, 0xccf635ee9e9e2fa4ull};
        const Outputs mixed = {0xcc51b45db290f83aull, 0x9121e681a549f33aull, 0xe17c21f4a892ded7ull, 0x7cf541dd98d26176ull,
                               0x18ba441c674749d6ull, 0x54820ea99fc950d0ull, 0x0985ce309e43a739ull, 0xb37c1cd375471378ull};
        const Outputs got0 = firstOutputs(0), got1 = firstOutputs(1), got42 = firstOutputs(42);
        const Outputs gotMixed = firstOutputs(mix(42, 0xED17));
        CHECK(got0 == seed0);
        CHECK(got1 == seed1);
        CHECK(got42 == seed42);
        CHECK(gotMixed == mixed);
    }
    {   // the frozen picks for seed 42 (below is a modulo, range and percent are built on it, skewed is two below draws)
        constexpr std::array<std::size_t, kDraws> belowWant = {3, 1, 8, 4, 0, 2, 5, 8};
        constexpr std::array<std::size_t, kDraws> rangeWant = {8, 8, 3, 5, 9, 7, 5, 9};
        constexpr std::array<bool, kDraws> percentWant = {true, false, false, false, false, false, true, true};
        constexpr std::array<std::size_t, kDraws> skewedWant = {11, 37, 31, 2, 3, 3, 93, 16};
        std::array<std::size_t, kDraws> belowGot{}, rangeGot{}, skewedGot{};
        std::array<bool, kDraws> percentGot{};
        SplitMix64 b(42), r(42), p(42), s(42);
        for (std::size_t i = 0; i < kDraws; ++i) {
            belowGot[i] = b.below(10);
            rangeGot[i] = r.range(3, 9);
            percentGot[i] = p.percent(30);
            skewedGot[i] = s.skewed(100);
        }
        CHECK(belowGot == belowWant);
        CHECK(rangeGot == rangeWant);
        CHECK(percentGot == percentWant);
        CHECK(skewedGot == skewedWant);
        SplitMix64 zero(1);
        CHECK_EQ(zero.below(0), std::size_t{0});
        CHECK_EQ(zero.uniform(0), std::size_t{0});
    }
    {   // uniform: in range, deterministic, and (for n that is a power of two or small) the same first picks as the modulo while no draw is rejected
        SplitMix64 a(42), b(42);
        bool inRange = true, same = true;
        for (std::size_t i = 0; i < 1000; ++i) {
            const std::size_t n = 1 + i % 37;
            const std::size_t x = a.uniform(n);
            const std::size_t y = b.uniform(n);
            inRange = inRange && x < n;
            same = same && x == y;
        }
        CHECK(inRange);
        CHECK(same);
        SplitMix64 g(42);
        constexpr std::array<std::size_t, kDraws> uniformWant = {3, 1, 8, 4, 0, 2, 5, 8};
        std::array<std::size_t, kDraws> uniformGot{};
        for (std::size_t& v : uniformGot) v = g.uniform(10);
        CHECK(uniformGot == uniformWant);
    }
    {   // shuffle: a permutation, deterministic, pinned for seed 7 over iota(0, 10), and the empty and single-element ranges are untouched
        std::array<int, 10> v{};
        std::iota(v.begin(), v.end(), 0);
        SplitMix64 g(7);
        shuffle(v, g);
        constexpr std::array<int, 10> want = {8, 1, 5, 9, 0, 4, 3, 2, 6, 7};
        CHECK(v == want);
        std::array<int, 10> sorted = v;
        std::ranges::sort(sorted);
        std::array<int, 10> identity{};
        std::iota(identity.begin(), identity.end(), 0);
        CHECK(sorted == identity);
        std::array<int, 1> one = {5};
        shuffle(one, g);
        CHECK_EQ(one[0], 5);
    }
    return finish();
}

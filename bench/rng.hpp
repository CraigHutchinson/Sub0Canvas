// The one deterministic generator of the benchmark corpus and the tests (AGENTS.md "Code style", docs/MOCK_SQUAD.md section 8): SplitMix64 and the integer-only picks built on it.
// Header-only and never part of lib/ (the library is pure: it draws nothing). No `<random>` distribution is used anywhere on purpose: their mapping from bits to values
// is implementation-defined, so the same seed would give different bytes on libstdc++ and libc++. Here the same seed gives the same values everywhere.
#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ranges>
#include <utility>

namespace sub0canvas::rng {

inline constexpr std::uint64_t kGolden = 0x9E3779B97F4A7C15ull;

/// SplitMix64 (Steele, Lea and Flood 2014): a 64-bit state advanced by the golden-ratio constant and scrambled on output. Seed 0 first gives 0xE220A8397B1DCDAF
/// (the published reference). It satisfies `std::uniform_random_bit_generator`, but use the picks below rather than a `<random>` distribution.
///
/// `next`, `below`, `range`, `percent` and `skewed` are **frozen**: the corpus bytes (fixtures, perf/metrics.json) depend on them, `below` being `next() % n` with its
/// modulo bias. Use `uniform` for anything new. Never put two draws in one expression or argument list: their evaluation order is unspecified and differs between
/// compilers (docs/MOCK_SQUAD.md, section 8); every draw is its own statement.
class SplitMix64
{
public:
    using result_type = std::uint64_t;

    constexpr explicit SplitMix64(std::uint64_t seed = 0) noexcept : state_(seed) {}

    [[nodiscard]] static constexpr result_type min() noexcept { return 0; }
    [[nodiscard]] static constexpr result_type max() noexcept { return std::numeric_limits<result_type>::max(); }

    /// The next 64 random bits.
    constexpr result_type next() noexcept
    {
        state_ += kGolden;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    constexpr result_type operator()() noexcept { return next(); }

    /// A pick in [0, n) by modulo (0 when n is 0). Frozen, biased for n that does not divide 2^64: the corpus depends on it.
    [[nodiscard]] constexpr std::size_t below(std::size_t n) noexcept { return n == 0 ? 0 : static_cast<std::size_t>(next() % n); }
    /// A pick in [lo, hi], inclusive. Frozen.
    [[nodiscard]] constexpr std::size_t range(std::size_t lo, std::size_t hi) noexcept
    {
        const std::size_t offset = below(hi - lo + 1);
        return lo + offset;
    }
    /// True with probability p percent. Frozen.
    [[nodiscard]] constexpr bool percent(std::size_t p) noexcept { return below(100) < p; }
    /// A pick skewed toward low indexes (the product of two uniform picks): a few common words, a long tail of rare ones. Frozen: two draws, first factor first.
    [[nodiscard]] constexpr std::size_t skewed(std::size_t n) noexcept
    {
        const std::size_t a = below(n);
        const std::size_t b = below(n);
        return a * b / n;
    }

    /// An unbiased pick in [0, n) by rejection (0 when n is 0), for new uses only; the number of draws it takes varies, but is a function of the seed alone.
    [[nodiscard]] constexpr std::size_t uniform(std::size_t n) noexcept
    {
        if (n == 0) return 0;
        const std::uint64_t limit = max() - (max() % n + 1) % n;   // the largest multiple of n, less one: draws above it would favour the low values
        std::uint64_t v = next();
        while (v > limit) v = next();
        return static_cast<std::size_t>(v % n);
    }

private:
    std::uint64_t state_;
};

static_assert(std::uniform_random_bit_generator<SplitMix64>);

/// A seed derived from two values (the corpus's per-file and per-stream seeds): the second output of a generator started at `a + kGolden * (b + 1)`.
[[nodiscard]] constexpr std::uint64_t mix(std::uint64_t a, std::uint64_t b) noexcept
{
    SplitMix64 r(a + kGolden * (b + 1));
    (void)r.next();
    return r.next();
}

/// A deterministic Fisher-Yates shuffle (`std::shuffle`'s mapping is implementation-defined): from the back, each element swaps with a pick in [0, i] by `uniform`.
template <std::ranges::random_access_range R>
    requires std::permutable<std::ranges::iterator_t<R>>
constexpr void shuffle(R&& r, SplitMix64& g)
{
    const auto n = static_cast<std::size_t>(std::ranges::distance(r));
    for (std::size_t i = n; i > 1; --i) {
        const std::size_t j = g.uniform(i);
        std::ranges::iter_swap(std::ranges::begin(r) + static_cast<std::ptrdiff_t>(i - 1), std::ranges::begin(r) + static_cast<std::ptrdiff_t>(j));
    }
}

}   // namespace sub0canvas::rng

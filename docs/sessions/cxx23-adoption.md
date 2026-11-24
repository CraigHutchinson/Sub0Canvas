# Follow-up session: C++23 adopted throughout, the RNG included

Status: open (planned 2026-11-24 by the planning agent; slices A0-A3 done, see the Outcome). Backlog: [phase 1](../BACKLOG.md). Parent: [backbone-next-wave.md](backbone-next-wave.md).

## Goal

Use the C++23 (and C++20) library where it shortens or hardens the code, **without moving a byte of output** (fixtures/mock, perf/metrics.json, the corpus) and without
regressing the perf gate. Floor: gcc 14 (Linux CI, `-Werror`), clang 19 + libstdc++ (Linux CI), MinGW gcc 16 (local), clang 22 (Windows; state in AGENTS.md whether it uses
libstdc++ or libc++, check with `clang++ -v`). Issue 4 (evaluation order, [MOCK_SQUAD.md](../MOCK_SQUAD.md) section 8) is the reason bytes are the line.

## Found by the mining (counts are text matches, 2026-11-24)

No ranges or views, no `std::span`, no `std::print`/`println`, no `std::to_underlying`, no `if consteval`; 224 raw index loops (lib/src 89, tests 69, bench 52); 104 iterator-pair
algorithm calls; 43 stringstream uses; 208 `std::to_string`; 6 `snprintf` (doc_layers.cpp:79 and :1182, outline.cpp:27); 92 `std::optional`; about 18 public value-returning functions without
`[[nodiscard]]` (claims.hpp:124,128,130,133,136; inbox.hpp:65; query.hpp:101; session.hpp:300; store.hpp:105,109,111,150,218,220,226,227,284; view.hpp:173); 37 `const char*` parameters.
RNG: one hand-written SplitMix64 `Rng` (bench/corpus.cpp:12-38; `below` = `next() % n`, frozen: the corpus bytes depend on it); two `std::mt19937` sites in tests (test_canvas_model.cpp:90,
test_inbox.cpp:276-277, the latter with `std::shuffle`, whose mapping is implementation-defined); `std::random_device` in app/ only names temp files (stays).

**Two things the mining missed:** `lib/src/doc_layers.cpp:1182` builds item keys with `std::hash<std::string>` (implementation-defined: libstdc++ and libc++ differ; the CI compilers happen to agree), and
`app/sub0canvas.cpp:42` avoids `std::println` because of "GCC 13": gcc 14 is the floor, so state that in AGENTS.md.

## Slices (each leaves `ctest --preset quick` green and `git diff fixtures/ perf/` empty unless stated)

- **A0** `doc_layers.cpp:1182`: replace `std::hash` with a fixed hash (FNV-1a 64, or the existing content id). The only slice that may move bytes (`issues/<kind>/<hex>` keys): check
  first whether those keys reach fixtures/mock or golden; if so, a reviewed fixture regeneration with a mechanical diff check (as in MOCK_SQUAD.md section 8), standing alone in its commit.
- **A1** `bench/rng.hpp`, namespace `sub0canvas::rng`, header-only, **never in lib/** (the library is pure): `SplitMix64` (constexpr, noexcept, satisfies `std::uniform_random_bit_generator`) with
  `next` (the exact body of corpus.cpp:25-31), frozen `below`/`range`/`percent`/`skewed` (each draw its own statement), a new unbiased `uniform(n)` (rejection) **for new uses only**, `mix`, and a
  deterministic Fisher-Yates `shuffle` over `std::ranges::random_access_range`. An INTERFACE target `sub0canvas_rng` linked by `sub0canvas_bench_corpus` and the tests; `using Rng = rng::SplitMix64;`
  in corpus.cpp keeps its call sites. `lib/tests/test_rng.cpp` pins: `static_assert(SplitMix64{0}.next() == 0xE220A8397B1DCDAFull)` (the published reference), the first 8 outputs for seeds 0, 1, 42
  and `mix(42, 0xED17)` **captured from the current corpus.cpp code before moving it**, below/range/skewed for seed 42, `uniform(n)` in range, a pinned shuffle of `iota(0,10)` at seed 7.
  Verify: `test_corpus`, `Sub0Canvas_Bench_metrics`, empty `git diff perf/ fixtures/`.
- **A2** test_inbox (shuffle) and test_canvas_model move to the shared generator (grep test_canvas_model for pinned case counts or seed expectations first; if any, leave that site).
- **A3** `tools/check_cxx_rules.cmake` (run with `cmake -P`, a quick-tier ctest, no new dependency) failing on: `std::mt19937|std::shuffle|std::random_device|<random>` outside app/;
  `std::hash<` in lib/src; `snprintf|sprintf` in lib/ and bench/; a line in bench/corpus.cpp with two or more generator draws. Plus an AGENTS.md Code-style bullet ("no `<random>` distributions or
  `std::hash` in output paths; one generator draw per statement") and "gcc 14 is the floor" in Build.
- **B1/B2** `[[nodiscard]]` on the listed headers and their callers (a public API change: an INTEGRATION.md note; fix callers with `(void)` or by using the result; build consumer/mock_squad).
- **B3** `snprintf` to `std::format` (`%4zu| ` to `{:4}| `, `%016llx` to `{:016x}`, record.cpp `\\u%04x` to `{:04x}`): byte-identical for integers. **Never convert `std::to_string(double)`** (it is `%f`; format prints the
  shortest form), and only convert `to_string` inside `+` chains of three or more parts.
- **B4** `std::println(stderr, ...)` for fprintf/printf in app, bench mains, consumer tools, fixture_source.hpp; remove the GCC 13 comment; check no script parses the output.
- **C1..Cn** one file per slice: `starts_with`/`contains`, `std::to_underlying`, `std::ranges` algorithms, `views::enumerate`/`zip`/`iota` (tests and bench first, then lib/src outside the hot list).
  Traps: `enumerate`'s index is `ptrdiff_t` (a `-Wsign-compare` error against `size_t`: leave such loops); views can defeat vectorisation (none in gated hot loops).
- **D1** transparent string_view lookup (`std::less<>`, a transparent hash for unordered maps) on string-keyed maps in store, claims and query. **The only slice that touches gated timing:** run the perf gate
  on a quiet machine and report the delta with its noise; re-baseline only on a measured improvement, never to absorb a regression.
- **D2..** `std::expected` in place of `std::optional` where the caller loses a reason: one public function per slice, tagged for INTEGRATION.

## Support matrix

`print/println`, `ranges::to`, `enumerate`, `zip`, `to_underlying`, `unreachable`, `expected`, `move_only_function`: all four toolchains. `flat_map/flat_set`, `mdspan`: **not** on gcc 14 or clang 19 + libstdc++ 14
(libstdc++ 15+), so not used until the floor rises, then guarded by `__cpp_lib_flat_map`. `std::generator`: partial on clang 19.

## Do not change

`lib/src/json_util.hpp` and simdjson interop (perf-gated, byte-exact); the writers that make fixtures or metrics (projection.hpp, event and store snapshot JSON, mock_squad record.cpp/loop.cpp emission order, the corpus
emission sequence); the corpus `below` modulo semantics; hot loops measured by `perf/baselines/croglegion.json` (no views there); public `std::function` signatures (`Clock`, `Reducer`: `move_only_function` would make
`Store` non-copyable for every consumer); deducing this (no use found).

## Rules

Work in your own full clone, never a checkout another session holds. Commit each slice separately with specific `git add` paths; verify with gcc locally and the hosted full tier (`gh workflow run ci-full.yml`) for
clang; never regenerate a fixture or re-baseline to hide a difference; write a mechanical "identical modulo what changed" check for any fixture that moves.

## Outcome (A0-A3)

Done (2026-11-24, gcc/MinGW local; one commit each): **A0, A1, A2, A3**. Not started: B1-B4, C, D slices. Not run: the hosted full tier (clang 19, clang 22), so clang is unverified.

* **A0** moved bytes, as the brief allowed. The `issues/<kind>/<hex>` keys do reach `fixtures/mock` (7 files: g4-rename-resign-move layers and store, g5 turn-0); `golden/`, `perf/` and the viewer fixtures do not carry them.
  The key is now `hashView(key)` (the file's own fixed hash, no new function) instead of `std::hash<std::string>`; the fixtures were regenerated with `mock_squad --update` and
  `tools/check_rekey_diff.cmake` proves every changed file is identical to its committed version once the keys are masked (7 files, all pass; a tampered file fails). A one-off tool, kept with its commit.
* **A1** `bench/rng.hpp` and `lib/tests/test_rng.cpp`. The pinned values were captured by a throwaway program with a verbatim copy of the pre-move `Rng` (seed 0 matches the published `0xE220A8397B1DCDAF`); the seed-7 shuffle was checked against a plain
  modulo Fisher-Yates reference. `test_corpus` and `Sub0Canvas_Bench_metrics` pass, `git diff fixtures/ perf/` empty. `skewed` now names its two draws (`a` then `b`; the product is symmetric, so gcc's old right-to-left order gave the same value).
* **A2** both tests moved; neither pinned case counts or seeds. The new sequences exposed a bug **in the test model**, not the library: the rebase check sorted a stale transaction's ops by `from`, which ties `AFTER 22` and `LINES 22`
  (case 470, seed 20261112); it now sorts by the span, as `modelApply` does. The insertAfter line also drew twice in one initialiser list and was split.
* **A3** `tools/check_cxx_rules.cmake`, ctest `check_cxx_rules` (label smoke, in `quick`), plus the AGENTS.md bullet and "gcc 14 is the floor".

Deviations: (1) rule 3 (`snprintf`) cannot pass on the tree as it is (5 calls in doc_layers.cpp and outline.cpp, slice B3), so the script holds an allowlist by count (doc_layers 2, outline 3); a new call fails and B3 empties the list.
(2) Rule 4 is checked per statement, not per line as the brief says: a per-line check flags 11 legitimate lines (a `for` header, declarator lists and a braced list are all sequenced). It splits at `;`, at `?`/`:`, at commas outside parentheses and at braces, and covers
bench/ and lib/tests/ (not just corpus.cpp); a guard for the common shapes, not a proof. Comments and string literals are ignored by every rule. (3) corpus.cpp's own `kGolden` and `mix` are gone (now `rng::mix`).
Left alone: braced-list draws in corpus.cpp (e.g. `ids = {word(r.skewed(..)), word(r.skewed(..))}`, well-defined left to right); the AGENTS.md brief's "state whether clang 22 uses libstdc++ or libc++" was not checked (no clang run).
Remaining: the hosted full tier for clang, B1-B4, C, D.

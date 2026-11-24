# Follow-up session: C++23 adopted throughout, the RNG included

Status: open (planned 2026-11-24 by the planning agent; slices A0-A3 and B1-B4 done, see the Outcomes). Backlog: [phase 1](../BACKLOG.md). Parent: [backbone-next-wave.md](backbone-next-wave.md).

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

## Outcome (B1-B4)

Done (2026-11-24, gcc 16 MinGW local; one commit each): **B1, B2, B3, B4**. `ctest --preset quick` (23 tests), `test_consumer_mock_squad` and `npm test` (136) pass after each slice; `git diff fixtures/ perf/` empty throughout, no byte moved. Not run: the hosted full tier (clang 19, clang 22, gcc 14), so those toolchains are unverified.

* **B1/B2** `[[nodiscard]]` on every function the brief listed (line numbers had drifted a little; found by grep). Only 7 callers dropped a result: `registerSubscriptionReducers` (bench_inbox, test_inbox) and `registerViewReducers` (bench_session x3, test_views_sessions x2) now take `(void)`; every other caller already used the result, and the consumer needed no change. A scan of every header (multi-line declarations included) found no other public value-returning function without the attribute apart from `operator++(int)` and the defaulted `operator==`, left alone as idiomatic. INTEGRATION.md has the note.
* **B3** the six `snprintf` calls (doc_layers.cpp x2, outline.cpp x3, record.cpp) are `std::format`; the allowlist in `tools/check_cxx_rules.cmake` is empty (a planted `snprintf` in bench/ fails the script). No `std::to_string` was converted: the brief only permitted it, and none of the chains was worth the risk.
* **B4** every `printf`/`fprintf` is `std::println`; the two `eprintln` helpers in app/ call `std::println(stderr, ...)`; the GCC 13 comment is removed. Nothing parses that output (checked the node tests, tools and CMake).

Deviation: B4 needed a build change the brief did not name. On MinGW GCC 16, `std::println` to a stream does not link without `-lstdc++exp` (undefined `std::__open_terminal`). `cmake/print_link.cmake` probes with `check_cxx_source_compiles` and links `stdc++exp` only when the plain link fails; it is included by the top-level and the consumer project. On Linux gcc 14 and clang 19 + libstdc++ the plain probe should pass (unverified here).
Remaining: the hosted full tier for the clang and gcc 14 legs (including the print probe), C and D slices. The other `std::fputs`/`ostringstream` output paths in app/ were left as they are.

## Outcome (C)

Done (2026-11-24, gcc 16 MinGW local): **C1-C4**, one commit per area per directory group (`git log` subjects start `C1`..`C4`). `ctest --preset quick` (23), `test_consumer_mock_squad` and `npm test` (136) pass after each group; `git diff fixtures/ perf/` empty throughout. No timing run, no hosted run (clang 19/22, gcc 14 unverified).

* **C1** `contains`/`starts_with` for `find(..) != npos`, `rfind(x, 0) == 0`, `count(k) != 0`: lib/tests (104 sites), bench, consumer, app, then lib/src. A scripted rewrite (receiver-aware for the negated forms), reviewed hunk by hunk; one-argument `find` only (a position argument is never rewritten).
* **C2** `std::to_underlying` for the two enum casts in tests (`Carry`, `ChangeKind`). The only other enum cast, `stats_.outcomes[static_cast<std::size_t>(r.outcome)]` (resolver.cpp:275), is in a gated hot path and was left.
* **C3** `std::ranges::sort/stable_sort/find/find_if/any_of/all_of/none_of/count/reverse` for whole-container iterator pairs (`stable_sort` stayed `stable_sort`); `views::zip` (test_inbox, test_corpus, test_outline) and `views::enumerate` (test_block_index, app `shown[i].number`, gate.cpp's failure excerpt: the signed index is cast or compared with a literal only). `remove_if`/`unique`/`lower_bound`/`upper_bound` and partial ranges (`begin() + n`) were not converted (the erase idiom needs `std::erase_if`; a different result type).
* **C4** `std::string_view` for the `const char*` key parameters of the JSON helpers in test_golden_replay/test_query and the `startsWith` helpers in canvas.cpp/turn_control.cpp. No `std::span` candidate: the only pointer+size parameter is `hashBytes` in doc_layers.cpp, a hash kernel in the gated index path.

**Left alone on purpose (hot):** json_util.hpp, projection, event.cpp, store.cpp, query.cpp, claims.cpp, view.cpp, session.cpp, resolver.cpp, inbox.cpp (all measured by croglegion.json or byte-exact writers), doc_layers.cpp except the gate-text parser (`BlockIndex` update/resolve), revision_index.cpp's loops, bench/corpus.cpp's emission (only `countLines` changed), mock_squad record.cpp and loop.cpp emission order. Their `find(..) == npos` filters (query.cpp:635, store.cpp:852, inbox.cpp:188 and :344) are per-item predicates called from the measured loops, so they stay.

**Counts (text matches, lib bench consumer app; before at 0d67154, after):** raw `for (.. i = 0; i < ..)` loops 208 to 202 (most are counted numeric loops where `views::iota` would not shorten them, or index-fed output); iterator-pair algorithm calls 114 to 71 (the rest: hot files, `remove_if`/`unique`/bounds, partial ranges); `static_cast` of an enum 3 to 1; `rfind(x,0)`/`find==npos` 134 to 7 (the 7 are `find` results compared by position, e.g. `rfind(':') == npos ? 0 : rfind(':') + 1`, and the four hot predicates).

**Remains:** the raw index loops over containers in the hot files and in lib/src (blocks.cpp, outline.cpp, turn_control.cpp): left because the index feeds a line number or an output; a `std::ranges` pass over the hot files waits for the quiet-machine timing step. The hosted full tier still has to prove clang 19/22 and gcc 14. No `tools/check_cxx_rules.cmake` rule was added: a reliable pattern for "iterator pair" or "index loop" does not exist.

## Outcome (D1, code)

Code only, no timing run (the owner times it, A/B against `ebc1f040`, the commit before the first D1 commit). Most string-keyed containers in lib/ were already transparent (`std::less<>` on the public maps, `StringHash` + `std::equal_to<>` on the store, claims and inbox tables), so the slice was the remaining temporaries:

* **claims.cpp**: `coverKeys`, `resolveInto` and the `bucket` probe take `std::string_view`; the `std::string{target}` / `std::string{id}` temporaries in `claimsOn`, `holderOf`, `notesOn`, `resolveBlock` and `findBlocker` are gone.
* **inbox.cpp**: `matches()` probes `q.events` (a `std::set<std::string, std::less<>>`) with the kind-name view instead of a temporary; the cold build probes `byKindName` by view and builds the key only on first sight.
* **query.cpp / query.hpp**: `QueryDocument::texts_` (private) becomes an `unordered_map` with `StringHash`/`std::equal_to<>`; `text(textId)` probes by view. It is only found and `try_emplace`d, never iterated, so no output order depends on it.

Left as is: `doc_layers.cpp` `files.find(std::string{b->path})` (the `std::map<std::string,std::string>` parameter type is public API: `layerDetail`, `layerInline`); `QueryDocument::layerMeta()` / `meta_` (public accessor type); `view.cpp` `pending_`, `resolver.hpp` `texts_`/`prepared_`, `revision_index` `ordinalOf`/`askedAt`/`sizes`/`lines_`, `canvas.hpp` `files_` (every probe already holds a `std::string`, so no temporary to remove); `store.cpp` snapshot-load `backlinks[std::string{...}]`, `query.cpp` `folders/files.emplace` and `session.cpp` `sessions_[...]` (insertions need an owned key). `BlockIndex::byPath` is already a `std::map<std::string_view,...>`. No unordered container touched here is iterated into output. **INTEGRATION.md note: none** (the only type change is a private member, `QueryDocument::texts_`, an ABI-only change).

Commits to A/B (baseline `ebc1f040`): `beceba8` (claims), `ab546f0` (inbox), `0b99264` (query). Rows that should move: `claims check/lookup 1e3 targets at 1e3|1e5 claims` (claims); `inbox cold read of one subscription`, `inbox read, warm index` (inbox); query rows are not in the baseline, so nothing gated moves there. Verified: build, `ctest --preset quick` (23/23), `test_consumer_mock_squad`, `git diff fixtures/ perf/` empty.

## Outcome (D1, timing) and where C++23 adoption stands (2026-11-24)

Interleaved A/B of `ebc1f04` against D1 on a quiet machine (AC, load 6-9%, 10-15 rounds): **no row regressed and none improved beyond noise**; `claims lookup` about -6% (below the 10% tolerance), `claims check` +1%, `inbox cold read` 0%, `inbox read, warm index` +3.9% (2.1% spread, only 10 rounds: re-run at 15+ if it matters), `store emit`, `layeredJson 1 turn` and `index warm resolve` flat. D1 stays as code hygiene (no temporaries built for a lookup), not as a speed-up; nothing was re-baselined. Done and green in the hosted full tier (Linux gcc 14, Linux clang 19, Windows MinGW): A0-A3, B1-B4, C1-C4, D1. Pushed through `a0beb29`.

Left: **D2** `std::expected` in place of `std::optional` where the caller loses a reason (one public function per slice: each changes a public signature, so the owner decides which, then an INTEGRATION.md note and a tag); a **`ranges` pass over the hot files** (store, query, claims, view, session, resolver, inbox, BlockIndex), only with an A/B per file on a quiet machine (tell the engine session first) and only keeping what the gate shows is not slower; the 202 raw index loops that remain (mostly in those hot files); `std::to_string` chains (208) where integer-only and three or more parts; MSVC, macOS and sanitizer legs of the full tier were skipped in every run (run them once before a release).

**For Sub0Squad when it takes the next tag:** item keys `issues/<kind>/<hex>` changed (A0: a fixed hash replaces `std::hash<std::string>`, stable on every platform); `[[nodiscard]]` on the claims, inbox, query, store, session and view functions means `registerViewReducers`, `registerSubscriptionReducers` and `store.emit` results must be used or cast to void; `std::println` needs `<print>` (gcc 14 floor) and MinGW gcc 16 links `stdc++exp` through `cmake/print_link.cmake`.

## Outcome (D2)

Done (2026-11-24, gcc 16 MinGW local; one commit). **Converted: one function.** `applyEdits` (edits.hpp): `std::optional<std::map<std::string,std::string>> (reply, files, allowed, std::string& error)` to `std::expected<std::map<std::string,std::string>, std::string> (reply, files, allowed)`. It had 15 failure causes reported through an out-parameter; the reason now travels in the return value. Callers: `canvas.cpp` (`applySnippets`, text unchanged), `test_replies.cpp`, `test_edits.cpp` (28 calls; a `reasonOf` helper). No app, bench, tools or consumer caller (the consumer reaches it through `applyReply`). Error texts are byte-identical. INTEGRATION.md carries the breaking-change note; no R-row names a signature, so REQUIREMENTS.md is unchanged.

Tests: `test_edits` already pinned 10 failure texts (LINES range, mixed kinds, missing SEARCH, ambiguous SEARCH, path not allowed, unclosed block, no blocks, ...); added the exact `error()` value for the no-blocks case, the header with no closing `===`, no earlier version, and an empty SEARCH. `ctest --preset quick` 23/23, `test_consumer_mock_squad` pass, `git diff fixtures/ perf/` empty. npm test not run (app/ untouched). No timing run, no hosted clang run.

**Left as `std::optional` (and why):** `parseTargetKind`, `parseState`, `parseDeltaForm` (claims.hpp, store.hpp, view.hpp): one failure cause (unknown name), callers use `value_or` or print the name they already hold; `parseState` is also on the store load path. `holderOf`, `conflictSince` ("is there one"); `textAtPin` (absent at the pin; ten session.cpp callers, hot session path); `DocumentSource::changes` / `resolve` (virtual interface, one cause: unknown, `unknown` is the documented contract; `changes` is on the revision-index build); `DocText::text`, `Overlay::overlay`/`set`, `BaseText` (lookups); `RevisionIndex::carry` (`Result<optional>`: the optional is "no carry", the Result already holds the error); struct members and parameters (`ttl`, `form`, `budget`, `lastTransition`, `inbox`).

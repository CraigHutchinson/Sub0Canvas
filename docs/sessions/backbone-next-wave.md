# Follow-up session: the backbone, next wave (after S1, views and sessions, Fabric F0)

Status: open (written 2026-11-24). Backlog: [phase 1](../BACKLOG.md). Design: [PLAN.md](../PLAN.md) section 3.2 (judgment calls), [VIEWS.md](../VIEWS.md) sections 6-8,
[MOCK_SQUAD.md](../MOCK_SQUAD.md) section 7, [TEST_AUDIT.md](../TEST_AUDIT.md).

## Goal

Carry the backbone from "library built and merged" to "a consumer can use it": the gate and release housekeeping first, then the Fabric protocol and transport, the
pinned and tip workers in the mock squad, and the views limits. Keep every extension point open (`ViewStore`, `ScratchLayer`, `SessionPolicy`, `PresenceProvider`,
`ConflictView`, `Compactor`); decide nothing the PLAN marks as later.

## Where it stands (2026-11-24)

- **Built and merged on `main`:** B0-B4, G1, S0, S1, K0, D0, V1a-q (pure query layer), views and sessions, the mock squad (MS0-MS3), the perf and CI adoption.
  S1's `layeredJson` and events-page timing regressions are fixed (`902827c`, `28d3341`); output bytes unchanged.
- **Done since the first version of this brief (2026-11-24, second session):** issue #4 closed (the hosted full tier is green on Linux gcc, Linux clang and Windows MinGW at
  `45b0fe2`); MS4 (two-actor claims, R-CONS-6/7) and MS5 (pinned and tip workers on sessions, R-CONS-8/9) built; see [ms4-ms5-mock-squad-claims-and-views.md](ms4-ms5-mock-squad-claims-and-views.md)
  Outcomes for the deviations and what each left. Sub0Fabric `main` is at `dc1cc05`, CI green.
- **Released and adopted (2026-11-24, third session):** v0.4.0 is tagged (full tier green). Since the tag: C++23 adoption A0-D2 is done and green ([cxx23-adoption.md](cxx23-adoption.md) has the Outcomes
  and the short list of what is left: a `ranges` pass over the gated hot files, only with an A/B per file on a quiet machine, and the MSVC, macOS and sanitizer legs of the full tier, never yet run). A0 changed the
  `issues/<kind>/<hex>` item keys, `[[nodiscard]]` is on the claims/inbox/query/store/session/view results, and `applyEdits` now returns `std::expected`: the next tag (v0.5.0) is a breaking one for Sub0Squad
  ([INTEGRATION.md](../INTEGRATION.md)).
- **Lessons that changed the code (read before writing tests or generators):**
  1. gcc 14 `-Werror` on range-for over simdjson temporaries: tests use by-value `objectOf`/`arrayOf`. `CHECK_EQ` binds references (`auto&& _a = (a)`), so `f()[0]`,
     `names().front()` and `string_view{temp}` dangle: name the value first. CI is the check (no local gcc 14).
  2. **Evaluation order** is unspecified: gcc and the Windows toolchains go right to left, Linux clang left to right. Never put two generator draws, or a read and a write of
     one variable, in one argument list or operator chain (corpus generator and the loop's `failed` record were fixed; [MOCK_SQUAD.md](../MOCK_SQUAD.md) section 8).
  3. A test that builds temporary git repositories must not inherit the hook's `GIT_DIR` (`test_viewer` now unsets it).
  4. Do not share one checkout between sessions: another session had switched `D:\Craig\GitHub\Sub0Canvas` to its own branch under this one, and a push from a linked worktree
     left that checkout with `core.bare=true` (restored; see [Sub0Math issue 5](https://github.com/CraigHutchinson/Sub0Math/issues/5)). **Work in your own full clone** (this
     session used `D:\Craig\GitHub\Sub0Canvas-dev` and `-fix`; the gate reads that clone's own `.git/sub0perf`), never a linked worktree, and never in a checkout another session holds.
     Those clones are scratch: remove them (and the `Sub0Canvas-clang-build` directory) when no longer needed.
- **Gate lesson:** a benchmark row must time the operation, not the setup (`bench_session.cpp` row `session replay` timed an empty call and stalled the gate for 70 minutes;
  fixed in `516130f`; reported on [Sub0Math issue 6](https://github.com/CraigHutchinson/Sub0Math/issues/6)). A new row has no baseline, so the gate cannot judge it until a re-baseline.
  The last push left 10 rows inconclusive, probably load from the llmengine session; re-run the gate on a quiet machine before trusting it.
- **Sub0Fabric:** F0 is pushed and CI is green (the executable-bit fix is in). Its next work is W3/W4 below.
- **Timing machine:** the Sub0LlmEngine session shares this host and was asked to keep quiet during timing. Tell the user when you need it, and when you are done.

## Do first (housekeeping, each small)

1. Re-confirm with `git log origin/main..main` and `gh run list`; take your own clone first (lesson 4). The sub0perf-extraction session (PR 6, branch `use-sub0perf`) may
   have merged by now: re-pin Sub0Perf per its change.
2. **Release v0.4.0** (owner confirms before the tag): issue #4 no longer blocks it; the full tier is green. It carries S1, B1-B4, G1, V1a-q, views/sessions, MS0-MS5.
3. **Re-baseline timings** only after one deliberate full steady run (AC, quiet, `accept` with reasons): the S1 rows and the `query` and `session` rows are not in
   `perf/baselines/croglegion.json`, so the gate cannot judge them.
4. **Test audit** ([TEST_AUDIT.md](../TEST_AUDIT.md)): measure the runtimes, add the small variant rows and the tier test, then demote the heavy rows once Sub0Math can
   declare a per-row tier. The Sub0Math items belong in Sub0Math issues (parallel rows is [issue 8](https://github.com/CraigHutchinson/Sub0Math/issues/8); hook overwrite is
   issue 5; slow `compare` is issue 6). The sub0perf code is migrating there, so do not build it here.
5. Sub0Platform: the `strerror` clang failure has no issue yet; file one.

## Then the backbone, in this order

1. **Mock-squad leftovers from MS4/MS5** (the Outcome sections list them): a pinned actor's reads (`answerRead`) still show the tip, not the pin; an `advance` that lands edits
   through the loop; views combined with claims; fork; atomic land over two base revisions; `advance` with a moved held block (no anchors are held, so `anchors` is 0); the inbox
   half of B2 is not mocked. Regenerate `fixtures/mock` only if the byte change is the intended one and verify identity modulo what changed.
2. **Views and sessions, the limits** in VIEWS.md section 8: write them down as tests or as `SessionPolicy` data, one at a time. Presence and peer patches stay deferred.
3. **Fabric W3, protocol and projection** (`CraigHutchinson/Sub0Fabric`): the `query-1` protocol over the store; the projection shared with `layeredJson`.
4. **Fabric W4, transport:** the single `IHttpFront` / `IHttpClient` seam; implementation A is Sub0Squad's hand-rolled HTTP/1.1 lifted onto Sub0Platform `net`
   plus SSE (the lighter first one); implementation B is Boost.Beast, already built in Sub0LlmEngine `src/net/beast_http.cpp` ([Sub0LlmEngine issue 1](https://github.com/CraigHutchinson/Sub0LlmEngine/issues/1)
   tracks its adoption). Keep Canvas itself free of I/O (R-BB-7).
5. **The read-only Sub0Log adapter (D3)** and the frames-versus-lines journals for the Squad's W5 adoption (`lines` is the journal-1 file unchanged; `frames` is checksummed).
6. **V1a-page:** JS windowing (K0 shows 8 of 10 interactions breach on 1,590 files); then V1/V2 comments and inboxes.
7. **Block discovery D1-D2** and the `index.outliners` snapshot field (approved as one reviewed batch, because it regenerates the fixtures). Open D0 faults:
   Rust lifetimes, CRLF Allman text, nested functions and classes as nodes, and YAML, JSON, XML and Markdown blocks.
8. **S1 target:** heap per block is 319 B against the 200 B target (an expected failure in `perf/metrics.json`); S2 (sketches, the merge pass) is the way down.

## Owner-run, not for an agent

The T0 comparison on the new pin (`half-written`, `undeclared-member`; needs the shared engine), a real-attempt capture for R-U1-4, and Sub0Squad's adoption items
(its own session: [MOCK_SQUAD.md](../MOCK_SQUAD.md) section 7 is the feed-back list).

## Rules for the session

Commit as you go with specific `git add` paths (never `commit -a`); confirm before pushing or tagging; never skip hooks or edit `.githooks/pre-push`; accept a metric
change only through `bench_check.py accept` with a reason; record a decision where PLAN.md section 3.2 holds the others; tidy (worktrees, scratch files, branches) after
each iteration; use the sub-agents the owner named (opus to plan, haiku to mine data, sonnet to implement) and resume them with `SendMessage` after a rate limit.

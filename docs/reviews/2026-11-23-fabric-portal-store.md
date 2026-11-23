# Consolidated review and plan: Sub0Fabric, the portal and the stores (2026-11-23)

Status: review, read-only; nothing here is built. Settles PLAN.md O6, O10 and the scope of O11 (section 3.1 of [../PLAN.md](../PLAN.md)). The
cross-cutting rule the owner set the same day is the yardstick: every aspect **data-driven**, **extensible** (a seam with more than one proved
implementation) and **performance at the core**.

## Scope and inputs

| repository | commit | read |
|---|---|---|
| Sub0Canvas | 8bf7024 (2026-11-23) | `docs/PLAN.md`, `DECISIONS.md` 3, 7, 9, `AUTHORING.md` 3, `tools/serve.mjs`, `src/view.js` |
| Sub0Squad | 9f7a261 (2026-11-23) | `docs/PORTAL_WORK_OVERVIEW.md`, `docs/sessions/portal-access.md`, `canvas-backbone-adoption.md`, `include/sub0squad/portal.hpp`, `src/portal.cpp`, `src/journal.cpp`, `src/engine.cpp` (HTTP client) |
| Sub0Platform | 5feccdf (2026-11-19) | `cpp/include/sub0platform/net.hpp`, `cpp/README.md` |
| Sub0Log | d9ab9fd (2026-10-05) | `include/sub0log/merge.hpp`, `docs/multi-process.md`, `REQUIREMENTS.md` R9.1 |

Method: one reviewer (inline), lenses A-G applied to these slices only. Not read: Sub0Pub, Sub0Pipeline, Sub0MemPage, Sub0Squad's daemon
internals beyond the journal, Sub0Log's binary record format. Findings that depend on those are marked *unverified*.

## 1. Verdict

The three projects do solve **one** problem three times: an append-only, per-writer, crash-safe record stream with a reader that merges, plus a
local HTTP surface that shows it. The Fabric trigger written in DECISIONS.md section 9 ("extract on the second consumer") **is already met**:
Squad's `journal-1`, Canvas's designed event log and Sub0Log's segment merge exist or are specified today, with three different ordering rules and
two HTTP/page stacks. So the owner's decision to build Sub0Fabric now is sound. Its risk is the one section 9 named, designing for consumers that
have not used it, and the fix is to **build it by lifting what already runs** (Squad's journal and the portal's hardening), not from the spec.
The server question (O6) is mis-framed as Node against C++: the **protocol and the projection must be defined once, transport-independent**, and
the transport is a seam with at least two implementations. One premise in DECISIONS.md section 7 is stale: Platform's `net` has landed.

## 2. Canonical decisions

| # | topic | decision | sources |
|---|---|---|---|
| D1 | Fabric's trigger and first content | Met. F0 is **Squad's journal lifted into Fabric** (segment, torn-tail recovery, `journal-1` codec, its tests), behaviour-preserving; Canvas's events are the second user, new kinds on the same machinery. | DECISIONS 9; `journal.cpp`; Log `merge.hpp` |
| D2 | Fabric scope (modules) | `segment` (append, framing, torn tail, rollover, durability policy), `record` (versioned envelope `{kind@ver, writer, clock, payload}`, kinds as a data registry), `order` (policy per stream: `seq`, `lamport`, `aligned-time`; **streaming** k-way merge), `cursor` (durable cursors, `since(cursor)`), `store` (segment storage seam), `transport` (stdio lines, HTTP+SSE), `projection` (versioned, field-allowlisted views). Depends on Platform for I/O and simdjson for JSON; **no dependency on Canvas, Squad or Log**. | PLAN 3.1; AUTHORING 3 |
| D3 | Sub0Log | Not extended: its contract is lossy by design (R9.1 counts drops; a domain log must never drop). Log gets a **read-only codec adapter now (owner, 2026-11-23), built in W2**, so the portal can show diagnostics beside work, which is also a second proved codec for the Fabric record seam. Log itself is unchanged. | DECISIONS 9; Log R9.1 |
| D4 | Ordering is data | The order rule is a header field of the stream, not a library constant: Squad keeps `seq`, Canvas uses `lamport` with writer tie-break (O3), Log's anchored time is `aligned-time`. One merge, three policies. | Squad journal; O3; Log `merge.hpp` |
| D5 | Physical per writer, logical per project (settles O10) | A Squad attempt writes its own segment in the per-project **format** (as O10 proposed); "per project" is a **read-time merge** across homes, never a move of data. This is also Sub0Log's mechanism and O2's per-writer refs. The portal's Work registry is already that reader's source list. Per-attempt stays behind an option, off by default (B2). | O10; `PORTAL_WORK_OVERVIEW.md` |
| D6 | Identity | One rule across all three: a record's writer is a composite `{namespace, writer}` (project, source home, source-local id), because two homes both mint `w-000001`. Block ids stay birth ids (O4). Browser-facing keys are opaque. | `PORTAL_WORK_OVERVIEW.md` "Work and run identity"; O4 |
| D7 | Server (settles O6) | **Protocol first**: one versioned query/event protocol (A10) with golden contract tests, independent of transport. Transport seam, two implementations proved: **stdio JSON lines** (agents, CLI, tests, and a Node bridge if wanted) and **native HTTP+SSE on Platform `net`**. The Node bridge remains an *optional* delivery process (Tailscale/ngrok viewer), not the default. **Decided (owner, 2026-11-23): native HTTP is the local default now**, not chosen by measurement. **HTTP/2 is the target wherever tooling supports it**, with **HTTP/1.1 the required baseline**: the protocol version is negotiated behind the transport seam, so no consumer depends on it. Constraint to settle at W4: browsers speak HTTP/2 only over TLS, never over plain `http://`, and Platform's `net` has no TLS or HTTP/2 framing. So HTTP/2 needs a TLS layer (a local certificate) and either a vetted library (for example nghttp2, each dependency approved on its own) or our own framing; on plain loopback the viewer runs HTTP/1.1. The benefit that justifies it is server-sent events: HTTP/1.1 allows about six connections per origin, which the viewer avoids by using **one multiplexed event stream per page** (cursor-addressed), so HTTP/2 is an optimisation, not a prerequisite. K0 budgets still apply and are measured on both versions. | AUTHORING 3, 3.2; `net.hpp`; `portal.cpp` |
| D8 | Portal integration (settles the "beside, integrated or within" question) | **Within at the data layer, integrated at the shell, beside as step one.** Work items, campaigns and executions are Canvas items with links; lifecycle, verdict and freshness are the three fields the portal doc already keeps apart. The viewer's `centre` is already "any layer key, never a fixed list"; Squad contributes view definitions, not a second app. Step one is a deep link from a portal row to its canvas view. | BACKBONE items; `view.js` header; `PORTAL_WORK_OVERVIEW.md` |
| D9 | The O2 smoke-test backends (**owner, 2026-11-23**) | A (committed files) and D (local only) are **one implementation with two roots**, so they do not prove the seam. Beside **git refs (B)** and **the directory store (A and D by root)**, add **git notes (C)** and **SQLite (E)** as **proofs of concept only**: not fully implemented, each just enough to pass the one store contract suite (append, `since(cursor)`, atomic batch). They are **build and release gates, not always enabled** (an optional CI and release job, off by default, no consumer links them). One is promoted to a real backend only if it proves a good option on the section 5 budgets. | DECISIONS 3.1 |
| D10 | One HTTP half | Squad has a hand-rolled HTTP server (`portal.cpp`) and a separate hand-rolled client with chunked decoding (`engine.cpp`), and Canvas has a third in Node. Lift server and client into **one `http` module** (**home: Fabric `transport`, owner 2026-11-23**; Platform stays mechanism-only), carrying the existing hardening: loopback bind, per-run token with `Origin` check, `Host` check against DNS rebinding (Canvas's), path escape check, response caps. | `portal.cpp`; `engine.cpp`; `serve.mjs` |
| D11 | Projections are policy | The remote viewer's "field allowlist, opt-in project list, opaque ids" is **one projection definition** (data) read by both the native portal and any Node viewer. No second hand-written projection. | `portal-access.md` |

## 3. Correctness defects and stale claims

| Defect | Source |
|---|---|
| DECISIONS.md section 7 says the Node bridge stays "when Sub0Platform's `net` module lands"; `net` has landed (`SocketListener`, `SocketStream`, loopback listen, deadlines, `waitForConnection`) and Squad's portal already serves HTTP on it. The premise for the bridge as default is gone. | `net.hpp`; `portal.cpp:56-114` |
| Sub0Log's `Merger::merged()` returns **every record** in one vector (docs/memory.md notes it already halved peak memory). Fine for diagnostics, wrong for a store that must pass R-SCALE: Fabric's reader must stream. | `merge.hpp` |
| Canvas's two designs disagree on who owns the page: AUTHORING section 3 has the Node bridge serve it; Squad's portal serves its page from a string with no external resources, and `portal-access.md` adds a second Node viewer. Three page-delivery paths, no stated owner. | `serve.mjs`; `portal_page.cpp`; `portal-access.md` |
| *Unverified:* whether `canvas-events-1` and `journal-1` envelopes already collide on field names (`seq`, `type`). Check before D2's record envelope is frozen. | `journal.cpp` |

## 4. Algorithmic redundancy removed

* Three ordering schemes (`seq`, `lamport` + writer, anchored wall time) -> one streaming merge with an ordering policy (D4).
* Three HTTP surfaces (Squad server, Squad client, Node bridge) -> one `http` module (D10) and one protocol (D7).
* Two projection definitions (native portal state, Node viewer allowlist) -> one data definition (D11).
* Two identity conventions (composite execution key, birth and writer ids) -> one composite writer id (D6).
* Torn-tail recovery written in Squad's journal, Sub0Log's framing and (designed) Canvas's log -> one `segment` (D2).

## 5. Performance requirements for Fabric (binding on every module)

Set the numbers before adoption, as R-SCALE does for Canvas; each is a benchmark in the repository from F0.

| path | rule |
|---|---|
| append | O(1), one writer per segment, no lock; durability is a **policy** (per record, per batch, per interval), default batch |
| `since(cursor)` | O(records returned) via a sparse offset index per segment; never a rescan |
| merge | streaming k-way over open segments, bounded memory in the number of segments, not records |
| parse | simdjson on-demand, zero-copy views; one JSON library family-wide (already true for Canvas and Squad) |
| git-ref backend | one commit per **batch**, never per event; a segment chunk is the commit's tree |
| live fan-out | one in-memory ring of recent events shared by every subscriber (SSE or stdio), cursor-addressed |
| projection | computed once per refresh and shared across clients (the portal brief already asks for this) |
| indexes and caches | per user, keyed by object id (A19); disposable |

## 6. Ordered plan

| Wave | Work | Repo | Gate |
|---|---|---|---|
| W0 | Docs: record D1-D11 in PLAN.md (O6, O10, O11), correct DECISIONS.md section 7, mark section 9 decided, add the cross-cutting rule to AGENTS.md, re-scope `portal-access.md` to D7 and D11 | Canvas, Squad | docs only |
| W1 | **v0.3.0 and the Squad adoption brief, unchanged** (O1). Fabric must not delay it. | Canvas, Squad | tag green; Squad suite green |
| W2 | Create Sub0Fabric from the family CI standard; F0 = lift Squad's journal (segment, recovery, codec, tests) behind `segment`/`record`/`order`/`cursor` and the directory store, and the read-only Sub0Log codec adapter (D3); benchmark harness with section 5's budgets | Fabric | budgets recorded; Squad's journal tests pass on Fabric; a Sub0Log segment reads through the adapter |
| W3 | P0: the versioned protocol and projection format, with transport-independent golden contract tests | Fabric | contract suite green over an in-process transport |
| W4 | Transports: stdio server and native HTTP+SSE (D10 lift), measured against K0; choose the local default by the rule in D7 | Fabric, Platform | K0 baselines; both transports pass the same contract suite |
| W5 | Squad: journal onto Fabric behind an option (per attempt, D5); the Work overview reads through the merged reader; deep link from portal row to canvas view | Squad | option off by default; T0 unchanged |
| W6 | Canvas: B0 format revision (O5) and B1 events on Fabric; git-ref store and git-notes store beside the directory store (D9) | Canvas, Fabric | the one store contract suite passes on all three |
| W7 | Viewer shell integration: view-definition seam, work items as items (D8); optional Node viewer from the shared projection (D11) | Canvas, Squad | R-AUTH budgets hold with portal data loaded |

W2 and W3 touch only Sub0Fabric and can run in parallel after W1; W5 and W6 touch different repositories and can run in parallel after W4.

## 7. Owner decisions

1. ~~**Third store backend (D9).**~~ **Decided (owner, 2026-11-23): git notes (C) and SQLite (E) as proofs of concept, gated at build and release, off by default.**
2. ~~**Local default transport (D7).**~~ **Decided (owner, 2026-11-23): native HTTP now, HTTP/2 ideally where tooling supports it throughout, HTTP/1.1 as the baseline.** Open sub-question for W4: the TLS and library route to HTTP/2 (see D7).
3. ~~**Portal depth (D8).**~~ **Decided (owner, 2026-11-23): all three, in order**: beside first (deep links), then the integrated shell, then within at the data layer through Fabric's merged reader (W7).
4. ~~**Fabric housing (D10).**~~ **Decided (owner, 2026-11-23): the `http` module lives in Fabric `transport`; Platform stays mechanism-only.**
5. ~~**Sub0Log (D3).**~~ **Decided (owner, 2026-11-23): a read-only Fabric adapter now, in W2; Log unchanged.**

All five owner decisions are settled. The remaining step is W0, recording them in PLAN.md, DECISIONS.md and the Squad brief.

## 8. Progress

| Wave | State |
|---|---|
| W0 | done 2026-11-23 (PLAN.md, DECISIONS.md, AGENTS.md, RESUME.md here; `portal-access.md` in Sub0Squad) |
| W1 | not started |
| W2-W7 | not started |

# Follow-up session: MS0, the mock squad baseline

Status: open (written 2026-11-23). Backlog: "MS0" under *Now*. Design: [MOCK_SQUAD.md](../MOCK_SQUAD.md) (all of it; sections 1-3 are the spec).

## Goal

Build the consumer project `consumer/mock_squad/` (scenario reader, gate, reference loop, record writer, consumer contract test) and about eight
scenarios, frozen in today's format under `fixtures/mock/`. No format change (that is MS1 = B0), no library change unless a scenario exposes a bug
(then fix it here first, with a test, as its own commit). The mock uses public headers only.

## What is known

* The squad's loop is `Sub0Squad/src/canvas_agent.cpp` (read-only reference; `persistCanvas` ~240, `layeredVerdict` ~405, the turn loop ~700-1000); its
  fake model is `Sub0Squad/tests/fake_engine.hpp` (`fake::Reply`). Mirror its reply shape; copy no policy that the library already owns.
* `fixtures/replies` and `lib/tests/test_replies.cpp` hold the section syntax to reuse. `doc_driver.hpp` is the driver style.
* No `install(EXPORT)` exists: the consumer fetches the library with CPM, as Sub0Squad does, with `CPM_Sub0Canvas_SOURCE` set to this checkout.
* `project(sub0canvas VERSION 0.2.1)` is stale at the v0.3.0 tag; contract check (f) must fail on it, so fix the version in the same slice (next release
  carries it).

## Steps

1. The consumer CMake project and a `ctest --build-and-test` entry in the top-level build (label `full`; `smoke` only if about 30 s or less).
2. `scenario`, `gate`, `loop`, `record` as in MOCK_SQUAD.md section 1; every squad constant in a `LoopPolicy`.
3. Scenarios G1, G2, G4, B1, B3, B8, B9, B12, each citing the T0 case or intervention it mimics, with hand-written `# expect`, `--- want`, `expect-ids`.
4. `test_consumer_contract` (MOCK_SQUAD.md section 3 a-f); `--update` regenerates `fixtures/mock/<name>/`; review the files by hand once.
5. Update `fixtures/README.md`, REQUIREMENTS.md (a row for the consumer contract), TESTING.md (the consumer rung), BACKLOG and RESUME.

## Done means

`ctest --preset full` and `npm test` pass; the scenarios fail when the library is broken on purpose (mutate one rule, see the red, revert); the mock
trace and attempt files are byte-stable across two runs; MOCK_SQUAD.md section 6 gains what the baseline taught.

## Environment and constraints

[AGENTS.md](../../AGENTS.md). Commit as you go, never push or tag without the owner, only your own files.

# Follow-up session: move Sub0Squad's platform layer to Sub0Platform

Status: planned (2026-11-19), delegated phase by phase to Haiku 5.5 agents; the owner reviews each phase before the next starts and before any push. P3 (Sub0Canvas consume) is done on branch `platform-consume`, pending owner review.
Origin: the `getenv` tidy-up in [BACKLOG.md](../BACKLOG.md). Platform code belongs in Sub0Platform, whose `cpp/README.md` says the C++ library is
"Sub0Squad's layer 0, lifted here". Squad's own brief: `docs/sessions/platform-unification.md` (step 3), `docs/PLATFORM.md`.

## Goal

1. Sub0Platform holds the C++ platform library (`platform_error`, `process`, `proc`, `host`, `file`, `net`, `ipc`, `wait`), its tests, its layering
   check and a shared test helper (`check.hpp`), built and tested on Linux and Windows.
2. The `getenv` fix lives there once: a read-only `getEnvironment(name) -> std::optional<std::string>` beside `setEnvironment`/`unsetEnvironment`
   (`_dupenv_s` on Windows), and the test helper uses it.
3. Sub0Squad consumes Sub0Platform through CPM and deletes its copy. Sub0Canvas consumes it for its test helper and drops `lib/tests/check.hpp`'s copy.

## Phases (each ends with local commits on a branch, nothing pushed)

* **P1, Sub0Platform: lift and fix.** Clone to a scratch directory; branch `cpp-lift`. Copy Squad's eight platform stems (headers, `_posix`/`_win32`
  backends, `platform_*_util.hpp`), `tests/platform_child.cpp`, the six `test_platform_*` programs, `check.hpp`, and `cmake/CheckLayers.cmake` adapted to
  the one layer. Rename the namespace `sub0squad::platform` to `sub0platform` and headers to `sub0platform/<module>.hpp`; the module names match the
  Python package (`process`, `proc`, `host`, `file`, `net`, `ipc`, `wait`, `error`). Add `getEnvironment`, test it (set, unset, absent, empty value),
  and switch `check.hpp` to it. Add a `cpp` job to `.github/workflows/tests.yml` (ubuntu, windows MinGW). Update `cpp/README.md`. Tests pass locally
  (Strawberry GCC 13 on this machine supports `<expected>`).
* **P2, Sub0Squad: consume.** After P1 is reviewed. Branch from `main`. CPM with `CPM_Sub0Platform_SOURCE` pointing at the P1 checkout (a tag is the
  owner's step). Replace `include/sub0squad/platform_*.hpp` by forwarding headers only if the rename touches too many files; otherwise rename call
  sites. Delete the moved sources and tests, keep the `layering` test for the rest. All Squad tests that ran before still pass.
* **P3, Sub0Canvas: consume.** After P1, in parallel with P2. `CPMAddPackage` for Sub0Platform behind the existing `cmake/get_cpm.cmake`; tests include
  the shared `check.hpp`; delete `lib/tests/check.hpp`; `ctest --preset quick` unchanged; remove the BACKLOG and RESUME `getenv` notes.
* **Owner steps after review:** push Sub0Platform and tag, repoint P2 and P3 at the tag, push those branches.

## Risks

* Squad has other sessions committing to `main`; P2 must start from a fresh fetch and stay small to rebase.
* The namespace rename is the widest edit in the work; the forwarding-header fallback exists so P2 stays reviewable.
* Squad is C++23 with `std::expected`; Sub0Platform inherits that floor (open decision 2 in Squad's brief). Sub0Log is not touched.

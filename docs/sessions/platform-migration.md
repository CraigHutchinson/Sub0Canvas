# Follow-up session: move Sub0Squad's platform layer to Sub0Platform (what is left)

Backlog: item 14. Origin: the `getenv` tidy-up. Platform code belongs in Sub0Platform, whose `cpp/README.md` says the C++ library is "Sub0Squad's layer 0, lifted here". Squad's own brief: `docs/sessions/platform-unification.md` (step 3), `docs/PLATFORM.md`.

## Goal

1. Sub0Platform holds the C++ platform library (`platform_error`, `process`, `proc`, `host`, `file`, `net`, `ipc`, `wait`), its tests, its layering check and the shared test helper `check.hpp`, built and tested on Linux and Windows.
2. The `getenv` fix lives there once: a read-only `getEnvironment(name) -> std::optional<std::string>` beside `setEnvironment`/`unsetEnvironment` (`_dupenv_s` on Windows).
3. Sub0Squad consumes Sub0Platform through CPM and deletes its copy. Sub0Canvas already consumes it for its test helper (`sub0platform/testing/check.hpp`).

## Left

* Check Sub0Platform's `cpp/README.md` first for what is lifted. Whatever is not: lift Squad's eight platform stems (headers, `_posix`/`_win32` backends, utilities), `tests/platform_child.cpp`, the `test_platform_*` programs and `check.hpp`, rename the namespace `sub0squad::platform` to `sub0platform` and the headers to `sub0platform/<module>.hpp`, add `getEnvironment` with its tests, and a `cpp` job (ubuntu, windows MinGW).
* Sub0Squad consumes it: CPM with `CPM_Sub0Platform_SOURCE` at the checkout (a tag is the owner's step); forwarding headers only if the rename touches too many files; delete the moved sources and tests, keep Squad's `layering` test; every Squad test still passes.
* Owner steps: push Sub0Platform and tag, repoint Squad's and Canvas's pins at the tag, push those branches.

## Done means

Sub0Squad builds and passes its suite on Sub0Platform with no copy of the platform layer; Canvas's `app/` and `serve` use it for files, locks and processes.

## Risks

Squad has other sessions committing to `main`: start from a fresh fetch and keep the change small to rebase. The namespace rename is the widest edit (the forwarding-header fallback keeps it reviewable). Squad is C++23 with `std::expected`; Sub0Platform inherits that floor.

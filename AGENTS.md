# Working in sub0canvas

Instructions for agents and people continuing this project. Start with [docs/RESUME.md](docs/RESUME.md) (where it stands) and
[docs/BACKLOG.md](docs/BACKLOG.md) (what is next). A task with its own brief is in [docs/sessions/](docs/sessions/README.md): start a session with
"Read `docs/sessions/<file>.md` and do it."

## What this is

sub0canvas is a library and a viewer for **layered documents**: a codebase held as revisioned files (the canvas), seen through layers (issues,
review notes, prose, plans, history) that are bound to **stable block identities** rather than line numbers. It began inside Sub0Squad, a local-model
coding squad, which remains its first consumer. Its direction is to become the backbone where agents, people and sources (git, GitHub) meet, and to
hold up on codebases of billions of lines.

| read | for |
|---|---|
| [docs/CANVAS.md](docs/CANVAS.md) | the revisioned document, transactions, folded views and reads (built) |
| [docs/LAYERED_DOCUMENT.md](docs/LAYERED_DOCUMENT.md) | block identity, layers, the issues layer, formats, prior art (built: slices 1 and 2) |
| [docs/BACKBONE.md](docs/BACKBONE.md) | the design ahead: state, events, subscriptions, claims, adapters, history, scale, and the ordered slices |
| [docs/USE_CASES.md](docs/USE_CASES.md) | who it is for and what each use case demands |
| [docs/TESTING.md](docs/TESTING.md) | the proving ladder: unit, golden, use case, scale, consumer |
| [docs/INTEGRATION.md](docs/INTEGRATION.md) | how a change reaches Sub0Squad: a tag here, a pull request there |
| [README.md](README.md) | the viewer, the input format, building the library |

## Layout

* `lib/include/sub0canvas/*.hpp`, `lib/src/*.cpp`: the C++23 library, target `sub0canvas::sub0canvas`, namespace `sub0canvas`. Pure: no I/O, no
  threads, no clock reads, no environment.
* `lib/tests/`: C++ tests (`check.hpp`, one executable per area).
* `index.html`, `src/*.js`, `test/*.test.js`, `tools/*.mjs`: the static viewer (no build step, no dependencies, Node 20+ for its tests).
* `fixtures/demo/`: the viewer's hand-made demo; `fixtures/golden/`: real producer output, frozen.
* `docs/`: the design and process documents above; `docs/sessions/`: briefs for follow-up sessions.

## Build and test

```bash
cmake --preset default -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++   # set CPM_SOURCE_CACHE to share a dependency cache
cmake --build --preset default
ctest --preset quick      # what the pre-push gate and CI smoke run; ctest --preset full for everything
npm test                  # the viewer alone
```

On Windows the toolchain is MinGW-w64 UCRT (WinLibs GCC) with Ninja. Activate the pre-push gate once per clone: `git config core.hooksPath .githooks`,
and prove it with `python ../CraigHutchinson.github/tools/ci/hook_check.py --repo .`.

## Code style

* C++23, `-Wall -Wextra -Werror` (GCC-style flags; there is no MSVC leg).
* camelCase for functions and variables, PascalCase for types, `kName` for constants; `#pragma once`; Doxygen `///` on every public declaration,
  saying what it is for and its contract, not restating the name.
* `[[nodiscard]]` on functions whose result matters; `std::format` and `std::print`; `std::expected` for fallible operations in new code.
* Line length about 160; comments explain why, and cite the doc section a rule comes from.
* JavaScript: classic scripts (the page runs from `file://`), no dependencies, `node:test`.

## Rules (the owner's standing rules)

* **Nothing leaves the machine**: no frontier model, no uploading of code or data. Ask before downloads, installs or credentials.
* **Commit as you go** to `main` (fetch, rebase, build, test); **confirm with the owner before pushing**; never force-push or skip hooks; never
  stash, reset or switch branches in a checkout another session shares; commit only your own files.
* **This is a public repository: never name or link a private repository** (the pre-push gate checks, `private_names.py`).
* **Report faithfully**: regressions named, skipped steps said, numbers with their noise. Never throw work away.
* **CI**: smoke on every push and pull request; the full tier on `v*` tags, `ci:full`, dispatch and a change-gated weekly run. No full run per push;
  performance is measured locally only.
* **Changes the squad consumes** go here first, are tagged, then reach Sub0Squad through a pull request there ([docs/INTEGRATION.md](docs/INTEGRATION.md)).
* **Docs**: each piece of evidence has one home; a brief links to it rather than copying it. Retire a document once its content is integrated.

#!/usr/bin/env bash
# Import the canvas library from a Sub0Squad checkout at a git ref into lib/ (the one-time split, and any re-sync before the cut-over).
#
#   tools/import-sub0squad.sh <sub0squad repo> [ref=origin/main]
#
# Each file is read with `git show <ref>:<path>`, so the working tree of that checkout (and whatever another session has uncommitted there) is never read.
# The rewrite is mechanical: namespace sub0squad -> sub0canvas, "sub0squad/<stem>.hpp" -> "sub0canvas/<stem>.hpp" for the library's own headers,
# and the test programs lose the squad's composition root (registerBuiltinHarnesses) and the squad headers they never used.
set -euo pipefail
squad=${1:?usage: import-sub0squad.sh <sub0squad repo> [ref]}
ref=${2:-origin/main}
here=$(cd "$(dirname "$0")/.." && pwd)
stems="outline edits canvas doc_layers"

show() { git -C "$squad" show "$ref:$1"; }
rewrite() {
    local s=$(cat)
    for stem in $stems; do s=${s//\"sub0squad\/$stem.hpp\"/\"sub0canvas\/$stem.hpp\"}; done
    s=${s//namespace sub0squad/namespace sub0canvas}
    s=${s//sub0squad::/sub0canvas::}
    printf '%s\n' "$s"
}

mkdir -p "$here/lib/include/sub0canvas" "$here/lib/src" "$here/lib/tests"
for stem in $stems; do
    show "include/sub0squad/$stem.hpp" | rewrite > "$here/lib/include/sub0canvas/$stem.hpp"
    show "src/$stem.cpp" | rewrite > "$here/lib/src/$stem.cpp"
done
show src/json_util.hpp | rewrite > "$here/lib/src/json_util.hpp"
show tests/check.hpp | rewrite > "$here/lib/tests/check.hpp"
show tests/test_doc_layers.cpp | rewrite > "$here/lib/tests/test_doc_layers.cpp"
# test_edits: only the library is under test; the squad headers and its harness registry go.
show tests/test_edits.cpp | rewrite \
    | grep -v -E '^#include "(workflow_sim|sub0squad/[a-z_]+)\.hpp"' \
    | grep -v 'registerBuiltinHarnesses' \
    | sed 's|^#include "sub0canvas/outline.hpp"$|#include "sub0canvas/outline.hpp"\n#include "check.hpp"\n\nusing namespace sub0canvas;|' \
    > "$here/lib/tests/test_edits.cpp"   # the check macros and the namespace came through the squad's workflow_sim.hpp
echo "imported $(git -C "$squad" rev-parse --short "$ref") ($ref) from $squad" | tee "$here/lib/IMPORTED_FROM"

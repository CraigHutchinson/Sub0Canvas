#pragma once
/// @file hunks.hpp
/// Which lines of a block a commit changed (docs/LAYERED_DOCUMENT.md, "Hunk ranges"): the hunks of a `git diff -U0` mapped into the spans the block had at the
/// old revision and has at the new one. Pure data and text; the caller runs git.

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sub0canvas {

/// One hunk as git's `@@ -oldStart,oldLen +newStart,newLen @@` header gives it, in file lines. A zero length means a pure insertion or deletion, and the
/// start is then the line the change follows (git's convention for -U0).
struct DiffHunk
{
    int oldStart = 0, oldLen = 0, newStart = 0, newLen = 0;
};

/// A block's span at one revision: 1-based, inclusive.
struct BlockSpan
{
    std::string id;
    int first = 0, last = 0;
};

/// The changed lines of one block, relative to the block: line 1 is the block's first line at that revision, and a zero length keeps git's convention (the
/// change follows relative line `start`; 0 is before the block's first line). A side the hunk does not touch inside the block has length 0.
struct HunkSpan
{
    int oldStart = 0, oldLen = 0, newStart = 0, newLen = 0;
    [[nodiscard]] bool operator==(const HunkSpan&) const = default;
};

/// The hunks of a patch by path, from `git diff -U0 --no-renames` output (`diff --git a/P b/P` headers; a path git quotes is skipped), in patch order.
/// Hunks of binary files do not exist, so they are not counted.
[[nodiscard]] std::map<std::string, std::vector<DiffHunk>> parseHunks(std::string_view patch);

/// What `mapHunks` found for one file.
struct HunkMapping
{
    std::map<std::string, std::vector<HunkSpan>> byBlock;   ///< by block id, in hunk order: one span per hunk that touches the block
    std::size_t total = 0;                                  ///< the hunks given
    std::size_t outside = 0;                                ///< hunks that touch no block present at both revisions (between blocks, in an added block, in a file
                                                            ///< that was added or deleted): `total - outside` hunks are placed in at least one block
};

/// Maps `hunks` into the blocks of one file. `before` and `after` are the file's blocks at the old and the new revision; only an id present in both can
/// take a span (an added block has no old lines to relate to). A hunk touches a block when its old lines overlap the block's old span or its new lines
/// overlap the new span, and it is clipped to each span; one hunk spanning two blocks gives each its own span and counts once in `total - outside`.
[[nodiscard]] HunkMapping mapHunks(const std::vector<DiffHunk>& hunks, const std::vector<BlockSpan>& before, const std::vector<BlockSpan>& after);

}  // namespace sub0canvas

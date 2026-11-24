#pragma once
/// @file git_plumbing.hpp
/// Pure parsers of git's plumbing output (docs/BACKBONE.md section 5, A21; docs/TESTING.md rule 2: tests read captured output). The library never runs
/// git: `app/git_source.cpp` does, with exactly the commands named here, and these functions turn what git printed into the `DocumentSource` vocabulary
/// (source.hpp). Every parser is total: malformed input is an error value naming the byte offset, never a throw and never a guess.
///
/// All of them read NUL-terminated (`-z`) forms, so a path with a space, a quote or a newline arrives byte-exact. The one assumption is that no path or subject
/// holds the control bytes 0x01 and 0x02, which the log format uses as separators.

#include "sub0canvas/source.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace sub0canvas::gitplumbing {

/// The `--format` of the log command, `git log --first-parent --diff-merges=first-parent -M -C --name-status -z --format=<kLogFormat>`: 0x01 starts a record,
/// then the commit id, the parents (space separated), the author's name, the author date (strict ISO) and the subject, separated by 0x02. `-z` ends the header
/// with a NUL and the name-status entries follow, each `status NUL path NUL` (a rename or copy: `status NUL from NUL path NUL`).
inline constexpr std::string_view kLogFormat = "%x01%H%x02%P%x02%an%x02%aI%x02%s";

/// `git log ... --name-status -z --format=kLogFormat`: the commits in the order git printed them, with their changes (`History::commits`, newest first).
[[nodiscard]] std::expected<std::vector<Commit>, std::string> parseLog(std::string_view raw);

/// `git diff-tree -r -M -C --name-status -z <a> <b>`: the changes alone, no commit header.
[[nodiscard]] std::expected<std::vector<PathChange>, std::string> parseNameStatus(std::string_view raw);

/// One entry of a tree listing.
struct TreeEntry
{
    std::string mode, type, oid, path;   ///< `100644 blob <oid> <path>`: type is "blob", "tree" or "commit" (a submodule)
};

/// `git ls-tree -r -z <revision> [-- <paths>]`.
[[nodiscard]] std::expected<std::vector<TreeEntry>, std::string> parseTree(std::string_view raw);

/// The paths of a listing that hold a document: regular and executable blobs, not symbolic links, directories or submodules. Sorted.
[[nodiscard]] std::vector<std::string> documentPaths(const std::vector<TreeEntry>& entries);

/// `git cat-file --batch` for `expected` requests: one `<oid> <type> <size> LF <bytes> LF` per object, or `<name> missing LF`. A blob is `present`; a missing (or
/// ambiguous) object is `unknown`, which is how a partial clone without its blob answers when lazy fetching is off (GIT_NO_LAZY_FETCH=1); another object type (a
/// directory, a submodule's commit) is `absent`: there is no document there. Fewer or more answers than `expected` is an error, so a request that was dropped
/// can never shift every later answer by one.
[[nodiscard]] std::expected<std::vector<FileText>, std::string> parseCatFileBatch(std::string_view raw, std::size_t expected);

/// The refs as the lines `git cat-file --batch` reads on stdin (`<revision>:<path>`, one per line).
[[nodiscard]] std::string catFileRequests(const std::vector<FileRef>& refs);

}  // namespace sub0canvas::gitplumbing

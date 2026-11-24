#pragma once
/// @file source.hpp
/// Document sources (docs/BACKBONE.md section 5, amendments A20 and A21; docs/SCALE.md sections 3 and 4): where a revisioned document comes from. A source
/// answers four questions, each shaped by what a very large repository allows:
///   * `history`: the revisions of a SCOPE (a few paths or directories), never of the whole tree (R-SCALE-1, R-SCALE-9);
///   * `read`: the texts of (revision, path) pairs in one batch, because a process per object is too slow at 10^8 objects (A21);
///   * `list`: the files under a scope at a revision (the baseline of a replay that starts mid-history);
///   * `changes`: what one revision changed anywhere, with rename and copy detection (the only way to learn where a file came from).
/// Anything a source cannot supply because the object or the history is missing (a partial or shallow clone) is UNKNOWN, an explicit state the caller renders
/// and never an error (R-SCALE-7, A20).
///
/// The core implements none of this for a particular source: `app/git_source.cpp` runs git, `lib/src/git_plumbing.cpp` parses what git prints (pure),
/// and a test adapter over a `Canvas` is the second implementation that keeps the seam honest. Revision ids are opaque strings (R-SRC-5).

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sub0canvas {

/// One revision (a commit, a canvas transaction): its opaque id and its place in the graph.
struct RevisionInfo
{
    std::string id;
    std::vector<std::string> parents;   ///< first parent first; empty for a root, and for the oldest commit a shallow clone holds
    std::string author, time, message;  ///< `time` as the source wrote it (git: strict ISO 8601); `message` is the subject line only
};

/// How a path changed in one revision.
enum class ChangeKind { added, modified, deleted, renamed, copied, typeChanged };

/// One changed path. `from` is the old name of a rename and the origin of a copy; `similarity` (0-100) is the source's own score for those.
struct PathChange
{
    ChangeKind kind = ChangeKind::modified;
    std::string path;   ///< the path after the change (for `deleted`, the path that went away)
    std::string from;
    int similarity = 0;
};

/// A revision with the changes of the paths a query asked about (the other paths' changes are not listed: a revision that is huge elsewhere costs nothing).
struct Commit
{
    RevisionInfo info;
    std::vector<PathChange> changes;
};

/// A first-parent line of revisions, newest first.
struct History
{
    std::vector<Commit> commits;
    bool cut = false;   ///< older revisions exist that this source cannot supply (a shallow clone's boundary): the oldest listed is not the root
};

/// Whether a file's text is there. `absent` is a known fact (the path does not exist at that revision, or holds no document: a directory, a submodule);
/// `unknown` is the explicit marker for a missing object (A20).
enum class Presence { present, absent, unknown };

/// The text of one (revision, path).
struct FileText
{
    Presence presence = Presence::unknown;
    std::string text;   ///< only when `presence` is `present`

    [[nodiscard]] static FileText known(std::string text) { return {Presence::present, std::move(text)}; }
    [[nodiscard]] static FileText missing() { return {Presence::absent, {}}; }
    [[nodiscard]] static FileText unknown() { return {Presence::unknown, {}}; }
};

/// One object to read.
struct FileRef
{
    std::string revision, path;
};

/// The files under a scope at a revision; `known` is false when the revision's tree is not available.
struct Listing
{
    bool known = false;
    std::vector<std::string> paths;   ///< sorted
};

/// A scope names what a query is about: each entry is a file path or a directory prefix (`src/` or `src`); it matches the path itself and everything beneath
/// it. An empty scope matches every path.
using Scope = std::vector<std::string>;

/// Whether `path` is inside `scope`.
[[nodiscard]] inline bool inScope(const Scope& scope, std::string_view path)
{
    if (scope.empty()) return true;
    for (std::string_view entry : scope) {
        while (entry.size() > 1 && entry.back() == '/') entry.remove_suffix(1);
        if (entry.empty() || entry == ".") return true;
        if (path == entry) return true;
        if (path.size() > entry.size() && path.starts_with(entry) && path[entry.size()] == '/') return true;
    }
    return false;
}

/// A source of revisioned documents. Contract: pure reads, deterministic for a given repository state, safe to call repeatedly; no method throws for a
/// missing object (that is `unknown`).
class DocumentSource
{
public:
    virtual ~DocumentSource() = default;

    /// The first-parent line ending at `to`, newest first, restricted to the revisions that change a path of `scope` (so a revision that touched nothing of
    /// it is never listed, and a real source need not walk it: git's changed-path filters, A21), at most `limit` of them (0: all). Each commit lists only the
    /// changes inside `scope`, with rename and copy detection applied between paths that are both inside it; a merge is shown as its change against its first
    /// parent, which is what `git blame --first-parent` attributes lines by. An unknown `to` gives no commits.
    [[nodiscard]] virtual History history(const std::string& to, const Scope& scope, std::size_t limit) const = 0;

    /// What `revision` changed against `parent`, over every path, with rename and copy detection; nullopt when the trees are not available (unknown).
    [[nodiscard]] virtual std::optional<std::vector<PathChange>> changes(const std::string& parent, const std::string& revision) const = 0;

    /// The texts of `refs`, one answer per ref in the same order, read as one batch.
    [[nodiscard]] virtual std::vector<FileText> read(const std::vector<FileRef>& refs) const = 0;

    /// The files under `scope` at `revision`, sorted; `known` is false when that tree is not available.
    [[nodiscard]] virtual Listing list(const std::string& revision, const Scope& scope) const = 0;

    /// The full id of a revision name ("HEAD", a branch, an abbreviation), or nullopt when it names nothing here.
    [[nodiscard]] virtual std::optional<std::string> resolve(const std::string& name) const = 0;
};

}  // namespace sub0canvas

#pragma once
/// @file fixture_source.hpp
/// Test-side `DocumentSource`s (docs/TESTING.md rule 2: tests read captured data, never run git).
///
///   * `FixtureSource` serves the captured plumbing output of fixtures/git/tinycalc: the whole-repository log, the texts of every change under src/, the trees
///     of the windows' base revisions. It EMULATES what a pathspec does to `git log` (a rename with one side outside the pathspec shows as an addition or a
///     deletion), and test_revision_index checks that emulation against git's own path-limited logs, also captured, so it is evidence rather than an assumption.
///   * `CanvasSource` is the second implementation of the seam: a `Canvas` (a consumer's in-process revisions) read as a document source. It lives here, in a test,
///     because the library's source seam must not depend on a consumer (docs/MOCK_SQUAD.md).
#include "sub0canvas/canvas.hpp"
#include "sub0canvas/git_plumbing.hpp"
#include "sub0canvas/source.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <print>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fixture {

using namespace sub0canvas;

inline std::string slurp(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) { std::println(stderr, "cannot read {}", p.string()); std::exit(2); }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline std::vector<std::string> lines(const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) if (!l.empty()) out.push_back(l);
    return out;
}

/// What `git log -- <pathspec>` shows of one commit's whole-repository changes: only the paths inside the pathspec, and a rename or copy with one side outside it
/// as the addition or deletion git's limited diff sees.
inline std::vector<PathChange> limitedTo(const std::vector<PathChange>& changes, const Scope& scope)
{
    std::vector<PathChange> out;
    for (const PathChange& c : changes) {
        const bool now = inScope(scope, c.path);
        if (c.kind == ChangeKind::renamed) {
            const bool was = inScope(scope, c.from);
            if (now && was) out.push_back(c);
            else if (now) out.push_back({ChangeKind::added, c.path, {}, 0});
            else if (was) out.push_back({ChangeKind::deleted, c.from, {}, 0});
        } else if (c.kind == ChangeKind::copied) {
            if (!now) continue;
            out.push_back(inScope(scope, c.from) ? c : PathChange{ChangeKind::added, c.path, {}, 0});
        } else if (now) {
            out.push_back(c);
        }
    }
    return out;
}

/// The captured tinycalc repository (or its shallow clone) as a DocumentSource, with counters and switches for the missing-object tests.
class FixtureSource final : public DocumentSource
{
public:
    /// `dir`: fixtures/git/tinycalc. `shallow`: serve the depth-25 clone's log and texts (its oldest commit is a boundary: history is cut).
    FixtureSource(const std::filesystem::path& dir, bool shallow = false) : shallow_(shallow)
    {
        const std::string prefix = shallow ? "shallow-" : "";
        auto log = gitplumbing::parseLog(slurp(dir / (prefix + "log.z")));
        if (!log) { std::println(stderr, "log: {}", log.error()); std::exit(2); }
        commits_ = std::move(*log);
        for (std::size_t i = 0; i < commits_.size(); ++i) byId_[commits_[i].info.id] = i;
        head_ = commits_.front().info.id;
        const auto refs = lines(slurp(dir / (prefix + "blobs.refs")));
        auto texts = gitplumbing::parseCatFileBatch(slurp(dir / (prefix + "blobs.batch")), refs.size());
        if (!texts) { std::println(stderr, "blobs: {}", texts.error()); std::exit(2); }
        for (std::size_t i = 0; i < refs.size(); ++i) blobs_[refs[i]] = std::move((*texts)[i]);
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {   // the trees captured for a window's base: tree-<name>.z, its base id in the manifest
            const std::string name = entry.path().filename().string();
            if (shallow || !name.starts_with("tree-")) continue;
            trees_[name] = slurp(entry.path());
        }
    }

    /// The base revision of a captured tree file (from the manifest the test read).
    void addTree(const std::string& revision, const std::string& treeFile)
    {
        auto entries = gitplumbing::parseTree(trees_.at(treeFile));
        if (!entries) { std::println(stderr, "tree: {}", entries.error()); std::exit(2); }
        listings_[revision] = gitplumbing::documentPaths(*entries);
    }

    /// Texts that are not available (a partial clone without them): answered `unknown`.
    void dropBlob(const std::string& revision, const std::string& path) { dropped_.insert(revision + ":" + path); }
    void dropTrees() { listings_.clear(); }

    [[nodiscard]] const std::vector<Commit>& commits() const { return commits_; }
    [[nodiscard]] const std::string& head() const { return head_; }
    /// `c042` for the commit whose subject starts `c042:`; "" for a side-branch commit or an unknown id.
    [[nodiscard]] std::string labelOf(const std::string& id) const
    {
        const auto it = byId_.find(id);
        if (it == byId_.end()) return {};
        const std::string& m = commits_[it->second].info.message;
        const auto colon = m.find(':');
        return colon == std::string::npos ? std::string{} : m.substr(0, colon);
    }
    [[nodiscard]] std::string idOfLabel(const std::string& label) const
    {
        for (const Commit& c : commits_) if (c.info.message.starts_with(label + ":")) return c.info.id;
        return {};
    }

    /// Everything `read` was asked for, in order (R-SCALE-9: no text of a revision that did not change the scope is ever requested).
    mutable std::vector<FileRef> reads;
    mutable std::size_t historyCalls = 0, changesCalls = 0;

    [[nodiscard]] History history(const std::string& to, const Scope& scope, std::size_t limit) const override
    {
        ++historyCalls;
        History out;
        auto it = byId_.find(to);
        while (it != byId_.end()) {
            const Commit& c = commits_[it->second];
            auto limited = limitedTo(c.changes, scope);
            if (!limited.empty()) {
                out.commits.push_back({c.info, std::move(limited)});
                if (limit != 0 && out.commits.size() >= limit) return out;
            }
            if (c.info.parents.empty()) { out.cut = shallow_; break; }
            it = byId_.find(c.info.parents.front());
        }
        return out;
    }

    [[nodiscard]] std::optional<std::vector<PathChange>> changes(const std::string& parent, const std::string& revision) const override
    {
        ++changesCalls;
        const auto it = byId_.find(revision);
        if (it == byId_.end() || commits_[it->second].info.parents.empty() || commits_[it->second].info.parents.front() != parent) return std::nullopt;
        return commits_[it->second].changes;
    }

    [[nodiscard]] std::vector<FileText> read(const std::vector<FileRef>& refs) const override
    {
        std::vector<FileText> out;
        for (const FileRef& r : refs) {
            reads.push_back(r);
            const std::string key = r.revision + ":" + r.path;
            const auto it = blobs_.find(key);
            out.push_back(it == blobs_.end() || dropped_.count(key) ? FileText::unknown() : it->second);
        }
        return out;
    }

    [[nodiscard]] Listing list(const std::string& revision, const Scope& scope) const override
    {
        const auto it = listings_.find(revision);
        if (it == listings_.end()) return {};
        Listing l;
        l.known = true;
        for (const auto& p : it->second) if (inScope(scope, p)) l.paths.push_back(p);
        return l;
    }

    [[nodiscard]] std::optional<std::string> resolve(const std::string& name) const override
    {
        if (name == "HEAD" || name == "main") return head_;
        if (byId_.count(name)) return name;
        if (const std::string id = idOfLabel(name); !id.empty()) return id;
        return std::nullopt;
    }

private:
    bool shallow_ = false;
    std::vector<Commit> commits_;                 // newest first, as git printed them
    std::map<std::string, std::size_t> byId_;
    std::string head_;
    std::map<std::string, FileText> blobs_;       // "<revision>:<path>"
    std::map<std::string, std::string> trees_;    // tree file name -> raw `ls-tree -z` output
    std::map<std::string, std::vector<std::string>> listings_;   // revision -> document paths
    std::set<std::string> dropped_;
};

/// A `Canvas` as a document source: the second implementation of the seam. A canvas numbers revisions per FILE, a source needs one line of revisions, so the
/// adapter is given the STEPS that made it: step k lists the files (and their per-file revision) that the k-th revision of the whole changed. Revision ids are
/// "r0" (the files at their revision 1) and "r1", "r2", ... : opaque strings (R-SRC-5), not canvas revision numbers.
class CanvasSource final : public DocumentSource
{
public:
    using Step = std::vector<std::pair<std::string, int>>;
    CanvasSource(const Canvas& canvas, std::vector<Step> steps) : canvas_(canvas), steps_(std::move(steps)) {}

    [[nodiscard]] static std::string idOf(std::size_t k) { return "r" + std::to_string(k); }

    [[nodiscard]] History history(const std::string& to, const Scope& scope, std::size_t limit) const override
    {
        History out;
        const auto at = index(to);
        if (!at) return out;
        for (std::size_t k = *at + 1; k-- > 0;) {
            Commit c;
            c.info.id = idOf(k);
            if (k > 0) c.info.parents = {idOf(k - 1)};
            c.info.message = "step " + std::to_string(k);
            for (const auto& [path, rev] : steps_[k]) {
                if (!inScope(scope, path)) continue;
                const bool existed = k > 0 && revisionAt(k - 1, path) > 0;
                c.changes.push_back({existed ? ChangeKind::modified : ChangeKind::added, path, {}, 0});
                (void)rev;
            }
            if (c.changes.empty()) continue;
            out.commits.push_back(std::move(c));
            if (limit != 0 && out.commits.size() >= limit) break;
        }
        return out;
    }
    [[nodiscard]] std::optional<std::vector<PathChange>> changes(const std::string& parent, const std::string& revision) const override
    {
        const auto at = index(revision);
        if (!at || *at == 0 || idOf(*at - 1) != parent) return std::nullopt;
        std::vector<PathChange> out;
        for (const auto& [path, rev] : steps_[*at]) { out.push_back({revisionAt(*at - 1, path) > 0 ? ChangeKind::modified : ChangeKind::added, path, {}, 0}); (void)rev; }
        return out;
    }
    [[nodiscard]] std::vector<FileText> read(const std::vector<FileRef>& refs) const override
    {
        std::vector<FileText> out;
        for (const FileRef& r : refs) {
            const auto at = index(r.revision);
            const int rev = at ? revisionAt(*at, r.path) : 0;
            out.push_back(rev > 0 ? FileText::known(canvas_.textAt(r.path, rev)) : FileText::missing());
        }
        return out;
    }
    [[nodiscard]] Listing list(const std::string& revision, const Scope& scope) const override
    {
        Listing l;
        const auto at = index(revision);
        if (!at) return l;
        l.known = true;
        for (const std::string& p : canvas_.paths()) if (revisionAt(*at, p) > 0 && inScope(scope, p)) l.paths.push_back(p);
        return l;
    }
    [[nodiscard]] std::optional<std::string> resolve(const std::string& name) const override
    {
        if (name == "HEAD") return idOf(steps_.size() - 1);
        return index(name) ? std::optional<std::string>(name) : std::nullopt;
    }

private:
    [[nodiscard]] std::optional<std::size_t> index(const std::string& id) const
    {
        for (std::size_t k = 0; k < steps_.size(); ++k) if (idOf(k) == id) return k;
        return std::nullopt;
    }
    /// The canvas revision of `path` after step k (0: not on the canvas yet).
    [[nodiscard]] int revisionAt(std::size_t k, const std::string& path) const
    {
        int rev = 0;
        for (std::size_t i = 0; i <= k; ++i) for (const auto& [p, r] : steps_[i]) if (p == path) rev = r;
        return rev;
    }
    const Canvas& canvas_;
    std::vector<Step> steps_;
};

}  // namespace fixture
